/**
 * @file geometry.hpp
 * @brief Núcleo geométrico inmutable sobre la API C de GEOS.
 *
 * FlatCAM C++ usa GEOS para todas las operaciones 2D: uniones, diferencias,
 * intersecciones, buffers, simplificación, reparación y transformaciones.
 * Este archivo define los tipos básicos (`Pt`, `Ring`, `BBox`,
 * `PolygonContours`) y dos envoltorios RAII:
 *
 * - `fc::Geom`: geometría inmutable con copia barata (puntero compartido).
 * - `fc::SpatialIndex`: índice STRtree para consultas por caja envolvente.
 *
 * GEOS no es reentrante en su API clásica; `initGeosRuntime()` se llama de
 * forma automática desde las fábricas de `Geom`.
 */
#pragma once

#include <geos_c.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fc {

/**
 * @brief Punto bidimensional en milímetros.
 *
 * El eje X crece hacia la derecha y el eje Y hacia arriba, igual que en
 * Gerber/Excellon y en el canvas.
 */
struct Pt {
    double x = 0.0; ///< Coordenada X en milímetros.
    double y = 0.0; ///< Coordenada Y en milímetros.
};

/// Secuencia de puntos que describe un anillo, una polilínea o un contorno.
using Ring = std::vector<Pt>;

/**
 * @brief Caja envolvente alineada a los ejes.
 *
 * `valid` distingue una caja vacía (sin geometría) de una caja real en el
 * origen. Las operaciones de expansión y contención respetan esa bandera.
 */
struct BBox {
    double xmin = 0.0; ///< Límite inferior en X.
    double ymin = 0.0; ///< Límite inferior en Y.
    double xmax = 0.0; ///< Límite superior en X.
    double ymax = 0.0; ///< Límite superior en Y.
    bool valid = false; ///< `true` si la caja contiene geometría.

    BBox() = default;

    /**
     * @brief Construye una caja válida a partir de sus límites.
     * @param x0 Límite inferior X.
     * @param y0 Límite inferior Y.
     * @param x1 Límite superior X.
     * @param y1 Límite superior Y.
     */
    BBox(double x0, double y0, double x1, double y1)
        : xmin(x0), ymin(y0), xmax(x1), ymax(y1), valid(true) {}

    /// @return Ancho de la caja (puede ser negativo si está mal construida).
    double width() const { return xmax - xmin; }

    /// @return Alto de la caja (puede ser negativo si está mal construida).
    double height() const { return ymax - ymin; }

    /**
     * @brief Indica si un punto cae dentro de la caja.
     * @param p Punto a comprobar.
     * @return `true` si la caja es válida y el punto está dentro o en el borde.
     */
    bool contains(const Pt& p) const {
        return valid && p.x >= xmin && p.x <= xmax && p.y >= ymin && p.y <= ymax;
    }

    /**
     * @brief Expande la caja un margen uniforme.
     * @param margin Distancia en milímetros (puede ser negativa).
     * @return Nueva caja expandida; la original si no era válida.
     */
    BBox expanded(double margin) const {
        if (!valid) return *this;
        return BBox{xmin - margin, ymin - margin, xmax + margin, ymax + margin};
    }
};

/**
 * @brief Contorno exterior con sus agujeros.
 *
 * Es la representación que usan las herramientas CAM para recorrer un
 * polígono: primero el anillo exterior y luego cada hueco.
 */
struct PolygonContours {
    Ring outer;                ///< Anillo exterior (sentido irrelevante para GEOS).
    std::vector<Ring> holes;   ///< Agujeros interiores.
};

/**
 * @brief Envoltorio inmutable y compartido de una geometría GEOS.
 *
 * Las copias comparten el mismo `GEOSGeometry` mediante `shared_ptr`; por eso
 * son baratas y seguras. Cada operación devuelve una geometría nueva y no
 * modifica la original. Las geometrías inválidas se representan con un handle
 * nulo (`valid() == false`).
 *
 * @note GEOS se inicializa perezosamente en las fábricas; no es necesario
 *       llamar a `initGeosRuntime()` manualmente.
 */
class Geom {
public:
    /// Construye una geometría vacía (sin handle).
    Geom() = default;

    /**
     * @brief Toma posesión de un `GEOSGeometry*` ya creado.
     * @param g Puntero devuelto por GEOS; puede ser `nullptr`.
     * @return Geometría que libera `g` al destruirse.
     */
    static Geom adopt(GEOSGeometry* g);

    /// @return Polígono vacío válido (útil como identidad de uniones).
    static Geom emptyPolygon();

    /// @brief Crea un punto.
    static Geom point(double x, double y);

    /// @brief Crea una polilínea a partir de una secuencia de puntos.
    static Geom line(const Ring& pts);

    /**
     * @brief Crea un polígono con agujeros opcionales.
     * @param shell Anillo exterior.
     * @param holes Agujeros interiores.
     */
    static Geom polygon(const Ring& shell, const std::vector<Ring>& holes = {});

    /// @brief Crea un rectángulo alineado a los ejes.
    static Geom box(double xmin, double ymin, double xmax, double ymax);

    /**
     * @brief Crea un círculo aproximado por segmentos.
     * @param center Centro en mm.
     * @param radius Radio en mm.
     * @param quadSegs Segmentos por cuadrante (más = más suave).
     */
    static Geom circle(const Pt& center, double radius, int quadSegs = 16);

    /**
     * @brief Crea un óvalo (rectángulo con extremos semicirculares).
     * @param center Centro en mm.
     * @param width Ancho total.
     * @param height Alto total.
     * @param quadSegs Segmentos por cuadrante de los extremos.
     */
    static Geom obround(const Pt& center, double width, double height, int quadSegs = 16);

    /**
     * @brief Crea un polígono regular.
     * @param center Centro en mm.
     * @param diameter Diámetro de la circunferencia circunscrita.
     * @param vertices Cantidad de vértices (≥ 3).
     * @param rotationDeg Rotación inicial en grados.
     */
    static Geom regularPolygon(const Pt& center, double diameter, int vertices,
                               double rotationDeg = 0.0);

    /**
     * @brief Agrupa varias geometrías en una colección.
     * @param parts Componentes.
     * @param type Tipo GEOS de la colección (por defecto `GEOS_MULTIPOLYGON`).
     */
    static Geom collection(const std::vector<Geom>& parts, int type = GEOS_MULTIPOLYGON);

    /**
     * @brief Une todas las geometrías de una lista.
     * @param parts Componentes.
     * @return Unión; polígono vacío si la lista está vacía.
     */
    static Geom unionOf(const std::vector<Geom>& parts);

    /**
     * @brief Interpreta geometría en formato WKT.
     * @param wkt Texto WKT (POINT, LINESTRING, POLYGON, MULTIPOLYGON...).
     * @throw std::runtime_error si el WKT no se puede parsear.
     */
    static Geom fromWkt(const std::string& wkt);

    /// @return `true` si el handle interno no es nulo.
    bool valid() const { return static_cast<bool>(h_); }

    /// @return `true` si GEOS considera la geometría vacía.
    bool empty() const;

    /// @return Área en mm²; 0 para geometrías inválidas.
    double area() const;

    /// @return Longitud/perímetro en mm; 0 para geometrías inválidas.
    double length() const;

    /// @return Caja envolvente; `valid == false` si no hay geometría.
    BBox bounds() const;

    /// @return `true` si GEOS valida la topología (sin auto-intersecciones).
    bool isValid() const;

    /// @return Identificador GEOS (`GEOS_POLYGON`, `GEOS_LINESTRING`, etc.).
    int typeId() const;

    /// @return Cantidad de componentes de una colección.
    int numGeometries() const;

    /**
     * @brief Accede a una componente de una colección.
     * @param i Índice basado en cero.
     * @return Componente; geometría vacía si el índice no es válido.
     */
    Geom geometryN(int i) const;

    /// @return Cantidad de puntos de la primera secuencia de coordenadas.
    int numPoints() const;

    /// @return Coordenadas de la primera secuencia (exterior o línea).
    Ring coords() const;

    /**
     * @brief Descompone una geometría poligonal en contornos y agujeros.
     * @return Lista de `PolygonContours`; vacía si no hay polígonos.
     */
    std::vector<PolygonContours> polygons() const;

    /**
     * @brief Aplica un buffer (offset) uniforme.
     * @param distance Distancia en mm; positiva expande, negativa contrae.
     * @param quadSegs Segmentos por cuadrante de las curvas.
     */
    Geom buffer(double distance, int quadSegs = 16) const;

    /**
     * @brief Buffer con estilos de extremo y unión.
     * @param distance Distancia en mm.
     * @param quadSegs Segmentos por cuadrante.
     * @param capStyle Estilo de extremo (`GEOS_CAP_ROUND`, etc.).
     * @param joinStyle Estilo de unión (`GEOS_JOIN_ROUND`, etc.).
     * @param mitreLimit Límite de inglete para uniones en punta.
     */
    Geom bufferStyle(double distance, int quadSegs, int capStyle, int joinStyle,
                     double mitreLimit = 5.0) const;

    /// @brief Unión booleana con otra geometría.
    Geom unite(const Geom& other) const;

    /// @brief Diferencia booleana (`this - other`).
    Geom difference(const Geom& other) const;

    /// @brief Intersección booleana.
    Geom intersection(const Geom& other) const;

    /// @return `true` si ambas geometrías se tocan o solapan.
    bool intersects(const Geom& other) const;

    /// @return Distancia mínima en mm entre ambas geometrías.
    double distance(const Geom& other) const;

    /**
     * @brief Obtiene el par de puntos más cercano entre dos geometrías.
     * @return Primer punto en `this`, segundo en `other`; `(0,0)` si falla.
     */
    std::pair<Pt, Pt> nearestPoints(const Geom& other) const;

    /// @brief Unión de todas las componentes de la geometría.
    Geom unaryUnion() const;

    /// @brief Envolvente convexa.
    Geom convexHull() const;

    /**
     * @brief Simplifica la geometría con el algoritmo Douglas-Peucker.
     * @param tolerance Tolerancia en mm.
     */
    Geom simplify(double tolerance) const;

    /// @brief Repara polígonos auto-intersectados aplicando `buffer(0)`.
    Geom fixed() const;

    /**
     * @brief Transforma cada punto con una función arbitraria.
     * @param fn Función `(Pt) -> Pt` aplicada a cada vértice.
     */
    Geom transformXY(const std::function<Pt(Pt)>& fn) const;

    /// @brief Traslada la geometría.
    Geom translate(double dx, double dy) const;

    /// @brief Rota la geometría en grados alrededor de un origen.
    Geom rotateDeg(double angle, const Pt& origin = {0.0, 0.0}) const;

    /// @brief Escala la geometría respecto de un origen.
    Geom scale(double sx, double sy, const Pt& origin = {0.0, 0.0}) const;

    /**
     * @brief Serializa la geometría a WKT.
     * @param decimals Decimales de las coordenadas.
     */
    std::string toWkt(int decimals = 9) const;

    /// @return Puntero crudo a GEOS (no transferir la propiedad).
    GEOSGeometry* raw() const { return h_.get(); }

private:
    using Handle = std::shared_ptr<GEOSGeometry>; ///< Handle compartido con deleter GEOS.
    Handle h_;                                   ///< Geometría subyacente.

    explicit Geom(Handle h) : h_(std::move(h)) {}
};

/**
 * @brief Índice espacial envolvente de `GEOSSTRtree`.
 *
 * Inserta geometrías con un identificador entero y permite consultar qué
 * identificadores intersectan una caja. Es útil para acelerar la detección de
 * colisiones y la selección de objetos.
 *
 * El índice no es copiable y debe construirse (`build()`) antes de consultar.
 */
class SpatialIndex {
public:
    /// Crea un índice vacío.
    SpatialIndex();

    /// Libera el STRtree de GEOS.
    ~SpatialIndex();

    SpatialIndex(const SpatialIndex&) = delete;
    SpatialIndex& operator=(const SpatialIndex&) = delete;

    /**
     * @brief Inserta una geometría con un identificador.
     * @param g Geometría a indexar (se conserva una copia compartida).
     * @param id Identificador devuelto en las consultas.
     */
    void insert(const Geom& g, std::uint32_t id);

    /// Construye el árbol; obligatorio antes de `query()`.
    void build();

    /**
     * @brief Consulta los identificadores que intersectan una caja.
     * @param box Caja de búsqueda.
     * @return Identificadores en orden no especificado.
     */
    std::vector<std::uint32_t> query(const BBox& box) const;

private:
    GEOSSTRtree* tree_ = nullptr;      ///< Árbol propiedad de esta clase.
    std::vector<Geom> keep_;           ///< Mantiene vivas las geometrías indexadas.
    std::vector<std::uint32_t> ids_;   ///< Identificadores paralelos a `keep_`.
    bool built_ = false;               ///< Evita reconstruir o consultar antes de tiempo.
};

/// @return Nombre legible de un tipo GEOS (`Polygon`, `LineString`, ...).
std::string geometryTypeName(int typeId);

/**
 * @brief Inicializa el runtime global de GEOS.
 *
 * GEOS usa estado global y no es reentrante en su API clásica. Las fábricas de
 * `Geom` llaman a esta función automáticamente; es idempotente.
 */
void initGeosRuntime();

}  // namespace fc
