#pragma once

#include <vector>

#include "flatcam/geometry.hpp"

namespace fc {

struct ThievingOptions {
    double dotDiameter = 0.5;
    double spacing = 2.0;
    double clearance = 0.5;
    double margin = 0.5;
};

// Grid of copper dots placed in the empty area (never closer than
// clearance + radius to existing copper).
std::vector<Pt> thievingPoints(const Geom& copper, const BBox& area,
                               const ThievingOptions& options);

}  // namespace fc
