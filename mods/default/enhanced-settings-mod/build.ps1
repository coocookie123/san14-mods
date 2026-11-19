param([string]$SdkRoot=(Join-Path $PSScriptRoot '../../../sdk'),[string]$CompilerRoot=$env:SAN14_MSVC_ROOT,[switch]$Tests)
$ErrorActionPreference='Stop'
& (Join-Path $SdkRoot 'tools/build_plugin.ps1') -ProjectRoot $PSScriptRoot -SdkRoot $SdkRoot -CompilerRoot $CompilerRoot -Tests:$Tests
if($LASTEXITCODE){throw 'Plugin build or tests failed'}
