#pragma once

#include "flatcam/geometry.hpp"

#include <QString>
#include <QTransform>
#include <vector>

// Converts every <text> element of an SVG file to filled geometry. Text is
// laid out with Qt's font engine, so the result follows the system font;
// group transforms, per-element transforms, font-size and text-anchor are
// honoured.
std::vector<fc::Geom> svgTextGeometry(const QString& path);

// Converts every TEXT/MTEXT entity of a DXF file (including those inside
// INSERTed blocks) to filled geometry.
std::vector<fc::Geom> dxfTextGeometry(const QString& path);

// Parses the subset of the SVG transform syntax used by FlatCAM
// (translate, scale, rotate, matrix, skewX, skewY).
QTransform parseSvgTransformQt(const QString& text);
