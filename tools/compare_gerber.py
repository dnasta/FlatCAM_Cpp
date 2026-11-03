#!/usr/bin/env python3
"""Compares the C++ Gerber parser against the original FlatCAM parser.

Usage:
    tools/refenv/bin/python tools/compare_gerber.py <file.gbr> [--tolerance 1e-6]
"""

import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
CLI = HERE.parent / "build" / "flatcam-cli"
REFERENCE = HERE / "flatcam_reference.py"
PYTHON = HERE / "refenv" / "bin" / "python"


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        print(__doc__)
        return 2
    path = args[0]
    tolerance = 1e-6
    for arg in sys.argv[1:]:
        if arg.startswith("--tolerance="):
            tolerance = float(arg.split("=", 1)[1])

    ours = json.loads(subprocess.check_output([str(CLI), "gerber", path, "--json", "--flatcam-compat"]))
    ref = json.loads(subprocess.check_output([str(PYTHON), str(REFERENCE), path]))

    print(f"file       : {path}")
    print(f"reference  : area={ref['area']:.9f} bounds={ref['bounds']}")
    print(f"c++        : area={ours['area']:.9f} bounds={ours['bounds']}")

    problems = []
    if ref["bounds"] and ours["bounds"]:
        for i, name in enumerate(["xmin", "ymin", "xmax", "ymax"]):
            delta = abs(ref["bounds"][i] - ours["bounds"][name])
            if delta > tolerance:
                problems.append(f"{name} delta {delta:.3e}")
    area_delta = abs(ref["area"] - ours["area"])
    if area_delta > max(tolerance, ref["area"] * 1e-6):
        problems.append(f"area delta {area_delta:.3e}")

    if problems:
        print("MISMATCH:")
        for p in problems:
            print("  -", p)
        return 1

    print("OK: geometry matches the original parser")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
