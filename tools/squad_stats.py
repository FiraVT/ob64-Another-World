#!/usr/bin/env python3
"""Statistics of the vanilla enemy squads (docs/anotherworld/design-balance-master-plan.md 2).

Reads the per-scene squad lists in ../gameinfo/mission detail/*.md and the
class sizes and best rows in docs/class-catalog.md.

    python tools/squad_stats.py [gameinfo-dir]
"""
import collections
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
GAMEINFO = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(REPO), "gameinfo")

# Names in the mission pages that differ from the catalog's.
ALIASES = {
    "cocatrice": "cockatrice",
    "centurion (female)": "centurion",
    "centurion (male)": "centurion",
    "zombie (male)": "zombie",
    "zombie (female)": "zombie",
    "lycanthrope/werewolf": "lycanthrope",
    "temple command": "temple commander",
    "superior knight": "knight",
    "gatekeeper*": "gatekeeper",
    "swordmaster": "sword master",
}


def norm(name):
    name = name.strip().lower()
    return ALIASES.get(name, name)


def load_catalog():
    text = open(os.path.join(REPO, "docs", "class-catalog.md"), encoding="utf-8").read()
    size, best = {}, {}
    for m in re.finditer(r"^\| (\d+) \| ([^|]+?) \| (\w+) \| (\d) \| (\d) \| \d+ \| \d+ \| (\w+) \|", text, re.M):
        # Some catalog names keep the game's text codes: 0x0E/0x0F around the
        # name and 0x10 'c' for a space ("\x0ePlatinum\x10cDragon\x0f").
        name = re.sub(r"[\x0e\x0f]", "", m.group(2).replace("\x10c", " ")).strip().lower()
        size.setdefault(name, int(m.group(4)))
        best.setdefault(name, m.group(6))
    return size, best


def load_squads():
    squads = []
    paths = glob.glob(os.path.join(GAMEINFO, "mission detail", "*.md"))
    for path in sorted(paths, key=lambda p: int(re.search(r"Scene_(\d+)", p).group(1))):
        scene = int(re.search(r"Scene_(\d+)", path).group(1))
        text = open(path, encoding="utf-8").read()
        for block in re.split(r"\nClass\tLevel\tPosition\n", text)[1:]:
            members = []
            for line in block.split("\n"):
                m = re.match(r"(Unit Leader:)?([^\t]+)\t([^\t]*)\t(.+)", line)
                if not m:
                    break
                members.append((bool(m.group(1)), m.group(2).strip(), m.group(3).strip(), m.group(4).strip()))
            if members:
                squads.append((scene, members))
    return squads


def cell(pos):
    parts = [p.strip().lower() for p in pos.split(",")]
    rows = {"front": 0, "middle": 1, "back": 2}
    cols = {"left": 0, "center": 1, "right": 2}
    if len(parts) != 2 or parts[0] not in rows or parts[1] not in cols:
        return None
    return rows[parts[0]], cols[parts[1]]


def main():
    size, best = load_catalog()
    squads = load_squads()
    unknown = collections.Counter()
    n_large = collections.Counter()
    groups = collections.Counter()
    sizes = collections.Counter()
    row_fit = collections.Counter()
    adjacency = collections.Counter()
    for _, members in squads:
        classes = [norm(c) for _, c, _, _ in members]
        unknown.update(c for c in classes if c not in size)
        n_large[sum(1 for c in classes if size.get(c, 1) == 2)] += 1
        groups[len(set(classes[1:]))] += 1
        sizes[len(members)] += 1
        cells = [(norm(c), cell(p)) for _, c, _, p in members]
        for c, pos in cells:
            row = None if pos is None else ("front", "middle", "back")[pos[0]]
            if best.get(c) and row:
                row_fit["fit" if best[c] == row else "off"] += 1
            if size.get(c, 1) == 2 and pos:
                near = [q for _, q in cells if q and q != pos and max(abs(q[0] - pos[0]), abs(q[1] - pos[1])) <= 1]
                adjacency["large with a neighbour" if near else "large alone"] += 1
        large = [q for c, q in cells if size.get(c, 1) == 2]
        if len(large) == 2 and None not in large:
            (r1, c1), (r2, c2) = large
            adjacency["two large, adjacent" if max(abs(r1 - r2), abs(c1 - c2)) <= 1 else "two large, apart"] += 1

    print("squads", len(squads))
    print("members per squad", sorted(sizes.items()))
    print("distinct member classes", sorted(groups.items()))
    print("large members per squad", sorted(n_large.items()))
    print("large placement", dict(adjacency))
    print("members in their best row", dict(row_fit))
    print("names not in the catalog", unknown.most_common(10))
    by_stage = collections.defaultdict(list)
    for scene, members in squads:
        stage = "1-9" if scene <= 9 else "10-19" if scene <= 19 else "20-29" if scene <= 29 else "30-43"
        by_stage[stage].append(len(members))
    for k in ("1-9", "10-19", "20-29", "30-43"):
        v = by_stage[k]
        print("scenes", k, "squads", len(v), "average members", round(sum(v) / len(v), 2))
    print("leaders", collections.Counter(m[0][1] for _, m in squads).most_common(8))


if __name__ == "__main__":
    main()

