#!/usr/bin/env python3
"""Print the latest GitHub Actions runs and failing steps (public repo, no auth needed)."""
import json
import sys
import urllib.request

REPO = "aceqbit/BudgetCC"


def get(url):
    return json.load(urllib.request.urlopen(url))


def main() -> int:
    runs = get(f"https://api.github.com/repos/{REPO}/actions/runs?per_page=3")["workflow_runs"]
    bad = 0
    for r in runs:
        print(r["head_sha"][:7], r["status"], r["conclusion"], r["display_title"][:60])
    r = runs[0]
    for j in get(f"https://api.github.com/repos/{REPO}/actions/runs/{r['id']}/jobs")["jobs"]:
        print(" ", j["name"], j["status"], j["conclusion"])
        for s in j["steps"]:
            if s["conclusion"] not in ("success", "skipped", None):
                print("     FAILED step:", s["name"])
                bad += 1
    return 1 if bad or r["conclusion"] not in ("success", None) else 0


if __name__ == "__main__":
    sys.exit(main())
