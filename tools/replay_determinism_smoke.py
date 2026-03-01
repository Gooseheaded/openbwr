import argparse
import csv
import hashlib
import json
import subprocess
from pathlib import Path


def read_csv(path: Path):
	with path.open("r", encoding="utf-8-sig", newline="") as f:
		return list(csv.DictReader(f))


def run_classifier_json(classifier: str, target: Path):
	p = subprocess.run([classifier, str(target), "--json"], capture_output=True, text=True)
	if p.returncode != 0:
		raise RuntimeError((p.stderr or p.stdout).strip())
	return json.loads(p.stdout)


def canonical_json(obj):
	return json.dumps(obj, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def main():
	ap = argparse.ArgumentParser(description="Replay determinism smoke check for classifier output")
	ap.add_argument("--repo-root", default=".")
	ap.add_argument("--classifier", required=True)
	ap.add_argument("--update", action="store_true", help="rewrite smoke digest")
	args = ap.parse_args()

	repo_root = Path(args.repo_root).resolve()
	testdata = repo_root / "tests" / "testdata"
	manifest_path = testdata / "replays" / "manifest.csv"
	smoke_path = testdata / "replays" / "smoke.sha256"

	rows = read_csv(manifest_path)
	lines = []
	for row in rows:
		if row.get("status") != "ok":
			continue
		fixture = testdata / Path(*row["fixture_path"].split("/"))
		if not fixture.exists():
			raise RuntimeError(f"missing replay fixture file: {row['fixture_path']}")

		first = run_classifier_json(args.classifier, fixture)
		second = run_classifier_json(args.classifier, fixture)
		if canonical_json(first) != canonical_json(second):
			raise RuntimeError(f"nondeterministic classifier output for {row['fixture_path']}")

		lines.append("|".join([
			row["fixture_path"],
			canonical_json(first),
		]))

	payload = "\n".join(lines).encode("utf-8")
	digest = hashlib.sha256(payload).hexdigest()

	if args.update or not smoke_path.exists():
		smoke_path.write_text(digest + "\n", encoding="utf-8")
		print(f"smoke digest updated: {smoke_path}")
		print(f"digest={digest}")
		return 0

	expected = smoke_path.read_text(encoding="utf-8").strip()
	if digest != expected:
		print(f"smoke digest mismatch: expected={expected} got={digest}")
		return 1

	print(f"replay smoke digest ok: {digest}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
