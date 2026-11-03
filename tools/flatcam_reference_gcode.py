#!/usr/bin/env python3
"""Reference drilling G-code produced by the original FlatCAM CNCjob.

Usage:
    tools/refenv/bin/python tools/flatcam_reference_gcode.py <file.drl> \
        [--z-cut -1.5] [--z-move 2] [--feed 60] [--spindle 1000]

The output is the G-code that FlatCAM's `default` preprocessor would generate
for the drilling job (travel path simplification is stubbed, so only the
sequence of plunge/rapid moves is compared).
"""

import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from flatcam_reference import FakeApp, ORIGINAL, install_stubs  # noqa: E402


class Workspace:
    def __init__(self):
        import importlib

        from camlib import CNCjob
        from appPreProcessor import preprocessors as pp_registry

        importlib.import_module("preprocessors.default")  # registers "default"

        self.CNCjob = CNCjob
        self.preprocessors = pp_registry


class Travel:
    @staticmethod
    def travel_coordinates(start_point, end_point, tooldia):
        # Straight move to the target at travel height.
        return [(None, (end_point[0], end_point[1]))]


def tool_data(dia, z_cut, z_move, feed):
    return {
        "tools_drill_cutz": z_cut,
        "tools_drill_multidepth": False,
        "tools_drill_depthperpass": abs(z_cut),
        "tools_drill_travelz": z_move,
        "tools_drill_f_plunge": False,
        "tools_drill_f_retract": True,
        "tools_drill_feedrate_z": feed,
        "tools_drill_feedrate_rapid": feed * 10,
        "tools_drill_feedrate_probe": feed,
        "tools_drill_spindlespeed": 1000,
        "tools_drill_dwell": False,
        "tools_drill_dwelltime": 1000,
        "tools_drill_spindledir": "CW",
        "tools_drill_toolchangez": 10.0,
        "tools_drill_toolchangexy": "",
        "tools_drill_startz": 5.0,
        "tools_drill_endz": 5.0,
        "tools_drill_endxy": "",
        "tools_drill_z_pdepth": 0.1,
        "tools_drill_offset": 0.0,
        "tools_drill_type": "drill",
    }


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        print(__doc__)
        return 2
    path = args[0]

    z_cut = -1.5
    z_move = 2.0
    feed = 60.0
    for arg in sys.argv[1:]:
        if arg.startswith("--z-cut="):
            z_cut = float(arg.split("=", 1)[1])
        elif arg.startswith("--z-move="):
            z_move = float(arg.split("=", 1)[1])
        elif arg.startswith("--feed="):
            feed = float(arg.split("=", 1)[1])

    install_stubs()
    sys.path.insert(0, ORIGINAL)
    import builtins

    builtins._ = lambda s: s

    from appParsers.ParseExcellon import Excellon
    from camlib import Geometry

    ws = Workspace()
    preprocessors = ws.preprocessors

    class App(FakeApp):
        defaults = dict(FakeApp.defaults)
        defaults.update({
            "cncjob_coords_type": "G90",
            "cncjob_steps_per_circle": 64,
            "excellon_search_time": 1,
        })

        def __init__(self):
            super().__init__()
            self.preprocessors = preprocessors
            self.exc_areas = Travel()

            class Log:
                def debug(self, *a, **k):
                    pass

                info = warning = debug

                def error(self, *a, **k):
                    print("[log error]", a, file=sys.stderr)

            self.log = Log()

        def dec_format(self, value, decimals):
            return f"{value:.{decimals}f}"

    app = App()

    # Parse the Excellon file with the original parser.
    Excellon.app = app
    Geometry.app = app
    Excellon.default_data = {}
    Geometry.default_data = {}
    exc = Excellon()
    with open(path) as f:
        exc.parse_lines(f.readlines())

    tools = {}
    for number, tool in exc.tools.items():
        drills = tool.get("drills", [])
        if not drills:
            continue
        tools[number] = {
            "tooldia": tool["tooldia"],
            "drills": drills,
            "data": tool_data(tool["tooldia"], z_cut, z_move, feed),
        }
    if not tools:
        print("ERROR: no drills", file=sys.stderr)
        return 1

    app.exc_tools = tools
    app.options = {
        "type": "Excellon",
        "xmin": min(d.x for t in tools.values() for d in t["drills"]),
        "xmax": max(d.x for t in tools.values() for d in t["drills"]),
        "ymin": min(d.y for t in tools.values() for d in t["drills"]),
        "ymax": max(d.y for t in tools.values() for d in t["drills"]),
        "Tools_in_use": [[str(n), t["tooldia"], len(t["drills"])] for n, t in tools.items()],
    }

    ws.CNCjob.app = app
    Geometry.app = app
    job = ws.CNCjob(units="MM", kind="drill")
    job.use_ui = False
    job.options = app.options
    job.exc_tools = tools
    job.exc_cnc_tools = {}
    job.toolchange = True
    job.xy_toolchange = None
    job.xy_end = None
    job.startz = 5.0
    job.z_end = 5.0
    job.z_toolchange = 10.0
    job.z_depthpercut = abs(z_cut)
    job.multidepth = False
    job.z_pdepth = 0.1
    job.feedrate_probe = feed
    job.coordinates_type = "G90"
    job.coords_decimals = 4
    job.fr_decimals = 2
    job.f_plunge = False
    job.f_retract = True
    job.dwell = False
    job.dwelltime = 1000
    job.spindlespeed = 1000
    job.spindledir = "CW"
    job.pp_excellon_name = "default"
    job.pp_geometry_name = "default"
    job.steps_per_circle = 64

    lines = []
    first = True
    keys = sorted(tools.keys())
    from shapely.geometry import Point

    for index, number in enumerate(keys):
        points = [Point(d.x, d.y) for d in tools[number]["drills"]]
        result = job.excellon_tool_gcode_gen(
            tool=number,
            points=points,
            tools=tools,
            first_pt=(0.0, 0.0),
            is_first=first,
            is_last=(index == len(keys) - 1),
            opt_type="T",
            toolchange=True,
        )
        if result == "fail":
            print("ERROR: tool gcode generation failed", file=sys.stderr)
            return 1
        gcode, _last_pt, start_gcode = result
        if start_gcode:
            lines.append(start_gcode)
        lines.append(gcode)
        first = False

    print("".join(lines))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
