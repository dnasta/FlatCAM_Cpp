/**
 * @file geometry.cpp
 * @brief Núcleo geométrico: envoltorio RAII de GEOS y operaciones de uso CAM.
 *
 * Implementa fc::Geom (shared_ptr sobre GEOSGeometry, con fábricas de puntos,
 * líneas, polígonos, cajas, círculos, obrounds y polígonos regulares), las
 * operaciones booleanas y de buffer, las transformaciones afines y las
 * consultas de área, longitud, límites y contornos. Todo delega en GEOS, cuyo
 * runtime se inicializa de forma perezosa y segura (initGeosRuntime): la API
 * clásica de GEOS no es reentrante y aborta si no se llamó antes a initGEOS.
 */
#include "flatcam/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace fc {
namespace {

/**
 * @brief Callbacks vacíos que silencian avisos y errores de GEOS.
 *
 * GEOS escribe en stderr si no se le pasan handlers; la aplicación prefiere no
 * ensuciar la salida, así que ambos se descartan (cubren geosNotice y
 * geosError).
 */
void geosNotice(const char*, ...) {}
void geosError(const char*, ...) {}


/**
 * @brief Crea un polígono vacío y, si GEOS no lo permite, una colección vacía.
 * @return Geometría nueva (aún sin adoptar).
 */
GEOSGeometry* createEmpty() {
    GEOSGeometry* g = GEOSGeom_createEmptyPolygon();
    if (!g) g = GEOSGeom_createEmptyCollection(GEOS_GEOMETRYCOLLECTION);
    return g;
}

/**
 * @brief Copia la secuencia de coordenadas de un anillo GEOS a un Ring.
 * @param ring Anillo o secuencia GEOS de origen.
 * @return Vértices en orden; vector vacío si no hay secuencia de coordenadas
 *         o si GEOS no puede informar su tamaño.
 * @note El último punto puede repetir el primero: GEOS cierra los anillos y
 *       esta función no elimina ese duplicado.
 */
Ring readRing(const GEOSGeometry* ring) {
    Ring out;
    const GEOSCoordSequence* cs = GEOSGeom_getCoordSeq(ring);
    if (!cs) return out;
    unsigned int n = 0;
    if (!GEOSCoordSeq_getSize(cs, &n)) return out;
    out.reserve(n);
    for (unsigned int i = 0; i < n; ++i) {
        double x = 0.0;
        double y = 0.0;
        if (GEOSCoordSeq_getXY(cs, i, &x, &y)) out.push_back({x, y});
    }
    return out;
}

/**
 * @brief Construye un LinearRing GEOS cerrado a partir de puntos sueltos.
 *
 * Copia los puntos y agrega el primero al final si el anillo no venía cerrado.
 * La propiedad de la secuencia y del anillo pasa al llamador.
 * @param pts Vértices del contorno (sin repetir el primero).
 * @return Anillo nuevo (debe liberarse con GEOSGeom_destroy).
 * @throw std::invalid_argument si hay menos de 3 puntos.
 * @throw std::runtime_error si GEOS falla al crear la secuencia o el anillo.
 */
GEOSGeometry* makeRing(const Ring& pts) {
    if (pts.size() < 3) throw std::invalid_argument("ring needs at least 3 points");
    Ring closed = pts;
    if (closed.front().x != closed.back().x || closed.front().y != closed.back().y) {
        closed.push_back(closed.front());
    }
    GEOSCoordSequence* seq = GEOSCoordSeq_create(static_cast<unsigned int>(closed.size()), 2);
    if (!seq) throw std::runtime_error("GEOSCoordSeq_create failed");
    for (std::size_t i = 0; i < closed.size(); ++i) {
        if (!GEOSCoordSeq_setXY(seq, static_cast<unsigned int>(i), closed[i].x, closed[i].y)) {
            GEOSCoordSeq_destroy(seq);
            throw std::runtime_error("GEOSCoordSeq_setXY failed");
        }
    }
    GEOSGeometry* ring = GEOSGeom_createLinearRing(seq);
    if (!ring) {
        GEOSCoordSeq_destroy(seq);
        throw std::runtime_error("GEOSGeom_createLinearRing failed");
    }
    return ring;
}

/// Empaqueta el functor de transformación para el callback C de GEOS.
struct TransformData {
    const std::function<Pt(Pt)>* fn;
};

/**
 * @brief Callback de GEOSGeom_transformXY: aplica el functor punto a punto.
 *
 * Modifica las coordenadas in situ y devuelve 1 para que GEOS continúe.
 * @param x Coordenada X de entrada y salida.
 * @param y Coordenada Y de entrada y salida.
 * @param userdata Puntero a TransformData con el std::function a aplicar.
 * @return Siempre 1; GEOS interpreta otro valor como aborto de la operación.
 */
int transformCallback(double* x, double* y, void* userdata) {
    auto* data = static_cast<TransformData*>(userdata);
    const Pt p = (*data->fn)({*x, *y});
    *x = p.x;
    *y = p.y;
    return 1;
}

/**
 * @brief Adopta el resultado de una operación GEOS o falla.
 * @param g Geometría devuelta por GEOS (puede ser nullptr).
 * @return Geom que toma la propiedad del puntero.
 * @throw std::runtime_error si @p g es nullptr, es decir si la operación falló.
 */
Geom require(GEOSGeometry* g) {
    if (!g) throw std::runtime_error("GEOS operation failed");
    return Geom::adopt(g);
}

}  // namespace

// ---------------------------------------------------------------- construction

/**
 * @brief Inicializa GEOS una única vez por proceso.
 *
 * Usa un inicializador estático local (thread-safe desde C++11) para llamar a
 * initGEOS con handlers silenciosos. Es obligatorio antes de cualquier otra
 * función de GEOS, incluidas las fábricas de geometrías vacías.
 */
void initGeosRuntime() {
    static const bool initialized = [] {
        initGEOS(geosNotice, geosError);
        return true;
    }();
    (void)initialized;
}

/**
 * @brief Toma la propiedad de un puntero GEOS crudo.
 * @param g Geometría a adoptar; si es nullptr devuelve un Geom nulo.
 * @return Geom que libera @p g con GEOSGeom_destroy.
 */
Geom Geom::adopt(GEOSGeometry* g) {
    initGeosRuntime();
    if (!g) return Geom();
    return Geom(Handle(g, GEOSGeom_destroy));
}

/**
 * @brief Devuelve un polígono vacío válido para acumular uniones.
 * @note Se usa como valor inicial de reducciones (por ejemplo en la
 *       construcción de macros) para no tratar aparte el caso "sin geometría".
 */
Geom Geom::emptyPolygon() {
    initGeosRuntime();
    return adopt(createEmpty());
}

/**
 * @brief Crea una geometría puntual.
 * @param x Coordenada X.
 * @param y Coordenada Y.
 * @throw std::runtime_error si GEOS no crea la secuencia o el punto.
 */
Geom Geom::point(double x, double y) {
    initGeosRuntime();
    GEOSCoordSequence* seq = GEOSCoordSeq_create(1, 2);
    if (!seq) throw std::runtime_error("GEOSCoordSeq_create failed");
    if (!GEOSCoordSeq_setXY(seq, 0, x, y)) {
        GEOSCoordSeq_destroy(seq);
        throw std::runtime_error("GEOSCoordSeq_setXY failed");
    }
    GEOSGeometry* g = GEOSGeom_createPoint(seq);
    if (!g) {
        GEOSCoordSeq_destroy(seq);
        throw std::runtime_error("GEOSGeom_createPoint failed");
    }
    return adopt(g);
}

/**
 * @brief Crea una LineString con los puntos dados.
 *
 * Con menos de 2 puntos devuelve una LineString vacía en lugar de fallar, para
 * que los llamadores puedan filtrar con empty() sin ramas extra.
 * @param pts Vértices en orden.
 * @throw std::runtime_error si GEOS falla al crear la secuencia o la línea.
 */
Geom Geom::line(const Ring& pts) {
    initGeosRuntime();
    if (pts.size() < 2) {
        GEOSGeometry* g = GEOSGeom_createEmptyLineString();
        if (!g) throw std::runtime_error("GEOSGeom_createEmptyLineString failed");
        return adopt(g);
    }
    GEOSCoordSequence* seq = GEOSCoordSeq_create(static_cast<unsigned int>(pts.size()), 2);
    if (!seq) throw std::runtime_error("GEOSCoordSeq_create failed");
    for (std::size_t i = 0; i < pts.size(); ++i) {
        if (!GEOSCoordSeq_setXY(seq, static_cast<unsigned int>(i), pts[i].x, pts[i].y)) {
            GEOSCoordSeq_destroy(seq);
            throw std::runtime_error("GEOSCoordSeq_setXY failed");
        }
    }
    GEOSGeometry* g = GEOSGeom_createLineString(seq);
    if (!g) {
        GEOSCoordSeq_destroy(seq);
        throw std::runtime_error("GEOSGeom_createLineString failed");
    }
    return adopt(g);
}

/**
 * @brief Crea un polígono con contorno exterior y agujeros.
 *
 * Sólo usa los agujeros con 3 o más puntos; si el exterior tiene menos de 3
 * devuelve un polígono vacío. Ante cualquier fallo libera los anillos ya
 * creados antes de propagar la excepción.
 * @param shell Contorno exterior.
 * @param holes Agujeros (opcional).
 * @throw std::runtime_error si GEOS no puede crear el polígono.
 * @note No valida auto-intersecciones ni la orientación de los anillos.
 */
Geom Geom::polygon(const Ring& shell, const std::vector<Ring>& holes) {
    initGeosRuntime();
    if (shell.size() < 3) return emptyPolygon();
    GEOSGeometry* shellRing = makeRing(shell);
    std::vector<GEOSGeometry*> holeRings;
    holeRings.reserve(holes.size());
    try {
        for (const Ring& h : holes) {
            if (h.size() >= 3) holeRings.push_back(makeRing(h));
        }
    } catch (...) {
        for (GEOSGeometry* h : holeRings) GEOSGeom_destroy(h);
        GEOSGeom_destroy(shellRing);
        throw;
    }
    GEOSGeometry* g = GEOSGeom_createPolygon(
        shellRing, holeRings.empty() ? nullptr : holeRings.data(),
        static_cast<unsigned int>(holeRings.size()));
    if (!g) {
        GEOSGeom_destroy(shellRing);
        for (GEOSGeometry* h : holeRings) GEOSGeom_destroy(h);
        throw std::runtime_error("GEOSGeom_createPolygon failed");
    }
    return adopt(g);
}

/**
 * @brief Crea un rectángulo alineado a los ejes.
 * @param xmin Esquina inferior izquierda en X.
 * @param ymin Esquina inferior izquierda en Y.
 * @param xmax Esquina superior derecha en X.
 * @param ymax Esquina superior derecha en Y.
 * @note El anillo se entrega ya cerrado (el último punto repite el primero);
 *       makeRing detecta el cierre y no lo duplica.
 */
Geom Geom::box(double xmin, double ymin, double xmax, double ymax) {
    return polygon({{xmin, ymin}, {xmax, ymin}, {xmax, ymax}, {xmin, ymax}, {xmin, ymin}});
}

/**
 * @brief Crea un círculo como buffer de un punto.
 * @param center Centro en mm.
 * @param radius Radio en mm.
 * @param quadSegs Segmentos por cuadrante; más segmentos, más fidelidad.
 * @return Polígono que aproxima el círculo.
 */
Geom Geom::circle(const Pt& center, double radius, int quadSegs) {
    return point(center.x, center.y).buffer(radius, quadSegs);
}

/**
 * @brief Crea un óvalo (obround) de ancho y alto dados.
 *
 * Coloca dos círculos de radio min(w,h)/2 separados a lo largo del lado mayor
 * y toma su envolvente convexa, que es el óvalo exacto.
 * @param center Centro del óvalo.
 * @param width Ancho total; si es <= 0 devuelve vacío.
 * @param height Alto total; si es <= 0 devuelve vacío.
 * @param quadSegs Segmentos por cuadrante de cada círculo.
 */
Geom Geom::obround(const Pt& center, double width, double height, int quadSegs) {
    if (width <= 0.0 || height <= 0.0) return emptyPolygon();
    const double r = std::min(width, height) / 2.0;
    Pt a = center;
    Pt b = center;
    if (width > height) {
        a.x += (width - height) / 2.0;
        b.x -= (width - height) / 2.0;
    } else {
        a.y += (height - width) / 2.0;
        b.y -= (height - width) / 2.0;
    }
    return circle(a, r, quadSegs).unite(circle(b, r, quadSegs)).convexHull();
}

/**
 * @brief Crea un polígono regular inscripto en un círculo.
 *
 * Genera los vértices empezando en el ángulo 0 y, si hace falta, rota el
 * resultado alrededor de @p center.
 * @param center Centro y eje de rotación.
 * @param diameter Diámetro del círculo circunscripto.
 * @param vertices Cantidad de lados (mínimo 3).
 * @param rotationDeg Rotación final en grados.
 * @return Polígono; vacío si los parámetros no son utilizables.
 */
Geom Geom::regularPolygon(const Pt& center, double diameter, int vertices, double rotationDeg) {
    if (vertices < 3 || diameter <= 0.0) return emptyPolygon();
    Ring pts;
    pts.reserve(static_cast<std::size_t>(vertices));
    const double r = diameter / 2.0;
    for (int i = 0; i < vertices; ++i) {
        const double a = 2.0 * M_PI * static_cast<double>(i) / static_cast<double>(vertices);
        pts.push_back({center.x + r * std::cos(a), center.y + r * std::sin(a)});
    }
    Geom g = polygon(pts);
    if (rotationDeg != 0.0) g = g.rotateDeg(rotationDeg, center);
    return g;
}

/**
 * @brief Interpreta geometría en WKT (usado al leer proyectos .FlatPrj).
 * @param wkt Texto WKT (POLYGON, MULTIPOLYGON, LINESTRING, ...).
 * @throw std::runtime_error si GEOS no puede crear el lector o el WKT es
 *        inválido.
 */
Geom Geom::fromWkt(const std::string& wkt) {
    initGeosRuntime();
    GEOSWKTReader* reader = GEOSWKTReader_create();
    if (!reader) throw std::runtime_error("GEOSWKTReader_create failed");
    GEOSGeometry* g = GEOSWKTReader_read(reader, wkt.c_str());
    GEOSWKTReader_destroy(reader);
    if (!g) throw std::runtime_error("invalid WKT geometry");
    return adopt(g);
}

/**
 * @brief Serializa la geometría a WKT.
 *
 * Configura el escritor sin espacios sobrantes y con la precisión pedida, que
 * es la que determina el tamaño de los proyectos guardados.
 * @param decimals Dígitos decimales de redondeo.
 * @return Texto WKT; "POLYGON EMPTY" si el Geom es nulo.
 * @throw std::runtime_error si el escritor falla.
 */
std::string Geom::toWkt(int decimals) const {
    if (!h_) return "POLYGON EMPTY";
    initGeosRuntime();
    GEOSWKTWriter* writer = GEOSWKTWriter_create();
    if (!writer) throw std::runtime_error("GEOSWKTWriter_create failed");
    GEOSWKTWriter_setTrim(writer, 1);
    GEOSWKTWriter_setRoundingPrecision(writer, decimals);
    char* text = GEOSWKTWriter_write(writer, h_.get());
    GEOSWKTWriter_destroy(writer);
    if (!text) throw std::runtime_error("GEOSWKTWriter_write failed");
    std::string out(text);
    GEOSFree(text);
    return out;
}

/**
 * @brief Agrupa varias geometrías en una colección GEOS del tipo pedido.
 *
 * Clona cada parte válida, de modo que el resultado no comparte memoria con
 * las entradas; si no queda ninguna devuelve un polígono vacío.
 * @param parts Geometrías a agrupar.
 * @param type Identificador GEOS de colección (por ejemplo GEOS_MULTIPOLYGON).
 * @throw std::runtime_error si GEOS no puede crear la colección.
 */
Geom Geom::collection(const std::vector<Geom>& parts, int type) {
    std::vector<GEOSGeometry*> raw;
    raw.reserve(parts.size());
    for (const Geom& p : parts) {
        if (p.valid()) raw.push_back(GEOSGeom_clone(p.raw()));
    }
    if (raw.empty()) return emptyPolygon();
    GEOSGeometry* g = GEOSGeom_createCollection(type, raw.data(), static_cast<unsigned int>(raw.size()));
    if (!g) {
        for (GEOSGeometry* p : raw) GEOSGeom_destroy(p);
        throw std::runtime_error("GEOSGeom_createCollection failed");
    }
    return adopt(g);
}

/**
 * @brief Unión de muchas geometrías en un solo resultado.
 *
 * Descarta nulos y vacíos, devuelve directamente el único elemento si queda
 * uno solo y, con varios, arma un MultiPolygon y aplica unaryUnion, que es más
 * eficiente que encadenar uniones binarias.
 * @param parts Geometrías a unir.
 * @return Unión; polígono vacío si no hay partes utilizables.
 * @note Con una sola parte válida se devuelve compartiendo memoria con ella.
 */
Geom Geom::unionOf(const std::vector<Geom>& parts) {
    if (parts.empty()) return emptyPolygon();
    if (parts.size() == 1) return parts.front();
    std::vector<Geom> valid;
    valid.reserve(parts.size());
    for (const Geom& p : parts) {
        if (p.valid() && !p.empty()) valid.push_back(p);
    }
    if (valid.empty()) return emptyPolygon();
    if (valid.size() == 1) return valid.front();
    return Geom::collection(valid, GEOS_MULTIPOLYGON).unaryUnion();
}

// ---------------------------------------------------------------- properties

/**
 * @brief Indica si el Geom es nulo o si GEOS lo considera vacío.
 */
bool Geom::empty() const { return !h_ || GEOSisEmpty(h_.get()) == 1; }

/**
 * @brief Área total de la geometría.
 * @return Área en unidades cuadradas; 0 para líneas y puntos, y también si
 *         GEOS falla.
 */
double Geom::area() const {
    if (!h_) return 0.0;
    double a = 0.0;
    if (!GEOSArea(h_.get(), &a)) return 0.0;
    return a;
}

/**
 * @brief Longitud total de la geometría.
 * @return Perímetro en polígonos y largo en líneas; 0 si GEOS falla.
 */
double Geom::length() const {
    if (!h_) return 0.0;
    double l = 0.0;
    if (!GEOSLength(h_.get(), &l)) return 0.0;
    return l;
}

/**
 * @brief Calcula la caja envolvente.
 * @return BBox válida, o una BBox inválida (valid = false) si el Geom es nulo
 *         o vacío.
 */
BBox Geom::bounds() const {
    BBox b;
    if (!h_ || GEOSisEmpty(h_.get()) == 1) return b;
    double xmin = 0.0, ymin = 0.0, xmax = 0.0, ymax = 0.0;
    if (GEOSGeom_getXMin(h_.get(), &xmin) && GEOSGeom_getYMin(h_.get(), &ymin) &&
        GEOSGeom_getXMax(h_.get(), &xmax) && GEOSGeom_getYMax(h_.get(), &ymax)) {
        b = BBox{xmin, ymin, xmax, ymax};
    }
    return b;
}

/**
 * @brief Validez topológica según GEOSisValid.
 * @note Un polígono auto-intersectado es inválido; Geom::fixed() lo repara.
 */
bool Geom::isValid() const { return h_ && GEOSisValid(h_.get()) == 1; }

/// Tipo GEOS (GEOS_POLYGON, GEOS_LINESTRING, ...) o -1 si el Geom es nulo.
int Geom::typeId() const { return h_ ? GEOSGeomTypeId(h_.get()) : -1; }

/// Cantidad de partes de una colección; 0 si el Geom es nulo.
int Geom::numGeometries() const { return h_ ? GEOSGetNumGeometries(h_.get()) : 0; }

/**
 * @brief Devuelve una parte de la colección como Geom independiente.
 *
 * Clona la parte para que el resultado sobreviva al original.
 * @param i Índice de la parte.
 * @return Parte solicitada, o Geom nulo si el índice no existe.
 */
Geom Geom::geometryN(int i) const {
    if (!h_) return Geom();
    const GEOSGeometry* g = GEOSGetGeometryN(h_.get(), i);
    if (!g) return Geom();
    return adopt(GEOSGeom_clone(g));
}

/**
 * @brief Cantidad de puntos del contorno principal.
 *
 * En polígonos cuenta el anillo exterior (los huecos no se incluyen); en el
 * resto usa la secuencia de coordenadas de la propia geometría.
 * @return Cantidad de vértices; 0 si no aplica.
 */
int Geom::numPoints() const {
    if (!h_) return 0;
    const GEOSGeometry* g = h_.get();
    const GEOSGeometry* target = g;
    if (GEOSGeomTypeId(g) == GEOS_POLYGON) target = GEOSGetExteriorRing(g);
    const GEOSCoordSequence* cs = GEOSGeom_getCoordSeq(target);
    if (!cs) return 0;
    unsigned int n = 0;
    GEOSCoordSeq_getSize(cs, &n);
    return static_cast<int>(n);
}

/**
 * @brief Vértices del contorno principal como Ring.
 *
 * En polígonos devuelve el anillo exterior (incluido el punto de cierre); en
 * líneas, todos sus puntos.
 * @return Lista de puntos; vacía si el Geom es nulo o no tiene coordenadas.
 */
Ring Geom::coords() const {
    if (!h_) return {};
    const GEOSGeometry* g = h_.get();
    const GEOSGeometry* target = g;
    if (GEOSGeomTypeId(g) == GEOS_POLYGON) target = GEOSGetExteriorRing(g);
    return readRing(target);
}

/**
 * @brief Descompone la geometría en contornos exteriores con sus huecos.
 *
 * Maneja Polygon, MultiPolygon y GeometryCollection (recursivo por parte).
 * Descarta contornos con menos de 3 puntos y huecos degenerados, de modo que
 * el resultado sólo contiene polígonos dibujables.
 * @return Lista de PolygonContours en el orden de las partes.
 */
std::vector<PolygonContours> Geom::polygons() const {
    std::vector<PolygonContours> out;
    if (!h_ || GEOSisEmpty(h_.get()) == 1) return out;

    switch (GEOSGeomTypeId(h_.get())) {
        case GEOS_POLYGON: {
            const GEOSGeometry* ext = GEOSGetExteriorRing(h_.get());
            if (!ext) break;
            PolygonContours pc;
            pc.outer = readRing(ext);
            if (pc.outer.size() >= 3) {
                const int n = GEOSGetNumInteriorRings(h_.get());
                for (int i = 0; i < n; ++i) {
                    const GEOSGeometry* inner = GEOSGetInteriorRingN(h_.get(), i);
                    if (inner) {
                        Ring hole = readRing(inner);
                        if (hole.size() >= 3) pc.holes.push_back(std::move(hole));
                    }
                }
                out.push_back(std::move(pc));
            }
            break;
        }
        case GEOS_MULTIPOLYGON:
        case GEOS_GEOMETRYCOLLECTION: {
            const int n = numGeometries();
            for (int i = 0; i < n; ++i) {
                const GEOSGeometry* part = GEOSGetGeometryN(h_.get(), i);
                if (!part) continue;
                std::vector<PolygonContours> sub = adopt(GEOSGeom_clone(part)).polygons();
                out.insert(out.end(), sub.begin(), sub.end());
            }
            break;
        }
        default:
            break;
    }
    return out;
}

// ---------------------------------------------------------------- operations

/**
 * @brief Aplica un buffer circular uniforme (dilatar o contraer).
 * @param distance Radio en mm; un valor negativo contrae la geometría.
 * @param quadSegs Segmentos por cuadrante de la aproximación.
 * @return Geometría resultante; Geom nulo si el origen es nulo.
 * @throw std::runtime_error si GEOS falla.
 */
Geom Geom::buffer(double distance, int quadSegs) const {
    if (!h_) return Geom();
    return require(GEOSBuffer(h_.get(), distance, quadSegs));
}

/**
 * @brief Buffer con estilos de extremo y unión configurables.
 *
 * Se usa para trazos Gerber y PDF que necesitan extremos planos (cap 2) o
 * cuadrados (cap 3) en vez del redondeo por defecto.
 * @param distance Radio del buffer.
 * @param quadSegs Segmentos por cuadrante.
 * @param capStyle Estilo de extremo GEOS (1 redondo, 2 plano, 3 cuadrado).
 * @param joinStyle Estilo de unión GEOS.
 * @param mitreLimit Límite de inglete para uniones en punta.
 */
Geom Geom::bufferStyle(double distance, int quadSegs, int capStyle, int joinStyle,
                       double mitreLimit) const {
    if (!h_) return Geom();
    return require(GEOSBufferWithStyle(h_.get(), distance, quadSegs, capStyle, joinStyle, mitreLimit));
}

/**
 * @brief Unión booleana con otra geometría.
 * @param other Operando derecho.
 * @return Resultado de GEOSUnion; Geom nulo si falta alguno de los operandos.
 */
Geom Geom::unite(const Geom& other) const {
    if (!h_ || !other.h_) return {};
    return require(GEOSUnion(h_.get(), other.h_.get()));
}

/**
 * @brief Diferencia A menos B (resta de coberturas).
 * @param other Operando a restar.
 * @return Resultado; Geom nulo si falta alguno de los operandos.
 */
Geom Geom::difference(const Geom& other) const {
    if (!h_ || !other.h_) return {};
    return require(GEOSDifference(h_.get(), other.h_.get()));
}

/**
 * @brief Intersección de coberturas.
 * @param other Operando derecho.
 * @return Resultado; Geom nulo si falta alguno de los operandos.
 */
Geom Geom::intersection(const Geom& other) const {
    if (!h_ || !other.h_) return {};
    return require(GEOSIntersection(h_.get(), other.h_.get()));
}

/**
 * @brief Par de puntos más cercanos entre dos geometrías.
 *
 * Es el dato que usan las herramientas de distancia mínima y de diámetro
 * óptimo para dibujar la cota sobre el lienzo.
 * @param other Geometría con la que se mide.
 * @return {punto en this, punto en other}; ceros si no hay resultado.
 */
std::pair<Pt, Pt> Geom::nearestPoints(const Geom& other) const {
    initGeosRuntime();
    if (!h_ || !other.h_) return {{0.0, 0.0}, {0.0, 0.0}};
    GEOSCoordSequence* seq = GEOSNearestPoints(h_.get(), other.h_.get());
    if (!seq) return {{0.0, 0.0}, {0.0, 0.0}};
    double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
    GEOSCoordSeq_getXY(seq, 0, &x0, &y0);
    GEOSCoordSeq_getXY(seq, 1, &x1, &y1);
    GEOSCoordSeq_destroy(seq);
    return {{x0, y0}, {x1, y1}};
}

/**
 * @brief Distancia mínima entre las dos geometrías.
 * @return Distancia en mm; también 0 cuando GEOS falla o hay nulos, por lo que
 *         no distingue "se tocan" de "error".
 */
double Geom::distance(const Geom& other) const {
    if (!h_ || !other.h_) return 0.0;
    double d = 0.0;
    if (!GEOSDistance(h_.get(), other.h_.get(), &d)) return 0.0;
    return d;
}

/**
 * @brief Verdadero si las coberturas se tocan o se cruzan.
 */
bool Geom::intersects(const Geom& other) const {
    if (!h_ || !other.h_) return false;
    return GEOSIntersects(h_.get(), other.h_.get()) == 1;
}

/**
 * @brief Une las partes de una colección en un resultado sin solapes.
 * @note Es la operación que limpia el resultado de acumular flashes y trazos.
 */
Geom Geom::unaryUnion() const {
    if (!h_) return {};
    return require(GEOSUnaryUnion(h_.get()));
}

/**
 * @brief Envolvente convexa de la geometría.
 * @note Se usa para el barrido de trazos con apertura rectangular.
 */
Geom Geom::convexHull() const {
    if (!h_) return {};
    return require(GEOSConvexHull(h_.get()));
}

/**
 * @brief Simplifica el contorno con la tolerancia dada (Douglas-Peucker).
 * @param tolerance Distancia máxima en mm que puede apartarse el contorno.
 */
Geom Geom::simplify(double tolerance) const {
    if (!h_) return {};
    return require(GEOSSimplify(h_.get(), tolerance));
}

/**
 * @brief Devuelve una versión topológicamente válida de la geometría.
 *
 * Si GEOS ya la considera válida devuelve una copia compartida; si no, aplica
 * buffer(0), el recurso habitual para reparar auto-intersecciones.
 */
Geom Geom::fixed() const {
    if (!h_) return {};
    if (isValid()) return *this;
    return buffer(0.0, 8);
}

/**
 * @brief Traslada la geometría.
 * @param dx Desplazamiento en X.
 * @param dy Desplazamiento en Y.
 */
Geom Geom::translate(double dx, double dy) const {
    return transformXY([dx, dy](Pt p) { return Pt{p.x + dx, p.y + dy}; });
}

/**
 * @brief Rota la geometría alrededor de un origen.
 * @param angle Ángulo en grados, sentido antihorario.
 * @param origin Punto de giro.
 */
Geom Geom::rotateDeg(double angle, const Pt& origin) const {
    const double a = angle * M_PI / 180.0;
    const double c = std::cos(a);
    const double s = std::sin(a);
    return transformXY([c, s, origin](Pt p) {
        const double dx = p.x - origin.x;
        const double dy = p.y - origin.y;
        return Pt{origin.x + dx * c - dy * s, origin.y + dx * s + dy * c};
    });
}

/**
 * @brief Escala de forma independiente en X e Y respecto de un origen.
 * @param sx Factor de escala en X.
 * @param sy Factor de escala en Y.
 * @param origin Punto fijo de la transformación.
 */
Geom Geom::scale(double sx, double sy, const Pt& origin) const {
    return transformXY([sx, sy, origin](Pt p) {
        return Pt{origin.x + (p.x - origin.x) * sx, origin.y + (p.y - origin.y) * sy};
    });
}

// transformXY is used by translate/rotate/scale and by the parsers.
/**
 * @brief Aplica una función arbitraria a cada vértice.
 *
 * Es la primitiva de translate/rotate/scale y la que usan los importadores
 * (SVG, DXF, HPGL, PDF) para aplicar matrices afines. Delega en
 * GEOSGeom_transformXY, que crea una geometría nueva.
 * @param fn Functor Pt a Pt aplicado a cada coordenada.
 * @return Copia transformada; Geom nulo si el origen es nulo.
 */
Geom Geom::transformXY(const std::function<Pt(Pt)>& fn) const {
    if (!h_) return {};
    TransformData data{&fn};
    return require(GEOSGeom_transformXY(h_.get(), transformCallback, &data));
}

/**
 * @brief Nombre legible del tipo GEOS.
 * @param typeId Identificador devuelto por Geom::typeId().
 * @return "Point", "Polygon", ... o "Unknown" si no se reconoce.
 */
std::string geometryTypeName(int typeId) {
    switch (typeId) {
        case GEOS_POINT: return "Point";
        case GEOS_LINESTRING: return "LineString";
        case GEOS_LINEARRING: return "LinearRing";
        case GEOS_POLYGON: return "Polygon";
        case GEOS_MULTIPOINT: return "MultiPoint";
        case GEOS_MULTILINESTRING: return "MultiLineString";
        case GEOS_MULTIPOLYGON: return "MultiPolygon";
        case GEOS_GEOMETRYCOLLECTION: return "GeometryCollection";
        default: return "Unknown";
    }
}

}  // namespace fc
