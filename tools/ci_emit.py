#!/usr/bin/env python3
"""Turn stdin into one GitHub Actions ::error:: annotation (multi-line text survives in the annotation)."""
import sys

text = sys.stdin.read()[:3500]
print("::error::" + text.replace("%", "%25").replace("\r", "").replace("\n", "%0A"))
