#pragma once

#include <string>
#include <vector>

#include "flatcam/geometry.hpp"

namespace fc {

// Exports polygon geometry (filled) and strokes to a plain SVG file.
void exportSvg(const std::string& path, const std::vector<Geom>& solids,
               const std::vector<Geom>& follows, const BBox& bounds);

}  // namespace fc
