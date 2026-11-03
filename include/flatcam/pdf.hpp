#pragma once

#include <string>
#include <vector>

#include "flatcam/geometry.hpp"

namespace fc {

// Imports the vector artwork of a PDF file (path construction and painting
// operators). Text is ignored, matching FlatCAM's PDF parser. Returns an empty
// vector when no vector content can be decoded.
std::vector<Geom> importPdfVector(const std::string& path);

}  // namespace fc
