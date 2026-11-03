/**
 * @file dxf.cpp
 * @brief Exportador DXF R2000 (AC1015) de la geometría de un objeto.
 *
 * Convierte los polígonos de cobre y los recorridos abiertos a entidades
 * LWPOLYLINE en milímetros (INSUNITS = 4). El módulo es sólo de escritura: la
 * importación de DXF vive en importers.cpp.
 * @note Los huecos se emiten como polilíneas cerradas independientes, sin
 *       jerarquía de contorno, igual que hace FlatCAM.
 */
#include "flatcam/dxf.hpp"

#include <fstream>
#include <sstream>

namespace fc {
namespace {

/**
 * @brief Escribe una polilínea DXF (LWPOLYLINE) en el buffer de salida.
 *
 * Emite los pares grupo mínimos: código 8 (capa "0"), 90 (cantidad de
 * vértices), 70 (bandera de cierre) y los pares 10/20 por cada vértice.
 * @param out Flujo de texto donde se acumula el documento DXF.
 * @param pts Vértices en milímetros; con menos de 2 no se escribe nada.
 * @param closed true para marcar la polilínea como cerrada (código 70 = 1).
 */
void writePolyline(std::ostringstream& out, const Ring& pts, bool closed) {
    if (pts.size() < 2) return;
    out << "0\nLWPOLYLINE\n8\n0\n90\n" << pts.size() << "\n70\n" << (closed ? 1 : 0) << "\n";
    for (const Pt& p : pts) {
        out << "10\n" << p.x << "\n20\n" << p.y << "\n";
    }
}

}  // namespace

/**
 * @brief Exporta sólidos y recorridos a un archivo DXF completo.
 *
 * Arma en memoria las secciones HEADER ($ACADVER = AC1015, $INSUNITS = 4) y
 * ENTITIES. Los sólidos se escriben como polilíneas cerradas (el contorno de
 * cada polígono y uno por hueco); los recorridos se escriben abiertos,
 * aceptando LineString/LinearRing simples o colecciones, de las que se toman
 * sólo las partes lineales.
 * @param path Ruta de destino; se sobrescribe si ya existe.
 * @param solids Polígonos de cobre (con huecos) a exportar.
 * @param follows Contornos abiertos (aislamiento, fresado) a exportar.
 * @throw std::runtime_error si el archivo no se puede abrir para escritura.
 */
void exportDxf(const std::string& path, const std::vector<Geom>& solids,
               const std::vector<Geom>& follows) {
    std::ostringstream out;
    out << "0\nSECTION\n2\nHEADER\n";
    out << "9\n$ACADVER\n1\nAC1015\n";
    out << "9\n$INSUNITS\n70\n4\n";
    out << "0\nENDSEC\n";
    out << "0\nSECTION\n2\nENTITIES\n";

    for (const Geom& g : solids) {
        for (const PolygonContours& pc : g.polygons()) {
            writePolyline(out, pc.outer, true);
            for (const Ring& hole : pc.holes) writePolyline(out, hole, true);
        }
    }
    for (const Geom& g : follows) {
        if (!g.valid() || g.empty()) continue;
        if (g.typeId() == GEOS_LINESTRING || g.typeId() == GEOS_LINEARRING) {
            writePolyline(out, g.coords(), false);
        } else {
            for (int i = 0; i < g.numGeometries(); ++i) {
                const Geom part = g.geometryN(i);
                if (part.typeId() == GEOS_LINESTRING || part.typeId() == GEOS_LINEARRING)
                    writePolyline(out, part.coords(), false);
            }
        }
    }

    out << "0\nENDSEC\n0\nEOF\n";

    std::ofstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot write DXF: " + path);
    file << out.str();
}

}  // namespace fc
