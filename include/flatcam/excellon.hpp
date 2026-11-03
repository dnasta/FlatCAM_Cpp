/**
 * @file excellon.hpp
 * @brief Parser de archivos Excellon (taladros y ranuras de PCB).
 *
 * Soporta cabecera `M48`, definición de herramientas `T#C...`, unidades
 * métricas/imperiales, supresión de ceros, taladros, ranuras `G85`, ruteo
 * `M15/M16`, repeticiones `R` y archivos sin cabecera. La salida está en
 * unidades de la aplicación y conserva la tabla de herramientas para el
 * editor y la herramienta de perforación.
 */
#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "flatcam/geometry.hpp"
#include "flatcam/units.hpp"

namespace fc {

/**
 * @brief Opciones de importación de Excellon.
 */
struct ExcellonOptions {
    Units appUnits = Units::Mm;   ///< Unidad de salida.
    int stepsPerCircle = 64;      ///< Segmentos por círculo de los taladros.
    char defaultZeros = 'L';      ///< Supresión de ceros si el archivo no la declara.
};

/**
 * @brief Herramienta Excellon con sus taladros y ranuras.
 */
struct ExcellonTool {
    int number = 0;                          ///< Número de herramienta (`T#`).
    double diameter = 0.0;                   ///< Diámetro en unidades de la app.
    bool diameterKnown = false;              ///< `true` si el archivo definió el diámetro.
    std::vector<Pt> drills;                  ///< Centros de taladros.
    std::vector<std::pair<Pt, Pt>> slotSegments; ///< Extremos de ranuras.
    std::vector<Geom> geometry;              ///< Geometría dibujada (unidades del archivo).
};

/**
 * @brief Resultado completo de parsear un Excellon.
 */
struct ExcellonResult {
    Units fileUnits = Units::Mm; ///< Unidades del archivo.
    bool unitsKnown = false;     ///< `true` si el archivo declaró unidades.
    char zeros = 'L';            ///< Supresión de ceros efectiva.
    int formatUpperMm = 3;       ///< Dígitos enteros en mm.
    int formatLowerMm = 3;       ///< Dígitos fraccionarios en mm.
    int formatUpperIn = 2;       ///< Dígitos enteros en pulgadas.
    int formatLowerIn = 4;       ///< Dígitos fraccionarios en pulgadas.

    std::map<int, ExcellonTool> tools; ///< Herramientas por número.
    std::vector<Geom> geometry;        ///< Taladros y ranuras, en unidades de la app.
    Geom solid;                        ///< Unión de `geometry`.
    BBox bounds;                       ///< Límites de la geometría.

    std::size_t drillCount = 0; ///< Cantidad de taladros.
    std::size_t slotCount = 0;  ///< Cantidad de ranuras.
    std::vector<std::string> warnings; ///< Avisos no fatales.

    /// @return `true` si no hay geometría.
    bool empty() const { return solid.empty(); }
};

/**
 * @brief Parser reutilizable de archivos Excellon.
 */
class ExcellonParser {
public:
    /**
     * @brief Crea un parser con las opciones dadas.
     * @param options Unidades, pasos por círculo y ceros por defecto.
     */
    explicit ExcellonParser(ExcellonOptions options = {});

    /**
     * @brief Parsea un archivo Excellon.
     * @param path Ruta del archivo.
     * @return Resultado con herramientas y geometría.
     * @throw std::runtime_error si el archivo no se puede abrir.
     */
    ExcellonResult parseFile(const std::string& path) const;

    /**
     * @brief Parsea Excellon desde memoria.
     * @param source Contenido completo.
     * @return Resultado del parseo.
     */
    ExcellonResult parseString(std::string_view source) const;

private:
    ExcellonOptions opt_; ///< Opciones de importación.
};

}  // namespace fc
