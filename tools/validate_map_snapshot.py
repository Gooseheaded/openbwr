import argparse
import csv
import hashlib
import json
import subprocess
from pathlib import Path


def read_csv(path: Path):
	with path.open("r", encoding="utf-8-sig", newline="") as f:
		return list(csv.DictReader(f))


def run_classifier(classifier: str, target: Path):
	p = subprocess.run([classifier, str(target), "--json"], capture_output=True, text=True)
	if p.returncode != 0:
		raise RuntimeError((p.stderr or p.stdout).strip())
	return json.loads(p.stdout)


def main():
	ap = argparse.ArgumentParser(description="Validate map snapshot digest for classifier output")
	ap.add_argument("--repo-root", default=".")
	ap.add_argument("--classifier", required=True)
	ap.add_argument("--update", action="store_true", help="rewrite snapshot digest")
	args = ap.parse_args()

	repo_root = Path(args.repo_root).resolve()
	testdata = repo_root / "tests" / "testdata"
	manifest_path = testdata / "maps" / "manifest.csv"
	snapshot_path = testdata / "maps" / "snapshot.sha256"

	rows = read_csv(manifest_path)
	lines = []
	for row in rows:
		if row.get("status") != "ok":
			continue
		fixture = testdata / Path(*row["fixture_path"].split("/"))
		if not fixture.exists():
			raise RuntimeError(f"missing fixture file: {row['fixture_path']}")
		out = run_classifier(args.classifier, fixture)
		line = "|".join([
			row["fixture_path"],
			str(out.get("classification")),
			str(out.get("map_version")),
			str(out.get("tileset_index")),
			str(out.get("max_group_index")),
			str(out.get("classic_max_group_index")),
			str(out.get("uses_new_tiles")),
			str(out.get("requires_scr_semantic_pack")),
		])
		lines.append(line)

	payload = "\n".join(lines).encode("utf-8")
	digest = hashlib.sha256(payload).hexdigest()

	if args.update or not snapshot_path.exists():
		snapshot_path.write_text(digest + "\n", encoding="utf-8")
		print(f"snapshot updated: {snapshot_path}")
		print(f"digest={digest}")
		return 0

	expected = snapshot_path.read_text(encoding="utf-8").strip()
	if digest != expected:
		print(f"snapshot mismatch: expected={expected} got={digest}")
		return 1

	print(f"map snapshot digest ok: {digest}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
