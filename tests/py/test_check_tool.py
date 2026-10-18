import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

import check  # noqa: E402


def test_default_preset_is_known(monkeypatch):
    monkeypatch.delenv("BCC_PRESET", raising=False)
    assert check.default_preset() in {"windows-msvc", "dev-mingw", "linux-gcc"}


def test_env_override(monkeypatch):
    monkeypatch.setenv("BCC_PRESET", "tsan")
    assert check.default_preset() == "tsan"
