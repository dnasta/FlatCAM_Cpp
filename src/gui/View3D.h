/**
 * @file View3D.h
 * @brief Vista 3D de la placa y el cobre (QPainter, sin GPU).
 *
 * Genera caras extruidas para la placa, el cobre y sus paredes, las sombrea
 * con iluminación Lambert y las dibuja ordenadas por profundidad.
 */
#pragma once

#include <QDialog>
#include <QPoint>
#include <vector>

#include "flatcam/geometry.hpp"
#include "UnitSpinBox.h"

class QDoubleSpinBox;
class QWidget;

/**
 * @brief Diálogo de vista 3D.
 */
class View3D : public QDialog {
    Q_OBJECT

public:
    /**
     * @brief Crea la vista 3D.
     * @param solids Cobre visible.
     * @param holes Taladros a dibujar sobre la placa.
     * @param parent Widget padre.
     */
    View3D(std::vector<fc::Geom> solids, std::vector<fc::Geom> holes = {},
           QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    /// Cara proyectada con profundidad y sombreado.
    struct Face {
        std::vector<QPointF> points;                 ///< Contorno exterior proyectado.
        std::vector<std::vector<QPointF>> holes;     ///< Agujeros proyectados.
        double depth = 0.0;                          ///< Profundidad media para ordenar.
        double shade = 1.0;                          ///< Factor de iluminación.
        bool top = true;                             ///< Cara superior.
        bool copper = true;                          ///< Cobre o placa.
    };

    /// Recalcula caras, sombreado y orden.
    void rebuildFaces();

    /// Proyecta un punto 3D a pantalla y devuelve profundidad.
    QPointF project(double x, double y, double z, double* depth) const;
    /// Ajusta escala y desplazamiento al contenido.
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
