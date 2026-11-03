#pragma once

#include <string>
#include <vector>

#include "flatcam/geometry.hpp"
#include "flatcam/units.hpp"

namespace fc {

// Importers: SVG (paths, basic shapes, groups and text) and DXF
// (LINE/LWPOLYLINE/CIRCLE/ARC/ELLIPSE/SPLINE, INSERTed blocks and TEXT/MTEXT).
// Curves are approximated with segments.
std::vector<Geom> importSvg(const std::string& path);
std::vector<Geom> importDxf(const std::string& path);

// A TEXT/MTEXT entity resolved to its final position (INSERT transforms
// included). The GUI turns it into outlines with the system font engine.
struct DxfText {
    std::string text;
    double x = 0.0;         // anchor point in millimetres
    double y = 0.0;
    double height = 2.5;    // nominal cap height in millimetres
    double rotation = 0.0;  // degrees, counter-clockwise
    int hAlign = 0;         // 0 left, 1 centre, 2 right
    int vAlign = 0;         // 0 baseline, 1 bottom, 2 middle, 3 top
};
std::vector<DxfText> importDxfTexts(const std::string& path);
std::vector<Geom> importHpgl(const std::string& path);

// Writes a Gerber file with polygons as regions and paths as strokes.
void writeGerber(const std::string& path, const std::vector<Geom>& solids,
                 const std::vector<Geom>& follows, Units units);

// Writes an Excellon drill file from the parsed tools (drills, slots, diameters).
void writeExcellon(const std::string& path, const struct ExcellonResult& result, Units units);

// Minimal HPGL plotter output for solids and strokes (millimetres / plotter units).
void exportHpgl(const std::string& path, const std::vector<Geom>& solids,
                const std::vector<Geom>& follows);

}  // namespace fc
