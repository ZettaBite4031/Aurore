[CmdletBinding()]
param(
  [ValidateSet("Debug", "Release")]
  [string]$Configuration = "Debug",

  [ValidateSet("x64")]
  [string]$Platform = "x64",

  [string]$CMakeGenerator = "Visual Studio 18 2026",

  [string]$GitExecutable,
  [string]$CMakeExecutable,
  [string]$MSBuildExecutable,

  [switch]$CheckToolsOnly,
  [switch]$SkipDependencies,
  [switch]$SkipTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RepositoryRoot = Split-Path -Parent $PSScriptRoot
$OutputDirectory = Join-Path $RepositoryRoot "$Platform/$Configuration"
$TestResultsDirectory = Join-Path $RepositoryRoot "test-results"

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

function Resolve-Executable {
  param(
    [Parameter(Mandatory)]
    [string]$DisplayName,

    [Parameter(Mandatory)]
    [string]$CommandName,

    [string]$ExplicitPath,

    [string[]]$CandidatePaths = @(),

    [Parameter(Mandatory)]
    [string]$InstallHint
  )

  if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
    if (-not (Test-Path $ExplicitPath -PathType Leaf)) {
      throw "$DisplayName was not found at the explicitly configured path: $ExplicitPath"
    }

    return (Resolve-Path $ExplicitPath).Path
  }

  $Command = Get-Command $CommandName -CommandType Application -ErrorAction SilentlyContinue
  if ($null -ne $Command) {
    return $Command.Source
  }

  foreach ($CandidatePath in $CandidatePaths) {
    if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
      continue
    }

    if (Test-Path $CandidatePath -PathType Leaf) {
      return (Resolve-Path $CandidatePath).Path
    }
  }

  throw "$DisplayName could not be located. $InstallHint"
}

function Resolve-VSWhereExecutable {
  $CandidatePaths = @()

  if (-not [string]::IsNullOrWhiteSpace(${env:ProgramFiles(x86)})) {
    $CandidatePaths += Join-Path `
      ${env:ProgramFiles(x86)} `
      "Microsoft Visual Studio/Installer/vswhere.exe"
  }

  if (-not [string]::IsNullOrWhiteSpace($env:ProgramFiles)) {
    $CandidatePaths += Join-Path `
      $env:ProgramFiles `
      "Microsoft Visual Studio/Installer/vswhere.exe"
  }

  $Command = Get-Command "vswhere.exe" -CommandType Application -ErrorAction SilentlyContinue
  if ($null -ne $Command) {
    return $Command.Source
  }

  foreach ($CandidatePath in $CandidatePaths) {
    if (Test-Path $CandidatePath -PathType Leaf) {
      return (Resolve-Path $CandidatePath).Path
    }
  }

  return $null
}

function Get-VisualStudioInstallations {
  param(
    [string]$VSWhereExecutable
  )

  if ([string]::IsNullOrWhiteSpace($VSWhereExecutable)) {
    return @()
  }

  $Installations = @(
    & $VSWhereExecutable `
      -all `
      -prerelease `
      -products * `
      -property installationPath
  )

  if ($LASTEXITCODE -ne 0) {
    throw "Visual Studio Installer failed while enumerating installations."
  }

  return @(
    $Installations |
      Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
      Select-Object -Unique
  )
}

function Find-MSBuildExecutable {
  param(
    [string]$ExplicitPath,
    [string[]]$VisualStudioInstallations
  )

  if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
    return Resolve-Executable `
      -DisplayName "MSBuild" `
      -CommandName "MSBuild.exe" `
      -ExplicitPath $ExplicitPath `
      -InstallHint "Install Visual Studio with the 'Desktop development with C++' workload."
  }

  $CandidatePaths = @()
  foreach ($Installation in $VisualStudioInstallations) {
    $CandidatePaths += Join-Path `
      $Installation `
      "MSBuild/Current/Bin/amd64/MSBuild.exe"

    $CandidatePaths += Join-Path `
      $Installation `
      "MSBuild/Current/Bin/MSBuild.exe"
  }

  try {
    return Resolve-Executable `
      -DisplayName "MSBuild" `
      -CommandName "MSBuild.exe" `
      -CandidatePaths $CandidatePaths `
      -InstallHint "Install Visual Studio with the 'Desktop development with C++' workload."
  }
  catch {
    $SearchRoots = @()
    if (-not [string]::IsNullOrWhiteSpace($env:ProgramFiles)) {
      $SearchRoots += Join-Path $env:ProgramFiles "Microsoft Visual Studio"
    }
    if (-not [string]::IsNullOrWhiteSpace(${env:ProgramFiles(x86)})) {
      $SearchRoots += Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio"
    }

    foreach ($SearchRoot in $SearchRoots) {
      if (-not (Test-Path $SearchRoot -PathType Container)) {
        continue
      }

      $Match = Get-ChildItem `
        -Path $SearchRoot `
        -Filter "MSBuild.exe" `
        -File `
        -Recurse `
        -ErrorAction SilentlyContinue |
        Where-Object {
          $_.FullName -match "[\\/]MSBuild[\\/]Current[\\/]Bin[\\/]"
        } |
        Select-Object -First 1

      if ($null -ne $Match) {
        return $Match.FullName
      }
    }

    throw
  }
}

function Assert-CMakeGenerator {
  param(
    [Parameter(Mandatory)]
    [string]$CMakeExecutable,

    [Parameter(Mandatory)]
    [string]$Generator
  )

  $CapabilitiesJson = & $CMakeExecutable -E capabilities | Out-String
  if ($LASTEXITCODE -ne 0) {
    throw "CMake failed while reporting its supported generators."
  }

  try {
    $Capabilities = $CapabilitiesJson | ConvertFrom-Json
  }
  catch {
    throw "CMake returned an unreadable capabilities document."
  }

  $GeneratorNames = @($Capabilities.generators | ForEach-Object { $_.name })
  if ($GeneratorNames -notcontains $Generator) {
    throw "CMake does not support the required generator '$Generator'. Install the matching Visual Studio version or pass -CMakeGenerator with a supported generator."
  }
}

$VSWhereExecutable = Resolve-VSWhereExecutable
$VisualStudioInstallations = Get-VisualStudioInstallations `
  -VSWhereExecutable $VSWhereExecutable

$MSBuildExecutable = Find-MSBuildExecutable `
  -ExplicitPath $MSBuildExecutable `
  -VisualStudioInstallations $VisualStudioInstallations

if (-not $SkipDependencies) {
  $GitCandidates = @()
  $CMakeCandidates = @()

  if (-not [string]::IsNullOrWhiteSpace($env:ProgramFiles)) {
    $GitCandidates += Join-Path $env:ProgramFiles "Git/cmd/git.exe"
    $CMakeCandidates += Join-Path $env:ProgramFiles "CMake/bin/cmake.exe"
  }

  if (-not [string]::IsNullOrWhiteSpace(${env:ProgramFiles(x86)})) {
    $GitCandidates += Join-Path ${env:ProgramFiles(x86)} "Git/cmd/git.exe"
    $CMakeCandidates += Join-Path ${env:ProgramFiles(x86)} "CMake/bin/cmake.exe"
  }

  if (-not [string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
    $GitCandidates += Join-Path $env:LOCALAPPDATA "Programs/Git/cmd/git.exe"
    $CMakeCandidates += Join-Path $env:LOCALAPPDATA "Programs/CMake/bin/cmake.exe"
  }

  foreach ($Installation in $VisualStudioInstallations) {
    $GitCandidates += Join-Path `
      $Installation `
      "Common7/IDE/CommonExtensions/Microsoft/TeamFoundation/Team Explorer/Git/cmd/git.exe"

    $CMakeCandidates += Join-Path `
      $Installation `
      "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
  }

  $GitExecutable = Resolve-Executable `
    -DisplayName "Git" `
    -CommandName "git.exe" `
    -ExplicitPath $GitExecutable `
    -CandidatePaths $GitCandidates `
    -InstallHint "Install Git for Windows or add it to PATH."

  $CMakeExecutable = Resolve-Executable `
    -DisplayName "CMake" `
    -CommandName "cmake.exe" `
    -ExplicitPath $CMakeExecutable `
    -CandidatePaths $CMakeCandidates `
    -InstallHint "Install CMake or add the Visual Studio CMake component."

  Assert-CMakeGenerator `
    -CMakeExecutable $CMakeExecutable `
    -Generator $CMakeGenerator
}

Write-Host "Build tools:"
if (-not $SkipDependencies) {
  Write-Host "  Git:     $GitExecutable"
  Write-Host "  CMake:   $CMakeExecutable"
}
Write-Host "  MSBuild: $MSBuildExecutable"
Write-Host ""

if ($CheckToolsOnly) {
  Write-Host "Tool discovery completed successfully."
  return
}

Push-Location $RepositoryRoot
try {
  if (-not $SkipDependencies) {
    Invoke-NativeCommand -Executable $GitExecutable -ArgumentList @(
      "submodule", "update", "--init", "--recursive"
    )

    Invoke-NativeCommand -Executable $CMakeExecutable -ArgumentList @(
      "-S", "vendor/Sonnet",
      "-B", "vendor/Sonnet/build",
      "-G", $CMakeGenerator,
      "-A", $Platform,
      "-DSONNET_BUILD_SHARED=ON",
      "-DSONNET_BUILD_TESTS=OFF",
      "-DSONNET_INSTALL=OFF"
    )

    Invoke-NativeCommand -Executable $CMakeExecutable -ArgumentList @(
      "--build", "vendor/Sonnet/build",
      "--config", $Configuration,
      "--target", "sonnet",
      "--parallel"
    )

    Invoke-NativeCommand -Executable $CMakeExecutable -ArgumentList @(
      "-S", "vendor/googletest",
      "-B", "vendor/googletest/build",
      "-G", $CMakeGenerator,
      "-A", $Platform,
      "-Dgtest_force_shared_crt=ON",
      "-DBUILD_GMOCK=OFF",
      "-DINSTALL_GTEST=OFF"
    )

    Invoke-NativeCommand -Executable $CMakeExecutable -ArgumentList @(
      "--build", "vendor/googletest/build",
      "--config", $Configuration,
      "--target", "gtest",
      "--parallel"
    )
  }

  $RequiredDependencyFiles = @(
    "vendor/Sonnet/build/$Configuration/sonnet.lib",
    "vendor/Sonnet/build/$Configuration/sonnet.dll",
    "vendor/googletest/build/lib/$Configuration/gtest.lib"
  )

  foreach ($RequiredFile in $RequiredDependencyFiles) {
    if (-not (Test-Path $RequiredFile -PathType Leaf)) {
      throw "Required dependency output was not produced: $RequiredFile"
    }
  }

  Invoke-NativeCommand -Executable $MSBuildExecutable -ArgumentList @(
    "Aurore.slnx",
    "/m",
    "/t:Build",
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform",
    "/v:minimal",
    "/nologo"
  )

  if (-not (Test-Path $OutputDirectory -PathType Container)) {
    throw "Aurore output directory does not exist: $OutputDirectory"
  }

  Copy-Item `
    "vendor/Sonnet/build/$Configuration/sonnet.dll" `
    $OutputDirectory `
    -Force

  if (-not $SkipTests) {
    $TestExecutable = Join-Path $OutputDirectory "Aurore.Tests.exe"
    if (-not (Test-Path $TestExecutable -PathType Leaf)) {
      throw "Test executable was not produced: $TestExecutable"
    }

    New-Item -ItemType Directory -Path $TestResultsDirectory -Force | Out-Null
    $ResultFile = Join-Path $TestResultsDirectory "Aurore.Tests-$Configuration.xml"

    Invoke-NativeCommand -Executable $TestExecutable -ArgumentList @(
      "--gtest_color=yes",
      "--gtest_output=xml:$ResultFile"
    )
  }
}
finally {
  Pop-Location
}
