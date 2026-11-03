/**
 * @file TextGeometry.h
 * @brief Conversión de texto SVG/DXF a contornos con la fuente del sistema.
 */
#pragma once

#include "flatcam/geometry.hpp"

#include <QString>
#include <QTransform>
#include <vector>

/**
 * @brief Convierte los `<text>` de un SVG en geometría rellena.
 *
 * Usa la fuente del sistema, respeta transformaciones de grupos y del propio
 * elemento, tamaño, familia, peso y anclaje. Los contadores de las letras se
 * conservan como huecos.
 *
 * @param path Archivo SVG.
 * @return Polígonos de texto (uno por elemento).
 */
std::vector<fc::Geom> svgTextGeometry(const QString& path);

/**
 * @brief Convierte TEXT/MTEXT de un DXF en geometría rellena.
 * @param path Archivo DXF.
 * @return Polígonos de texto, con alineación y transformaciones resueltas.
 */
std::vector<fc::Geom> dxfTextGeometry(const QString& path);

/**
 * @brief Interpreta transformaciones SVG.
 * @param text Cadena `translate(...) scale(...) ...`.
 * @return Transformación Qt equivalente.
 */
QTransform parseSvgTransformQt(const QString& text);
