#!/usr/bin/env python3
"""Compares two NC files (FlatCAM production G-code vs this port).

Usage:
    tools/refenv/bin/python tools/compare_nc.py --kind cut ours.nc flatcam.nc
    tools/refenv/bin/python tools/compare_nc.py --kind drill ours.nc flatcam.nc
"""

import math
import re
import sys
from pathlib import Path

RAPID = re.compile(r"^G00\s+X(-?[\d.]+)\s+Y(-?[\d.]+)")
CUT = re.compile(r"^G01\s+Z(-?[\d.]+)")
MOVE = re.compile(r"^G01\s+X(-?[\d.]+)\s+Y(-?[\d.]+)")
TOOL = re.compile(r"^T(\d+)\s*$")
LIFT = re.compile(r"^G00\s+Z")


def parse(path):
    tool = None
    x = y = 0.0
    z = None
    paths = []
    current = []
    drills = {}
    last_xy = None

    for line in Path(path).read_text(errors="replace").splitlines():
        line = line.strip()
        m = TOOL.match(line)
        if m:
            tool = int(m.group(1))
            drills.setdefault(tool, [])
            continue
        m = RAPID.match(line)
        if m:
            last_xy = (float(m.group(1)), float(m.group(2)))
            x, y = last_xy
            continue
        m = CUT.match(line)
        if m:
            z = float(m.group(1))
            if z < 0 and last_xy:
                if not current:
                    current = [last_xy]
                if tool is not None:
                    drills.setdefault(tool, []).append(last_xy)
            continue
        m = MOVE.match(line)
        if m and z is not None and z < 0 and current:
            x, y = float(m.group(1)), float(m.group(2))
            current.append((x, y))
            continue
        if LIFT.match(line):
            if current:
                paths.append(current)
                current = []
            z = None

    if current:
        paths.append(current)
    return {"paths": paths, "drills": drills}


def length(paths):
    total = 0.0
    for path in paths:
        for i in range(1, len(path)):
            total += math.dist(path[i - 1], path[i])
    return total


def bounds(paths):
    xs = [p[0] for path in paths for p in path]
    ys = [p[1] for path in paths for p in path]
    return min(xs), min(ys), max(xs), max(ys)


def compare_cut(ours, ref):
    problems = []
    if len(ours["paths"]) != len(ref["paths"]):
        problems.append(f"paths {len(ours['paths'])} != {len(ref['paths'])}")

    la, lb = length(ours["paths"]), length(ref["paths"])
    if abs(la - lb) > 1e-2:
        problems.append(f"cut length {la:.4f} != {lb:.4f}")

    ba, bb = bounds(ours["paths"]), bounds(ref["paths"])
    for name, a, b in zip(["xmin", "ymin", "xmax", "ymax"], ba, bb):
        if abs(a - b) > 1e-2:
            problems.append(f"{name} {a:.4f} != {b:.4f}")

    print(f"cut length : {la:.4f} mm / FlatCAM {lb:.4f} mm")
    print(f"paths      : {len(ours['paths'])} / FlatCAM {len(ref['paths'])}")
    print(f"bounds     : {ba} / {bb}")
    return problems


def compare_drill(ours, ref, mirror_x=False):
    problems = []
    if mirror_x:
        for tool, points in ours["drills"].items():
            ours["drills"][tool] = [(-x, y) for x, y in points]
    if sorted(ours["drills"]) != sorted(ref["drills"]):
        problems.append(f"tools {sorted(ours['drills'])} != {sorted(ref['drills'])}")

    for tool in ref["drills"]:
        a = sorted((round(x, 3), round(y, 3)) for x, y in ours["drills"].get(tool, []))
        b = sorted((round(x, 3), round(y, 3)) for x, y in ref["drills"][tool])
        if a != b:
            problems.append(f"T{tool}: {len(a)} holes != {len(b)}")
        print(f"T{tool}: {len(a)} taladros / FlatCAM {len(b)}")
    return problems


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    kind = None
    mirror_x = False
    files = []
    args = sys.argv[1:]
    i = 0
    while i < len(args):
        arg = args[i]
        if arg == "--kind" and i + 1 < len(args):
            kind = args[i + 1]
            i += 2
            continue
        if arg == "--mirror-x":
            mirror_x = True
        elif arg.startswith("--kind="):
            kind = arg.split("=", 1)[1]
        elif not arg.startswith("--"):
            files.append(arg)
        i += 1
    if kind not in ("cut", "drill") or len(files) != 2:
        print(__doc__)
        return 2

    ours = parse(files[0])
    ref = parse(files[1])
    problems = (compare_cut(ours, ref) if kind == "cut"
                else compare_drill(ours, ref, mirror_x=mirror_x))

    if problems:
        print("MISMATCH:")
        for p in problems:
            print("  -", p)
        return 1
    print("OK: el G-code coincide con el de FlatCAM (normalizado)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
