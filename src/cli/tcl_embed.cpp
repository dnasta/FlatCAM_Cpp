#ifdef FC_HAVE_TCL

#include <tcl.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "flatcam/cutout.hpp"
#include "flatcam/excellon.hpp"
#include "flatcam/gcode.hpp"
#include "flatcam/gerber.hpp"
#include "flatcam/isolation.hpp"
#include "flatcam/ncc.hpp"
#include "flatcam/paint.hpp"
#include "flatcam/dxf.hpp"
#include "flatcam/svg.hpp"
#include "flatcam/thieving.hpp"

namespace fc {
namespace {

struct ShellState {
    GerberResult gerber;
    bool hasGerber = false;
    ExcellonResult excellon;
    bool hasExcellon = false;
    PathJob paths;
    DrillJob drills;
    bool hasPathJob = false;
    bool hasDrillJob = false;
};

const char* arg(Tcl_Interp*, Tcl_Obj* const objv[], int index, int count) {
    return index < count ? Tcl_GetString(objv[index]) : nullptr;
}

double numberArg(Tcl_Interp* interp, Tcl_Obj* const objv[], int index, int count, double fallback) {
    if (index >= count) return fallback;
    double value = fallback;
    if (Tcl_GetDoubleFromObj(interp, objv[index], &value) != TCL_OK) return fallback;
    return value;
}

void setPaths(ShellState& state, const std::vector<Geom>& paths, const BBox& bounds, double tool) {
    state.paths = PathJob{};
    state.paths.bounds = bounds;
    state.paths.toolDiameter = tool;
    for (const Geom& g : paths) {
        Ring ring = g.coords();
        if (ring.size() >= 2) state.paths.paths.emplace_back(ring.begin(), ring.end());
    }
    state.hasPathJob = true;
    state.hasDrillJob = false;
}

int cmdOpenGerber(ClientData data, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    auto* state = static_cast<ShellState*>(data);
    if (objc < 2) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("usage: open_gerber file", -1));
        return TCL_ERROR;
    }
    try {
        state->gerber = GerberParser().parseFile(Tcl_GetString(objv[1]));
        state->hasGerber = true;
        Tcl_SetObjResult(interp, Tcl_NewStringObj(Tcl_GetString(objv[1]), -1));
        return TCL_OK;
    } catch (const std::exception& e) {
        Tcl_SetResult(interp, const_cast<char*>(e.what()), TCL_VOLATILE);
        return TCL_ERROR;
    }
}

int cmdOpenExcellon(ClientData data, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    auto* state = static_cast<ShellState*>(data);
    if (objc < 2) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("usage: open_excellon file", -1));
        return TCL_ERROR;
    }
    try {
        state->excellon = ExcellonParser().parseFile(Tcl_GetString(objv[1]));
        state->drills = drillJobFromExcellon(state->excellon);
        state->hasExcellon = true;
        state->hasDrillJob = true;
        state->hasPathJob = false;
        Tcl_SetObjResult(interp, Tcl_NewStringObj(Tcl_GetString(objv[1]), -1));
        return TCL_OK;
    } catch (const std::exception& e) {
        Tcl_SetResult(interp, const_cast<char*>(e.what()), TCL_VOLATILE);
        return TCL_ERROR;
    }
}

int cmdIsolate(ClientData data, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    auto* state = static_cast<ShellState*>(data);
    if (!state->hasGerber) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("no gerber loaded", -1));
        return TCL_ERROR;
    }
    IsolationOptions opt;
    opt.toolDiameter = numberArg(interp, objv, 1, objc, 0.2);
    opt.passes = static_cast<int>(numberArg(interp, objv, 2, objc, 1));
    opt.overlap = numberArg(interp, objv, 3, objc, 0.0) / 100.0;
    const IsolationResult result = isolateCopper(state->gerber.solid, opt);
    setPaths(*state, result.paths, result.bounds, opt.toolDiameter);
    Tcl_SetObjResult(interp, Tcl_NewIntObj(static_cast<int>(result.paths.size())));
    return TCL_OK;
}

int cmdCutout(ClientData data, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    auto* state = static_cast<ShellState*>(data);
    if (!state->hasGerber) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("no gerber loaded", -1));
        return TCL_ERROR;
    }
    CutoutOptions opt;
    opt.toolDiameter = numberArg(interp, objv, 1, objc, 1.0);
    opt.margin = numberArg(interp, objv, 2, objc, 0.0);
    opt.tabCount = static_cast<int>(numberArg(interp, objv, 3, objc, 0));
    opt.tabWidth = numberArg(interp, objv, 4, objc, 0.0);
    const CutoutResult result = cutoutGeometry(state->gerber.solid, opt);
    setPaths(*state, result.paths, result.bounds, opt.toolDiameter);
    Tcl_SetObjResult(interp, Tcl_NewIntObj(static_cast<int>(result.paths.size())));
    return TCL_OK;
}

int cmdNcc(ClientData data, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    auto* state = static_cast<ShellState*>(data);
    if (!state->hasGerber) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("no gerber loaded", -1));
        return TCL_ERROR;
    }
    NccOptions opt;
    opt.toolDiameter = numberArg(interp, objv, 1, objc, 0.5);
    opt.stepOver = numberArg(interp, objv, 2, objc, 50.0) / 100.0;
    const BBox b = state->gerber.bounds.expanded(1.0);
    const Geom area = b.valid ? Geom::box(b.xmin, b.ymin, b.xmax, b.ymax) : Geom();
    const NccResult result = clearCopper(state->gerber.solid, area, opt);
    setPaths(*state, result.paths, result.bounds, opt.toolDiameter);
    Tcl_SetObjResult(interp, Tcl_NewIntObj(static_cast<int>(result.paths.size())));
    return TCL_OK;
}

int cmdPaint(ClientData data, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    auto* state = static_cast<ShellState*>(data);
    if (!state->hasGerber) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("no gerber loaded", -1));
        return TCL_ERROR;
    }
    PaintOptions opt;
    opt.toolDiameter = numberArg(interp, objv, 1, objc, 0.1);
    opt.stepOver = numberArg(interp, objv, 2, objc, 50.0) / 100.0;
    const PaintResult result = paintArea(state->gerber.solid, opt);
    setPaths(*state, result.paths, result.bounds, opt.toolDiameter);
    Tcl_SetObjResult(interp, Tcl_NewIntObj(static_cast<int>(result.paths.size())));
    return TCL_OK;
}

int cmdMill(ClientData data, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    auto* state = static_cast<ShellState*>(data);
    if (!state->hasGerber) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("no gerber loaded", -1));
        return TCL_ERROR;
    }
    std::vector<std::vector<Pt>> lines;
    for (const Geom& g : state->gerber.follows) {
        if (!g.valid() || g.empty()) continue;
        if (g.typeId() == GEOS_LINESTRING || g.typeId() == GEOS_LINEARRING) {
            Ring ring = g.coords();
            if (ring.size() >= 2) lines.emplace_back(ring.begin(), ring.end());
        }
    }
    state->paths = PathJob{};
    state->paths.paths = std::move(lines);
    state->paths.bounds = state->gerber.bounds;
    state->paths.toolDiameter = numberArg(interp, objv, 1, objc, 0.2);
    state->hasPathJob = true;
    state->hasDrillJob = false;
    Tcl_SetObjResult(interp, Tcl_NewIntObj(static_cast<int>(state->paths.paths.size())));
    return TCL_OK;
}

int cmdExportSvg(ClientData data, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    auto* state = static_cast<ShellState*>(data);
    if (!state->hasGerber || objc < 2) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("usage: export_svg file", -1));
        return TCL_ERROR;
    }
    exportSvg(Tcl_GetString(objv[1]), state->gerber.solids, state->gerber.follows,
              state->gerber.bounds);
    return TCL_OK;
}

int cmdExportDxf(ClientData data, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    auto* state = static_cast<ShellState*>(data);
    if (!state->hasGerber || objc < 2) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("usage: export_dxf file", -1));
        return TCL_ERROR;
    }
    exportDxf(Tcl_GetString(objv[1]), state->gerber.solids, state->gerber.follows);
    return TCL_OK;
}

int cmdThieving(ClientData data, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    auto* state = static_cast<ShellState*>(data);
    if (!state->hasGerber) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("no gerber loaded", -1));
        return TCL_ERROR;
    }
    ThievingOptions opt;
    opt.dotDiameter = numberArg(interp, objv, 1, objc, 0.5);
    opt.spacing = numberArg(interp, objv, 2, objc, 2.0);
    opt.clearance = numberArg(interp, objv, 3, objc, 0.5);
    const std::vector<Pt> dots = thievingPoints(state->gerber.solid, state->gerber.bounds, opt);
    Tcl_SetObjResult(interp, Tcl_NewIntObj(static_cast<int>(dots.size())));
    return TCL_OK;
}

int cmdExportGcode(ClientData data, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    auto* state = static_cast<ShellState*>(data);
    if (objc < 2) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("usage: export_gcode file", -1));
        return TCL_ERROR;
    }
    GCodeOptions opt;
    std::string code;
    if (state->hasDrillJob) code = drillingGCode(state->drills, opt);
    else if (state->hasPathJob) code = pathsGCode(state->paths, opt);
    else {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("nothing to export", -1));
        return TCL_ERROR;
    }
    std::ofstream out(Tcl_GetString(objv[1]), std::ios::binary);
    if (!out) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("cannot write file", -1));
        return TCL_ERROR;
    }
    out << code;
    return TCL_OK;
}

int cmdInfo(ClientData data, Tcl_Interp* interp, int, Tcl_Obj* const[]) {
    auto* state = static_cast<ShellState*>(data);
    std::ostringstream os;
    if (state->hasGerber) os << "gerber area=" << state->gerber.solid.area() << "\n";
    if (state->hasExcellon)
        os << "excellon drills=" << state->excellon.drillCount << "\n";
    if (state->hasPathJob) os << "paths=" << state->paths.paths.size() << "\n";
    if (state->hasDrillJob) os << "tools=" << state->drills.tools.size() << "\n";
    const std::string text = os.str();
    Tcl_SetObjResult(interp, Tcl_NewStringObj(text.c_str(), -1));
    return TCL_OK;
}

}  // namespace

int runTclScript(const std::string& path) {
    Tcl_FindExecutable(nullptr);
    Tcl_Interp* interp = Tcl_CreateInterp();
    if (!interp) {
        std::cerr << "cannot create Tcl interpreter\n";
        return 1;
    }
    if (Tcl_Init(interp) != TCL_OK) {
        // Core commands (set, puts, if, proc...) still work.
        std::cerr << "tcl init warning: " << Tcl_GetStringResult(interp) << "\n";
    }

    ShellState state;
    Tcl_CreateObjCommand(interp, "open_gerber", cmdOpenGerber, &state, nullptr);
    Tcl_CreateObjCommand(interp, "open_excellon", cmdOpenExcellon, &state, nullptr);
    Tcl_CreateObjCommand(interp, "isolate", cmdIsolate, &state, nullptr);
    Tcl_CreateObjCommand(interp, "cutout", cmdCutout, &state, nullptr);
    Tcl_CreateObjCommand(interp, "ncc", cmdNcc, &state, nullptr);
    Tcl_CreateObjCommand(interp, "paint", cmdPaint, &state, nullptr);
    Tcl_CreateObjCommand(interp, "export_gcode", cmdExportGcode, &state, nullptr);
    Tcl_CreateObjCommand(interp, "mill", cmdMill, &state, nullptr);
    Tcl_CreateObjCommand(interp, "export_svg", cmdExportSvg, &state, nullptr);
    Tcl_CreateObjCommand(interp, "export_dxf", cmdExportDxf, &state, nullptr);
    Tcl_CreateObjCommand(interp, "thieving", cmdThieving, &state, nullptr);
    Tcl_CreateObjCommand(interp, "info", cmdInfo, &state, nullptr);

    if (path.empty() || path == "-") {
        std::string line;
        while (std::getline(std::cin, line)) {
            const int rc = Tcl_Eval(interp, line.c_str());
            const char* result = Tcl_GetStringResult(interp);
            if (rc != TCL_OK) std::cerr << "error: " << result << "\n";
            else if (result && *result) std::cout << result << "\n";
        }
        Tcl_DeleteInterp(interp);
        return 0;
    }

    const int rc = Tcl_EvalFile(interp, path.c_str());
    if (rc != TCL_OK) {
        std::cerr << "tcl error: " << Tcl_GetStringResult(interp) << "\n";
        Tcl_DeleteInterp(interp);
        return 1;
    }
    Tcl_DeleteInterp(interp);
    return 0;
}

}  // namespace fc

#endif  // FC_HAVE_TCL
