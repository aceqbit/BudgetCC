#!/usr/bin/env python3
"""Print the annotations (failure tails) of failed jobs of the latest GitHub Actions run."""
import json
import urllib.request

REPO = "aceqbit/BudgetCC"


def get(url):
    return json.load(urllib.request.urlopen(url))


def main() -> int:
    run = get(f"https://api.github.com/repos/{REPO}/actions/runs?per_page=1")["workflow_runs"][0]
    for j in get(run["jobs_url"])["jobs"]:
        if j["conclusion"] != "failure":
            continue
        print("=====", j["name"])
        for a in get(f"https://api.github.com/repos/{REPO}/check-runs/{j['id']}/annotations"):
            if "Node.js" not in a["message"] and "ubuntu-latest" not in a["message"]:
                print(a["message"][:6000])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
