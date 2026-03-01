import argparse
import csv
import json
import subprocess
from pathlib import Path


def read_csv(path: Path):
	with path.open("r", encoding="utf-8-sig", newline="") as f:
		return list(csv.DictReader(f))


def run_classifier(classifier: str, target: Path):
	p = subprocess.run([classifier, str(target), "--json"], capture_output=True, text=True)
	stdout = p.stdout.strip()
	stderr = p.stderr.strip()
	json_obj = None
	if p.returncode == 0:
		try:
			json_obj = json.loads(stdout)
		except json.JSONDecodeError:
			return {
				"ok": False,
				"returncode": p.returncode,
				"stdout": stdout,
				"stderr": stderr,
				"json": None,
				"error": "classifier output was not valid JSON",
			}
	return {
		"ok": p.returncode == 0,
		"returncode": p.returncode,
		"stdout": stdout,
		"stderr": stderr,
		"json": json_obj,
		"error": "",
	}


def check_positive_manifest(testdata_root: Path, classifier: str, manifest_path: Path, is_replay: bool):
	rows = read_csv(manifest_path)
	failures = []
	checked = 0

	for row in rows:
		if row.get("status", "") != "ok":
			continue
		fixture_rel = row["fixture_path"]
		fixture_path = testdata_root / Path(*fixture_rel.split("/"))
		checked += 1

		if not fixture_path.exists():
			failures.append(f"missing fixture file: {fixture_rel}")
			continue

		result = run_classifier(classifier, fixture_path)
		if not result["ok"]:
			failures.append(
				f"classifier failed for {fixture_rel}: rc={result['returncode']} stderr={result['stderr'] or result['stdout']}"
			)
			continue

		out = result["json"] or {}
		expected_classification = row.get("expected_classification", "")
		if out.get("classification") != expected_classification:
			failures.append(
				f"classification mismatch for {fixture_rel}: expected {expected_classification}, got {out.get('classification')}"
			)

		strict = row.get("strict_version_assert", "false").lower() == "true"
		expected_version = row.get("map_version", "")
		actual_version = out.get("map_version")

		if strict:
			if expected_version == "":
				failures.append(f"strict_version_assert=true but expected map_version empty for {fixture_rel}")
			else:
				try:
					expected_version_int = int(expected_version)
				except ValueError:
					failures.append(f"invalid expected map_version '{expected_version}' for {fixture_rel}")
					continue
				if actual_version != expected_version_int:
					failures.append(
						f"map_version mismatch for {fixture_rel}: expected {expected_version_int}, got {actual_version}"
					)
		elif is_replay:
			# Low-confidence replay fixtures are allowed to lack map_version.
			pass

		expected_uses_new_tiles = row.get("expected_uses_new_tiles", "")
		if expected_uses_new_tiles != "":
			if expected_uses_new_tiles.lower() == "true":
				expected_bool = True
			elif expected_uses_new_tiles.lower() == "false":
				expected_bool = False
			else:
				expected_bool = None
			actual_uses = out.get("uses_new_tiles")
			if expected_bool is not None and actual_uses != expected_bool:
				failures.append(
					f"uses_new_tiles mismatch for {fixture_rel}: expected {expected_bool}, got {actual_uses}"
				)

	return checked, failures


def check_negative_manifest(testdata_root: Path, classifier: str, manifest_path: Path):
	rows = read_csv(manifest_path)
	failures = []
	checked = 0

	for row in rows:
		if row.get("status", "") != "ok":
			continue
		fixture_rel = row["fixture_path"]
		expected_err = row.get("expected_error_contains", "")
		fixture_path = testdata_root / Path(*fixture_rel.split("/"))
		checked += 1

		if not fixture_path.exists():
			failures.append(f"missing negative fixture file: {fixture_rel}")
			continue

		result = run_classifier(classifier, fixture_path)
		if result["ok"]:
			failures.append(f"expected classifier failure for {fixture_rel}, but command succeeded")
			continue

		combined = (result["stderr"] + "\n" + result["stdout"]).strip()
		if expected_err and expected_err not in combined:
			failures.append(
				f"error mismatch for {fixture_rel}: expected substring '{expected_err}', got '{combined[:240]}'"
			)

	return checked, failures


def main():
	ap = argparse.ArgumentParser(description="Validate SCR fixture manifests against classifier")
	ap.add_argument("--repo-root", default=".", help="openbw repository root")
	ap.add_argument("--classifier", required=True, help="path to openbw_asset_classify executable")
	args = ap.parse_args()

	repo_root = Path(args.repo_root).resolve()
	testdata_root = repo_root / "tests" / "testdata"
	classifier = args.classifier

	maps_manifest = testdata_root / "maps" / "manifest.csv"
	replays_manifest = testdata_root / "replays" / "manifest.csv"
	negative_manifest = testdata_root / "negative" / "manifest.csv"

	missing = [str(p) for p in [maps_manifest, replays_manifest, negative_manifest] if not p.exists()]
	if missing:
		print("missing manifest files:")
		for p in missing:
			print(f"- {p}")
		return 2

	maps_checked, maps_failures = check_positive_manifest(testdata_root, classifier, maps_manifest, is_replay=False)
	replays_checked, replays_failures = check_positive_manifest(testdata_root, classifier, replays_manifest, is_replay=True)
	neg_checked, neg_failures = check_negative_manifest(testdata_root, classifier, negative_manifest)

	failures = maps_failures + replays_failures + neg_failures

	print(f"maps_checked={maps_checked}")
	print(f"replays_checked={replays_checked}")
	print(f"negative_checked={neg_checked}")
	print(f"failures={len(failures)}")

	if failures:
		for f in failures:
			print(f"FAIL: {f}")
		return 1

	print("fixture manifest validation passed")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
