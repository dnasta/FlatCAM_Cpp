#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "flatcam/geometry.hpp"
#include "flatcam/units.hpp"

namespace fc {

struct ExcellonOptions {
    Units appUnits = Units::Mm;
    int stepsPerCircle = 64;
    char defaultZeros = 'L';
};

struct ExcellonTool {
    int number = 0;
    double diameter = 0.0;
    bool diameterKnown = false;
    std::vector<Pt> drills;
    std::vector<std::pair<Pt, Pt>> slotSegments;
    std::vector<Geom> geometry;  // file units
};

struct ExcellonResult {
    Units fileUnits = Units::Mm;
    bool unitsKnown = false;
    char zeros = 'L';
    int formatUpperMm = 3;
    int formatLowerMm = 3;
    int formatUpperIn = 2;
    int formatLowerIn = 4;

    std::map<int, ExcellonTool> tools;
    std::vector<Geom> geometry;  // holes and slots, app units
    Geom solid;                  // union of geometry, app units
    BBox bounds;

    std::size_t drillCount = 0;
    std::size_t slotCount = 0;
    std::vector<std::string> warnings;

    bool empty() const { return solid.empty(); }
};

class ExcellonParser {
public:
    explicit ExcellonParser(ExcellonOptions options = {});

    ExcellonResult parseFile(const std::string& path) const;
    ExcellonResult parseString(std::string_view source) const;

private:
    ExcellonOptions opt_;
};

}  // namespace fc
