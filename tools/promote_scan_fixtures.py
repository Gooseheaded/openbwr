import argparse
import csv
import os
import shutil
from pathlib import Path


def ensure_dir(path: Path) -> None:
	path.mkdir(parents=True, exist_ok=True)


def clear_dir_files(path: Path) -> None:
	if not path.exists():
		return
	for p in path.iterdir():
		if p.is_file():
			p.unlink()


def read_csv(path: Path):
	with path.open("r", encoding="utf-8-sig", newline="") as f:
		return list(csv.DictReader(f))


def write_csv(path: Path, rows, fieldnames):
	ensure_dir(path.parent)
	with path.open("w", encoding="utf-8", newline="") as f:
		w = csv.DictWriter(f, fieldnames=fieldnames)
		w.writeheader()
		for r in rows:
			w.writerow(r)


def safe_copy(src: Path, dst: Path) -> bool:
	try:
		ensure_dir(dst.parent)
		shutil.copy2(src, dst)
		return True
	except Exception:
		return False


def next_name(prefix: str, idx: int, ext: str) -> str:
	return f"{prefix}_{idx:03d}{ext.lower()}"


def pick_latest_run(run_root: Path) -> Path:
	runs = [p for p in run_root.glob("run-*") if p.is_dir()]
	if not runs:
		raise RuntimeError(f"no run-* directories under {run_root}")
	runs.sort()
	return runs[-1]


def main():
	ap = argparse.ArgumentParser(description="Promote scan outputs into test fixtures")
	ap.add_argument("--run-dir", default="", help="classification-output/run-YYYYMMDD-HHMMSS")
	ap.add_argument("--run-root", default="classification-output", help="root directory containing run-* outputs")
	ap.add_argument("--repo-root", default=".", help="openbw repo root")
	ap.add_argument("--high-confidence-replays", type=int, default=20, help="number of high-confidence replay fixtures to copy")
	ap.add_argument("--low-confidence-replays", type=int, default=10, help="number of low-confidence replay fixtures to copy")
	args = ap.parse_args()

	repo_root = Path(args.repo_root).resolve()
	if args.run_dir:
		run_dir = Path(args.run_dir).resolve()
	else:
		run_dir = pick_latest_run((repo_root / args.run_root).resolve())

	shortlist_path = run_dir / "fixture_shortlist.csv"
	raw_path = run_dir / "raw_results.csv"
	if not shortlist_path.exists():
		raise RuntimeError(f"missing {shortlist_path}")
	if not raw_path.exists():
		raise RuntimeError(f"missing {raw_path}")

	shortlist = read_csv(shortlist_path)
	raw_rows = read_csv(raw_path)
	raw_by_path = {r.get("path", ""): r for r in raw_rows}

	testdata_root = repo_root / "tests" / "testdata"
	maps_root = testdata_root / "maps"
	replays_root = testdata_root / "replays"
	negative_root = testdata_root / "negative"

	for d in [
		maps_root / "classic",
		maps_root / "remastered",
		maps_root / "unsupported",
		replays_root / "high_confidence",
		replays_root / "low_confidence",
		negative_root / "map",
		negative_root / "replay",
	]:
		ensure_dir(d)
		clear_dir_files(d)

	map_manifest = []
	negative_manifest = []
	replay_manifest = []

	map_buckets = {
		"classic_map": ("classic", "classic"),
		"remastered_map": ("remastered", "remastered"),
		"unsupported_map": ("unsupported", "unsupported"),
	}
	map_indices = {"classic": 1, "remastered": 1, "unsupported": 1}
	negative_indices = {"map": 1, "replay": 1}

	for row in shortlist:
		bucket = row.get("bucket", "")
		src = Path(row.get("path", ""))
		ext = src.suffix if src.suffix else ".bin"

		if bucket in map_buckets:
			subdir, prefix = map_buckets[bucket]
			idx = map_indices[subdir]
			map_indices[subdir] += 1
			name = next_name(prefix, idx, ext)
			dst = maps_root / subdir / name
			copied = safe_copy(src, dst)
			raw = raw_by_path.get(str(src), {})
			map_manifest.append({
				"fixture_path": str(dst.relative_to(testdata_root)).replace("\\", "/"),
				"source_path": str(src),
				"expected_classification": row.get("classification", ""),
				"map_version": row.get("map_version", ""),
				"expected_uses_new_tiles": raw.get("uses_new_tiles", ""),
				"strict_version_assert": "true",
				"status": "ok" if copied else "missing_source",
				"note": row.get("note", ""),
			})

		elif bucket == "error_case":
			is_replay = (src.suffix.lower() == ".rep")
			subdir = "replay" if is_replay else "map"
			prefix = "negative_replay" if is_replay else "negative_map"
			idx = negative_indices[subdir]
			negative_indices[subdir] += 1
			name = next_name(prefix, idx, ext)
			dst = negative_root / subdir / name
			copied = safe_copy(src, dst)
			negative_manifest.append({
				"fixture_path": str(dst.relative_to(testdata_root)).replace("\\", "/"),
				"source_path": str(src),
				"expected_result": "tool_error",
				"expected_error_contains": row.get("error", ""),
				"status": "ok" if copied else "missing_source",
			})

	high_conf = [
		r for r in raw_rows
		if r.get("status") == "ok"
		and r.get("file_type") == "replay"
		and "classification by CHK VER" in (r.get("note") or "")
	]
	high_conf.sort(key=lambda r: r.get("path", ""))
	high_conf = high_conf[: max(0, args.high_confidence_replays)]

	low_conf = [
		r for r in raw_rows
		if r.get("status") == "ok"
		and r.get("file_type") == "replay"
		and "fallback classification by replay identifier" in (r.get("note") or "")
	]
	low_conf.sort(key=lambda r: r.get("path", ""))
	low_conf = low_conf[: max(0, args.low_confidence_replays)]

	for idx, row in enumerate(high_conf, start=1):
		src = Path(row["path"])
		dst = replays_root / "high_confidence" / next_name("replay_high", idx, src.suffix or ".rep")
		copied = safe_copy(src, dst)
		replay_manifest.append({
			"fixture_path": str(dst.relative_to(testdata_root)).replace("\\", "/"),
			"source_path": str(src),
			"expected_classification": row.get("classification", ""),
			"map_version": row.get("map_version", ""),
			"confidence": "high",
			"strict_version_assert": "true",
			"status": "ok" if copied else "missing_source",
			"note": row.get("note", ""),
		})

	for idx, row in enumerate(low_conf, start=1):
		src = Path(row["path"])
		dst = replays_root / "low_confidence" / next_name("replay_low", idx, src.suffix or ".rep")
		copied = safe_copy(src, dst)
		replay_manifest.append({
			"fixture_path": str(dst.relative_to(testdata_root)).replace("\\", "/"),
			"source_path": str(src),
			"expected_classification": row.get("classification", ""),
			"map_version": row.get("map_version", ""),
			"confidence": "low",
			"strict_version_assert": "false",
			"status": "ok" if copied else "missing_source",
			"note": row.get("note", ""),
		})

	write_csv(
		testdata_root / "maps" / "manifest.csv",
		map_manifest,
		[
			"fixture_path",
			"source_path",
			"expected_classification",
			"map_version",
			"expected_uses_new_tiles",
			"strict_version_assert",
			"status",
			"note",
		],
	)

	write_csv(
		testdata_root / "replays" / "manifest.csv",
		replay_manifest,
		[
			"fixture_path",
			"source_path",
			"expected_classification",
			"map_version",
			"confidence",
			"strict_version_assert",
			"status",
			"note",
		],
	)

	write_csv(
		testdata_root / "negative" / "manifest.csv",
		negative_manifest,
		[
			"fixture_path",
			"source_path",
			"expected_result",
			"expected_error_contains",
			"status",
		],
	)

	print(f"run_dir={run_dir}")
	print(f"maps={len(map_manifest)} replays={len(replay_manifest)} negatives={len(negative_manifest)}")
	print(f"output={testdata_root}")


if __name__ == "__main__":
	main()
