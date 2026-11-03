/**
 * @file pdf.hpp
 * @brief Importación de geometría vectorial desde PDF.
 */
#pragma once

#include <string>
#include <vector>

#include "flatcam/geometry.hpp"

namespace fc {

/**
 * @brief Importa el contenido vectorial de un PDF.
 *
 * Interpreta operadores de construcción y pintado de caminos (`m`, `l`, `c`,
 * `re`, `S`, `f`, etc.) y descomprime streams Flate. El texto se ignora, igual
 * que en el parser original de FlatCAM.
 *
 * @param path Archivo PDF.
 * @return Geometría en mm; vacío si no hay vectores decodificables.
 */
std::vector<Geom> importPdfVector(const std::string& path);

}  // namespace fc
