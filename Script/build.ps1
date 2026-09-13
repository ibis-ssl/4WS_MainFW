# Configure and build the firmware from any working directory.
param(
  [ValidateSet("Debug", "Release")]
  [string]$Configuration = "Debug",
  [string]$CMakePath = "",
  [string]$NinjaPath = "",
  [string]$ToolchainBinPath = "",
  [string]$CubeCLTPath = "",
  [switch]$Rebuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir
. (Join-Path $scriptDir "common.ps1")

$cltRoots = Get-CubeCLTRoots -CubeCLTPath $CubeCLTPath
$preferCLT = [bool]$CubeCLTPath
$cmake = Resolve-Executable -Name "cmake.exe" -ExplicitPath $CMakePath -SearchRoots $cltRoots -RelativePaths @("CMake\bin\cmake.exe", "bin\cmake.exe") -PreferSearchRoots:$preferCLT
$ninja = Resolve-Executable -Name "ninja.exe" -ExplicitPath $NinjaPath -SearchRoots $cltRoots -RelativePaths @("Ninja\bin\ninja.exe", "bin\ninja.exe") -PreferSearchRoots:$preferCLT

if ($ToolchainBinPath) {
  $toolchainDir = Resolve-Path -LiteralPath $ToolchainBinPath -ErrorAction SilentlyContinue
  if (-not $toolchainDir -or -not (Get-Item -LiteralPath $toolchainDir.Path).PSIsContainer) {
    throw "GNU Arm toolchain bin directory not found: $ToolchainBinPath"
  }
  $toolchainBin = $toolchainDir.Path
} else {
  $gccPath = Resolve-Executable -Name "arm-none-eabi-gcc.exe" -SearchRoots $cltRoots -RelativePaths @("GNU-tools-for-STM32\bin\arm-none-eabi-gcc.exe", "GNU-tools-for-STM32\tools\bin\arm-none-eabi-gcc.exe") -PreferSearchRoots:$preferCLT
  $toolchainBin = Split-Path -Parent $gccPath
}

$toolNames = @("arm-none-eabi-gcc.exe", "arm-none-eabi-g++.exe", "arm-none-eabi-objcopy.exe", "arm-none-eabi-size.exe")
$resolvedTools = @{}
foreach ($toolName in $toolNames) {
  $toolPath = Join-Path $toolchainBin $toolName
  if (-not (Test-Path -LiteralPath $toolPath -PathType Leaf)) {
    throw "GNU Arm tool missing from toolchain directory: $toolPath"
  }
  $resolvedTools[$toolName] = (Resolve-Path -LiteralPath $toolPath).Path
}

$oldPath = $env:Path
$env:Path = "$toolchainBin;$(Split-Path -Parent $ninja);$oldPath"
try {
  Push-Location $repoRoot
  try {
    $configureArgs = @(
      "--preset", $Configuration,
      "-DCMAKE_MAKE_PROGRAM=$ninja",
      "-DCMAKE_C_COMPILER=$($resolvedTools['arm-none-eabi-gcc.exe'])",
      "-DCMAKE_ASM_COMPILER=$($resolvedTools['arm-none-eabi-gcc.exe'])",
      "-DCMAKE_CXX_COMPILER=$($resolvedTools['arm-none-eabi-g++.exe'])",
      "-DCMAKE_OBJCOPY=$($resolvedTools['arm-none-eabi-objcopy.exe'])",
      "-DCMAKE_SIZE=$($resolvedTools['arm-none-eabi-size.exe'])"
    )
    Invoke-NativeCommand -Executable $cmake -Arguments $configureArgs -FailureMessage "CMake configure failed for $Configuration"

    $buildArgs = @("--build", "--preset", $Configuration)
    if ($Rebuild) { $buildArgs += "--clean-first" }
    Invoke-NativeCommand -Executable $cmake -Arguments $buildArgs -FailureMessage "Build failed for $Configuration"
  } finally {
    Pop-Location
  }
} finally {
  $env:Path = $oldPath
}
