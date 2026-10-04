[CmdletBinding()]
param(
  [ValidateSet("Debug", "Release")]
  [string]$Configuration = "Debug",

  [switch]$SkipTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Preset = "windows-$($Configuration.ToLowerInvariant())"

function Invoke-NativeCommand {
  param(
    [Parameter(Mandatory, Position = 0)]
    [string]$Executable,

    [Parameter(Mandatory, Position = 1)]
    [string[]]$ArgumentList
  )

  & $Executable @ArgumentList

  if ($LASTEXITCODE -ne 0) {
    throw "Command failed with exit code $LASTEXITCODE`: $Executable $($ArgumentList -join ' ')"
  }
}

Push-Location (Split-Path -Parent $PSScriptRoot)

try {
  Invoke-NativeCommand "cmake" @(
    "--preset",
    $Preset
  )

  Invoke-NativeCommand "cmake" @(
    "--build",
    "--preset",
    $Preset,
    "--parallel"
  )

  if (-not $SkipTests) {
    Invoke-NativeCommand "ctest" @(
      "--preset",
      $Preset
    )
  }
}
finally {
  Pop-Location
}
