param(
  [string]$BwapiDir = (Join-Path (Split-Path -Parent $PSScriptRoot) "bwapi"),
  [string]$Configuration = "Release",
  [int]$EnableUi = 0,
  [int]$Jobs = 0
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $BwapiDir)) {
  throw "BWAPI repo was not found at: $BwapiDir"
}

$configureScript = Join-Path $BwapiDir "configure.ps1"
$buildScript = Join-Path $BwapiDir "build.ps1"

if (-not (Test-Path $configureScript)) {
  throw "Missing script: $configureScript"
}
if (-not (Test-Path $buildScript)) {
  throw "Missing script: $buildScript"
}

Write-Host "Configuring BWAPI against this OpenBW checkout..."
& $configureScript -OpenBwDir $PSScriptRoot -BuildType $Configuration -EnableUi $EnableUi
if ($LASTEXITCODE -ne 0) {
  throw "Configure failed with exit code $LASTEXITCODE"
}

foreach ($target in @("BWAPILauncher", "ExampleAIModule")) {
  Write-Host "Building $target (includes OpenBW components)..."
  & $buildScript -Configuration $Configuration -Target $target -Jobs $Jobs
  if ($LASTEXITCODE -ne 0) {
    throw "Build failed for $target with exit code $LASTEXITCODE"
  }
}

Write-Host "OpenBW-integrated build complete."
