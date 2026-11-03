/**
 * @file importers.hpp
 * @brief Importadores/exportadores vectoriales y de imágenes.
 *
 * Agrupa SVG, DXF, HPGL2, PDF, imágenes y escritores Gerber/Excellon/HPGL.
 * La geometría de salida está en milímetros, salvo que se indique otra cosa.
 */
#pragma once

#include <string>
#include <vector>

#include "flatcam/geometry.hpp"
#include "flatcam/units.hpp"

namespace fc {

/**
 * @brief Importa geometría de un SVG.
 * @param path Archivo SVG.
 * @return Formas, curvas aplanadas, transformaciones y texto.
 */
std::vector<Geom> importSvg(const std::string& path);
/**
 * @brief Importa geometría de un DXF.
 * @param path Archivo DXF.
 * @return Líneas, polilíneas, arcos, elipses, splines, bloques y texto resuelto.
 */
std::vector<Geom> importDxf(const std::string& path);

/**
 * @brief Entidad TEXT/MTEXT resuelta a su posición final.
 *
 * Incluye las transformaciones de los INSERT. La GUI la convierte en contornos
 * con la fuente del sistema.
 */
struct DxfText {
    std::string text;       ///< Texto.
    double x = 0.0;         ///< Punto de anclaje X (mm).
    double y = 0.0;         ///< Punto de anclaje Y (mm).
    double height = 2.5;    ///< Altura nominal de mayúsculas (mm).
    double rotation = 0.0;  ///< Rotación en grados, antihoraria.
    int hAlign = 0;         ///< 0 izquierda, 1 centro, 2 derecha.
    int vAlign = 0;         ///< 0 línea base, 1 abajo, 2 medio, 3 arriba.
};
/**
 * @brief Importa sólo los textos de un DXF, ya transformados.
 * @param path Archivo DXF.
 */
std::vector<DxfText> importDxfTexts(const std::string& path);

/**
 * @brief Importa geometría de un HPGL2.
 * @param path Archivo HPGL/HPG/PLT.
 */
std::vector<Geom> importHpgl(const std::string& path);

/**
 * @brief Escribe un Gerber con polígonos como regiones y trazos.
 * @param path Archivo de salida.
 * @param solids Polígonos rellenos.
 * @param follows Trazos.
 * @param units Unidades del archivo.
 */
void writeGerber(const std::string& path, const std::vector<Geom>& solids,
                 const std::vector<Geom>& follows, Units units);

/**
 * @brief Escribe un Excellon a partir de las herramientas parseadas.
 * @param path Archivo de salida.
 * @param result Taladros, ranuras y diámetros.
 * @param units Unidades del archivo.
 */
void writeExcellon(const std::string& path, const struct ExcellonResult& result, Units units);

/**
 * @brief Exporta sólidos y trazos a HPGL.
 * @param path Archivo de salida.
 * @param solids Polígonos.
 * @param follows Trazos.
 */
void exportHpgl(const std::string& path, const std::vector<Geom>& solids,
                const std::vector<Geom>& follows);

}  // namespace fc
