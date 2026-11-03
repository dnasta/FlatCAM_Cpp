/**
 * @file isolation.hpp
 * @brief Aislamiento de cobre por offsets sucesivos.
 *
 * Implementa el esquema de FlatCAM: para cada pasada se calcula
 * `offset = Ø * ((2i+1)/2) - i * solape * Ø`, se hace un buffer del cobre y se
 * extraen los contornos exteriores e interiores como recorridos.
 */
#pragma once

#include <vector>

#include "flatcam/geometry.hpp"

namespace fc {

/**
 * @brief Parámetros del aislamiento.
 */
struct IsolationOptions {
    double toolDiameter = 0.2; ///< Diámetro de la fresa.
    int passes = 1;            ///< Cantidad de pasadas.
    double overlap = 0.0;      ///< Solape como fracción del diámetro (0..0.99).
    int quadrantSegments = 16; ///< Segmentos por cuadrante.
    bool invert = false;       ///< Invierte el sentido (climb/conventional).
    bool exteriorsOnly = false;///< Sólo contornos exteriores.
    bool interiorsOnly = false;///< Sólo agujeros/contornos interiores.
    bool avoidIntersections = false; ///< Omite pasadas que chocan con otro cobre (rest).
};

/**
 * @brief Resultado del aislamiento.
 */
struct IsolationResult {
    std::vector<Geom> paths;       ///< Recorridos del centro de la fresa.
    std::vector<Geom> envelopes;   ///< Cobre dilatado por pasada (preview).
    std::vector<Geom> notIsolated; ///< Polígonos sin pasada válida.
    double totalLength = 0.0;     ///< Longitud total.
    BBox bounds;                  ///< Límites.
};

/**
 * @brief Calcula el aislamiento de una geometría de cobre.
 * @param copper Cobre original.
 * @param options Pasadas, solape, filtros y sentido.
 * @return Recorridos, envolventes y longitud.
 */
IsolationResult isolateCopper(const Geom& copper, const IsolationOptions& options);

/**
 * @brief Rest machining con una herramienta menor.
 *
 * Reutiliza el trabajo de una herramienta mayor y aísla sólo las zonas que
 * ésta no pudo alcanzar.
 *
 * @param copper Cobre original.
 * @param previousToolDiameter Diámetro de la herramienta anterior.
 * @param options Opciones de la herramienta menor.
 */
IsolationResult isolateRest(const Geom& copper, double previousToolDiameter,
                            const IsolationOptions& options);

}  // namespace fc
