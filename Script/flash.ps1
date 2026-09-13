# Program the single application ELF through ST-Link, or inspect the command with -DryRun.
param(
  [ValidateSet("Debug", "Release")]
  [string]$Configuration = "Debug",
  [string]$ProgrammerPath = "",
  [string]$CubeCLTPath = "",
  [string]$Serial = "",
  [switch]$List,
  [switch]$ConnectOnly,
  [switch]$NoVerify,
  [switch]$NoReset,
  [switch]$DryRun
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir
. (Join-Path $scriptDir "common.ps1")

$cltRoots = Get-CubeCLTRoots -CubeCLTPath $CubeCLTPath
$programmer = Resolve-Executable -Name "STM32_Programmer_CLI.exe" -ExplicitPath $ProgrammerPath -SearchRoots $cltRoots -RelativePaths @(
  "STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
  "bin\STM32_Programmer_CLI.exe"
) -PreferSearchRoots:([bool]$CubeCLTPath)

if ($List) {
  $arguments = @("-l", "st-link-only")
} else {
  $arguments = @("-c", "port=SWD")
  if ($Serial) { $arguments += "sn=$Serial" }

  if (-not $ConnectOnly) {
    $firmware = Join-Path $repoRoot "build\$Configuration\mother_4steer.elf"
    if (-not (Test-Path -LiteralPath $firmware -PathType Leaf)) {
      throw "Firmware not found: $firmware. Run build.ps1 first."
    }
    $arguments += @("-d", $firmware)
    if (-not $NoVerify) { $arguments += "-v" }
    if (-not $NoReset) { $arguments += "-rst" }
  }
}

if ($DryRun) {
  Write-Output (Format-CommandLine -Executable $programmer -Arguments $arguments)
  return
}

Invoke-NativeCommand -Executable $programmer -Arguments $arguments -FailureMessage "STM32 programming failed"
