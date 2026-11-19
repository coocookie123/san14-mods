param([Parameter(Mandatory=$true)][string]$ScenarioFile,[string]$SdkRoot=(Join-Path $PSScriptRoot '../../../sdk'),[string]$Output)
$ErrorActionPreference='Stop'
if(-not $Output){$Output=Join-Path $PSScriptRoot 'build/two-city-battle.s14mod'}
& python (Join-Path $PSScriptRoot 'tools/build_duel_scenario.py') $ScenarioFile (Join-Path $PSScriptRoot 'content/scenario.json') -o (Join-Path $PSScriptRoot 'content/duel.s14')
if($LASTEXITCODE){throw 'Native scenario authoring failed'}
& (Join-Path $SdkRoot 'tools/pack_mod.ps1') -ModDir $PSScriptRoot -Output $Output
if($LASTEXITCODE){throw 'Content package failed'}
