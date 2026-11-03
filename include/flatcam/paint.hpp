#pragma once

#include <vector>

#include "flatcam/geometry.hpp"

namespace fc {

struct PaintOptions {
    double toolDiameter = 0.1;
    double stepOver = 0.5;   // fraction of the tool diameter
    double margin = 0.0;     // grow the region before painting
    int quadrantSegments = 16;
    bool invert = false;
    bool includeHoles = true;
};

struct PaintResult {
    std::vector<Geom> paths;  // contour-parallel tool paths
    double totalLength = 0.0;
    BBox bounds;
};

// Fills a region with contour-parallel offsets (pocket style). This is the
// strategy FlatCAM uses for "paint" objects such as logos.
PaintResult paintArea(const Geom& area, const PaintOptions& options);

}  // namespace fc
