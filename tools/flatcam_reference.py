#!/usr/bin/env python3
"""Reference geometry extractor for FlatCAM 2024.4.

Runs the *original* Python Gerber parser (camlib/ParseGerber) headless by
stubbing its GUI and heavyweight optional dependencies, and prints the
resulting geometry as JSON so it can be compared against the C++ port.

Usage:
    tools/refenv/bin/python tools/flatcam_reference.py <file.gbr>
"""

import json
import os
import sys
import types
from pathlib import Path

HERE = Path(__file__).resolve().parent
ORIGINAL = os.environ.get(
    "FLATCAM_SRC",
    str(HERE.parent.parent / "originales" / "FlatCAM_2024_4" / "FlatCAM"),
)


def module(name, **attrs):
    mod = types.ModuleType(name)
    for key, value in attrs.items():
        setattr(mod, key, value)
    sys.modules[name] = mod
    return mod


def install_stubs():
    # Qt
    qt = module("PyQt5")
    qt_widgets = module("PyQt5.QtWidgets")
    qt_core = module("PyQt5.QtCore")

    class QApplication:
        @staticmethod
        def processEvents(*_args, **_kwargs):
            return None

    qt_widgets.QApplication = QApplication

    class QSettings:
        def __init__(self, *_args, **_kwargs):
            pass

        def value(self, *_args, **_kwargs):
            return None

        def contains(self, *_args, **_kwargs):
            return False

        def setValue(self, *_args, **_kwargs):
            return None

    qt_core.QSettings = QSettings
    qt.QtWidgets = qt_widgets
    qt.QtCore = qt_core

    # Heavy optional dependencies
    rtree = module("rtree")
    rtree_index = module("rtree.index")

    class Index:
        def __init__(self, *_args, **_kwargs):
            pass

        def insert(self, *_args, **_kwargs):
            pass

        def intersection(self, *_args, **_kwargs):
            return []

    rtree_index.Index = Index
    rtree.index = rtree_index

    lxml = module("lxml")
    lxml.etree = module("lxml.etree")
    lxml.etree.parse = lambda *_a, **_k: None
    lxml.etree.tostring = lambda *_a, **_k: b""

    descartes = module("descartes")
    descartes_patch = module("descartes.patch")
    descartes_patch.PolygonPatch = lambda *a, **k: None
    descartes.patch = descartes_patch

    rasterio = module("rasterio")
    rasterio_features = module("rasterio.features")
    rasterio_features.shapes = lambda *_a, **_k: []
    rasterio.features = rasterio_features

    module("ezdxf")

    ortools = module("ortools")
    constraint = module("ortools.constraint_solver")
    constraint.pywrapcp = module("ortools.constraint_solver.pywrapcp")
    constraint.routing_enums_pb2 = module("ortools.constraint_solver.routing_enums_pb2")
    ortools.constraint_solver = constraint

    # FlatCAM application modules
    app_common = module("appCommon")
    common = module("appCommon.Common")

    class GracefulException(Exception):
        pass

    common.GracefulException = GracefulException
    app_common.Common = common

    debug_enabled = os.environ.get("FC_REF_DEBUG") == "1"

    def log_message(level, args, kwargs):
        if not debug_enabled:
            return
        msg = args[0] if args else ""
        try:
            print(f"[{level}] {msg % args[1:] if len(args) > 1 else msg}", file=sys.stderr)
        except Exception:
            print(f"[{level}] {msg}", file=sys.stderr)

    class Logger:
        def debug(self, *args, **kwargs):
            log_message("debug", args, kwargs)

        def info(self, *args, **kwargs):
            log_message("info", args, kwargs)

        def warning(self, *args, **kwargs):
            log_message("warning", args, kwargs)

        def error(self, *args, **kwargs):
            log_message("error", args, kwargs)

    app_logger = module("appLogger")
    app_logger.getLogger = lambda *_a, **_k: Logger()

    app_translation = module("appTranslation")
    app_translation.apply_language = lambda *_a, **_k: (lambda s: s)

    module("appParsers.ParseSVG", re=__import__("re"), math=__import__("math"),
           np=__import__("numpy"), getLogger=lambda *_a, **_k: Logger(),
           svgparselength=lambda *_a, **_k: (0, None),
           getsvggeo=lambda *_a, **_k: None,
           svgparse_viewbox=lambda *_a, **_k: 1.0)
    import shapely.geometry as shp_geometry
    import shapely.ops as shp_ops

    dxf_attrs = {
        key: value
        for key, value in vars(shp_geometry).items()
        if not key.startswith("_")
    }
    dxf_attrs.update({
        "getLogger": lambda *_a, **_k: Logger(),
        "re": __import__("re"),
        "math": __import__("math"),
        "np": __import__("numpy"),
        "unary_union": shp_ops.unary_union,
        "sys": sys,
        "traceback": __import__("traceback"),
    })
    module("appParsers.ParseDXF", **dxf_attrs)


class FakeInform:
    def emit(self, *args, **_kwargs):
        if os.environ.get("FC_REF_DEBUG") == "1" and args:
            print(f"[inform] {args[0]}", file=sys.stderr)


class FakePlotCanvas:
    def new_shape_collection(self, **_kwargs):
        return object()


class FakeApp:
    decimals = 4
    is_legacy = False
    abort_flag = False
    options = {}

    defaults = {
        "units": "MM",
        "gerber_circle_steps": 64,
        "gerber_def_units": "IN",
        "gerber_def_zeros": "L",
        "gerber_simplification": False,
        "gerber_simp_tolerance": 0.0001,
        "gerber_use_buffer_for_union": False,
        "gerber_buffering": "full",
        "gerber_extra_buffering": False,
        "gerber_clean_apertures": True,
        "global_tolerance": 0.005,
    }

    class FakeProcContainer:
        new_text = ""
        text = ""

        def update_view_text(self, *_args, **_kwargs):
            return None

    def __init__(self):
        self.inform = FakeInform()
        self.plotcanvas = FakePlotCanvas()
        self.proc_container = self.FakeProcContainer()
        self.is_legacy = False
        self.abort_flag = False


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2

    install_stubs()
    sys.path.insert(0, ORIGINAL)
    import builtins

    builtins._ = lambda s: s

    from shapely.ops import unary_union
    from appParsers.ParseGerber import Gerber

    app = FakeApp()
    sys.modules["ParseGerber"] = sys.modules["appParsers.ParseGerber"]

    from camlib import Geometry

    Gerber.app = app
    Geometry.app = app
    gerber = Gerber()
    gerber.parse_file(sys.argv[1])

    solid = gerber.solid_geometry
    if isinstance(solid, list):
        solid = unary_union(solid)

    out = {
        "area": float(solid.area),
        "bounds": list(solid.bounds) if solid.bounds else None,
        "units": gerber.units,
        "apertures": len(gerber.apertures),
        "follow": len(gerber.follow_geometry or []),
        "valid": bool(solid.is_valid),
    }
    print(json.dumps(out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
