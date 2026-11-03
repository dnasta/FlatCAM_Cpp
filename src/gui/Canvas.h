#pragma once

#include <QPainterPath>
#include <QPointF>
#include <QTransform>
#include <QWidget>
#include <vector>

#include "flatcam/geometry.hpp"

class QPainter;

class Canvas : public QWidget {
    Q_OBJECT

public:
    struct LayerSnapshot {
        int id = 0;
        QPainterPath solid;
        QPainterPath follow;
        std::vector<fc::Geom> solids;
        std::vector<fc::Geom> follows;
        bool visible = true;
    };

    explicit Canvas(QWidget* parent = nullptr);

    void setGeometryData(const std::vector<fc::Geom>& solids, const std::vector<fc::Geom>& follows);
    void beginGeometry();
    int appendGeometry(const std::vector<fc::Geom>& solids, const std::vector<fc::Geom>& follows,
                       bool keepGeometry = true);
    void endGeometry();
    void setToolpaths(const std::vector<fc::Geom>& paths);

    std::vector<int> layerIds() const;
    void setLayerOrder(const std::vector<int>& ids);
    void setLayerVisible(int id, bool visible);
    void removeLayer(int id);
    int duplicateLayer(int id);
    fc::BBox layerBounds(int id) const;
    LayerSnapshot layerSnapshot(int id) const;
    void insertLayer(const LayerSnapshot& snapshot);
    std::vector<fc::Geom> layerSolids(int id) const;
    std::vector<fc::Geom> layerFollows(int id) const;
    void setLayerGeometry(int id, const std::vector<fc::Geom>& solids,
                          const std::vector<fc::Geom>& follows);
    void setLayerFilled(int id, bool filled);
    bool layerFilled(int id) const;
    void setLayerMulticolor(int id, bool multicolor);
    bool layerMulticolor(int id) const;
    int mergeVisibleLayers();
    void transformLayer(int id, const QTransform& transform);
    bool exportSvg(const QString& path) const;

    void clearData();
    void fit();
    void fitTo(const fc::BBox& bounds);
    void setGridVisible(bool visible);
    bool gridVisible() const { return gridVisible_; }
    void setGridStep(double step) { gridStep_ = step; update(); }
    double gridStep() const { return gridStep_; }
    void setColors(const QColor& background, const QColor& copper, const QColor& follow,
                   const QColor& toolpath);
    bool hasData() const { return hasData_; }
    double viewScale() const { return scale_; }
    QPointF viewOffset() const { return offset_; }
    void setView(double scale, const QPointF& offset);
    void centerOn(double x, double y);
    void setDisplayOrigin(const fc::Pt& origin) { displayOrigin_ = origin; update(); }
    fc::Pt displayOrigin() const { return displayOrigin_; }
    void setWorkspace(const fc::BBox& area) { workspace_ = area; update(); }
    fc::BBox workspace() const { return workspace_; }
    void setHudVisible(bool visible) { hudVisible_ = visible; update(); }
    bool hudVisible() const { return hudVisible_; }
    std::vector<fc::Geom> visibleSolids() const;

public slots:
    void zoomIn();
    void zoomOut();

signals:
    void cursorMoved(double x, double y);
    void layerPicked(int id);
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
    struct Layer {
        int id = 0;
        QPainterPath solid;
        QPainterPath follow;
        std::vector<fc::Geom> solids;
        std::vector<fc::Geom> follows;
        bool visible = true;
        bool filled = true;
        bool multicolor = false;
    };

    static QPainterPath buildPath(const std::vector<fc::Geom>& geoms);
    static QPainterPath buildLinePath(const std::vector<fc::Geom>& geoms);
    static void appendLinePath(QPainterPath& path, const fc::Geom& g);
    void zoomAt(const QPointF& anchor, double factor);
    void recomputeBounds();
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
