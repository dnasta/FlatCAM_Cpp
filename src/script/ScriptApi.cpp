#include "ScriptApi.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>

#include <sstream>
#include <vector>

#include "flatcam/cutout.hpp"
#include "flatcam/isolation.hpp"
#include "flatcam/ncc.hpp"
#include "flatcam/paint.hpp"
#include "flatcam/tools.hpp"
#include "flatcam/qr.hpp"

#include <QJSEngine>
#include <QJSValue>

namespace fc {
namespace {

void collectLines(const Geom& geom, std::vector<std::vector<Pt>>& out) {
    if (!geom.valid() || geom.empty()) return;
    const int type = geom.typeId();
    if (type == GEOS_LINESTRING || type == GEOS_LINEARRING) {
        Ring ring = geom.coords();
        if (ring.size() >= 2) out.emplace_back(ring.begin(), ring.end());
        return;
    }
    if (type == GEOS_MULTILINESTRING || type == GEOS_GEOMETRYCOLLECTION) {
        for (int i = 0; i < geom.numGeometries(); ++i) collectLines(geom.geometryN(i), out);
    }
}

}  // namespace

ScriptApi::ScriptApi(QObject* parent) : QObject(parent) {}

bool ScriptApi::openGerber(const QString& path) {
    try {
        gerber_ = GerberParser().parseFile(path.toStdString());
        hasGerber_ = true;
        error_.clear();
        if (sink_) sink_(QFileInfo(path).fileName(), "gerber", gerber_.solids, gerber_.follows);
        return true;
    } catch (const std::exception& e) {
        error_ = QString::fromUtf8(e.what());
        return false;
    }
}

bool ScriptApi::openExcellon(const QString& path) {
    try {
        excellon_ = ExcellonParser().parseFile(path.toStdString());
        drills_ = drillJobFromExcellon(excellon_);
        hasExcellon_ = true;
        hasDrillJob_ = true;
        hasPathJob_ = false;
        error_.clear();
        if (sink_) sink_(QFileInfo(path).fileName(), "excellon", excellon_.geometry, {});
        return true;
    } catch (const std::exception& e) {
        error_ = QString::fromUtf8(e.what());
        return false;
    }
}

int ScriptApi::isolate(double toolDiameter, int passes, double overlapPercent) {
    if (!hasGerber_) {
        error_ = "no gerber loaded";
        return -1;
    }
    IsolationOptions opt;
    opt.toolDiameter = toolDiameter;
    opt.passes = passes;
    opt.overlap = overlapPercent / 100.0;
    const IsolationResult result = isolateCopper(gerber_.solid, opt);
    paths_ = PathJob{};
    paths_.bounds = result.bounds;
    paths_.toolDiameter = toolDiameter;
    for (const Geom& g : result.paths) {
        Ring ring = g.coords();
        if (ring.size() >= 2) paths_.paths.emplace_back(ring.begin(), ring.end());
    }
    hasPathJob_ = true;
    hasDrillJob_ = false;
    if (sink_) sink_("isolate", "herramienta", {}, result.paths);
    return static_cast<int>(result.paths.size());
}

int ScriptApi::cutout(double toolDiameter, double margin, int tabs, double tabWidth) {
    if (!hasGerber_) {
        error_ = "no gerber loaded";
        return -1;
    }
    CutoutOptions opt;
    opt.toolDiameter = toolDiameter;
    opt.margin = margin;
    opt.tabCount = tabs;
    opt.tabWidth = tabWidth;
    const CutoutResult result = cutoutGeometry(gerber_.solid, opt);
    paths_ = PathJob{};
    paths_.bounds = result.bounds;
    paths_.toolDiameter = toolDiameter;
    for (const Geom& g : result.paths) {
        Ring ring = g.coords();
        if (ring.size() >= 2) paths_.paths.emplace_back(ring.begin(), ring.end());
    }
    hasPathJob_ = true;
    hasDrillJob_ = false;
    if (sink_) sink_("cutout", "herramienta", {}, result.paths);
    return static_cast<int>(result.paths.size());
}

int ScriptApi::ncc(double toolDiameter, double stepOverPercent) {
    if (!hasGerber_) {
        error_ = "no gerber loaded";
        return -1;
    }
    NccOptions opt;
    opt.toolDiameter = toolDiameter;
    opt.stepOver = stepOverPercent / 100.0;
    const BBox b = gerber_.bounds.expanded(1.0);
    const Geom area = b.valid ? Geom::box(b.xmin, b.ymin, b.xmax, b.ymax) : Geom();
    const NccResult result = clearCopper(gerber_.solid, area, opt);
    paths_ = PathJob{};
    paths_.bounds = result.bounds;
    paths_.toolDiameter = toolDiameter;
    for (const Geom& g : result.paths) {
        Ring ring = g.coords();
        if (ring.size() >= 2) paths_.paths.emplace_back(ring.begin(), ring.end());
    }
    hasPathJob_ = true;
    hasDrillJob_ = false;
    if (sink_) sink_("ncc", "herramienta", {}, result.paths);
    return static_cast<int>(result.paths.size());
}

int ScriptApi::paint(double toolDiameter, double stepOverPercent) {
    if (!hasGerber_) {
        error_ = "no gerber loaded";
        return -1;
    }
    PaintOptions opt;
    opt.toolDiameter = toolDiameter;
    opt.stepOver = stepOverPercent / 100.0;
    const PaintResult result = paintArea(gerber_.solid, opt);
    paths_ = PathJob{};
    paths_.bounds = result.bounds;
    paths_.toolDiameter = toolDiameter;
    for (const Geom& g : result.paths) {
        Ring ring = g.coords();
        if (ring.size() >= 2) paths_.paths.emplace_back(ring.begin(), ring.end());
    }
    hasPathJob_ = true;
    hasDrillJob_ = false;
    if (sink_) sink_("paint", "herramienta", {}, result.paths);
    return static_cast<int>(result.paths.size());
}

int ScriptApi::mill(double toolDiameter) {
    if (!hasGerber_) {
        error_ = "no gerber loaded";
        return -1;
    }
    std::vector<std::vector<Pt>> lines;
    for (const Geom& g : gerber_.follows) collectLines(g, lines);
    paths_ = PathJob{};
    paths_.paths = std::move(lines);
    paths_.bounds = gerber_.bounds;
    paths_.toolDiameter = toolDiameter;
    hasPathJob_ = true;
    hasDrillJob_ = false;
    std::vector<Geom> linesGeom;
    for (const auto& line : paths_.paths) linesGeom.push_back(Geom::line(line));
    if (sink_) sink_("mill", "herramienta", {}, linesGeom);
    return static_cast<int>(paths_.paths.size());
}

int ScriptApi::extractDrills(double maxDiameter) {
    if (!hasGerber_) {
        error_ = "no gerber loaded";
        return -1;
    }
    excellon_ = fc::extractDrills(gerber_, maxDiameter, Units::Mm);
    hasExcellon_ = true;
    drills_ = drillJobFromExcellon(excellon_);
    hasDrillJob_ = true;
    if (sink_) sink_("extract_drills", "excellon", excellon_.geometry, {});
    return static_cast<int>(excellon_.drillCount);
}

int ScriptApi::punch(double margin) {
    if (!hasGerber_ || !hasExcellon_) {
        error_ = "need gerber and excellon";
        return -1;
    }
    const Geom punched = fc::punchGerber(gerber_, excellon_, margin);
    if (!punched.valid() || punched.empty()) {
        error_ = "empty result";
        return -1;
    }
    gerber_.solid = punched;
    gerber_.solids = {punched};
    gerber_.bounds = punched.bounds();
    if (sink_) sink_("punch", "gerber", {punched}, {});
    return punched.numGeometries();
}

int ScriptApi::panelize(int rows, int cols, double gapX, double gapY, double angle) {
    if (!hasGerber_ || rows < 1 || cols < 1) {
        error_ = "no gerber loaded";
        return -1;
    }
    const BBox b = gerber_.bounds;
    if (!b.valid) return -1;
    const Pt center{(b.xmin + b.xmax) * 0.5, (b.ymin + b.ymax) * 0.5};
    std::vector<Geom> rotated;
    BBox rb;
    for (const Geom& g : gerber_.solids) {
        const Geom r = g.rotateDeg(angle, center);
        rotated.push_back(r);
        const BBox gb = r.bounds();
        if (!gb.valid) continue;
        if (!rb.valid) rb = gb;
        else {
            rb.xmin = std::min(rb.xmin, gb.xmin);
            rb.ymin = std::min(rb.ymin, gb.ymin);
            rb.xmax = std::max(rb.xmax, gb.xmax);
            rb.ymax = std::max(rb.ymax, gb.ymax);
        }
    }
    if (!rb.valid) return -1;
    const double dx = rb.width() + gapX;
    const double dy = rb.height() + gapY;
    std::vector<Geom> out;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const double ox = b.xmin - rb.xmin + c * dx;
            const double oy = b.ymin - rb.ymin + r * dy;
            for (const Geom& g : rotated) out.push_back(g.translate(ox, oy));
        }
    }
    if (sink_) sink_("panel", "gerber", out, {});
    return static_cast<int>(out.size());
}

int ScriptApi::qrCode(const QString& text, double module, int border) {
    const std::vector<Geom> geometry =
        fc::qrCodeGeometry(text.toStdString(), module, border, 0.0, 0.0, 'M');
    if (geometry.empty()) {
        error_ = "empty qr";
        return -1;
    }
    if (sink_) sink_("qr", "gerber", geometry, {});
    return static_cast<int>(geometry.size());
}

int ScriptApi::cornerMarks(double length, double margin) {
    if (!hasGerber_ || !gerber_.bounds.valid) {
        error_ = "no gerber loaded";
        return -1;
    }
    const std::vector<Geom> marks = fc::cornerMarks(gerber_.bounds, length, margin, false);
    if (sink_) sink_("corners", "herramienta", {}, marks);
    return static_cast<int>(marks.size());
}

double ScriptApi::optimalToolDiameter(double maxDiameter) {
    if (!hasGerber_) {
        error_ = "no gerber loaded";
        return -1.0;
    }
    const OptimalTool opt = fc::optimalIsolationDiameter(gerber_.solid, maxDiameter);
    return opt.valid ? opt.diameter : maxDiameter;
}

int ScriptApi::rulesCheck(double clearance) {
    if (!hasGerber_ || !hasExcellon_) {
        error_ = "need gerber and excellon";
        return -1;
    }
    const double d = gerber_.solid.distance(excellon_.solid);
    return d < clearance ? 1 : 0;
}

bool ScriptApi::exportGcode(const QString& path) {
    try {
        GCodeOptions opt;
        std::string code;
        if (hasDrillJob_) code = drillingGCode(drills_, opt);
        else if (hasPathJob_) code = pathsGCode(paths_, opt);
        else {
            error_ = "nothing to export";
            return false;
        }
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            error_ = "cannot write " + path;
            return false;
        }
        file.write(code.c_str());
        return true;
    } catch (const std::exception& e) {
        error_ = QString::fromUtf8(e.what());
        return false;
    }
}

QString ScriptApi::info() const {
    std::ostringstream os;
    if (hasGerber_) os << "gerber area=" << gerber_.solid.area() << " ";
    if (hasExcellon_) os << "drills=" << excellon_.drillCount << " ";
    if (hasPathJob_) os << "paths=" << paths_.paths.size() << " ";
    if (hasDrillJob_) os << "tools=" << drills_.tools.size() << " ";
    return QString::fromStdString(os.str());
}

void ScriptApi::log(const QString& text) { qInfo().noquote() << text; }

void installScriptApi(QJSEngine& engine, ScriptApi* api) {
    engine.globalObject().setProperty("flatcam", engine.newQObject(api));
    engine.evaluate("function print(s) { flatcam.log(String(s)); }");
}

}  // namespace fc
