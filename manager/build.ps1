param(
    [string]$SdkRoot=(Join-Path $PSScriptRoot '../sdk'),
    [string]$CompilerRoot=$env:SAN14_MSVC_ROOT,
    [switch]$Tests,
    [string]$AdapterSource='',
    [string]$GumRoot='',
    [string]$JsonRoot='',
    [string]$WorkerManifest='',
    [string]$AdapterRuntimeBundle='',
    [string]$PythonInclude='',
    [string]$ArtifactRoot='',
    [switch]$Installer
)
$ErrorActionPreference='Stop'
$taskProject=$PSScriptRoot
if(-not $SdkRoot){throw 'Provide -SdkRoot or SAN14_SDK_DIR'}
$taskSdk=[IO.Path]::GetFullPath($SdkRoot)
if(-not (Test-Path -LiteralPath "$taskSdk/include/san14_mod.h")){throw 'SDK include directory is incomplete'}
if(-not $CompilerRoot){$CompilerRoot=$env:VCToolsInstallDir}
if(-not $CompilerRoot){
    $taskVswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if(Test-Path -LiteralPath $taskVswhere){foreach($taskVs in (& $taskVswhere -all -products '*' -property installationPath)){
        $taskCompiler=Get-ChildItem -LiteralPath (Join-Path $taskVs 'VC/Tools/MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1
        if($taskCompiler){$CompilerRoot=$taskCompiler.FullName;break}
    }}
}
if(-not $CompilerRoot){throw 'Provide -CompilerRoot or SAN14_MSVC_ROOT'}
$CompilerRoot=[IO.Path]::GetFullPath($CompilerRoot)
if($AdapterSource){$AdapterSource=[IO.Path]::GetFullPath($AdapterSource)}
if($GumRoot){$GumRoot=[IO.Path]::GetFullPath($GumRoot)}
if($JsonRoot){$JsonRoot=[IO.Path]::GetFullPath($JsonRoot)}
if($AdapterRuntimeBundle){$AdapterRuntimeBundle=[IO.Path]::GetFullPath($AdapterRuntimeBundle)}
$taskCl=Join-Path $CompilerRoot 'bin/Hostx64/x64/cl.exe'
if(-not (Test-Path -LiteralPath $taskCl)){throw 'MSVC x64 compiler not found'}
$taskWindowsSdk=(Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots').KitsRoot10
$taskSdkVersion=(Get-ChildItem -LiteralPath "$taskWindowsSdk/Include" -Directory | Where-Object {Test-Path -LiteralPath "$($_.FullName)/um/Windows.h"} | Sort-Object Name -Descending | Select-Object -First 1).Name
$taskOut=if($ArtifactRoot){Join-Path ([IO.Path]::GetFullPath($ArtifactRoot)) 'native'}else{Join-Path $taskProject 'build/native'}
[void][IO.Directory]::CreateDirectory($taskOut)
$taskWorkerDigest=''
if($WorkerManifest){
    $taskWorkerDigest=(Get-FileHash -LiteralPath $WorkerManifest -Algorithm SHA256).Hash.ToLowerInvariant()
}
"inline constexpr char worker_bundle_manifest_sha256[] = `"$taskWorkerDigest`";" |
    Set-Content -LiteralPath (Join-Path $taskOut 'worker_bundle_pin.h') -Encoding ascii
$taskOldPath=$env:PATH;$taskOldInclude=$env:INCLUDE;$taskOldLib=$env:LIB
function Invoke-Compile([string[]]$InputFiles,[string]$OutputName,[string[]]$Extra=@(),[string[]]$Libraries=@(),[switch]$Dll){
    $taskArgs=@($script:taskFlags)+$Extra
    if($Dll){$taskArgs+='/LD'}
    $taskArgs+=@($InputFiles | ForEach-Object {[IO.Path]::GetRelativePath($script:taskOut,$_ )})
    & $script:taskCl @taskArgs "/Fe$OutputName" /link @Libraries /DYNAMICBASE /NXCOMPAT
    if($LASTEXITCODE){throw "Compilation failed: $OutputName"}
}
try{
    $env:PATH="$(Split-Path -Parent $taskCl);$taskOldPath"
    $env:INCLUDE="$CompilerRoot/include;$taskWindowsSdk/Include/$taskSdkVersion/ucrt;$taskWindowsSdk/Include/$taskSdkVersion/shared;$taskWindowsSdk/Include/$taskSdkVersion/um"
    $env:LIB="$CompilerRoot/lib/x64;$taskWindowsSdk/Lib/$taskSdkVersion/ucrt/x64;$taskWindowsSdk/Lib/$taskSdkVersion/um/x64"
    Push-Location -LiteralPath $taskOut
    try{
        $taskFlags=@('/nologo','/utf-8','/std:c++17','/EHa','/O2','/MT','/W4','/bigobj','/DUNICODE','/D_UNICODE')
        foreach($taskInclude in @($taskOut,"$taskProject/include","$taskProject/src","$taskProject/src/generic","$taskProject/third_party","$taskSdk/include")){$taskFlags+='/I'+[IO.Path]::GetRelativePath($taskOut,$taskInclude)}
        if($Installer){
            if(-not $AdapterSource -or -not $JsonRoot){throw 'The Setup stub requires -AdapterSource and -JsonRoot'}
            $taskInstallerIncludes=@($AdapterSource,$JsonRoot) | ForEach-Object {'/I'+[IO.Path]::GetRelativePath($taskOut,$_)}
            Invoke-Compile @("$taskProject/src/installer/installer_exe.cpp") 'san14-setup-stub.exe' $taskInstallerIncludes @('/SUBSYSTEM:WINDOWS')
            if($Tests){
                Invoke-Compile @("$taskProject/src/installer/installer_workflow_tests.cpp") 'installer-workflow-tests.exe' $taskInstallerIncludes
                & (Join-Path $taskOut 'installer-workflow-tests.exe')
                if($LASTEXITCODE){throw 'Installer workflow tests failed'}
            }
            return
        }
        $taskCore=@("$taskProject/src/core_services.cpp","$taskProject/src/adapter_loader.cpp",
                    "$taskProject/src/worker_service.cpp","$taskProject/src/generic/worker_transport.cpp")
        $taskLibs=@('kernel32.lib','user32.lib','shell32.lib','bcrypt.lib','advapi32.lib')
        Invoke-Compile ($taskCore+@("$taskProject/src/bootstrap.cpp","$taskProject/src/proxy.cpp")) 'dinput8.dll' @() $taskLibs -Dll
        Invoke-Compile ($taskCore+@("$taskProject/tests/adapter_probe.cpp")) 'adapter-probe.exe' @() $taskLibs
        if($Tests){
            Invoke-Compile @("$taskProject/tests/fixture_adapter.cpp") 'fixture-adapter.dll' @() @() -Dll
            Invoke-Compile @("$taskProject/tests/fixture_adapter.cpp") 'fixture-bad.dll' @('/DFIXTURE_BAD_ABI=1') @() -Dll
            Invoke-Compile @("$taskProject/tests/fixture_plugin.cpp") 'fixture-plugin.dll' @() @() -Dll
            Invoke-Compile ($taskCore+@("$taskProject/tests/core_tests.cpp")) 'core-tests.exe' @() $taskLibs
            & (Join-Path $taskOut 'core-tests.exe')
            if($LASTEXITCODE){throw 'Core integration tests failed'}
            Invoke-Compile ($taskCore+@("$taskProject/tests/worker_probe.cpp")) 'worker-probe.exe' @() $taskLibs
        }
        if($AdapterSource){
            $taskAdapter=[IO.Path]::GetFullPath($AdapterSource)
            if(-not $GumRoot -or -not $JsonRoot){throw 'Local adapter requires -GumRoot and -JsonRoot'}
            $taskExtra=@($taskAdapter,$GumRoot,$JsonRoot) | ForEach-Object {'/I'+[IO.Path]::GetRelativePath($taskOut,$_)}
            $taskAdapterFiles=@('adapter_entry.cpp','host.cpp','companion_runtime.cpp','command_engine.cpp','automation_runtime.cpp') | ForEach-Object {Join-Path $taskAdapter $_}
            $taskAdapterLibs=$taskLibs+@('comdlg32.lib','imm32.lib','advapi32.lib','ole32.lib',(Join-Path $GumRoot 'frida-gumjs.lib'))
            if($AdapterRuntimeBundle){
                if(-not $PythonInclude -or -not (Test-Path -LiteralPath (Join-Path $PythonInclude 'Python.h'))){throw 'Provide -PythonInclude for the in-process adapter runtime'}
                $taskExtra+='/I'+[IO.Path]::GetRelativePath($taskOut,$PythonInclude)
                $taskPrivateArchive=Join-Path $taskOut 'adapter-runtime.zip'
                & python (Join-Path $taskProject 'tools/embed_adapter_runtime.py') $AdapterRuntimeBundle (Join-Path $taskAdapter 'generated_adapter_runtime_identity.h') $taskPrivateArchive
                if($LASTEXITCODE){throw 'Adapter runtime resource preparation failed'}
                '2101 RCDATA "adapter-runtime.zip"' | Set-Content -LiteralPath 'adapter-runtime.rc' -Encoding ascii
                $taskRc=Join-Path $taskWindowsSdk "bin/$taskSdkVersion/x64/rc.exe"
                & $taskRc /nologo /foadapter-runtime.res adapter-runtime.rc
                if($LASTEXITCODE){throw 'Adapter runtime resource compilation failed'}
                $taskExtra+='/DSAN14_IN_PROCESS_ADAPTER'
                $taskAdapterFiles+=Join-Path $taskOut 'adapter-runtime.res'
            }
            Invoke-Compile $taskAdapterFiles 'GameAdapter.dll' $taskExtra $taskAdapterLibs -Dll
            $taskPackage=if($ArtifactRoot){Join-Path ([IO.Path]::GetFullPath($ArtifactRoot)) 'adapter-package'}else{Join-Path $taskProject '.local/game-adapter/package'}
            $taskBundle=Join-Path $taskPackage 'SAN14Base/GameAdapter'
            [void][IO.Directory]::CreateDirectory($taskBundle)
            Copy-Item -LiteralPath (Join-Path $taskOut 'GameAdapter.dll') -Destination $taskBundle
            @{schema_version=1;abi_version=1;sdk_abi_version=65536;adapter_id='san14-native-adapter';adapter_version='0.1.0';binary='GameAdapter.dll';sha256=(Get-FileHash -LiteralPath (Join-Path $taskBundle 'GameAdapter.dll') -Algorithm SHA256).Hash.ToLowerInvariant();embedded_commands=$true} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskBundle 'adapter.json') -Encoding utf8NoBOM
            & (Join-Path $taskOut 'adapter-probe.exe') $taskPackage
            if($LASTEXITCODE){throw 'Local adapter binary contract failed'}
            if($Tests){
                $fixtures=[ordered]@{
                    'game-profile-tests'=@('game_profile_tests.cpp')
                    'runtime-discovery-tests'=@('runtime_discovery_tests.cpp')
                    'native-hook-tests'=@('native_hook_tests.cpp')
                    'worker-transport-tests'=@('worker_transport_tests.cpp','worker_transport.cpp')
                    'automation-runtime-tests'=@('automation_runtime_tests.cpp','automation_runtime.cpp','command_engine.cpp')
                }
                foreach($fixture in $fixtures.GetEnumerator()){
                    $fixtureFiles=@($fixture.Value | ForEach-Object {Join-Path $taskAdapter $_})
                    Invoke-Compile $fixtureFiles "$($fixture.Key).exe" $taskExtra $taskAdapterLibs
                    & (Join-Path $taskOut "$($fixture.Key).exe")
                    if($LASTEXITCODE){throw "$($fixture.Key) failed"}
                }
                if($AdapterRuntimeBundle){
                    Invoke-Compile @((Join-Path $taskAdapter 'adapter_python_tests.cpp')) 'adapter-python-tests.exe' $taskExtra $taskAdapterLibs
                    & (Join-Path $taskOut 'adapter-python-tests.exe') $AdapterRuntimeBundle
                    if($LASTEXITCODE){throw 'In-process adapter Python integration failed'}
                    $singleWorkerFiles=$taskCore+@((Join-Path $taskAdapter 'adapter_single_worker_tests.cpp'),
                        (Join-Path $taskAdapter 'automation_runtime.cpp'),(Join-Path $taskAdapter 'command_engine.cpp'))
                    Invoke-Compile $singleWorkerFiles 'adapter-single-worker-tests.exe' $taskExtra $taskAdapterLibs
                    & (Join-Path $taskOut 'adapter-single-worker-tests.exe') (Join-Path $ArtifactRoot 'SAN14Base') (Join-Path $taskBundle 'GameAdapter.dll')
                    if($LASTEXITCODE){throw 'Single Worker and embedded GameAdapter integration failed'}
                }
            }
        }
    }finally{Pop-Location}
}finally{$env:PATH=$taskOldPath;$env:INCLUDE=$taskOldInclude;$env:LIB=$taskOldLib}
