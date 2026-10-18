"""Helpers for running a command in CI and surfacing its failure as a GitHub annotation.

Annotations are readable through the public API without authentication, which keeps CI failures diagnosable.
"""
import os
import re
import subprocess
import sys

_INTERESTING = re.compile(r"error|FAILED|Failure|Expected|Which is|Actual|undefined reference|fatal|Traceback|Assertion", re.I)


def annotate(title: str, text: str, limit: int = 3500) -> None:
    if not os.environ.get("GITHUB_ACTIONS"):
        return
    lines = text.splitlines()
    picked = [ln for ln in lines if _INTERESTING.search(ln)]
    body = "\n".join(picked[:60] if picked else lines[-30:])
    body = body[-limit:].replace("%", "%25").replace("\r", "").replace("\n", "%0A")
    print(f"::error title={title}::{body}", flush=True)


def run_logged(cmd, title: str, cwd=None) -> int:
    """Run cmd, echo its output, keep it, and annotate on failure. Returns the exit code."""
    print("+", " ".join(map(str, cmd)), flush=True)
    p = subprocess.Popen(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    captured = []
    assert p.stdout is not None
    for line in p.stdout:
        sys.stdout.write(line)
        captured.append(line)
        if len(captured) > 4000:
            del captured[:1000]
    rc = p.wait()
    if rc != 0:
        annotate(title, "".join(captured))
    return rc
