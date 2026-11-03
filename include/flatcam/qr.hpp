#pragma once

#include <string>
#include <vector>

#include "flatcam/geometry.hpp"

namespace fc {

// Generates the dark modules of a QR code as polygon geometry. The result is
// already unioned, in millimetres, with its bottom-left corner at (originX,
// originY).
std::vector<Geom> qrCodeGeometry(const std::string& text, double moduleSize,
                                 int borderModules = 4, double originX = 0.0,
                                 double originY = 0.0, char ecc = 'M');

}  // namespace fc
