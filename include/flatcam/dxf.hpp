#pragma once

#include <string>
#include <vector>

#include "flatcam/geometry.hpp"

namespace fc {

// Minimal DXF (AC1015) writer: polygons/rings as closed LWPOLYLINE and
// strokes as open LWPOLYLINE. Units: millimetres.
void exportDxf(const std::string& path, const std::vector<Geom>& solids,
               const std::vector<Geom>& follows);

}  // namespace fc
