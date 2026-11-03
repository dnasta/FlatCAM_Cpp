/**
 * @file rtree.cpp
 * @brief Índice espacial de geometrías sobre GEOSSTRtree.
 *
 * Mantiene copias de las geometrías insertadas (el árbol no puede apuntar a
 * objetos temporales) y devuelve los identificadores numéricos asociados en
 * las consultas por caja. La construcción del árbol es perezosa: ocurre en la
 * primera consulta o cuando se llama a build().
 */
#include "flatcam/geometry.hpp"

#include <stdexcept>

namespace fc {
namespace {

/// Contexto compartido entre GEOSSTRtree_query y el callback C.
struct QueryContext {
    std::vector<std::uint32_t>* out;
};

/**
 * @brief Callback C invocado por GEOS por cada elemento que cae en la ventana.
 *
 * GEOS devuelve el puntero tal como se insertó; como en la inserción se guardó
 * el id casteado a puntero, acá se reconstruye vía intptr_t antes de añadirlo
 * a la lista de resultados.
 * @param item Puntero opaco registrado en la inserción (el id).
 * @param userdata Puntero a QueryContext con el vector de salida.
 */
void queryCallback(void* item, void* userdata) {
    auto* ctx = static_cast<QueryContext*>(userdata);
    ctx->out->push_back(static_cast<std::uint32_t>(reinterpret_cast<std::intptr_t>(item)));
}

}  // namespace

/**
 * @brief Crea el árbol R con capacidad de nodo 10.
 * @throw std::runtime_error si GEOS no puede crear el árbol.
 * @note Llama antes a initGeosRuntime() porque la API clásica de GEOS aborta
 *       si no fue inicializada.
 */
SpatialIndex::SpatialIndex() {
    initGeosRuntime();
    tree_ = GEOSSTRtree_create(10);
    if (!tree_) throw std::runtime_error("GEOSSTRtree_create failed");
}

/// Destruye el árbol GEOS si llegó a crearse.
SpatialIndex::~SpatialIndex() {
    if (tree_) GEOSSTRtree_destroy(tree_);
}

/**
 * @brief Inserta una geometría asociándole un identificador.
 *
 * Guarda una copia en keep_ (dueña de la memoria), registra el id y entrega a
 * GEOS el puntero crudo junto con el id casteado a void*. Las geometrías
 * inválidas se ignoran en silencio.
 * @param g Geometría a indexar.
 * @param id Identificador que devolverá query().
 * @note El patrón previsto es insertar todo y recién entonces consultar:
 *       insertar después de build() exige reconstruir el árbol.
 */
void SpatialIndex::insert(const Geom& g, std::uint32_t id) {
    if (!g.valid()) return;
    keep_.push_back(g);
    ids_.push_back(id);
    GEOSSTRtree_insert(tree_, g.raw(), reinterpret_cast<void*>(static_cast<std::intptr_t>(id)));
}

/**
 * @brief Construye el árbol una única vez (idempotente).
 * @note Construir dos veces es un error de GEOS, por eso se protege con built_.
 */
void SpatialIndex::build() {
    if (!built_) {
        GEOSSTRtree_build(tree_);
        built_ = true;
    }
}

/**
 * @brief Devuelve los ids cuyas geometrías intersectan la caja dada.
 *
 * Fuerza build() si aún no se hizo, crea un rectángulo GEOS con la ventana y
 * delega en GEOSSTRtree_query. La comparación es sólo de envolventes: si se
 * necesita intersección exacta, el llamador debe verificarla después.
 * @param box Ventana de búsqueda; si no es válida devuelve lista vacía.
 * @return Ids en el orden en que GEOS los recorre (sin orden garantizado).
 */
std::vector<std::uint32_t> SpatialIndex::query(const BBox& box) const {
    std::vector<std::uint32_t> out;
    if (!box.valid) return out;
    if (!built_) const_cast<SpatialIndex*>(this)->build();
    GEOSGeometry* envelope = GEOSGeom_createRectangle(box.xmin, box.ymin, box.xmax, box.ymax);
    if (!envelope) return out;
    QueryContext ctx{&out};
    GEOSSTRtree_query(tree_, envelope, queryCallback, &ctx);
    GEOSGeom_destroy(envelope);
    return out;
}

}  // namespace fc
