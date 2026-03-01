param(
  [string]$BwapiDir = (Join-Path (Split-Path -Parent $PSScriptRoot) "bwapi"),
  [string]$ScrTilesetDir = (Join-Path $PSScriptRoot "tileset_data")
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $BwapiDir)) {
  throw "BWAPI repo was not found at: $BwapiDir"
}

$bwapiRunScript = Join-Path $BwapiDir "run.ps1"
if (-not (Test-Path $bwapiRunScript)) {
  throw "Missing script: $bwapiRunScript"
}

if ($ScrTilesetDir -and (Test-Path $ScrTilesetDir)) {
  $resolvedScrTilesetDir = (Resolve-Path $ScrTilesetDir).Path
  $env:OPENBW_SCR_TILESET_DIR = $resolvedScrTilesetDir
  Write-Host "Using OPENBW_SCR_TILESET_DIR=$resolvedScrTilesetDir"
} elseif (-not $env:OPENBW_SCR_TILESET_DIR) {
  Write-Warning "SCR tileset directory not found at '$ScrTilesetDir'. Set OPENBW_SCR_TILESET_DIR manually if needed."
}

Write-Host "OpenBW has no standalone launcher; running via BWAPI launcher..."
& $bwapiRunScript
