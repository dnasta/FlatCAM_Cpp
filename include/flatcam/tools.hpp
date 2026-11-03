/**
 * @file tools.hpp
 * @brief Utilidades CAM: distancia, herramienta óptima, taladros y reglas.
 *
 * Reúne funciones auxiliares usadas por las herramientas "extra" de FlatCAM:
 * medición entre capas, cálculo del diámetro máximo de aislamiento, extracción
 * de taladros desde un Gerber, punch, edición de aperturas, marcas de esquina
 * y comprobación de reglas.
 */
#pragma once

#include <vector>

#include "flatcam/excellon.hpp"
#include "flatcam/geometry.hpp"
#include "flatcam/gerber.hpp"
#include "flatcam/units.hpp"

namespace fc {

// Small utilities shared by the "extra" CAM tools (distance, optimal tool,
// drill extraction, corner marks, punch, rules check).

/**
 * @brief Resultado del cálculo de la herramienta óptima.
 */
struct OptimalTool {
    double diameter = 0.0; ///< Mayor fresa que aún aísla el cobre.
    double gap = 0.0;      ///< Separación que limita el diámetro.
    Pt pointA;             ///< Punto del primer elemento del par limitante.
    Pt pointB;             ///< Punto del segundo elemento.
    bool valid = false;    ///< `true` si se encontró un límite.
};

/**
 * @brief Calcula la separación mínima cobre-cobre y la fresa máxima.
 * @param copper Geometría de cobre.
 * @param maxDiameter Tope superior de búsqueda.
 * @return Diámetro, separación y puntos limitantes.
 */
OptimalTool optimalIsolationDiameter(const Geom& copper, double maxDiameter = 10.0);

/**
 * @brief Lista los pares de separación menores que `maxDiameter`.
 * @param copper Geometría de cobre.
 * @param maxDiameter Separación máxima a listar.
 * @param limit Cantidad máxima de pares.
 * @param minSeparation Separación mínima a considerar.
 * @return Pares ordenados de menor a mayor separación.
 */
std::vector<OptimalTool> optimalIsolationGaps(const Geom& copper, double maxDiameter = 10.0,
                                              std::size_t limit = 20,
                                              double minSeparation = 0.0);

/**
 * @brief Distancia mínima entre dos colecciones de geometría.
 * @param a Primera colección.
 * @param b Segunda colección.
 * @param nearestA Si no es nulo, recibe el punto más cercano de `a`.
 * @param nearestB Si no es nulo, recibe el punto más cercano de `b`.
 * @return Distancia en mm.
 */
double layersDistance(const std::vector<Geom>& a, const std::vector<Geom>& b,
                      Pt* nearestA = nullptr, Pt* nearestB = nullptr);

/**
 * @brief Extrae taladros circulares de un Gerber.
 *
 * Detecta flashes circulares y arma un `ExcellonResult` con una herramienta
 * por diámetro distinto.
 *
 * @param gerber Gerber parseado.
 * @param maxDiameter Diámetro máximo a considerar taladro.
 * @param appUnits Unidad de salida.
 * @param stepsPerCircle Segmentos por círculo.
 */
ExcellonResult extractDrills(const GerberResult& gerber, double maxDiameter = 3.0,
                             Units appUnits = Units::Mm, int stepsPerCircle = 32);

/**
 * @brief Resta los taladros de un Excellon al cobre de un Gerber.
 * @param gerber Cobre original.
 * @param drills Taladros a restar.
 * @param margin Holgura adicional alrededor del taladro.
 * @return Cobre con los taladros despejados.
 */
Geom punchGerber(const GerberResult& gerber, const ExcellonResult& drills,
                 double margin = 0.0);

/**
 * @brief Cambia el diámetro de una apertura circular en el texto Gerber.
 * @param gerber Texto original.
 * @param code Código de apertura (`D10`, `10`, etc.).
 * @param diameter Nuevo diámetro.
 * @param decimals Decimales a escribir.
 * @param changed Recibe la cantidad de reemplazos.
 * @return Texto modificado.
 */
std::string editCircleAperture(std::string gerber, const std::string& code, double diameter,
                               int decimals, int* changed = nullptr);

/**
 * @brief Ajusta una apertura para que su dimensión mayor sea `size`.
 *
 * Rectángulos y obrounds conservan su relación de aspecto; polígonos y
 * círculos usan el diámetro.
 *
 * @param gerber Texto original.
 * @param code Código de apertura.
 * @param size Nueva dimensión mayor.
 * @param decimals Decimales.
 * @param changed Recibe la cantidad de reemplazos.
 */
std::string editApertureSize(std::string gerber, const std::string& code, double size,
                             int decimals, int* changed = nullptr);

/**
 * @brief Genera marcas de esquina en L.
 * @param bounds Caja de referencia.
 * @param length Longitud de cada brazo.
 * @param margin Separación al borde.
 * @param outside `true` para dibujarlas por fuera.
 */
std::vector<Geom> cornerMarks(const BBox& bounds, double length = 3.0,
                              double margin = 0.0, bool outside = false);

}  // namespace fc
