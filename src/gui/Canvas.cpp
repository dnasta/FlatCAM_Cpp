/**
 * @file Canvas.cpp
 * @brief Implementación del visor 2D: capas, rutas Qt, zoom, pan y selección.
 *
 * Convierte la geometría GEOS de cada capa en `QPainterPath` (sólidos con
 * regla par-impar y recorridos abiertos), mantiene los límites globales para
 * ajustar la vista, dibuja rejilla adaptativa, ejes, área de trabajo, cobre,
 * recorridos, trayectorias y HUD, y traduce los eventos de ratón a zoom,
 * desplazamiento y selección. La exportación de film reutiliza las mismas rutas
 * mediante `QSvgGenerator`.
 */
#include "Canvas.h"

#include <QMouseEvent>
#include <QPainterPathStroker>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QSvgGenerator>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>

/**
 * @brief Construye el canvas vacío y configura el widget.
 *
 * Fija un tamaño mínimo de 400x300 px, activa el seguimiento del ratón (para
 * refrescar el HUD y la barra de estado sin pulsar botones) y el foco por
 * teclado. El rol de fondo `QPalette::Base` deja que la hoja de estilo de la
 * aplicación tiña el lienzo de forma coherente con el tema activo.
 *
 * @param parent Widget contenedor; la propiedad la asume Qt por jerarquía.
 * @note No carga geometría: mientras `hasData_` sea falso, paintEvent dibuja
 *       el mensaje de bienvenida.
 */
Canvas::Canvas(QWidget* parent) : QWidget(parent) {
    setMinimumSize(400, 300);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setBackgroundRole(QPalette::Base);
}

/**
 * @brief Convierte polígonos GEOS en una única ruta Qt con agujeros.
 *
 * Recorre todas las geometrías y todos sus `PolygonContours`. Por cada contorno
 * agrega el anillo exterior y luego cada agujero como subcamino independiente,
 * cerrando ambos con `closeSubpath()`. La ruta se crea con regla de relleno
 * **par-impar** (`Qt::OddEvenFill`): así un agujero contenido en el exterior
 * queda vacío sin depender de la orientación de los anillos.
 *
 * @param geoms Sólidos a dibujar; puede estar vacío.
 * @return Ruta lista para `QPainter::drawPath`, o ruta vacía si ningún anillo
 *         tiene al menos 3 puntos.
 * @note Las coordenadas se copian sin transformar (unidades de mundo, mm por
 *       convención) y se descartan los contornos degenerados. No se aplica
 *       grosor de línea: el trazo es cosmético y lo decide el `QPen` del
 *       llamador.
 */
QPainterPath Canvas::buildPath(const std::vector<fc::Geom>& geoms) {
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    for (const fc::Geom& g : geoms) {
        for (const fc::PolygonContours& poly : g.polygons()) {
            if (poly.outer.size() < 3) continue;
            QPolygonF outer;
            outer.reserve(static_cast<int>(poly.outer.size()));
            for (const fc::Pt& p : poly.outer) outer.append(QPointF(p.x, p.y));
            path.addPolygon(outer);
            path.closeSubpath();
            for (const fc::Ring& hole : poly.holes) {
                if (hole.size() < 3) continue;
                QPolygonF h;
                h.reserve(static_cast<int>(hole.size()));
                for (const fc::Pt& p : hole) h.append(QPointF(p.x, p.y));
                path.addPolygon(h);
                path.closeSubpath();
            }
        }
    }
    return path;
}

/**
 * @brief Agrega a una ruta los tramos de una geometría de tipo línea.
 *
 * Resuelve de forma recursiva `LINESTRING`/`LINEARRING` y las colecciones
 * (`MULTILINESTRING`, `GEOMETRYCOLLECTION`, `MULTIPOLYGON`) recorriendo sus
 * componentes con `geometryN()`. Para una línea usa `moveTo()` en el primer
 * vértice y `lineTo()` en los restantes; no cierra el anillo porque un
 * `LINEARRING` de GEOS ya repite el primer punto al final.
 *
 * @param path Ruta destino; se modifica en sitio.
 * @param g Geometría de origen.
 * @note Las geometrías sin componentes de línea (polígonos sueltos) se ignoran:
 *       los contornos rellenos se dibujan con buildPath, no aquí.
 * @warning Sólo se soportan segmentos rectos; los arcos ya llegan aplanados
 *          desde el parser, así que no se pierde información práctica.
 */
void Canvas::appendLinePath(QPainterPath& path, const fc::Geom& g) {
    if (!g.valid() || g.empty()) return;
    const int type = g.typeId();
    if (type == GEOS_LINESTRING || type == GEOS_LINEARRING) {
        const fc::Ring pts = g.coords();
        if (pts.size() < 2) return;
        path.moveTo(pts.front().x, pts.front().y);
        for (std::size_t i = 1; i < pts.size(); ++i) path.lineTo(pts[i].x, pts[i].y);
        return;
    }
    if (type == GEOS_MULTILINESTRING || type == GEOS_GEOMETRYCOLLECTION ||
        type == GEOS_MULTIPOLYGON) {
        const int n = g.numGeometries();
        for (int i = 0; i < n; ++i) appendLinePath(path, g.geometryN(i));
    }
}

/**
 * @brief Construye la ruta de recorridos (líneas) de un conjunto de geometrías.
 *
 * @param geoms Recorridos de la capa (contornos de aislamiento, taladros,
 *        G-code, etc.).
 * @return Ruta con todos los tramos; vacía si ninguna geometría aporta líneas.
 * @see appendLinePath
 */
QPainterPath Canvas::buildLinePath(const std::vector<fc::Geom>& geoms) {
    QPainterPath path;
    for (const fc::Geom& g : geoms) appendLinePath(path, g);
    return path;
}

/**
 * @brief Busca una capa por identificador para modificarla.
 *
 * @param id Identificador estable de la capa.
 * @return Puntero a la capa, o `nullptr` si no existe.
 * @note Búsqueda lineal: el número de capas es pequeño (decenas). El puntero
 *       deja de ser válido si se agrega o elimina una capa, porque
 *       `layers_` es un `std::vector`.
 */
Canvas::Layer* Canvas::findLayer(int id) {
    for (Layer& layer : layers_) {
        if (layer.id == id) return &layer;
    }
    return nullptr;
}

/**
 * @brief Variante constante de findLayer.
 *
 * @param id Identificador estable de la capa.
 * @return Puntero de sólo lectura a la capa, o `nullptr` si no existe.
 */
const Canvas::Layer* Canvas::findLayer(int id) const {
    for (const Layer& layer : layers_) {
        if (layer.id == id) return &layer;
    }
    return nullptr;
}

/**
 * @brief Reemplaza todo el contenido del visor por una única capa.
 *
 * Descarta las capas existentes y los límites previos, construye la capa con
 * las rutas de sólidos y recorridos, conserva la geometría GEOS y calcula la
 * caja envolvente global: primero con los sólidos y, si ninguno aporta límites
 * válidos, con los recorridos (caso típico de una capa sólo de G-code). Al
 * final ajusta la vista y repinta.
 *
 * @param solids Polígonos de cobre.
 * @param follows Recorridos abiertos.
 * @note `hasData_` sólo queda activo si la caja resultante es válida; si no,
 *       el visor sigue mostrando el mensaje de bienvenida.
 * @warning No limpia `toolpathPath_` (la previsualización CAM); para eso está
 *          clearData(). Tampoco reinicia `nextId_`, por lo que los
 *          identificadores siguen creciendo entre cargas.
 */
void Canvas::setGeometryData(const std::vector<fc::Geom>& solids,
                             const std::vector<fc::Geom>& follows) {
    layers_.clear();
    bounds_ = fc::BBox();
    hasData_ = false;

    Layer layer;
    layer.id = nextId_++;
    layer.solid = buildPath(solids);
    layer.follow = buildLinePath(follows);
    layer.solids = solids;
    layer.follows = follows;
    layers_.push_back(std::move(layer));

    for (const fc::Geom& g : solids) {
        const fc::BBox gb = g.bounds();
        if (!gb.valid) continue;
        if (!bounds_.valid) {
            bounds_ = gb;
        } else {
            bounds_.xmin = std::min(bounds_.xmin, gb.xmin);
            bounds_.ymin = std::min(bounds_.ymin, gb.ymin);
            bounds_.xmax = std::max(bounds_.xmax, gb.xmax);
            bounds_.ymax = std::max(bounds_.ymax, gb.ymax);
        }
    }
    if (!bounds_.valid) {
        for (const fc::Geom& g : follows) {
            const fc::BBox gb = g.bounds();
            if (!gb.valid) continue;
            if (!bounds_.valid) bounds_ = gb;
            else {
                bounds_.xmin = std::min(bounds_.xmin, gb.xmin);
                bounds_.ymin = std::min(bounds_.ymin, gb.ymin);
                bounds_.xmax = std::max(bounds_.xmax, gb.xmax);
                bounds_.ymax = std::max(bounds_.ymax, gb.ymax);
            }
        }
    }
    hasData_ = bounds_.valid;
    fit();
    update();
}

/**
 * @brief Inicia una carga por lotes de varias capas.
 *
 * Vacía capas, límites y la bandera de datos, pero **no** repinta: la
 * intención es que el llamador encadene varios appendGeometry y cierre con
 * endGeometry, que calcula el ajuste una sola vez.
 *
 * @note No limpia `toolpathPath_`.
 */
void Canvas::beginGeometry() {
    layers_.clear();
    bounds_ = fc::BBox();
    hasData_ = false;
}

/**
 * @brief Agrega una capa nueva durante una carga por lotes.
 *
 * Construye las rutas Qt de sólidos y recorridos, guarda la geometría GEOS,
 * extiende la caja global con los límites válidos de cada geometría y
 * devuelve el identificador asignado. La capa se agrega al final de la lista,
 * es decir, encima de las anteriores.
 *
 * @param solids Polígonos de la capa.
 * @param follows Recorridos de la capa.
 * @param keepGeometry `true` conserva la geometría GEOS para edición,
 *        exportación y undo; `false` la descarta para ahorrar memoria en
 *        proyectos grandes (sólo queda el dibujo).
 * @return Identificador de la capa recién creada.
 * @note No repinta ni ajusta la vista: eso lo hace endGeometry().
 */
int Canvas::appendGeometry(const std::vector<fc::Geom>& solids,
                           const std::vector<fc::Geom>& follows, bool keepGeometry) {
    Layer layer;
    layer.id = nextId_++;
    layer.solid = buildPath(solids);
    layer.follow = buildLinePath(follows);
    if (keepGeometry) {
        layer.solids = solids;
        layer.follows = follows;
    }
    layers_.push_back(std::move(layer));

    /// Extiende la caja global con los límites válidos de una geometría.
    auto grow = [this](const fc::BBox& gb) {
        if (!gb.valid) return;
        if (!bounds_.valid) {
            bounds_ = gb;
        } else {
            bounds_.xmin = std::min(bounds_.xmin, gb.xmin);
            bounds_.ymin = std::min(bounds_.ymin, gb.ymin);
            bounds_.xmax = std::max(bounds_.xmax, gb.xmax);
            bounds_.ymax = std::max(bounds_.ymax, gb.ymax);
        }
    };
    for (const fc::Geom& g : solids) grow(g.bounds());
    for (const fc::Geom& g : follows) grow(g.bounds());
    hasData_ = bounds_.valid;
    return layer.id;
}

/**
 * @brief Cierra una carga por lotes.
 *
 * Ajusta la vista a la caja acumulada y repinta una sola vez, evitando el
 * coste de reajustar por cada capa agregada.
 */
void Canvas::endGeometry() {
    fit();
    update();
}

/**
 * @brief Devuelve los identificadores de capa en orden de dibujo.
 *
 * @return Lista de ids de abajo hacia arriba (el último se dibuja encima).
 * @note Se usa para sincronizar el árbol del proyecto con el canvas.
 */
std::vector<int> Canvas::layerIds() const {
    std::vector<int> ids;
    ids.reserve(layers_.size());
    for (const Layer& layer : layers_) ids.push_back(layer.id);
    return ids;
}

/**
 * @brief Reordena las capas según una lista de identificadores.
 *
 * Primero copia las capas en el orden pedido (ignorando los ids que no
 * existen) y después agrega al final, conservando su orden relativo, las capas
 * que no aparecían en la lista. Así nunca se pierde ninguna capa.
 *
 * @param ids Orden deseado, de abajo hacia arriba.
 * @note El coste es O(n·m) con n capas y m ids, aceptable para decenas de
 *       capas. Los identificadores repetidos se ignoran y no se recalcula la
 *       caja (las capas no cambian de posición).
 */
void Canvas::setLayerOrder(const std::vector<int>& ids) {
    std::vector<Layer> ordered;
    ordered.reserve(layers_.size());
    std::vector<int> seen;
    for (int id : ids) {
        if (std::find(seen.begin(), seen.end(), id) != seen.end()) continue;
        for (const Layer& layer : layers_) {
            if (layer.id == id) {
                ordered.push_back(layer);
                seen.push_back(id);
                break;
            }
        }
    }
    for (const Layer& layer : layers_) {
        const bool already = std::any_of(ordered.begin(), ordered.end(),
                                         [&layer](const Layer& l) { return l.id == layer.id; });
        if (!already) ordered.push_back(layer);
    }
    layers_ = std::move(ordered);
    update();
}

/**
 * @brief Muestra u oculta una capa.
 *
 * @param id Identificador de la capa.
 * @param visible Estado deseado.
 * @note Las capas ocultas no se dibujan, no participan de
 *       `visibleSolids()`/`mergeVisibleLayers()` ni de la exportación SVG, pero
 *       sí cuentan en los límites globales. Recalcula el repintado; si el id no
 *       existe la llamada es inocua.
 */
void Canvas::setLayerVisible(int id, bool visible) {
    Layer* layer = findLayer(id);
    if (!layer) return;
    layer->visible = visible;
    update();
}

/**
 * @brief Elimina una capa por identificador.
 *
 * Usa el idioma *erase-remove* sobre el vector de capas, por lo que se
 * conservan el orden relativo y los ids de las restantes. Repinta al final.
 *
 * @param id Capa a quitar; si no existe no ocurre nada.
 * @note Recalcula límites y `hasData_`, así que `fit()` y `exportSvg()` quedan
 *       consistentes tras el borrado. No limpia `toolpathPath_`, que es una
 *       previsualización independiente.
 */
void Canvas::removeLayer(int id) {
    layers_.erase(std::remove_if(layers_.begin(), layers_.end(),
                                 [id](const Layer& layer) { return layer.id == id; }),
                  layers_.end());
    recomputeBounds();
    update();
}

/**
 * @brief Duplica una capa y la coloca encima de todas.
 *
 * Copia las rutas (compartidas implícitamente hasta que se modifiquen), la
 * geometría GEOS y los indicadores de visibilidad, relleno y multicolor, y
 * asigna un identificador nuevo.
 *
 * @param id Capa de origen.
 * @return Id de la copia, o -1 si la capa no existe.
 * @note No extiende los límites ni ajusta la vista: la copia ocupa la misma
 *       zona que el original, así que la caja no cambia.
 */
int Canvas::duplicateLayer(int id) {
    const Layer* source = findLayer(id);
    if (!source) return -1;
    Layer copy = *source;
    copy.id = nextId_++;
    layers_.push_back(copy);
    update();
    return copy.id;
}

/**
 * @brief Calcula los límites de una capa a partir de sus rutas.
 *
 * Une el rectángulo envolvente de los sólidos con el de los recorridos (los
 * recorridos pueden sobresalir del cobre, por ejemplo en un aislamiento) y
 * convierte el resultado a `fc::BBox`.
 *
 * @param id Identificador de la capa.
 * @return Caja válida en coordenadas de mundo, o caja inválida si la capa no
 *         existe o ambas rutas están vacías.
 * @note Es más barato que recorrer la geometría GEOS y sirve para hacer zoom a
 *       un objeto; el lado superior/izquierdo de `QRectF` coincide con el
 *       mínimo de cada eje.
 */
fc::BBox Canvas::layerBounds(int id) const {
    fc::BBox b;
    const Layer* layer = findLayer(id);
    if (!layer) return b;
    QRectF rect = layer->solid.boundingRect();
    const QRectF followRect = layer->follow.boundingRect();
    if (!followRect.isEmpty()) rect = rect.isEmpty() ? followRect : rect.united(followRect);
    if (rect.isEmpty()) return b;
    b.xmin = rect.left();
    b.ymin = rect.top();
    b.xmax = rect.right();
    b.ymax = rect.bottom();
    b.valid = true;
    return b;
}

/**
 * @brief Captura el estado completo de una capa para undo/redo.
 *
 * Copia id, rutas, geometría GEOS y visibilidad. El snapshot es
 * independiente del canvas: puede guardarse en un `QUndoCommand` y
 * reinsertarse después.
 *
 * @param id Capa a capturar.
 * @return Snapshot con id 0 si la capa no existe (las capas reales empiezan
 *         en 1).
 * @note No guarda los indicadores `filled` y `multicolor`: al deshacer un
 *       borrado esas preferencias de dibujo vuelven a su valor por defecto.
 */
Canvas::LayerSnapshot Canvas::layerSnapshot(int id) const {
    LayerSnapshot snapshot;
    const Layer* layer = findLayer(id);
    if (!layer) return snapshot;
    snapshot.id = layer->id;
    snapshot.solid = layer->solid;
    snapshot.follow = layer->follow;
    snapshot.solids = layer->solids;
    snapshot.follows = layer->follows;
    snapshot.visible = layer->visible;
    return snapshot;
}

/**
 * @brief Reinserta una capa a partir de un snapshot (deshacer borrado).
 *
 * Reconstruye la capa con el id original, restaura rutas, geometría y
 * visibilidad, y eleva `nextId_` para que los ids futuros no colisionen con el
 * restaurado.
 *
 * @param snapshot Estado capturado previamente con layerSnapshot().
 * @note La capa se agrega al final (queda dibujada encima); se recalculan
 *       límites y `hasData_` para que el visor vuelva a ajustarse correctamente.
 */
void Canvas::insertLayer(const LayerSnapshot& snapshot) {
    Layer layer;
    layer.id = snapshot.id;
    layer.solid = snapshot.solid;
    layer.follow = snapshot.follow;
    layer.solids = snapshot.solids;
    layer.follows = snapshot.follows;
    layer.visible = snapshot.visible;
    layers_.push_back(std::move(layer));
    nextId_ = std::max(nextId_, snapshot.id + 1);
    recomputeBounds();
    update();
}

/**
 * @brief Devuelve los sólidos GEOS de una capa.
 *
 * @param id Identificador de la capa.
 * @return Copia de la geometría de cobre; vector vacío si la capa no existe o
 *         se creó sólo con rutas (por ejemplo, mergeVisibleLayers).
 * @note La copia es superficial para GEOS: `fc::Geom` comparte la geometría
 *       subyacente, así que es barata.
 */
std::vector<fc::Geom> Canvas::layerSolids(int id) const {
    const Layer* layer = findLayer(id);
    return layer ? layer->solids : std::vector<fc::Geom>{};
}

/**
 * @brief Devuelve los recorridos GEOS de una capa.
 *
 * @param id Identificador de la capa.
 * @return Copia de los recorridos; vector vacío si la capa no existe.
 */
std::vector<fc::Geom> Canvas::layerFollows(int id) const {
    const Layer* layer = findLayer(id);
    return layer ? layer->follows : std::vector<fc::Geom>{};
}

/**
 * @brief Reemplaza la geometría de una capa y regenera su dibujo.
 *
 * Sustituye sólidos y recorridos, reconstruye ambas rutas Qt, recalcula los
 * límites globales (sin reajustar la vista) y repinta. Es el camino que usan
 * los editores, las transformaciones y las operaciones booleanas.
 *
 * @param id Capa a modificar; si no existe no ocurre nada.
 * @param solids Nuevos polígonos.
 * @param follows Nuevos recorridos.
 * @note Conserva la visibilidad y el modo de dibujo de la capa.
 */
void Canvas::setLayerGeometry(int id, const std::vector<fc::Geom>& solids,
                              const std::vector<fc::Geom>& follows) {
    Layer* layer = findLayer(id);
    if (!layer) return;
    layer->solids = solids;
    layer->follows = follows;
    layer->solid = buildPath(solids);
    layer->follow = buildLinePath(follows);
    recomputeBounds();
    update();
}

/**
 * @brief Recalcula la caja envolvente de todas las capas.
 *
 * Parte de una caja inválida y la extiende con la unión de las rutas de sólidos
 * y recorridos de cada capa, incluidas las ocultas. Actualiza `hasData_` según
 * el resultado.
 *
 * @note No ajusta la vista ni repinta: eso queda a cargo del llamador, para
 *       permitir ediciones encadenadas sin saltos de zoom.
 */
void Canvas::recomputeBounds() {
    bounds_ = fc::BBox();
    for (const Layer& layer : layers_) {
        QRectF rect = layer.solid.boundingRect();
        const QRectF followRect = layer.follow.boundingRect();
        if (!followRect.isEmpty()) rect = rect.isEmpty() ? followRect : rect.united(followRect);
        if (rect.isEmpty()) continue;
        fc::BBox b{rect.left(), rect.top(), rect.right(), rect.bottom()};
        if (!bounds_.valid) {
            bounds_ = b;
        } else {
            bounds_.xmin = std::min(bounds_.xmin, b.xmin);
            bounds_.ymin = std::min(bounds_.ymin, b.ymin);
            bounds_.xmax = std::max(bounds_.xmax, b.xmax);
            bounds_.ymax = std::max(bounds_.ymax, b.ymax);
        }
    }
    hasData_ = bounds_.valid;
}

/**
 * @brief Combina las capas visibles en una capa nueva ("film" interno).
 *
 * Une los sólidos con `QPainterPath::united()` y concatena los recorridos con
 * `addPath()`. El resultado se agrega encima de las demás capas; las originales
 * no se ocultan ni se borran.
 *
 * @return Id de la capa combinada, o -1 si no hay nada visible con contenido.
 * @note También concatena `solids`/`follows` de las capas visibles. Las capas
 *       cargadas en modo streaming (sin geometría) sólo aportan su dibujo, por
 *       lo que no se pueden transformar después.
 * @note La unión de rutas es una operación costosa con mucho cobre; en placas
 *       grandes puede tardar segundos.
 */
int Canvas::mergeVisibleLayers() {
    QPainterPath mergedSolid;
    QPainterPath mergedFollow;
    std::vector<fc::Geom> mergedSolids;
    std::vector<fc::Geom> mergedFollows;
    for (const Layer& layer : layers_) {
        if (!layer.visible) continue;
        mergedSolid = mergedSolid.united(layer.solid);
        mergedFollow.addPath(layer.follow);
        mergedSolids.insert(mergedSolids.end(), layer.solids.begin(), layer.solids.end());
        mergedFollows.insert(mergedFollows.end(), layer.follows.begin(), layer.follows.end());
    }
    if (mergedSolid.isEmpty() && mergedFollow.isEmpty()) return -1;

    Layer layer;
    layer.id = nextId_++;
    layer.solid = mergedSolid;
    layer.follow = mergedFollow;
    layer.solids = std::move(mergedSolids);
    layer.follows = std::move(mergedFollows);
    layers_.push_back(std::move(layer));
    recomputeBounds();
    update();
    return layer.id;
}

/**
 * @brief Define si la capa se dibuja rellena o sólo con contorno.
 *
 * @param id Identificador de la capa.
 * @param filled `true` para rellenar el cobre, `false` para dibujar el borde.
 * @note Sólo afecta al render; no cambia la geometría ni la exportación SVG,
 *       que siempre dibuja relleno.
 */
void Canvas::setLayerFilled(int id, bool filled) {
    Layer* layer = findLayer(id);
    if (!layer) return;
    layer->filled = filled;
    update();
}

/**
 * @brief Consulta el modo de relleno de una capa.
 *
 * @param id Identificador de la capa.
 * @return `true` si se dibuja rellena; `true` también si la capa no existe
 *         (valor por defecto del visor).
 */
bool Canvas::layerFilled(int id) const {
    const Layer* layer = findLayer(id);
    return layer ? layer->filled : true;
}

/**
 * @brief Consulta si la capa usa coloreado por primitiva.
 *
 * @param id Identificador de la capa.
 * @return `true` en modo multicolor, `false` si la capa no existe.
 */
bool Canvas::layerMulticolor(int id) const {
    const Layer* layer = findLayer(id);
    return layer ? layer->multicolor : false;
}

/**
 * @brief Activa o desactiva el coloreado por primitiva de una capa.
 *
 * En modo multicolor cada sólido recibe un tono derivado de su índice
 * (`i * 47 % 360`), lo que ayuda a distinguir aperturas y regiones contiguas.
 *
 * @param id Identificador de la capa.
 * @param multicolor Estado deseado.
 */
void Canvas::setLayerMulticolor(int id, bool multicolor) {
    Layer* layer = findLayer(id);
    if (!layer) return;
    layer->multicolor = multicolor;
    update();
}

/// @brief Define el ancho de línea de una capa en milímetros.
void Canvas::setLayerWidth(int id, double width) {
    Layer* layer = findLayer(id);
    if (!layer) return;
    layer->width = std::max(0.0, width);
    update();
}

/// @return Ancho de línea de una capa en milímetros.
double Canvas::layerWidth(int id) const {
    const Layer* layer = findLayer(id);
    return layer ? layer->width : 0.0;
}

/**
 * @brief Aplica una transformación afín a la geometría de una capa.
 *
 * Recorre sólidos y recorridos y transforma cada vértice con
 * `fc::Geom::transformXY`, usando el `QTransform` recibido como función de
 * mapeo; después reconstruye las rutas Qt y repinta. Al trabajar sobre GEOS, la
 * transformación también queda reflejada en las exportaciones.
 *
 * @param id Capa a transformar.
 * @param transform Mover/rotar/escalar/sesgar en coordenadas de mundo.
 * @note Recalcula límites y `hasData_`; el ajuste de vista y `exportSvg()`
 *       reflejan la transformación de inmediato.
 */
void Canvas::transformLayer(int id, const QTransform& transform) {
    Layer* layer = findLayer(id);
    if (!layer) return;
    /// Mapea cada vértice de cada geometría con la transformación dada.
    auto apply = [&transform](const std::vector<fc::Geom>& input) {
        std::vector<fc::Geom> output;
        output.reserve(input.size());
        for (const fc::Geom& g : input) {
            output.push_back(g.transformXY([&transform](fc::Pt p) {
                const QPointF q = transform.map(QPointF(p.x, p.y));
                return fc::Pt{q.x(), q.y()};
            }));
        }
        return output;
    };
    layer->solids = apply(layer->solids);
    layer->follows = apply(layer->follows);
    layer->solid = buildPath(layer->solids);
    layer->follow = buildLinePath(layer->follows);
    recomputeBounds();
    update();
}

/**
 * @brief Exporta las capas visibles a un SVG (film de fabricación).
 *
 * Calcula un alto en píxeles proporcional a la caja (ancho fijo de 1400 px),
 * define el `viewBox` en unidades de mundo y aplica una matriz que invierte el
 * eje Y (`[1 0 0 -1 0 ymin+ymax]`) para que el SVG, cuyo origen está arriba,
 * coincida con el mundo CAM, cuyo origen está abajo. Dibuja el cobre relleno y
 * los recorridos como líneas.
 *
 * @param path Ruta de destino del archivo `.svg`.
 * @return `true` si se escribió; `false` si no hay datos o si
 *         `QPainter::begin()` no pudo abrir el generador (ruta inválida, sin
 *         permisos, etc.).
 * @warning Usa colores fijos (cobre `200,149,46`, recorridos `74,144,217`) e
 *          ignora `setColors`. No incluye rejilla, ejes, área de trabajo,
 *          trayectorias ni HUD, y no comprueba el resultado final de la
 *          escritura.
 */
bool Canvas::exportSvg(const QString& path) const {
    if (!hasData_) return false;

    const double w = std::max(1e-6, bounds_.width());
    const double h = std::max(1e-6, bounds_.height());
    const int pixelWidth = 1400;
    const int pixelHeight = std::max(1, static_cast<int>(std::lround(pixelWidth * h / w)));

    QSvgGenerator generator;
    generator.setFileName(path);
    generator.setSize(QSize(pixelWidth, pixelHeight));
    generator.setViewBox(QRectF(bounds_.xmin, bounds_.ymin, w, h));
    generator.setTitle(QStringLiteral("FlatCAM C++"));
    generator.setDescription(QStringLiteral("Film export"));

    QPainter painter;
    if (!painter.begin(&generator)) return false;
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setTransform(QTransform(1, 0, 0, -1, 0, bounds_.ymin + bounds_.ymax));

    for (const Layer& layer : layers_) {
        if (!layer.visible) continue;
        painter.setBrush(QColor(200, 149, 46));
        painter.setPen(QPen(QColor(240, 196, 106), 0.0));
        painter.drawPath(layer.solid);

        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(74, 144, 217), 0.0));
        painter.drawPath(layer.follow);
    }
    painter.end();
    return true;
}

/**
 * @brief Vacía el visor por completo.
 *
 * Elimina todas las capas, la previsualización de trayectorias, los límites y
 * la bandera de datos, y repinta el mensaje de bienvenida.
 *
 * @note Conserva escala, desplazamiento, colores y preferencias de rejilla/HUD.
 */
void Canvas::clearData() {
    layers_.clear();
    toolpathPath_ = QPainterPath();
    bounds_ = fc::BBox();
    hasData_ = false;
    update();
}

/**
 * @brief Ajusta escala y desplazamiento para encuadrar todo el contenido.
 *
 * Reserva un margen fijo de 40 px, calcula la escala como el menor factor que
 * cabe en ancho y alto, descarta valores no finitos o no positivos (caja
 * degenerada o widget diminuto, que caen a escala 1.0) y centra la caja en el
 * widget invirtiendo el eje Y.
 *
 * @note No hace nada si `hasData_` es falso. No repinta: el llamador decide
 *       cuándo actualizar.
 */
void Canvas::fit() {
    if (!hasData_) return;
    const double w = std::max(1, width());
    const double h = std::max(1, height());
    const double bw = std::max(bounds_.width(), 1e-6);
    const double bh = std::max(bounds_.height(), 1e-6);
    scale_ = std::min((w - 40.0) / bw, (h - 40.0) / bh);
    if (!std::isfinite(scale_) || scale_ <= 0.0) scale_ = 1.0;
    const double cx = (bounds_.xmin + bounds_.xmax) / 2.0;
    const double cy = (bounds_.ymin + bounds_.ymax) / 2.0;
    offset_ = QPointF(w / 2.0 - cx * scale_, h / 2.0 + cy * scale_);
}

/// @brief Ajusta la vista incluyendo el origen de máquina cuando está cerca.
void Canvas::fitWithOrigin() {
    if (!hasData_) return;
    fc::BBox view = bounds_;
    const double diag = std::hypot(bounds_.width(), bounds_.height());
    const double nx = std::clamp(0.0, bounds_.xmin, bounds_.xmax);
    const double ny = std::clamp(0.0, bounds_.ymin, bounds_.ymax);
    const double distance = std::hypot(nx, ny);
    const bool inside = 0.0 >= bounds_.xmin && 0.0 <= bounds_.xmax &&
                        0.0 >= bounds_.ymin && 0.0 <= bounds_.ymax;
    if (!inside && distance <= std::max(10.0, diag * 6.0)) {
        view.xmin = std::min(view.xmin, 0.0);
        view.ymin = std::min(view.ymin, 0.0);
        view.xmax = std::max(view.xmax, 0.0);
        view.ymax = std::max(view.ymax, 0.0);
    }
    const double pad = std::max(0.5, diag * 0.04);
    view = view.expanded(pad);
    const double w = std::max(1, width());
    const double h = std::max(1, height());
    scale_ = std::min((w - 40.0) / std::max(view.width(), 1e-6),
                      (h - 40.0) / std::max(view.height(), 1e-6));
    if (!std::isfinite(scale_) || scale_ <= 0.0) scale_ = 1.0;
    const double cx = (view.xmin + view.xmax) / 2.0;
    const double cy = (view.ymin + view.ymax) / 2.0;
    offset_ = QPointF(w / 2.0 - cx * scale_, h / 2.0 + cy * scale_);
    update();
}

/**
 * @brief Configura la paleta del visor.
 *
 * Guarda el color de fondo y el de cobre; el borde del cobre se deriva
 * automáticamente aclarando un 30 % el relleno para que el contorno se
 * distinga tanto en tema claro como oscuro.
 *
 * @param background Fondo del lienzo.
 * @param copper Relleno del cobre.
 * @param follow Color de los recorridos.
 * @param toolpath Color de la previsualización CAM.
 */
void Canvas::setColors(const QColor& background, const QColor& copper, const QColor& follow,
                       const QColor& toolpath) {
    background_ = background;
    copperFill_ = copper;
    copperEdge_ = copper.lighter(130);
    followColor_ = follow;
    toolpathColor_ = toolpath;
    update();
}

/**
 * @brief Reúne el cobre de todas las capas visibles.
 *
 * @return Vector con los sólidos concatenados en orden de dibujo. Lo usan las
 *         herramientas (aislamiento, NCC, corte) cuando trabajan sobre "lo
 *         visible" en lugar de un objeto concreto.
 * @note El resultado puede contener geometrías solapadas: si luego se usa para
 *       operar, conviene unirlas. Las capas ocultas no aportan nada.
 */
std::vector<fc::Geom> Canvas::visibleSolids() const {
    std::vector<fc::Geom> out;
    for (const Layer& layer : layers_) {
        if (!layer.visible) continue;
        out.insert(out.end(), layer.solids.begin(), layer.solids.end());
    }
    return out;
}

/**
 * @brief Restaura escala y desplazamiento de la vista.
 *
 * @param scale Factor de píxeles por unidad de mundo; se ignora si es menor o
 *        igual a cero.
 * @param offset Traslación en píxeles hacia el origen de pantalla.
 * @note Útil para guardar y recuperar marcadores de vista.
 */
void Canvas::setView(double scale, const QPointF& offset) {
    if (scale <= 0.0) return;
    scale_ = scale;
    offset_ = offset;
    update();
}

/**
 * @brief Centra la vista en un punto de mundo.
 *
 * @param x Coordenada X del centro deseado.
 * @param y Coordenada Y del centro deseado.
 * @note Mantiene la escala actual e invierte el eje Y al convertir a píxeles.
 */
void Canvas::centerOn(double x, double y) {
    offset_ = QPointF(width() / 2.0 - x * scale_, height() / 2.0 + y * scale_);
    update();
}

/**
 * @brief Muestra u oculta la rejilla.
 *
 * @param visible Estado deseado; persiste entre cargas de archivos.
 */
void Canvas::setGridVisible(bool visible) {
    gridVisible_ = visible;
    update();
}

/**
 * @brief Ajusta la vista a una caja concreta (zoom a objeto).
 *
 * Sustituye los límites usados por fit/exportSvg y marca que hay datos, aunque
 * la caja corresponda a una sola capa; luego ajusta y repinta.
 *
 * @param bounds Caja a encuadrar; si es inválida la llamada no hace nada.
 * @warning Sobrescribe `bounds_`: tras usarla, un ajuste general (fit) o una
 *          exportación SVG se limitan a esa caja y no a todo el contenido.
 */
void Canvas::fitTo(const fc::BBox& bounds) {
    if (!bounds.valid) return;
    bounds_ = bounds;
    hasData_ = true;
    fit();
    update();
}

/**
 * @brief Reajusta la vista cuando cambia el tamaño del widget.
 *
 * Mantiene el contenido encuadrado tras redimensionar la ventana; no hace nada
 * si aún no hay datos. El evento de pintado posterior se encarga del repintado.
 *
 * @param event Evento de redimensión (no se usa).
 */
void Canvas::setPointPickMode(bool on) {
    pointPickMode_ = on;
    setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
    if (!on) markers_.clear();
    update();
}

/// @brief Reemplaza las marcas temporales de puentes manuales.
void Canvas::setMarkers(const std::vector<fc::Pt>& markers) {
    markers_ = markers;
    update();
}

void Canvas::resizeEvent(QResizeEvent*) {
    if (hasData_) fit();
}

/**
 * @brief Dibuja el lienzo completo.
 *
 * Orden de pintado: fondo, mensaje de bienvenida si no hay datos, rejilla
 * adaptativa (paso automático que mantiene entre 40 y 200 px por celda),
 * ejes en `displayOrigin_`, área de trabajo punteada, capas de abajo hacia
 * arriba (multicolor por primitiva o cobre uniforme más recorridos),
 * trayectorias CAM y, ya en coordenadas de dispositivo, el HUD con la posición
 * del cursor en mm.
 *
 * @param event Evento de pintado (no se usa; se repinta todo).
 * @note La transformación de mundo es `[scale 0 0 -scale ox oy]`, es decir, Y
 *       crece hacia arriba como en CAM. En modo multicolor se reconstruye la
 *       ruta de cada sólido en cada repintado: es correcto pero costoso con
 *       miles de primitivas.
 * @warning No hay recorte por región visible (sólo el que aplica Qt); con zoom
 *          extremo el coste de dibujar todas las rutas sigue siendo el mismo.
 */
void Canvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), background_);
    if (!hasData_) {
        p.setPen(QColor(120, 130, 140));
        p.drawText(rect(), Qt::AlignCenter, tr("Abra un archivo Gerber, Excellon o proyecto"));
        return;
    }

    QTransform t(scale_, 0.0, 0.0, -scale_, offset_.x(), offset_.y());
    p.setTransform(t);

    const fc::BBox view{(0.0 - offset_.x()) / scale_,
                        (offset_.y() - height()) / scale_,
                        (width() - offset_.x()) / scale_,
                        offset_.y() / scale_};
    double step = gridStep_ > 0.0 ? gridStep_ : 10.0;
    if (gridStep_ <= 0.0) {
        while (step * scale_ < 40.0) step *= 10.0;
        while (step * scale_ > 200.0) step /= 10.0;
    }
    if (gridVisible_) {
        p.setPen(QPen(gridColor_, 0.0));
        const double x0 = std::floor(view.xmin / step) * step;
        const double y0 = std::floor(view.ymin / step) * step;
        for (double x = x0; x <= view.xmax + step; x += step)
            p.drawLine(QPointF(x, view.ymin - step), QPointF(x, view.ymax + step));
        for (double y = y0; y <= view.ymax + step; y += step)
            p.drawLine(QPointF(view.xmin - step, y), QPointF(view.xmax + step, y));
    }

    // Ejes de máquina X=0 / Y=0 resaltados. Son la referencia desde la que
    // arranca el cabezal, distinta del origen de visualización configurable.
    const bool darkCanvas = background_.lightness() < 128;
    const QColor axisX = darkCanvas ? QColor(226, 108, 117, 230) : QColor(190, 40, 55, 230);
    const QColor axisY = darkCanvas ? QColor(96, 200, 136, 230) : QColor(20, 130, 70, 230);
    const double axisWidth = 2.0 / std::max(1.0, scale_);
    if (view.ymin <= 0.0 && view.ymax >= 0.0) {
        p.setPen(QPen(axisX, axisWidth));
        p.drawLine(QPointF(view.xmin, 0.0), QPointF(view.xmax, 0.0));
    }
    if (view.xmin <= 0.0 && view.xmax >= 0.0) {
        p.setPen(QPen(axisY, axisWidth));
        p.drawLine(QPointF(0.0, view.ymin), QPointF(0.0, view.ymax));
    }

    if (std::abs(displayOrigin_.x) > 1e-9 || std::abs(displayOrigin_.y) > 1e-9) {
        p.setPen(QPen(QColor(120, 130, 140, 150), 0.0, Qt::DashLine));
        p.drawLine(QPointF(bounds_.xmin, displayOrigin_.y),
                   QPointF(bounds_.xmax, displayOrigin_.y));
        p.drawLine(QPointF(displayOrigin_.x, bounds_.ymin),
                   QPointF(displayOrigin_.x, bounds_.ymax));
    }

    if (workspace_.valid) {
        QPen pen(QColor(120, 90, 200), 0.0, Qt::DashLine);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(workspace_.xmin, workspace_.ymin, workspace_.width(),
                          workspace_.height()));
    }

    for (const Layer& layer : layers_) {
        if (!layer.visible) continue;
        if (layer.multicolor) {
            for (std::size_t i = 0; i < layer.solids.size(); ++i) {
                QColor color = QColor::fromHsv(static_cast<int>((i * 47) % 360), 180, 220);
                color.setAlpha(190);
                p.setBrush(layer.filled ? QBrush(color) : Qt::NoBrush);
                p.setPen(QPen(color.lighter(125), 0.0));
                p.drawPath(buildPath({layer.solids[i]}));
            }
        } else {
            p.setBrush(layer.filled ? QBrush(copperFill_) : Qt::NoBrush);
            p.setPen(QPen(copperEdge_, 0.0));
            p.drawPath(layer.solid);
        }

        p.setBrush(Qt::NoBrush);
        QPen followPen(followColor_, layer.width);
        followPen.setCapStyle(Qt::RoundCap);
        followPen.setJoinStyle(Qt::RoundJoin);
        p.setPen(followPen);
        p.drawPath(layer.follow);
    }

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(toolpathColor_, 0.0));
    p.drawPath(toolpathPath_);

    p.setTransform(QTransform());
    p.setRenderHint(QPainter::TextAntialiasing, true);
    QFont small = p.font();
    small.setPointSizeF(9.0);
    p.setFont(small);
    const auto toScreen = [this](double x, double y) {
        return QPointF(x * scale_ + offset_.x(), -y * scale_ + offset_.y());
    };
    const QRect viewport = rect();
    const auto inside = [&viewport](const QPointF& p) {
        return viewport.adjusted(-2, -2, 2, 2).contains(p.toPoint());
    };
    if (!markers_.empty()) {
        const QColor markerColor = darkCanvas ? QColor(255, 190, 90, 240)
                                              : QColor(170, 95, 15, 240);
        p.setPen(QPen(markerColor, 1.5));
        p.setBrush(Qt::NoBrush);
        for (const fc::Pt& marker : markers_) {
            const QPointF s = toScreen(marker.x, marker.y);
            if (!inside(s)) continue;
            p.drawEllipse(s, 4.0, 4.0);
            p.drawLine(s + QPointF(-7, 0), s + QPointF(7, 0));
            p.drawLine(s + QPointF(0, -7), s + QPointF(0, 7));
        }
    }

    // Rótulos de los ejes y marcas de coordenadas.
    const QPointF originScreen = toScreen(0.0, 0.0);
    const QColor originColor = darkCanvas ? QColor(235, 238, 242, 230)
                                         : QColor(20, 24, 30, 230);
    if (inside(originScreen)) {
        p.setPen(QPen(originColor, 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(originScreen, 5.0, 5.0);
        p.drawLine(originScreen + QPointF(-8, 0), originScreen + QPointF(8, 0));
        p.drawLine(originScreen + QPointF(0, -8), originScreen + QPointF(0, 8));
        p.setPen(originColor);
        p.drawText(originScreen + QPointF(8, -7), QStringLiteral("0,0"));
    }
    if (inside(toScreen(0.0, 0.0))) {
        p.setPen(axisX);
        p.drawText(viewport.adjusted(8, 0, -8, -8), Qt::AlignRight | Qt::AlignBottom,
                   QStringLiteral("Y=0"));
        p.setPen(axisY);
        p.drawText(viewport.adjusted(8, 8, -8, -8), Qt::AlignLeft | Qt::AlignTop,
                   QStringLiteral("X=0"));
    }
    if (step * scale_ >= 28.0) {
        const QColor tickColor = darkCanvas ? QColor(160, 168, 178, 190)
                                            : QColor(85, 95, 105, 200);
        p.setPen(tickColor);
        const double x0 = std::floor(view.xmin / step) * step;
        for (double x = x0; x <= view.xmax + step && view.ymin <= 0.0 && view.ymax >= 0.0;
             x += step) {
            if (std::abs(x) < step * 0.01) continue;
            const QPointF s = toScreen(x, 0.0);
            if (!inside(s)) continue;
            p.drawLine(s + QPointF(0, -3), s + QPointF(0, 3));
            p.drawText(s + QPointF(3, -4), QString::number(x, 'f', step < 1.0 ? 2 : 0));
        }
        const double y0 = std::floor(view.ymin / step) * step;
        for (double y = y0; y <= view.ymax + step && view.xmin <= 0.0 && view.xmax >= 0.0;
             y += step) {
            if (std::abs(y) < step * 0.01) continue;
            const QPointF s = toScreen(0.0, y);
            if (!inside(s)) continue;
            p.drawLine(s + QPointF(-3, 0), s + QPointF(3, 0));
            p.drawText(s + QPointF(5, 3), QString::number(y, 'f', step < 1.0 ? 2 : 0));
        }
    }

    // Recorrido desde el origen de máquina hasta la PCB y medidas del área.
    if (bounds_.valid) {
        const double nx = std::clamp(0.0, bounds_.xmin, bounds_.xmax);
        const double ny = std::clamp(0.0, bounds_.ymin, bounds_.ymax);
        const QPointF target = toScreen(nx, ny);
        const bool outside = 0.0 < bounds_.xmin || 0.0 > bounds_.xmax ||
                             0.0 < bounds_.ymin || 0.0 > bounds_.ymax;
        if (outside && inside(originScreen) && inside(target)) {
            const QColor travelColor = darkCanvas ? QColor(255, 190, 90, 230)
                                                  : QColor(170, 95, 15, 230);
            p.setPen(QPen(travelColor, 1.4, Qt::DashLine));
            p.drawLine(originScreen, target);
            const double angle = std::atan2(target.y() - originScreen.y(),
                                            target.x() - originScreen.x());
            const double len = 9.0;
            const QPointF a = target - QPointF(std::cos(angle - 0.45) * len,
                                               std::sin(angle - 0.45) * len);
            const QPointF b = target - QPointF(std::cos(angle + 0.45) * len,
                                               std::sin(angle + 0.45) * len);
            p.drawLine(target, a);
            p.drawLine(target, b);
            p.setPen(darkCanvas ? QColor(255, 210, 140) : QColor(145, 75, 10));
            p.drawText((originScreen + target) / 2.0 + QPointF(6, -6),
                       QString("ΔX %1  ΔY %2")
                           .arg(nx, 0, 'f', 2)
                           .arg(ny, 0, 'f', 2));
        }

        const QString l1 = QString("PCB  X %1 … %2   (Δ %3)")
                               .arg(bounds_.xmin, 0, 'f', 2)
                               .arg(bounds_.xmax, 0, 'f', 2)
                               .arg(bounds_.width(), 0, 'f', 2);
        const QString l2 = QString("     Y %1 … %2   (Δ %3)")
                               .arg(bounds_.ymin, 0, 'f', 2)
                               .arg(bounds_.ymax, 0, 'f', 2)
                               .arg(bounds_.height(), 0, 'f', 2);
        const QString l3 = outside
                               ? QString("Origen 0,0 → ΔX %1  ΔY %2")
                                     .arg(nx, 0, 'f', 2)
                                     .arg(ny, 0, 'f', 2)
                               : QStringLiteral("Origen 0,0 dentro de la PCB");
        QFontMetrics metrics(small);
        const int width = std::max({metrics.horizontalAdvance(l1),
                                    metrics.horizontalAdvance(l2),
                                    metrics.horizontalAdvance(l3)}) + 18;
        const QRect box(10, 10, width, 62);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(8, 12, 16, 190));
        p.drawRoundedRect(box, 6, 6);
        p.setPen(QColor(214, 222, 232));
        p.drawText(box.adjusted(9, 4, -6, -4), Qt::AlignLeft | Qt::AlignTop, l1);
        p.drawText(box.adjusted(9, 20, -6, -4), Qt::AlignLeft | Qt::AlignTop, l2);
        p.setPen(QColor(255, 204, 128));
        p.drawText(box.adjusted(9, 40, -6, -4), Qt::AlignLeft | Qt::AlignTop, l3);
    }

    if (hudVisible_ && hasCursor_) {
        const QString text = QString("X %1  Y %2")
                                 .arg(cursorWorld_.x(), 0, 'f', 3)
                                 .arg(cursorWorld_.y(), 0, 'f', 3);
        const QRect box(10, 80, 180, 26);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 170));
        p.drawRoundedRect(box, 5, 5);
        p.setPen(QColor(220, 230, 240));
        p.drawText(box.adjusted(8, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
    }
}

/**
 * @brief Reemplaza la previsualización de trayectorias CAM.
 *
 * @param paths Recorridos a dibujar en color `toolpathColor_` sobre las capas;
 *        un vector vacío borra la previsualización.
 * @note Es una capa aparte, no participa de la selección ni de la exportación,
 *       y se pierde al recargar geometría con setGeometryData.
 */
void Canvas::setToolpaths(const std::vector<fc::Geom>& paths) {
    toolpathPath_ = buildLinePath(paths);
    update();
}

/**
 * @brief Aplica zoom manteniendo fijo un punto de pantalla.
 *
 * Convierte el ancla a mundo, multiplica la escala y recalcula el
 * desplazamiento para que ese punto de mundo vuelva a proyectarse sobre el
 * mismo píxel. La escala se limita inferiormente a 1e-9 para evitar divisiones
 * por cero; no hay límite superior.
 *
 * @param anchor Punto de anclaje en píxeles del widget.
 * @param factor Multiplicador (>1 acerca, <1 aleja).
 */
void Canvas::zoomAt(const QPointF& anchor, double factor) {
    const double wx = (anchor.x() - offset_.x()) / scale_;
    const double wy = -(anchor.y() - offset_.y()) / scale_;
    scale_ *= factor;
    if (scale_ < 1e-9) scale_ = 1e-9;
    offset_ = QPointF(anchor.x() - wx * scale_, anchor.y() + wy * scale_);
    update();
}

/**
 * @brief Acerca la vista un 25 % respecto del centro del widget.
 */
void Canvas::zoomIn() {
    zoomAt(QPointF(width() / 2.0, height() / 2.0), 1.25);
}

/**
 * @brief Aleja la vista un 20 % respecto del centro del widget.
 */
void Canvas::zoomOut() {
    zoomAt(QPointF(width() / 2.0, height() / 2.0), 0.8);
}

/**
 * @brief Traduce la rueda del ratón en zoom anclado al cursor.
 *
 * Usa un factor fijo de 1.15 (o su inverso) según el signo de
 * `angleDelta().y()`, sin escalar por la magnitud del giro, de modo que el zoom
 * sea predecible con trackpads y ruedas de distintos pasos.
 *
 * @param event Evento de rueda; su posición se usa como ancla.
 * @note Se ignora el desplazamiento horizontal y `pixelDelta()`; un delta Y
 *       nulo (giro puramente horizontal) se interpreta como alejar.
 */
void Canvas::wheelEvent(QWheelEvent* event) {
    const double factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    zoomAt(event->position(), factor);
}

/**
 * @brief Inicia el desplazamiento (pan) con el botón izquierdo.
 *
 * Guarda la posición de presión y la última posición, borra la marca de
 * arrastre y cambia el cursor a mano cerrada.
 *
 * @param event Evento de presión; los botones distintos del izquierdo se
 *        ignoran (no hay menú contextual ni pan con el botón central).
 */
void Canvas::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && pointPickMode_) {
        panning_ = false;
        return;
    }
    if (event->button() == Qt::LeftButton) {
        panning_ = true;
        pressPos_ = event->pos();
        dragged_ = false;
        lastMouse_ = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

/**
 * @brief Actualiza coordenadas del cursor y ejecuta el pan.
 *
 * Siempre convierte la posición a mundo, refresca el HUD, emite
 * `cursorMoved(x, y)` y, si se está arrastrando, suma el delta a `offset_`.
 * El arrastre se marca como tal cuando el cursor se aleja más de 3 px
 * (manhattan) del punto de presión, para no confundir un clic con un pan.
 *
 * @param event Evento de movimiento del ratón.
 * @note `position()` (QPointF) se usa para el mundo y `pos()` (QPoint) para el
 *       delta: mezcla de tipos deliberada para mantener el pan en píxeles
 *       enteros.
 */
void Canvas::mouseMoveEvent(QMouseEvent* event) {
    const double wx = (event->position().x() - offset_.x()) / scale_;
    const double wy = -(event->position().y() - offset_.y()) / scale_;
    cursorWorld_ = QPointF(wx, wy);
    hasCursor_ = true;
    if (hudVisible_) update();
    emit cursorMoved(wx, wy);

    if (!panning_) return;
    const QPoint delta = event->pos() - lastMouse_;
    if ((event->pos() - pressPos_).manhattanLength() > 3) dragged_ = true;
    lastMouse_ = event->pos();
    offset_ += QPointF(delta.x(), delta.y());
    update();
}

/**
 * @brief Termina el pan y resuelve la selección de capa.
 *
 * Si hubo arrastre, sólo cierra el pan. Si fue un clic, convierte la posición a
 * mundo y recorre las capas de arriba abajo buscando la primera visible que
 * contenga el punto, ya sea en el relleno (`QPainterPath::contains`) o a menos
 * de unos 3 px del recorrido (se engrosa la ruta con `QPainterPathStroker` a
 * 6 px de ancho y se comprueba la contención del trazo). Emite
 * `layerPicked(id)` o `layerPicked(-1)` si no hay impacto.
 *
 * @param event Evento de liberación; se ignora si no es el botón izquierdo.
 * @note La tolerancia se expresa en mundo (`6/scale_`) con un mínimo de 2
 *       unidades, así que se mantiene constante en píxeles al hacer zoom.
 */
void Canvas::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    if (pointPickMode_) {
        const QPointF world((event->position().x() - offset_.x()) / scale_,
                            -(event->position().y() - offset_.y()) / scale_);
        emit worldPicked(world.x(), world.y());
        return;
    }
    panning_ = false;
    setCursor(Qt::ArrowCursor);
    if (dragged_) return;
    const QPointF world((event->position().x() - offset_.x()) / scale_,
                        -(event->position().y() - offset_.y()) / scale_);
    for (auto it = layers_.rbegin(); it != layers_.rend(); ++it) {
        if (!it->visible) continue;
        if (it->solid.contains(world)) {
            emit layerPicked(it->id);
            return;
        }
        if (!it->follow.isEmpty()) {
            QPainterPathStroker stroker;
            stroker.setWidth(std::max(2.0, 6.0 / scale_));
            if (stroker.createStroke(it->follow).contains(world)) {
                emit layerPicked(it->id);
                return;
            }
        }
    }
    emit layerPicked(-1);
}

/**
 * @brief Resuelve el doble clic como activación de capa.
 *
 * Repite la prueba de impacto del clic simple (relleno o recorrido engrosado)
 * y emite `layerActivated(id)` para la primera capa visible alcanzada; el
 * llamador suele hacer zoom o abrir el editor del objeto.
 *
 * @param event Evento de doble clic; se ignora si no es el botón izquierdo.
 * @note A diferencia de mouseReleaseEvent, no emite ningún valor cuando no hay
 *       impacto (no existe un "ninguno activado"), así que el llamador no puede
 *       distinguir "sin objeto" de "evento no procesado".
 */
void Canvas::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    const QPointF world((event->position().x() - offset_.x()) / scale_,
                        -(event->position().y() - offset_.y()) / scale_);
    for (auto it = layers_.rbegin(); it != layers_.rend(); ++it) {
        if (!it->visible) continue;
        if (it->solid.contains(world)) {
            emit layerActivated(it->id);
            return;
        }
        if (!it->follow.isEmpty()) {
            QPainterPathStroker stroker;
            stroker.setWidth(std::max(2.0, 6.0 / scale_));
            if (stroker.createStroke(it->follow).contains(world)) {
                emit layerActivated(it->id);
                return;
            }
        }
    }
    emit layerActivated(-1);
}

/**
 * @brief Notifica que el cursor salió del widget.
 *
 * Apaga la bandera de cursor, refresca el HUD y emite `cursorMoved(NaN, NaN)`,
 * que es la señal convenida con la barra de estado para borrar las coordenadas
 * en lugar de congelar el último valor.
 *
 * @param event Evento de salida (no se usa).
 */
void Canvas::leaveEvent(QEvent*) {
    hasCursor_ = false;
    if (hudVisible_) update();
    emit cursorMoved(std::numeric_limits<double>::quiet_NaN(),
                     std::numeric_limits<double>::quiet_NaN());
}
