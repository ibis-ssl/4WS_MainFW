# Shared tool discovery and native-process error handling for project scripts.
Set-StrictMode -Version 2.0

function Resolve-Executable {
  param(
    [Parameter(Mandatory = $true)][string]$Name,
    [string]$ExplicitPath,
    [string[]]$SearchRoots = @(),
    [string[]]$RelativePaths = @(),
    [switch]$PreferSearchRoots
  )

  if ($ExplicitPath) {
    $resolved = Resolve-Path -LiteralPath $ExplicitPath -ErrorAction SilentlyContinue
    if (-not $resolved -or (Get-Item -LiteralPath $resolved.Path).PSIsContainer) {
      throw "$Name not found: $ExplicitPath"
    }
    return $resolved.Path
  }

  if ($PreferSearchRoots) {
    foreach ($root in $SearchRoots) {
      if (-not $root -or -not (Test-Path -LiteralPath $root -PathType Container)) { continue }
      foreach ($relativePath in $RelativePaths) {
        $candidate = Join-Path $root $relativePath
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
          return (Resolve-Path -LiteralPath $candidate).Path
        }
      }
    }
  }

  $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($command) { return $command.Source }

  if (-not $PreferSearchRoots) {
    foreach ($root in $SearchRoots) {
      if (-not $root -or -not (Test-Path -LiteralPath $root -PathType Container)) { continue }
      foreach ($relativePath in $RelativePaths) {
        $candidate = Join-Path $root $relativePath
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
          return (Resolve-Path -LiteralPath $candidate).Path
        }
      }
    }
  }

  throw "$Name was not found. Add it to PATH or specify its path explicitly."
}

function Get-CubeCLTRoots {
  param([string]$CubeCLTPath)

  if ($CubeCLTPath) {
    $resolved = Resolve-Path -LiteralPath $CubeCLTPath -ErrorAction SilentlyContinue
    if (-not $resolved -or -not (Get-Item -LiteralPath $resolved.Path).PSIsContainer) {
      throw "STM32CubeCLT directory not found: $CubeCLTPath"
    }
    return @($resolved.Path)
  }

  $roots = @()
  foreach ($parent in @("C:\ST", "C:\Program Files\STMicroelectronics")) {
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) { continue }
    $roots += Get-ChildItem -LiteralPath $parent -Directory -Filter "STM32CubeCLT_*" -ErrorAction SilentlyContinue |
      Sort-Object -Property @{ Expression = {
        $versionText = $_.Name -replace '^STM32CubeCLT_', ''
        $version = $null
        if ([version]::TryParse($versionText, [ref]$version)) { $version } else { [version]'0.0' }
      }; Descending = $true } | Select-Object -ExpandProperty FullName
  }
  return @($roots)
}

function Invoke-NativeCommand {
  param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [Parameter(Mandatory = $true)][string[]]$Arguments,
    [Parameter(Mandatory = $true)][string]$FailureMessage
  )

  & $Executable @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "$FailureMessage (exit code $LASTEXITCODE)"
  }
}

function Format-CommandLine {
  param([string]$Executable, [string[]]$Arguments)
  $parts = @($Executable) + $Arguments
  return (($parts | ForEach-Object {
    if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ }
  }) -join ' ')
}
