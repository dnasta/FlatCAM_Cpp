#pragma once

#include <vector>

#include "flatcam/geometry.hpp"

namespace fc {

enum class NccStrategy { Hatch, Paint };

struct NccOptions {
    NccStrategy strategy = NccStrategy::Hatch;
    double toolDiameter = 0.2;
    double stepOver = 0.5;   // fraction of the tool diameter
    double clearance = 0.0;  // extra safety distance from copper
    double angleDeg = 0.0;   // hatch direction
    double margin = 0.0;     // extend the hatch beyond the work area
    int quadrantSegments = 16;
};

struct NccResult {
    std::vector<Geom> paths;  // clearing passes (parallel lines)
    Geom clearedArea;         // area that gets cleared
    double totalLength = 0.0;
    BBox bounds;
};

// Clears the area that is not copper: builds a hatch of parallel lines inside
// `area - buffer(copper, toolRadius + clearance)` at `stepOver * toolDiameter`
// spacing. This is a first, deterministic NCC strategy (no path linking).
NccResult clearCopper(const Geom& copper, const Geom& area, const NccOptions& options);

}  // namespace fc
