#!/usr/bin/env python3
"""Download the six real Mahimahi downlink traces at a pinned commit and verify SHA-256.

The Mahimahi repository is GPL-3.0, so the traces are fetched, never vendored.
  python traces/fetch_real.py                   # fetch missing files, verify all
  python traces/fetch_real.py --update-manifest # (maintainers) recompute checksums
"""
import argparse
import hashlib
import json
import sys
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
MANIFEST = HERE / "MANIFEST.json"
DEST = HERE / "real"
REPO = "ravinet/mahimahi"
COMMIT = "f1346c38c415abfc98626dd82450f49a57033603"
# id -> file in the repo's traces/ directory (downlink files)
FILES = {
    "att_lte_driving": "ATT-LTE-driving.down",
    "att_lte_driving_2016": "ATT-LTE-driving-2016.down",
    "tmobile_lte_driving": "TMobile-LTE-driving.down",
    "verizon_lte_driving": "Verizon-LTE-driving.down",
    "tmobile_lte_short": "TMobile-LTE-short.down",
    "verizon_lte_short": "Verizon-LTE-short.down",
}


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def url_for(repo_file: str, repo: str = REPO, commit: str = COMMIT) -> str:
    return f"https://raw.githubusercontent.com/{repo}/{commit}/traces/{repo_file}"


def verify_file(path: Path, expected: str) -> bool:
    return path.is_file() and sha256_bytes(path.read_bytes()) == expected


def fetch(manifest: dict, dest: Path, opener=urllib.request.urlopen) -> None:
    """Fetch every file that is missing or fails its checksum. Raises on a mismatch after download."""
    dest.mkdir(parents=True, exist_ok=True)
    for entry in manifest["files"]:
        path = dest / f"{entry['id']}.trace"
        if verify_file(path, entry["sha256"]):
            continue
        with opener(entry["url"]) as r:
            data = r.read()
        if sha256_bytes(data) != entry["sha256"]:
            raise RuntimeError(f"checksum mismatch for {entry['id']}")
        path.write_bytes(data)


def build_manifest(opener=urllib.request.urlopen) -> dict:
    files = []
    for tid, name in FILES.items():
        url = url_for(name)
        with opener(url) as r:
            data = r.read()
        files.append({"id": tid, "repo_file": name, "url": url, "bytes": len(data), "sha256": sha256_bytes(data)})
    return {"repo": REPO, "commit": COMMIT, "license": "GPL-3.0 (not vendored)", "files": files}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--update-manifest", action="store_true")
    ap.add_argument("--dest", type=Path, default=DEST)
    a = ap.parse_args()
    if a.update_manifest:
        MANIFEST.write_text(json.dumps(build_manifest(), indent=2) + "\n", newline="\n")
        return
    fetch(json.loads(MANIFEST.read_text()), a.dest)
    print(f"{len(FILES)} traces verified in {a.dest}", file=sys.stderr)


if __name__ == "__main__":
    main()
