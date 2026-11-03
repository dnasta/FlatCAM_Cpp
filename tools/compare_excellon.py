#!/usr/bin/env python3
"""Compares the C++ Excellon parser against the original FlatCAM parser."""

import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
CLI = HERE.parent / "build" / "flatcam-cli"
REFERENCE = HERE / "flatcam_reference_excellon.py"
PYTHON = HERE / "refenv" / "bin" / "python"


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    path = sys.argv[1]
    tolerance = 1e-6
    for arg in sys.argv[1:]:
        if arg.startswith("--tolerance="):
            tolerance = float(arg.split("=", 1)[1])

    ours = json.loads(subprocess.check_output([str(CLI), "excellon", path, "--json"]))
    ref = json.loads(subprocess.check_output([str(PYTHON), str(REFERENCE), path]))

    print(f"file       : {path}")
    print(f"reference  : area={ref['area']:.9f} bounds={ref['bounds']} drills={ref['drills']} slots={ref['slots']}")
    print(f"c++        : area={ours['area']:.9f} bounds={ours['bounds']} drills={ours['drills']} slots={ours['slots']}")

    problems = []
    if ref["drills"] != ours["drills"]:
        problems.append(f"drills {ref['drills']} != {ours['drills']}")
    if ref["slots"] != ours["slots"]:
        problems.append(f"slots {ref['slots']} != {ours['slots']}")
    if ref["bounds"] and ours["bounds"]:
        for i, name in enumerate(["xmin", "ymin", "xmax", "ymax"]):
            delta = abs(ref["bounds"][i] - ours["bounds"][name])
            if delta > tolerance:
                problems.append(f"{name} delta {delta:.3e}")
    if abs(ref["area"] - ours["area"]) > max(tolerance, ref["area"] * 1e-6):
        problems.append(f"area delta {abs(ref['area'] - ours['area']):.3e}")

    if problems:
        print("MISMATCH:")
        for p in problems:
            print("  -", p)
        return 1
    print("OK: geometry matches the original parser")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
