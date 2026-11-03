#!/usr/bin/env python3
"""Reference isolation geometry using the original FlatCAM parser + shapely.

Usage:
    tools/refenv/bin/python tools/flatcam_reference_isolation.py <file.gbr> \
        [--tool-dia 0.2] [--passes 1] [--overlap 0] [--quad-segs 16]
"""

import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from flatcam_reference import FakeApp, ORIGINAL, install_stubs  # noqa: E402


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        print(__doc__)
        return 2
    path = args[0]

    tool_dia = 0.2
    passes = 1
    overlap = 0.0
    quad = 16
    for arg in sys.argv[1:]:
        if arg.startswith("--tool-dia="):
            tool_dia = float(arg.split("=", 1)[1])
        elif arg.startswith("--passes="):
            passes = int(arg.split("=", 1)[1])
        elif arg.startswith("--overlap="):
            overlap = float(arg.split("=", 1)[1]) / 100.0
        elif arg.startswith("--quad-segs="):
            quad = int(arg.split("=", 1)[1])

    install_stubs()
    sys.path.insert(0, ORIGINAL)
    import builtins

    builtins._ = lambda s: s

    from shapely.ops import unary_union
    from appParsers.ParseGerber import Gerber
    from camlib import Geometry

    app = FakeApp()
    Gerber.app = app
    Geometry.app = app

    gerber = Gerber()
    gerber.parse_file(path)
    solid = gerber.solid_geometry
    if isinstance(solid, list):
        solid = unary_union(solid)

    polygons = list(solid.geoms) if hasattr(solid, "geoms") else [solid]

    paths = []
    for p in range(passes):
        offset = tool_dia * ((2 * p + 1) / 2.0) - p * overlap * tool_dia
        buffered = unary_union([g.buffer(offset, quad) for g in polygons])
        parts = list(buffered.geoms) if hasattr(buffered, "geoms") else [buffered]
        for part in parts:
            paths.append(part.exterior)
            paths.extend(part.interiors)

    total = sum(line.length for line in paths)
    bounds = unary_union(paths).bounds if paths else None

    print(json.dumps({
        "paths": len(paths),
        "length": float(total),
        "bounds": list(bounds) if bounds else None,
    }))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
