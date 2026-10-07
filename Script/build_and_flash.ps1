# Build successfully before invoking the project flash script with matching options.
param(
  [ValidateSet("Debug", "Release")]
  [string]$Configuration = "Debug",
  [string]$CMakePath = "",
  [string]$NinjaPath = "",
  [string]$ToolchainBinPath = "",
  [string]$ProgrammerPath = "",
  [string]$CubeCLTPath = "",
  [string]$Serial = "",
  [ValidateRange(100, 24000)]
  [int]$Frequency = 1000,
  [switch]$Rebuild,
  [switch]$NoVerify,
  [switch]$NoReset,
  [switch]$DryRun
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildScript = Join-Path $scriptDir "build.ps1"
$flashScript = Join-Path $scriptDir "flash.ps1"

$buildArgs = @{
  Configuration = $Configuration
  CMakePath = $CMakePath
  NinjaPath = $NinjaPath
  ToolchainBinPath = $ToolchainBinPath
  CubeCLTPath = $CubeCLTPath
  Rebuild = $Rebuild
}
& $buildScript @buildArgs
if (-not $?) { throw "Build step failed" }

$flashArgs = @{
  Configuration = $Configuration
  ProgrammerPath = $ProgrammerPath
  CubeCLTPath = $CubeCLTPath
  Serial = $Serial
  Frequency = $Frequency
  NoVerify = $NoVerify
  NoReset = $NoReset
  DryRun = $DryRun
}
& $flashScript @flashArgs
if (-not $?) { throw "Flash step failed" }
