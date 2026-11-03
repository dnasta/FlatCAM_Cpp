#!/usr/bin/env python3
"""Reference cutout geometry using the original FlatCAM parser + shapely."""

import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from flatcam_reference import FakeApp, ORIGINAL, install_stubs  # noqa: E402


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        print("usage: flatcam_reference_cutout.py <file.gbr> [--tool-dia=1] [--margin=0] [--internal]")
        return 2
    path = args[0]

    tool_dia = 1.0
    margin = 0.0
    external = True
    for arg in sys.argv[1:]:
        if arg.startswith("--tool-dia="):
            tool_dia = float(arg.split("=", 1)[1])
        elif arg.startswith("--margin="):
            margin = float(arg.split("=", 1)[1])
        elif arg == "--internal":
            external = False

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
    solid = unary_union(solid)

    radius = tool_dia / 2.0
    offset = margin + radius if external else margin - radius
    envelope = solid.buffer(offset, 16) if offset != 0.0 else solid

    parts = list(envelope.geoms) if hasattr(envelope, "geoms") else [envelope]
    # FlatCAM's exterior cut uses only the outer boundary of the offset
    # polygon; inner contours (holes in the outline) are opt-in.
    paths = [part.exterior for part in parts]

    print(json.dumps({
        "paths": len(paths),
        "length": float(sum(p.length for p in paths)),
        "bounds": list(unary_union(paths).bounds) if paths else None,
    }))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
