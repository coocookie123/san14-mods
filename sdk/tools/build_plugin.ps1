param(
    [Parameter(Mandatory=$true)][string]$ProjectRoot,
    [string]$SdkRoot=(Split-Path -Parent $PSScriptRoot),
    [string]$CompilerRoot=$env:SAN14_MSVC_ROOT,
    [switch]$Tests
)
$ErrorActionPreference='Stop'
$taskProject=[IO.Path]::GetFullPath($ProjectRoot)
$taskSdk=[IO.Path]::GetFullPath($SdkRoot)
if(-not (Test-Path -LiteralPath (Join-Path $taskSdk 'include/san14_mod.h'))){throw 'SdkRoot must contain include/san14_mod.h'}
if(-not $CompilerRoot){$CompilerRoot=$env:VCToolsInstallDir}
if(-not $CompilerRoot){
    $taskVswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if(Test-Path -LiteralPath $taskVswhere){
        foreach($taskVs in (& $taskVswhere -all -products '*' -property installationPath)){
            $taskCompiler=Get-ChildItem -LiteralPath (Join-Path $taskVs 'VC/Tools/MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1
            if($taskCompiler){$CompilerRoot=$taskCompiler.FullName;break}
        }
    }
}
if(-not $CompilerRoot){throw 'Set SAN14_MSVC_ROOT or run from an MSVC development environment'}
$taskCl=Join-Path $CompilerRoot 'bin/Hostx64/x64/cl.exe'
if(-not (Test-Path -LiteralPath $taskCl)){throw 'MSVC x64 compiler not found'}
$taskWindowsSdk=(Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots').KitsRoot10
$taskWindowsSdkVersion=(Get-ChildItem -LiteralPath (Join-Path $taskWindowsSdk 'Include') -Directory | Where-Object {Test-Path -LiteralPath (Join-Path $_.FullName 'um/Windows.h')} | Sort-Object Name -Descending | Select-Object -First 1).Name
$taskManifest=Get-Content -LiteralPath (Join-Path $taskProject 'mod.json') -Raw | ConvertFrom-Json
if($taskManifest.entry.type -ne 'native'){throw 'This command builds native SDK plugins only'}
$taskSources=@(Get-ChildItem -LiteralPath (Join-Path $taskProject 'src') -File -Filter '*.cpp' | ForEach-Object {$_.FullName})
if(-not $taskSources.Count){throw 'No plugin C++ sources found'}
$taskOut=Join-Path $taskProject 'build/native'
[void][IO.Directory]::CreateDirectory($taskOut)
$taskDllName=[IO.Path]::GetFileName([string]$taskManifest.entry.path)
$taskOldPath=$env:PATH;$taskOldInclude=$env:INCLUDE;$taskOldLib=$env:LIB
try{
    $env:PATH="$(Split-Path -Parent $taskCl);$taskOldPath"
    $env:INCLUDE="$CompilerRoot/include;$taskWindowsSdk/Include/$taskWindowsSdkVersion/ucrt;$taskWindowsSdk/Include/$taskWindowsSdkVersion/shared;$taskWindowsSdk/Include/$taskWindowsSdkVersion/um"
    $env:LIB="$CompilerRoot/lib/x64;$taskWindowsSdk/Lib/$taskWindowsSdkVersion/ucrt/x64;$taskWindowsSdk/Lib/$taskWindowsSdkVersion/um/x64"
    Push-Location -LiteralPath $taskOut
    try{
        $taskFlags=@('/nologo','/utf-8','/std:c++17','/EHa','/O2','/MT','/W4','/DUNICODE','/D_UNICODE')
        $taskFlags+='/I'+[IO.Path]::GetRelativePath($taskOut,(Join-Path $taskSdk 'include'))
        $taskInput=@($taskSources | ForEach-Object {[IO.Path]::GetRelativePath($taskOut,$_ )})
        & $taskCl @taskFlags /LD @taskInput "/Fe$taskDllName" /link /DYNAMICBASE /NXCOMPAT
        if($LASTEXITCODE){throw 'Plugin build failed'}
        $taskDestination=Join-Path $taskProject ([string]$taskManifest.entry.path)
        [void][IO.Directory]::CreateDirectory((Split-Path -Parent $taskDestination))
        Copy-Item -LiteralPath (Join-Path $taskOut $taskDllName) -Destination $taskDestination
        $taskTestCount=0
        if($Tests){
            $taskTestFlags=$taskFlags+@('/I'+[IO.Path]::GetRelativePath($taskOut,(Join-Path $taskSdk 'testing')))
            foreach($taskTest in (Get-ChildItem -LiteralPath (Join-Path $taskProject 'tests') -Filter '*.cpp' -File | Sort-Object Name)){
                $taskExe=$taskTest.BaseName+'.exe'
                & $taskCl @taskTestFlags ([IO.Path]::GetRelativePath($taskOut,$taskTest.FullName)) "/Fe$taskExe" /link /DYNAMICBASE /NXCOMPAT
                if($LASTEXITCODE){throw "Test build failed: $($taskTest.Name)"}
                & (Join-Path $taskOut $taskExe)
                if($LASTEXITCODE){throw "Test failed: $($taskTest.Name)"}
                $taskTestCount++
            }
        }
        @{id=$taskManifest.id;version=$taskManifest.version;tests=$taskTestCount;dll=$taskDllName} | ConvertTo-Json -Compress
    }finally{Pop-Location}
}finally{$env:PATH=$taskOldPath;$env:INCLUDE=$taskOldInclude;$env:LIB=$taskOldLib}
