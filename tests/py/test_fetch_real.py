import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "traces"))

import fetch_real as f  # noqa: E402


def make_fixture(tmp_path):
    src = tmp_path / "src"
    src.mkdir()
    (src / "a.down").write_bytes(b"1\n2\n3\n")
    (src / "b.down").write_bytes(b"5\n9\n")
    files = []
    for tid, name in (("a", "a.down"), ("b", "b.down")):
        data = (src / name).read_bytes()
        files.append({"id": tid, "url": (src / name).as_uri(), "sha256": f.sha256_bytes(data)})
    return {"files": files}


def test_fetch_and_verify_local_fixture(tmp_path):
    m = make_fixture(tmp_path)
    dest = tmp_path / "real"
    f.fetch(m, dest)
    for e in m["files"]:
        assert f.verify_file(dest / f"{e['id']}.trace", e["sha256"])


def test_corrupt_existing_file_is_refetched(tmp_path):
    m = make_fixture(tmp_path)
    dest = tmp_path / "real"
    f.fetch(m, dest)
    (dest / "a.trace").write_bytes(b"corrupt")
    f.fetch(m, dest)
    assert f.verify_file(dest / "a.trace", m["files"][0]["sha256"])


def test_checksum_mismatch_raises(tmp_path):
    m = make_fixture(tmp_path)
    m["files"][0]["sha256"] = "0" * 64
    with pytest.raises(RuntimeError):
        f.fetch(m, tmp_path / "real")


def test_manifest_lists_the_six_pinned_traces():
    import json

    m = json.loads((ROOT / "traces" / "MANIFEST.json").read_text())
    assert m["commit"] == f.COMMIT
    assert [e["id"] for e in m["files"]] == list(f.FILES)
    assert all(len(e["sha256"]) == 64 for e in m["files"])
