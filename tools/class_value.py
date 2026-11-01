#!/usr/bin/env python3
"""The class value of war_director.cpp class_value(), from the ROM, for checking.

    python tools/class_value.py [level]
"""
import os, struct, sys
rom = open(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "assets", "ogre64.z64"), "rb").read()
S = lambda v: v - 0x8016AF80 + 0x40E80
C = S(0x80187C34)
A = 0x6099C


def name(c):
    ptr = struct.unpack(">I", rom[C + c * 0x48:C + c * 0x48 + 4])[0]
    o = S(ptr)
    return rom[o:o + 24].split(b"\0")[0].replace(b"\x10c", b" ").replace(b"\x0e", b"").replace(b"\x0f", b"").decode("latin-1")


def stat(r, i, lvl):  # HP STR VIT INT MEN AGI DEX
    return r[0x08 + 4 * i + 1] + lvl * r[0x08 + 4 * i + 2]


def attack_weight(a):
    e = rom[A + a * 0x10:A + a * 0x10 + 16]
    kind, target, effect, formula = e[4], e[6], e[7], e[9]
    if kind == 255 or (kind == 0 and formula == 0):
        return 0.0, 1          # no attack / odd (Vampire's Peek)
    reach = {1: 1.0, 2: 1.5, 3: 1.6, 4: 1.0, 5: 1.4, 6: 2.0, 9: 1.0, 10: 1.0}.get(target, 1.0)
    if kind in (0, 1):
        return 1.0 * reach, (1 if kind == 0 else 6)       # STR or DEX
    if kind == 3:
        return 1.5, 3                                      # a gear spell: INT, usually several targets
    if kind == 5:
        return 1.2 * reach, 13                             # breath: (STR + INT) / 2
    # specials
    if effect == 2:
        return 0.9, 4                                      # boost / heal an ally: MEN
    if e[10] == 0x08 and e[11] == 0x04:
        return 0.6 * reach, 3                              # sleep (Abyss, Mesmerize)
    if e[10] == 0x08 and e[11] in (0x05, 0x06) and kind == 4 and target >= 9:
        return 0.5, 3                                      # a debuff kiss
    if effect == 3:
        return 0.8, 3                                      # petrify gaze
    return 1.0 * reach, 3                                  # damaging specials (Banish, Wind Storm ...)


def value(c, lvl):
    r = rom[C + c * 0x48:C + (c + 1) * 0x48]
    hp, st, vi, it, me, ag, dx = (stat(r, i, lvl) for i in range(7))
    survive = (hp + 0.6 * vi + 0.3 * me + 0.2 * ag) / 100.0
    best = 0.0
    for k in range(3):
        a, n = r[0x37 + 2 * k], r[0x38 + 2 * k]
        if n > 9:
            continue
        w, s = attack_weight(a)
        power = {1: st, 6: dx, 3: it, 4: me, 13: (st + it) / 2}[s]
        best = max(best, n * w * power / 50.0)
    return 10.0 * survive * best


lvl = int(sys.argv[1]) if len(sys.argv) > 1 else 33
groups = [("Gremlin", [52, 74, 73, 48, 38]), ("melee", [53, 54, 55, 47, 5, 21, 22, 115, 116]),
          ("hawk", [39, 40, 41, 72, 70]), ("casters", [12, 27, 16, 31, 17, 18, 32, 51])]
for title, ids in groups:
    print(title)
    for c in ids:
        print(f"  {name(c):16s} {value(c, lvl):7.1f}")

