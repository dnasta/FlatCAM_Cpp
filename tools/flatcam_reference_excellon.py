#!/usr/bin/env python3
"""Reference geometry extractor for the original FlatCAM Excellon parser."""

import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from flatcam_reference import FakeApp, ORIGINAL, install_stubs  # noqa: E402


def main():
    if len(sys.argv) < 2:
        print("usage: flatcam_reference_excellon.py <file.drl>")
        return 2

    install_stubs()
    sys.path.insert(0, ORIGINAL)
    import builtins

    builtins._ = lambda s: s

    from shapely.ops import unary_union
    from appParsers.ParseExcellon import Excellon
    from camlib import Geometry

    app = FakeApp()
    Excellon.app = app
    Geometry.app = app
    Excellon.default_data = {}
    Geometry.default_data = {}

    exc = Excellon()
    with open(sys.argv[1], "r") as f:
        exc.parse_lines(f.readlines())
    exc.create_geometry()

    solid = exc.solid_geometry
    if isinstance(solid, list):
        solid = unary_union(solid)

    drills = sum(len(t.get("drills", [])) for t in exc.tools.values())
    slots = sum(len(t.get("slots", [])) for t in exc.tools.values())
    out = {
        "area": float(solid.area),
        "bounds": list(solid.bounds) if solid.bounds else None,
        "tools": len(exc.tools),
        "drills": drills,
        "slots": slots,
        "units": exc.units,
    }
    print(json.dumps(out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
