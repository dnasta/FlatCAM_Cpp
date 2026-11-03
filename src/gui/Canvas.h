/**
 * @file Canvas.h
 * @brief Visor 2D de capas, cobre, recorridos y herramientas de vista.
 *
 * `Canvas` mantiene una lista ordenada de capas. Cada capa guarda:
 * - una ruta `QPainterPath` para dibujar sólidos y recorridos,
 * - la geometría GEOS opcional para edición/exportación,
 * - estado de visibilidad, relleno y modo multicolor.
 *
 * El widget se encarga del zoom, pan, selección, rejilla, coordenadas y HUD.
 */
#pragma once

#include <QPainterPath>
#include <QPointF>
#include <QTransform>
#include <QWidget>
#include <vector>

#include "flatcam/geometry.hpp"

class QPainter;

/**
 * @brief Widget de dibujo 2D con capas.
 */
class Canvas : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Copia serializable de una capa (para undo/redo).
     */
    struct LayerSnapshot {
        int id = 0;                       ///< Identificador de capa.
        QPainterPath solid;               ///< Ruta de sólidos.
        QPainterPath follow;              ///< Ruta de recorridos.
        std::vector<fc::Geom> solids;     ///< Geometría de sólidos.
        std::vector<fc::Geom> follows;    ///< Geometría de recorridos.
        bool visible = true;              ///< Visibilidad.
    };

    /// @brief Crea un canvas vacío.
    explicit Canvas(QWidget* parent = nullptr);

    /// @brief Reemplaza el contenido por una única capa con la geometría dada.
    void setGeometryData(const std::vector<fc::Geom>& solids, const std::vector<fc::Geom>& follows);
    /// @brief Inicia una carga por lotes (evita recalcular límites por capa).
    void beginGeometry();

    /**
     * @brief Agrega una capa con geometría.
     * @param solids Polígonos.
     * @param follows Recorridos.
     * @param keepGeometry Conserva la geometría GEOS para edición.
     * @return Identificador de la nueva capa.
     */
    int appendGeometry(const std::vector<fc::Geom>& solids, const std::vector<fc::Geom>& follows,
                       bool keepGeometry = true);
    /// @brief Finaliza la carga por lotes y recalcula límites.
    void endGeometry();

    /// @brief Reemplaza la previsualización de trayectorias.
    void setToolpaths(const std::vector<fc::Geom>& paths);

    /// @return Identificadores de capa en orden de dibujo.
    std::vector<int> layerIds() const;

    /// @brief Reordena las capas según la lista de identificadores.
    void setLayerOrder(const std::vector<int>& ids);
    /// @brief Muestra u oculta una capa.
    void setLayerVisible(int id, bool visible);

    /// @brief Elimina una capa.
    void removeLayer(int id);
    /// @brief Duplica una capa y devuelve el nuevo id.
    int duplicateLayer(int id);

    /// @return Límites de una capa; caja inválida si no existe.
    fc::BBox layerBounds(int id) const;
    /// @brief Captura el estado de una capa para undo/redo.
    LayerSnapshot layerSnapshot(int id) const;

    /// @brief Inserta una capa a partir de un snapshot.
    void insertLayer(const LayerSnapshot& snapshot);
    /// @return Sólidos GEOS de una capa.
    std::vector<fc::Geom> layerSolids(int id) const;

    /// @return Recorridos GEOS de una capa.
    std::vector<fc::Geom> layerFollows(int id) const;
    /// @brief Reemplaza la geometría de una capa y actualiza su dibujo.
    void setLayerGeometry(int id, const std::vector<fc::Geom>& solids,
                          const std::vector<fc::Geom>& follows);

    /// @brief Dibuja la capa rellena o sólo con contorno.
    void setLayerFilled(int id, bool filled);
    /// @return `true` si la capa se dibuja rellena.
    bool layerFilled(int id) const;

    /// @brief Activa el coloreado por aperturas.
    void setLayerMulticolor(int id, bool multicolor);
    /// @return `true` si la capa usa modo multicolor.
    bool layerMulticolor(int id) const;

    /// @brief Combina las capas visibles en una capa nueva.
    int mergeVisibleLayers();
    /// @brief Aplica una transformación afín a una capa.
    void transformLayer(int id, const QTransform& transform);

    /// @brief Exporta las capas visibles a SVG.
    bool exportSvg(const QString& path) const;

    /// @brief Elimina todas las capas.
    void clearData();

    /// @brief Ajusta la vista a todo el contenido.
    void fit();

    /// @brief Ajusta la vista a una caja.
    void fitTo(const fc::BBox& bounds);
    /// @brief Muestra u oculta la rejilla.
    void setGridVisible(bool visible);

    /// @return `true` si la rejilla está visible.
    bool gridVisible() const { return gridVisible_; }
    /// @brief Fija el paso de rejilla (0 = automático).
    void setGridStep(double step) { gridStep_ = step; update(); }

    /// @return Paso de rejilla en mm (0 = automático).
    double gridStep() const { return gridStep_; }
    /// @brief Configura los colores del visor.
    void setColors(const QColor& background, const QColor& copper, const QColor& follow,
                   const QColor& toolpath);

    /// @return `true` si hay alguna capa con contenido.
    bool hasData() const { return hasData_; }
    /// @return Escala actual de la vista.
    double viewScale() const { return scale_; }

    /// @return Desplazamiento actual de la vista.
    QPointF viewOffset() const { return offset_; }
    /// @brief Restaura escala y desplazamiento.
    void setView(double scale, const QPointF& offset);

    /// @brief Centra la vista en coordenadas de mundo.
    void centerOn(double x, double y);
    /// @brief Define el origen mostrado en la barra de estado.
    void setDisplayOrigin(const fc::Pt& origin) { displayOrigin_ = origin; update(); }

    /// @return Origen de visualización.
    fc::Pt displayOrigin() const { return displayOrigin_; }
    /// @brief Define el área de trabajo a dibujar.
    void setWorkspace(const fc::BBox& area) { workspace_ = area; update(); }

    /// @return Área de trabajo.
    fc::BBox workspace() const { return workspace_; }
    /// @brief Muestra u oculta el HUD de coordenadas.
    void setHudVisible(bool visible) { hudVisible_ = visible; update(); }

    /// @return `true` si el HUD está visible.
    bool hudVisible() const { return hudVisible_; }
    /// @return Sólidos de todas las capas visibles.
    std::vector<fc::Geom> visibleSolids() const;

public slots:
    /// @brief Aumenta el zoom.
    void zoomIn();

    /// @brief Disminuye el zoom.
    void zoomOut();

signals:
    /// @brief Emite coordenadas de mundo al mover el cursor.
    void cursorMoved(double x, double y);

    /// @brief Emite el id de la capa bajo el clic.
    void layerPicked(int id);

    /// @brief Emite el id de la capa con doble clic.
    void layerActivated(int id);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    /// Representación interna de una capa.
    struct Layer {
        int id = 0;                       ///< Identificador de capa.
        QPainterPath solid;               ///< Ruta de sólidos.
        QPainterPath follow;              ///< Ruta de recorridos.
        std::vector<fc::Geom> solids;     ///< Geometría de sólidos.
        std::vector<fc::Geom> follows;    ///< Geometría de recorridos.
        bool visible = true;              ///< Visibilidad.
        bool filled = true;
        bool multicolor = false;
    };

    static QPainterPath buildPath(const std::vector<fc::Geom>& geoms);
    /// Construye la ruta de recorridos.
    static QPainterPath buildLinePath(const std::vector<fc::Geom>& geoms);

    /// Agrega una geometría de línea a una ruta Qt.
    static void appendLinePath(QPainterPath& path, const fc::Geom& g);
    /// Aplica zoom manteniendo fijo un punto de pantalla.
    void zoomAt(const QPointF& anchor, double factor);

    /// Recalcula límites y ajusta la vista si es necesario.
    void recomputeBounds();

    /// Busca una capa por id.
    Layer* findLayer(int id);
    const Layer* findLayer(int id) const;

    std::vector<Layer> layers_;
    QPainterPath toolpathPath_;
    fc::BBox bounds_;
    double scale_ = 1.0;
    QPointF offset_;
    QPoint lastMouse_;
    bool panning_ = false;
    QPoint pressPos_;
    bool dragged_ = false;
    bool hasData_ = false;
    bool gridVisible_ = true;
    double gridStep_ = 0.0;  // 0 = automatic
    fc::BBox workspace_;
    bool hudVisible_ = false;
    QPointF cursorWorld_;
    fc::Pt displayOrigin_{0.0, 0.0};
    bool hasCursor_ = false;
    QColor background_{16, 20, 24};
    QColor copperFill_{217, 164, 65, 170};
    QColor copperEdge_{240, 196, 106};
    QColor followColor_{111, 168, 220, 170};
    QColor toolpathColor_{102, 214, 132};
    int nextId_ = 1;
};
