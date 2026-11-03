/**
 * @file thieving.cpp
 * @brief Grilla de puntos de cobre para copper thieving.
 *
 * Implementa `fc::thievingPoints`: recorre el área de trabajo con una malla
 * regular de paso `spacing`, desplazada media separación para que los puntos
 * queden centrados, y descarta los que caen dentro del cobre dilatado por
 * `radio del punto + clearance`. El módulo no genera geometría ni G-code:
 * sólo devuelve los centros, y el llamador decide cómo dibujarlos o
 * exportarlos (por ejemplo como flashes Gerber).
 */

#include "flatcam/thieving.hpp"

#include <algorithm>
#include <cmath>

namespace fc {

/**
 * @brief Calcula los centros de una malla de puntos que no invade el cobre.
 *
 * El algoritmo es puramente raster: recorre `area` de izquierda a derecha y de
 * abajo hacia arriba con paso `spacing` (mínimo 0,1 mm), empezando en
 * `ymin/xmin + margin + spacing/2` para que el primer punto quede a media
 * separación del borde útil. Para cada candidato construye un círculo de
 * `dotDiameter/2` y lo descarta si intersecta el "keep-out", que es el cobre
 * original dilatado por `dotDiameter/2 + clearance` con 8 segmentos por
 * cuadrante.
 *
 * @param copper Cobre existente que debe evitarse (puede ser inválido).
 * @param area Rectángulo donde se siembra la grilla.
 * @param options Diámetro del punto, separación, distancia de seguridad y
 *                margen interior.
 * @return Centros `Pt` válidos, en orden de barrido (fila por fila).
 *
 * @note Si `area` no es válida devuelve un vector vacío; si `copper` no es
 *       válido no hay keep-out y se devuelven todos los puntos de la malla.
 * @note La separación se satura a 0,1 mm para evitar bucles infinitos con
 *       valores nulos o negativos.
 * @warning El costo es O(filas × columnas) con una intersección GEOS por punto;
 *          no se usa índice espacial, por lo que áreas grandes con muchos
 *          puntos pueden ser lentas.
 * @warning Si `margin` supera la mitad del ancho o alto del área, los bucles
 *          no se ejecutan y el resultado queda vacío sin aviso.
 */
std::vector<Pt> thievingPoints(const Geom& copper, const BBox& area,
                               const ThievingOptions& options) {
    std::vector<Pt> points;
    if (!area.valid) return points;

    const double radius = options.dotDiameter / 2.0;
    const double spacing = std::max(0.1, options.spacing);
    const Geom keepOut = copper.valid() ? copper.buffer(radius + options.clearance, 8) : Geom();

    const double margin = options.margin;
    const double x0 = area.xmin + margin + spacing / 2.0;
    const double y0 = area.ymin + margin + spacing / 2.0;
    const int nx = x0 <= area.xmax - margin
                       ? static_cast<int>(std::floor((area.xmax - margin - x0) / spacing)) + 1
                       : 0;
    const int ny = y0 <= area.ymax - margin
                       ? static_cast<int>(std::floor((area.ymax - margin - y0) / spacing)) + 1
                       : 0;
    for (int iy = 0; iy < ny; ++iy) {
        const double y = y0 + iy * spacing;
        for (int ix = 0; ix < nx; ++ix) {
            const double x = x0 + ix * spacing;
            const Geom dot = Geom::circle({x, y}, radius, 8);
            if (keepOut.valid() && keepOut.intersects(dot)) continue;
            points.push_back({x, y});
        }
    }
    return points;
}

}  // namespace fc
