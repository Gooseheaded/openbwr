param(
	[Parameter(Mandatory = $true)]
	[string[]]$Collections,
	[string]$ClassifierPath = ".\build-tests\openbw_asset_classify.exe",
	[string]$OutputRoot = ".\classification-output"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Test-HasNonAscii {
	param([string]$Value)
	foreach ($c in $Value.ToCharArray()) {
		if ([int][char]$c -gt 127) { return $true }
	}
	return $false
}

function Invoke-ClassifierOnce {
	param(
		[string]$Classifier,
		[string]$FilePath
	)

	$originalErrorAction = $ErrorActionPreference
	$output = $null
	$exitCode = 999
	try {
		$ErrorActionPreference = "Continue"
		$output = & $Classifier $FilePath --json 2>&1
		$exitCode = $LASTEXITCODE
	}
	catch {
		return [pscustomobject]@{
			ExitCode = 998
			Json = $null
			Error = $_.Exception.Message
			UsedTempPath = $false
		}
	}
	finally {
		$ErrorActionPreference = $originalErrorAction
	}

	$text = (($output | ForEach-Object { "$_" }) -join "`n").Trim()

	if ($exitCode -ne 0) {
		return [pscustomobject]@{
			ExitCode = $exitCode
			Json = $null
			Error = $text
			UsedTempPath = $false
		}
	}

	try {
		$obj = $text | ConvertFrom-Json -ErrorAction Stop
		return [pscustomobject]@{
			ExitCode = 0
			Json = $obj
			Error = $null
			UsedTempPath = $false
		}
	}
	catch {
		return [pscustomobject]@{
			ExitCode = 3
			Json = $null
			Error = "JSON parse failed: $($_.Exception.Message)`nRaw: $text"
			UsedTempPath = $false
		}
	}
}

function Parse-CollectionSpec {
	param([string]$Spec)

	if ($Spec -match "^[^=]+=.+$") {
		$parts = $Spec -split "=", 2
		return [pscustomobject]@{
			Name = $parts[0].Trim()
			Path = $parts[1].Trim()
		}
	}

	$name = Split-Path -Path $Spec -Leaf
	if ([string]::IsNullOrWhiteSpace($name)) {
		$name = $Spec.Replace(":", "").Replace("\\", "_").Replace("/", "_")
	}

	return [pscustomobject]@{
		Name = $name
		Path = $Spec
	}
}

function Get-CandidateFiles {
	param([string]$RootPath)

	$validExt = @(".scm", ".scx", ".chk", ".rep")
	Get-ChildItem -Path $RootPath -Recurse -File -ErrorAction SilentlyContinue |
		Where-Object { $validExt -contains $_.Extension.ToLowerInvariant() }
}

function Invoke-Classifier {
	param(
		[string]$Classifier,
		[string]$FilePath
	)

	$result = Invoke-ClassifierOnce -Classifier $Classifier -FilePath $FilePath
	if ($result.ExitCode -eq 0) {
		return $result
	}

	$shouldRetryWithTemp = $false
	if ($result.Error -match "failed to open" -and (Test-HasNonAscii -Value $FilePath)) {
		$shouldRetryWithTemp = $true
	}
	if (-not $shouldRetryWithTemp) {
		return $result
	}

	$tempDir = Join-Path ([System.IO.Path]::GetTempPath()) "openbw-classifier"
	New-Item -ItemType Directory -Path $tempDir -Force | Out-Null
	$extension = [System.IO.Path]::GetExtension($FilePath)
	$tempFile = Join-Path $tempDir (([System.Guid]::NewGuid().ToString("N")) + $extension)

	try {
		Copy-Item -LiteralPath $FilePath -Destination $tempFile -Force
		$retry = Invoke-ClassifierOnce -Classifier $Classifier -FilePath $tempFile
		if ($retry.ExitCode -eq 0 -and $null -ne $retry.Json) {
			$retry.UsedTempPath = $true
			$fallbackNote = "classified via temporary ASCII path due Unicode file-open limitation"
			if ([string]::IsNullOrWhiteSpace([string]$retry.Json.note)) {
				$retry.Json | Add-Member -NotePropertyName note -NotePropertyValue $fallbackNote -Force
			} else {
				$retry.Json.note = "$($retry.Json.note); $fallbackNote"
			}
			return $retry
		}
		return $result
	}
	catch {
		return $result
	}
	finally {
		if (Test-Path -LiteralPath $tempFile) {
			Remove-Item -LiteralPath $tempFile -Force -ErrorAction SilentlyContinue
		}
	}
}

if (-not (Test-Path -LiteralPath $ClassifierPath)) {
	throw "Classifier not found: $ClassifierPath"
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$runDir = Join-Path $OutputRoot "run-$timestamp"
New-Item -ItemType Directory -Path $runDir -Force | Out-Null

$allRows = New-Object System.Collections.Generic.List[object]

$parsedCollections = @()
foreach ($spec in $Collections) {
	$parsedCollections += Parse-CollectionSpec -Spec $spec
}

foreach ($col in $parsedCollections) {
	if (-not (Test-Path -LiteralPath $col.Path)) {
		Write-Warning "Skipping missing collection path: $($col.Path)"
		continue
	}

	Write-Host ""
	Write-Host "Scanning collection '$($col.Name)' at $($col.Path)..."

	$files = @(Get-CandidateFiles -RootPath $col.Path)
	$total = $files.Count
	Write-Host "Found $total candidate files."

	$i = 0
	foreach ($f in $files) {
		$i++
		if ($total -gt 0) {
			Write-Progress -Activity "Scanning $($col.Name)" -Status "$i / $total" -PercentComplete (($i / $total) * 100)
		}

		$result = Invoke-Classifier -Classifier $ClassifierPath -FilePath $f.FullName

		if ($result.ExitCode -eq 0) {
			$j = $result.Json
			$allRows.Add([pscustomobject]@{
				collection = $col.Name
				root = $col.Path
				path = $f.FullName
				file_type = $j.file_type
				map_version = $j.map_version
				classification = $j.classification
				requires_scr_semantic_pack = $j.requires_scr_semantic_pack
				tileset_index = $j.tileset_index
				max_group_index = $j.max_group_index
				classic_max_group_index = $j.classic_max_group_index
				uses_new_tiles = $j.uses_new_tiles
				note = $j.note
				used_temp_path = $result.UsedTempPath
				status = "ok"
				error = ""
			})
		}
		else {
			$allRows.Add([pscustomobject]@{
				collection = $col.Name
				root = $col.Path
				path = $f.FullName
				file_type = $f.Extension.ToLowerInvariant().TrimStart(".")
				map_version = $null
				classification = "error"
				requires_scr_semantic_pack = $null
				tileset_index = $null
				max_group_index = $null
				classic_max_group_index = $null
				uses_new_tiles = $null
				note = ""
				used_temp_path = $false
				status = "tool_error"
				error = $result.Error
			})
		}
	}

	Write-Progress -Activity "Scanning $($col.Name)" -Completed
}

if ($allRows.Count -eq 0) {
	throw "No files were processed."
}

$rawCsv = Join-Path $runDir "raw_results.csv"
$rawJsonl = Join-Path $runDir "raw_results.jsonl"

$allRows | Export-Csv -Path $rawCsv -NoTypeInformation -Encoding UTF8
$allRows | ForEach-Object { $_ | ConvertTo-Json -Compress } | Set-Content -Path $rawJsonl -Encoding UTF8

$okRows = @($allRows | Where-Object { $_.status -eq "ok" })
$totalRows = $allRows.Count

$summaryByCollection = foreach ($grp in ($okRows | Group-Object collection)) {
	$rows = @($grp.Group)
	$n = $rows.Count
	$classic = @($rows | Where-Object { $_.classification -eq "classic" }).Count
	$remastered = @($rows | Where-Object { $_.classification -eq "remastered" }).Count
	$unsupported = @($rows | Where-Object { $_.classification -eq "unsupported" }).Count
	$usesNewTiles = @($rows | Where-Object { $_.uses_new_tiles -eq $true }).Count

	[pscustomobject]@{
		collection = $grp.Name
		total_ok = $n
		classic = $classic
		remastered = $remastered
		unsupported = $unsupported
		uses_new_tiles = $usesNewTiles
		classic_pct = if ($n) { [math]::Round(100.0 * $classic / $n, 2) } else { 0 }
		remastered_pct = if ($n) { [math]::Round(100.0 * $remastered / $n, 2) } else { 0 }
		unsupported_pct = if ($n) { [math]::Round(100.0 * $unsupported / $n, 2) } else { 0 }
	}
}

$summaryByType = foreach ($grp in ($allRows | Group-Object collection, file_type, classification, status)) {
	$sample = $grp.Group[0]
	[pscustomobject]@{
		collection = $sample.collection
		file_type = $sample.file_type
		classification = $sample.classification
		status = $sample.status
		count = $grp.Count
	}
}

$replayFallbacks = @(
	$okRows |
	Where-Object {
		$_.file_type -eq "replay" -and
		$_.note -like "*fallback classification by replay identifier*"
	} |
	Select-Object collection, path, note
)

$unicodeFallbacks = @(
	$okRows |
	Where-Object { $_.used_temp_path -eq $true } |
	Select-Object collection, path, note
)

$errors = @($allRows | Where-Object { $_.status -ne "ok" })

$summaryCollectionCsv = Join-Path $runDir "summary_by_collection.csv"
$summaryTypeCsv = Join-Path $runDir "summary_by_type.csv"
$fallbackCsv = Join-Path $runDir "replay_fallbacks.csv"
$unicodeFallbackCsv = Join-Path $runDir "unicode_path_fallbacks.csv"
$errorsCsv = Join-Path $runDir "errors.csv"

$summaryByCollection | Export-Csv -Path $summaryCollectionCsv -NoTypeInformation -Encoding UTF8
$summaryByType | Export-Csv -Path $summaryTypeCsv -NoTypeInformation -Encoding UTF8
$replayFallbacks | Export-Csv -Path $fallbackCsv -NoTypeInformation -Encoding UTF8
$unicodeFallbacks | Export-Csv -Path $unicodeFallbackCsv -NoTypeInformation -Encoding UTF8
$errors | Export-Csv -Path $errorsCsv -NoTypeInformation -Encoding UTF8

Write-Host ""
Write-Host "Run complete."
Write-Host "Processed: $totalRows files"
Write-Host "OK: $($okRows.Count)"
Write-Host "Errors: $($errors.Count)"
Write-Host "Replay fallback classifications: $($replayFallbacks.Count)"
Write-Host "Unicode path fallback successes: $($unicodeFallbacks.Count)"
Write-Host ""
Write-Host "Output directory: $runDir"
Write-Host ""
Write-Host "Summary by collection:"
$summaryByCollection | Format-Table -AutoSize
