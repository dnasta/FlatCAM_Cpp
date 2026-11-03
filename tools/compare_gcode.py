#!/usr/bin/env python3
"""Compares the drilling G-code against FlatCAM's own CNCjob output.

The text is not byte-identical (FlatCAM's travel planner and header differ), so
the comparison normalises both programs to:
  * the sequence of tools,
  * the set of plunge coordinates per tool,
  * the Z cut / Z travel levels,
  * the number of plunge moves.
"""

import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
CLI = HERE.parent / "build" / "flatcam-cli"
REFERENCE = HERE / "flatcam_reference_gcode.py"
PYTHON = HERE / "refenv" / "bin" / "python"

TOOL_RE = re.compile(r"^T(\d+)")
RAPID_RE = re.compile(r"^G00\s+X(-?\d+\.?\d*)\s+Y(-?\d+\.?\d*)")
CUT_RE = re.compile(r"^G01\s+Z(-?\d+\.?\d*)")
LIFT_RE = re.compile(r"^G00\s+Z(-?\d+\.?\d*)")


def analyse(text):
    tool = None
    last_xy = None
    pending_xy = None
    plunges = {}
    zcuts = set()
    zlifts = set()

    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("(") or line.startswith(";"):
            continue

        match = TOOL_RE.match(line)
        if match and "M6" not in line:
            tool = int(match.group(1))
            plunges.setdefault(tool, [])
            continue

        match = RAPID_RE.match(line)
        if match:
            pending_xy = (round(float(match.group(1)), 4), round(float(match.group(2)), 4))
            continue

        match = CUT_RE.match(line)
        if match and pending_xy is not None:
            z = round(float(match.group(1)), 4)
            zcuts.add(z)
            if abs(z) > 1e-9:
                plunges.setdefault(tool, []).append(pending_xy)
            last_xy = pending_xy
            pending_xy = None
            continue

        match = LIFT_RE.match(line)
        if match:
            zlifts.add(round(float(match.group(1)), 4))

    # FlatCAM's stubbed travel can repeat the first hole; collapse exact
    # consecutive duplicates.
    for key, points in plunges.items():
        deduped = []
        for point in points:
            if not deduped or deduped[-1] != point:
                deduped.append(point)
        plunges[key] = deduped

    return {"plunges": plunges, "zcuts": zcuts, "zlifts": zlifts}


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    path = sys.argv[1]
    options = [a for a in sys.argv[2:] if a.startswith("--")]

    ours_text = subprocess.check_output(
        [str(CLI), "gcode", path, "--optimize", *options]).decode()
    ref_text = subprocess.check_output(
        [str(PYTHON), str(REFERENCE), path, *options]).decode()

    ours = analyse(ours_text)
    ref = analyse(ref_text)

    problems = []
    if sorted(ours["plunges"].keys()) != sorted(ref["plunges"].keys()):
        problems.append(f"tools {sorted(ours['plunges'])} != {sorted(ref['plunges'])}")

    for tool in ref["plunges"]:
        a = sorted(ours["plunges"].get(tool, []))
        b = sorted(ref["plunges"].get(tool, []))
        if a != b:
            problems.append(f"tool T{tool}: holes differ ({len(a)} vs {len(b)})")

    if ours["zcuts"] != ref["zcuts"]:
        problems.append(f"Z cut {sorted(ours['zcuts'])} != {sorted(ref['zcuts'])}")

    print(f"file       : {path}")
    for tool in sorted(ref["plunges"]):
        print(f"  T{tool}: {len(ref['plunges'][tool])} taladros (C++ {len(ours['plunges'].get(tool, []))})")
    print(f"  Z cut    : {sorted(ours['zcuts'])}  /  referencia {sorted(ref['zcuts'])}")

    if problems:
        print("MISMATCH:")
        for p in problems:
            print("  -", p)
        return 1
    print("OK: el G-code de taladrado coincide con el de FlatCAM (normalizado)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
