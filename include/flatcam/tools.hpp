#pragma once

#include <vector>

#include "flatcam/excellon.hpp"
#include "flatcam/geometry.hpp"
#include "flatcam/gerber.hpp"
#include "flatcam/units.hpp"

namespace fc {

// Small utilities shared by the "extra" CAM tools (distance, optimal tool,
// drill extraction, corner marks, punch, rules check).

struct OptimalTool {
    double diameter = 0.0;  // largest tool that can still isolate the copper
    double gap = 0.0;       // clearance that limits the diameter
    Pt pointA;              // closest points of the limiting pair
    Pt pointB;
    bool valid = false;
};

// Computes the narrowest copper-to-copper clearance of a geometry. The
// isolation tool cannot be wider than that gap.
OptimalTool optimalIsolationDiameter(const Geom& copper, double maxDiameter = 10.0);

// All clearance pairs narrower than maxDiameter, sorted ascending.
std::vector<OptimalTool> optimalIsolationGaps(const Geom& copper, double maxDiameter = 10.0,
                                              std::size_t limit = 20,
                                              double minSeparation = 0.0);

// Minimum distance between two collections of geometry (union of each side).
double layersDistance(const std::vector<Geom>& a, const std::vector<Geom>& b,
                      Pt* nearestA = nullptr, Pt* nearestB = nullptr);

// Extracts circular flashes from a parsed Gerber and builds an Excellon
// object with one tool per distinct diameter.
ExcellonResult extractDrills(const GerberResult& gerber, double maxDiameter = 3.0,
                             Units appUnits = Units::Mm, int stepsPerCircle = 32);

// Subtracts the (buffered) holes of an Excellon object from a Gerber copper
// layer. Returns the punched copper geometry.
Geom punchGerber(const GerberResult& gerber, const ExcellonResult& drills,
                 double margin = 0.0);

// Replaces the diameter of a circular aperture definition (%ADD<code>C,...*%).
// Returns the modified text; `changed` receives the number of replacements.
std::string editCircleAperture(std::string gerber, const std::string& code, double diameter,
                               int decimals, int* changed = nullptr);

// Edits a circle, rectangle, obround or polygon aperture so its largest
// dimension becomes `size`. Rectangles/obrounds keep their aspect ratio.
std::string editApertureSize(std::string gerber, const std::string& code, double size,
                             int decimals, int* changed = nullptr);

// L-shaped corner marks drawn inside the given bounds.
std::vector<Geom> cornerMarks(const BBox& bounds, double length = 3.0,
                              double margin = 0.0, bool outside = false);

}  // namespace fc
