#!/usr/bin/env python3
"""Compares the C++ isolation geometry against shapely + the original parser.

Usage:
    tools/refenv/bin/python tools/compare_isolation.py <file.gbr> \
        [--tool-dia=0.2] [--passes=1] [--overlap=0] [--quad-segs=16]
"""

import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
CLI = HERE.parent / "build" / "flatcam-cli"
REFERENCE = HERE / "flatcam_reference_isolation.py"
PYTHON = HERE / "refenv" / "bin" / "python"


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        print(__doc__)
        return 2
    path = args[0]
    options = [a for a in sys.argv[1:] if a.startswith("--")]

    ours = json.loads(subprocess.check_output(
        [str(CLI), "isolate", path, "--json", "--flatcam-compat", *options]))
    ref = json.loads(subprocess.check_output(
        [str(PYTHON), str(REFERENCE), path, *options]))

    print(f"file       : {path}  {' '.join(options)}")
    print(f"reference  : paths={ref['paths']} length={ref['length']:.9f} bounds={ref['bounds']}")
    print(f"c++        : paths={ours['paths']} length={ours['length']:.9f} bounds={ours['bounds']}")

    problems = []
    if ref["paths"] != ours["paths"]:
        problems.append(f"paths {ref['paths']} != {ours['paths']}")
    if abs(ref["length"] - ours["length"]) > max(1e-6, ref["length"] * 1e-9):
        problems.append(f"length delta {abs(ref['length'] - ours['length']):.3e}")
    if ref["bounds"] and ours["bounds"]:
        for i, name in enumerate(["xmin", "ymin", "xmax", "ymax"]):
            delta = abs(ref["bounds"][i] - ours["bounds"][name])
            if delta > 1e-6:
                problems.append(f"{name} delta {delta:.3e}")

    if problems:
        print("MISMATCH:")
        for p in problems:
            print("  -", p)
        return 1
    print("OK: isolation geometry matches the shapely reference")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
