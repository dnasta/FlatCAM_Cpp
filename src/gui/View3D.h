#pragma once

#include <QDialog>
#include <QPoint>
#include <vector>

#include "flatcam/geometry.hpp"
#include "UnitSpinBox.h"

class QDoubleSpinBox;
class QWidget;

class View3D : public QDialog {
    Q_OBJECT

public:
    View3D(std::vector<fc::Geom> solids, std::vector<fc::Geom> holes = {},
           QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct Face {
        std::vector<QPointF> points;
        std::vector<std::vector<QPointF>> holes;
        double depth = 0.0;
        double shade = 1.0;
        bool top = true;
        bool copper = true;
    };

    void rebuildFaces();
    QPointF project(double x, double y, double z, double* depth) const;
    void fitView();

    std::vector<fc::Geom> solids_;
    std::vector<fc::Geom> holes_;
    fc::BBox board_;
    bool showBoard_ = true;
    bool showCopper_ = true;
    std::vector<Face> topFaces_;
    std::vector<Face> bottomFaces_;
    std::vector<Face> boardFaces_;
    double thickness_ = 1.6;
    double copperThickness_ = 0.08;
    double yaw_ = 0.6;
    double pitch_ = 0.9;
    double zoom_ = 1.0;
    QPointF center_;
    QPoint lastMouse_;
    bool dragging_ = false;
    double scale_ = 1.0;
    QPointF offset_;
    UnitSpinBox* thicknessSpin_ = nullptr;
    UnitSpinBox* copperSpin_ = nullptr;
};
