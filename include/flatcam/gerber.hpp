/**
 * @file gerber.hpp
 * @brief Parser de Gerber RS-274X (capas de PCB).
 *
 * El parser lee el archivo completo o una cadena, interpreta el formato
 * (`FS`), las unidades (`MO`/`G70`/`G71`), aperturas estándar y macros,
 * trazos, arcos, regiones, polaridad y step & repeat. La salida contiene:
 *
 * - sólidos (polígonos rellenados, ya en unidades de la aplicación),
 * - recorridos (trazos y puntos),
 * - aperturas y macros para el editor,
 * - avisos de elementos no soportados.
 *
 * La geometría se construye con `fc::Geom` (GEOS) y se mantiene en milímetros,
 * salvo la copia por apertura que se guarda en unidades del archivo para
 * editarla.
 */
#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "flatcam/geometry.hpp"
#include "flatcam/units.hpp"

namespace fc {

/**
 * @brief Opciones de importación de Gerber.
 */
struct GerberOptions {
    Units appUnits = Units::Mm;          ///< Unidad de salida de la geometría.
    int stepsPerCircle = 64;             ///< Segmentos por círculo (FlatCAM: `gerber_circle_steps`).
    bool simplification = false;         ///< Simplifica la geometría al terminar.
    double simplifyTolerance = 0.005;    ///< Tolerancia de simplificación en mm.
    /// Reproduce el bug de FlatCAM 2024.4 al trazar con apertura rectangular:
    /// usa una caja entre los dos primeros puntos en vez de un barrido
    /// Minkowski. Se activa sólo para comparar geometría con el original.
    bool legacyRectStroke = false;
};

/**
 * @brief Definición de una apertura Gerber y las primitivas dibujadas con ella.
 */
struct GerberAperture {
    /// Tipo de apertura.
    enum class Kind {
        Circle,    ///< Círculo (`C`).
        Rectangle, ///< Rectángulo (`R`).
        Obround,   ///< Óvalo (`O`).
        Polygon,   ///< Polígono regular (`P`).
        Macro,     ///< Macro de apertura (`AM`).
        Region     ///< Región `G36/G37` sin apertura explícita.
    };

    Kind kind = Kind::Circle;        ///< Tipo.
    double size = 0.0;               ///< Tamaño principal (diámetro o lado).
    double width = 0.0;              ///< Ancho para rectángulos/obrounds.
    double height = 0.0;             ///< Alto para rectángulos/obrounds.
    double diameter = 0.0;           ///< Diámetro para círculos/polígonos.
    int vertices = 0;                ///< Vértices para polígonos.
    double rotation = 0.0;           ///< Rotación en grados.
    bool hasRotation = false;        ///< `true` si el archivo especificó rotación.
    std::string macroName;           ///< Nombre de la macro (si es `Macro`).
    std::vector<double> modifiers;   ///< Parámetros de la apertura/macro.
    std::vector<Geom> geometry;      ///< Primitivas dibujadas con esta apertura (unidades del archivo).
};

/**
 * @brief Macro de apertura (`AM`) tal como aparece en el archivo.
 */
struct GerberMacro {
    std::string name; ///< Nombre de la macro.
    std::string raw;  ///< Cuerpo completo original, para editarlo.
};

/**
 * @brief Resultado completo de parsear un Gerber.
 *
 * `solids` y `follows` están en unidades de la aplicación (mm). El campo
 * `solid` es la unión de todos los sólidos y se usa para área y límites.
 */
struct GerberResult {
    Units fileUnits = Units::Inch; ///< Unidades declaradas por el archivo.
    int intDigits = 3;             ///< Dígitos enteros del formato `FS`.
    int fracDigits = 4;            ///< Dígitos fraccionarios del formato `FS`.
    char zeros = 'L';              ///< Supresión de ceros (`L`, `T`, `D`).
    bool incremental = false;      ///< `true` si el archivo usa coordenadas incrementales.

    std::map<std::string, GerberAperture> apertures; ///< Aperturas por código `Dnn`.
    std::map<std::string, GerberMacro> macros;       ///< Macros `AM` por nombre.

    std::vector<Geom> solids;   ///< Polígonos oscuros, en unidades de la app.
    std::vector<Geom> follows;  ///< Trazos y puntos, en unidades de la app.
    Geom solid;                 ///< Unión de todos los sólidos (cobre).
    BBox bounds;                ///< Límites de la geometría.

    std::size_t flashes = 0;    ///< Cantidad de flashes `D03`.
    std::size_t segments = 0;   ///< Cantidad de trazos.
    std::size_t regions = 0;    ///< Cantidad de regiones `G36/G37`.
    std::vector<std::string> warnings; ///< Avisos no fatales.

    /// @return `true` si no se generó cobre.
    bool empty() const { return solid.empty(); }
};

/**
 * @brief Parser de archivos Gerber RS-274X.
 *
 * Es reutilizable: se construye con las opciones y se pueden parsear varios
 * archivos con la misma instancia. No guarda estado entre llamadas.
 */
class GerberParser {
public:
    /**
     * @brief Crea un parser con las opciones indicadas.
     * @param options Unidades de salida, pasos por círculo y simplificación.
     */
    explicit GerberParser(GerberOptions options = {});

    /**
     * @brief Parsea un archivo Gerber.
     * @param path Ruta del archivo.
     * @return Resultado con geometría, aperturas y avisos.
     * @throw std::runtime_error si el archivo no se puede abrir o el formato
     *        es irrecuperable.
     */
    GerberResult parseFile(const std::string& path) const;

    /**
     * @brief Parsea Gerber desde memoria.
     * @param source Contenido completo del archivo.
     * @return Resultado del parseo.
     * @throw std::runtime_error en errores irrecuperables.
     */
    GerberResult parseString(std::string_view source) const;

private:
    GerberOptions opt_; ///< Opciones de importación.
};

}  // namespace fc
