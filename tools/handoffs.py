#!/usr/bin/env python3
"""List the handoffs at the git tag `history-archive` newest-first with each file's first heading.

The handoffs are the recompilation's per-session record up to the fork. They
left the working tree on 2026-11-01 and are kept at the tag
(`docs/history/README.md`). This prints the whole sequence so "which is the
newest" and "what did session N set out to do" are one command
(`make handoffs`). Read one with
`git show history-archive:docs/history/handoffs/<file>`.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TAG = "history-archive"
FOLDER = "docs/history/handoffs/"


def git(*args: str) -> str:
    return subprocess.run(["git", "-C", REPO, *args], check=True,
                          capture_output=True, text=True, encoding="utf-8",
                          errors="replace").stdout


def sort_key(path: str) -> tuple[str, int]:
    name = os.path.basename(path)
    date = re.search(r"(\d{4}-\d{2}-\d{2})", name)
    session = re.search(r"session(\d+)", name)
    return (date.group(1) if date else "", int(session.group(1)) if session else 0)


def main() -> int:
    sys.stdout.reconfigure(errors="replace")
    try:
        listing = git("ls-tree", "--name-only", TAG, FOLDER)
    except subprocess.CalledProcessError:
        print("The tag %s is missing; run `git fetch --tags`." % TAG)
        return 1
    paths = [p for p in listing.splitlines()
             if re.match(r"HANDOFF-.*\.md$", os.path.basename(p))]
    for path in sorted(paths, key=sort_key, reverse=True):
        first = git("show", "%s:%s" % (TAG, path)).split("\n", 1)[0]
        print("%-44s %s" % (os.path.basename(path), first.lstrip("# ").strip()))
    print("\n%d handoff(s) at %s." % (len(paths), TAG))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
