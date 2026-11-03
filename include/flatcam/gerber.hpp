#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "flatcam/geometry.hpp"
#include "flatcam/units.hpp"

namespace fc {

struct GerberOptions {
    Units appUnits = Units::Mm;
    int stepsPerCircle = 64;  // FlatCAM: gerber_circle_steps
    bool simplification = false;
    double simplifyTolerance = 0.005;
    // FlatCAM 2024.4 builds a single box from the first two points of a path
    // when stroking with a rectangular aperture. That is a known bug; the
    // default here is the correct Minkowski sweep. Enable this for exact
    // geometric parity with the original for such files.
    bool legacyRectStroke = false;
};

struct GerberAperture {
    enum class Kind { Circle, Rectangle, Obround, Polygon, Macro, Region };

    Kind kind = Kind::Circle;
    double size = 0.0;
    double width = 0.0;
    double height = 0.0;
    double diameter = 0.0;
    int vertices = 0;
    double rotation = 0.0;
    bool hasRotation = false;
    std::string macroName;
    std::vector<double> modifiers;
    std::vector<Geom> geometry;  // geometry drawn with this aperture (file units)
};

struct GerberMacro {
    std::string name;
    std::string raw;
};

struct GerberResult {
    Units fileUnits = Units::Inch;
    int intDigits = 3;
    int fracDigits = 4;
    char zeros = 'L';
    bool incremental = false;

    std::map<std::string, GerberAperture> apertures;
    std::map<std::string, GerberMacro> macros;

    std::vector<Geom> solids;   // dark polygons, app units
    std::vector<Geom> follows;  // strokes / points, app units
    Geom solid;                 // unified copper, app units
    BBox bounds;

    std::size_t flashes = 0;
    std::size_t segments = 0;
    std::size_t regions = 0;
    std::vector<std::string> warnings;

    bool empty() const { return solid.empty(); }
};

class GerberParser {
public:
    explicit GerberParser(GerberOptions options = {});

    // Throws std::runtime_error on unrecoverable input errors.
    GerberResult parseFile(const std::string& path) const;
    GerberResult parseString(std::string_view source) const;

private:
    GerberOptions opt_;
};

}  // namespace fc
