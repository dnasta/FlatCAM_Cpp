#pragma once

#include <optional>
#include <vector>

#include "flatcam/geometry.hpp"

namespace fc {

struct CutoutOptions {
    double toolDiameter = 1.0;
    double margin = 0.0;  // extra clearance beyond the tool radius
    bool external = true; // cut outside (board) or inside (pocket)
    int quadrantSegments = 16;
    bool invert = false;
    // Tabs (bridges) that keep the part attached: 0 disables them.
    int tabCount = 0;      // usually 2 or 4
    double tabWidth = 0.0; // bridge width along the path
    bool includeHoles = false;  // also cut inner contours (cutouts in the board)
    // Optional rectangular work area; the cut is clipped to it.
    std::optional<BBox> area;
};

struct CutoutResult {
    std::vector<Geom> paths;
    Geom envelope;  // offset polygon, for preview
    double totalLength = 0.0;
    BBox bounds;
};

// Offset the outline by (margin +/- toolRadius) and return the resulting
// contours as tool centre paths. Mirrors FlatCAM's cutout buffering:
//   external: margin + toolDiameter/2
//   internal: margin - toolDiameter/2
CutoutResult cutoutGeometry(const Geom& outline, const CutoutOptions& options);

}  // namespace fc
