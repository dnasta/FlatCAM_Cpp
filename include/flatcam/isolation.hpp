#pragma once

#include <vector>

#include "flatcam/geometry.hpp"

namespace fc {

struct IsolationOptions {
    double toolDiameter = 0.2;
    int passes = 1;
    double overlap = 0.0;  // fraction of the tool diameter, 0.0 .. 0.99
    int quadrantSegments = 16;
    bool invert = false;         // reverse ring direction (climb vs conventional)
    bool exteriorsOnly = false;  // only outer contours
    bool interiorsOnly = false;  // only holes / inner contours
    // Rest machining: skip passes whose buffered area would collide with a
    // neighbouring copper feature (FlatCAM's generate_rest_geometry check).
    bool avoidIntersections = false;
};

struct IsolationResult {
    std::vector<Geom> paths;      // tool centre paths (LineStrings), app units
    std::vector<Geom> envelopes;  // buffered copper per pass (Polygons), for preview
    std::vector<Geom> notIsolated;  // polygons that had no valid pass
    double totalLength = 0.0;
    BBox bounds;
};

// Port of FlatCAM's isolation_geometry() + pass loop:
//   offset_i = toolDiameter * ((2*i + 1) / 2) - i * overlap * toolDiameter
//   envelope_i = union(copper_polygon.buffer(offset_i, quadSegs))
//   paths_i    = exterior + interiors of every envelope_i polygon
IsolationResult isolateCopper(const Geom& copper, const IsolationOptions& options);

// Rest machining: reuse a large tool's work and isolate, with a smaller tool,
// only the regions the large tool could not reach.
IsolationResult isolateRest(const Geom& copper, double previousToolDiameter,
                            const IsolationOptions& options);

}  // namespace fc
