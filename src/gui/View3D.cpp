/**
 * @file View3D.cpp
 * @brief Vista 3D sin GPU: extrusión de la placa y el cobre con `QPainter`.
 *
 * Construye caras (tapa, fondo y paredes) a partir de los polígonos de cobre y
 * de la caja de la placa, las proyecta con una rotación yaw/pitch, las sombrea
 * con un modelo Lambert simplificado y las dibuja ordenadas por profundidad
 * media. Incluye controles de espesor de placa y de cobre, toggles de
 * visibilidad y exportación a PNG.
 */
#include "View3D.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QDoubleSpinBox>
#include <QSettings>
#include <QCheckBox>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

/**
 * @brief Construye el diálogo, sus controles y la primera malla de caras.
 *
 * Crea los dos `UnitSpinBox` de espesor (placa y cobre, en mm o pulgadas según
 * la preferencia global), los toggles de placa/cobre, el botón de exportación
 * PNG y las conexiones que reconstruyen la malla al cambiar un espesor. Calcula
 * la caja de la placa como la unión de los límites de los sólidos, expandida
 * 2 mm por lado, y termina llamando a rebuildFaces().
 *
 * @param solids Polígonos de cobre a extruir (se mueven al miembro `solids_`).
 * @param holes Taladros a dibujar sobre el cobre (se mueven a `holes_`).
 * @param parent Widget padre del diálogo.
 * @note La caja de la placa se deriva **sólo** de los sólidos: si no hay cobre
 *       válido la placa no existe y la vista queda vacía aunque haya taladros.
 * @warning El nombre sugerido del PNG usa un ternario con ramas idénticas
 *          (View3D.cpp:93-95): `QFileInfo::exists(".")` siempre es cierto, de
 *          modo que la intención original (proponer otra ruta si el directorio
 *          actual no existe) quedó sin efecto.
 */
View3D::View3D(std::vector<fc::Geom> solids, std::vector<fc::Geom> holes, QWidget* parent)
    : QDialog(parent), solids_(std::move(solids)), holes_(std::move(holes)) {
    setWindowTitle(tr("Vista 3D"));
    resize(1000, 750);
    setMinimumSize(500, 400);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    UnitSpinBox::setDisplayInch(QSettings().value("preferences/displayInch", false).toBool());
    thicknessSpin_ = new UnitSpinBox(this);
    thicknessSpin_->setRange(0.1, 20.0);
    thicknessSpin_->setDecimals(2);
    thicknessSpin_->setValue(thickness_);
    thicknessSpin_->setSuffix(" mm");
    thicknessSpin_->setToolTip(tr("Espesor de la placa para extruir el cobre."));
    form->addRow(tr("Espesor de placa:"), thicknessSpin_);
    copperSpin_ = new UnitSpinBox(this);
    copperSpin_->setRange(0.01, 2.0);
    copperSpin_->setDecimals(3);
    copperSpin_->setValue(copperThickness_);
    copperSpin_->setSuffix(" mm");
    copperSpin_->setToolTip(tr("Altura del cobre extruido sobre la placa."));
    form->addRow(tr("Espesor de cobre:"), copperSpin_);
    layout->addLayout(form);
    auto* optionsRow = new QHBoxLayout();
    auto* boardCheck = new QCheckBox(tr("Mostrar placa"), this);
    boardCheck->setChecked(true);
    auto* copperCheck = new QCheckBox(tr("Mostrar cobre"), this);
    copperCheck->setChecked(true);
    auto* save = new QPushButton(tr("Guardar PNG..."), this);
    optionsRow->addWidget(boardCheck);
    optionsRow->addWidget(copperCheck);
    optionsRow->addStretch();
    optionsRow->addWidget(save);
    layout->addLayout(optionsRow);
    layout->addStretch(1);
    connect(boardCheck, &QCheckBox::toggled, this, [this](bool on) {
        showBoard_ = on;
        update();
    });
    connect(copperCheck, &QCheckBox::toggled, this, [this](bool on) {
        showCopper_ = on;
        update();
    });
    connect(save, &QPushButton::clicked, this, [this] {
        QString path = QFileDialog::getSaveFileName(
            this, tr("Guardar vista 3D"), "vista3d.png", tr("PNG (*.png)"));
        if (path.isEmpty()) return;
        if (!path.endsWith(".png", Qt::CaseInsensitive)) path += ".png";
        if (!grab().save(path, "PNG"))
            QMessageBox::warning(this, tr("Guardar vista 3D"),
                                 tr("No se pudo guardar la imagen en %1.").arg(path));
    });
    for (const fc::Geom& solid : solids_) {
        const fc::BBox b = solid.bounds();
        if (!b.valid) continue;
        if (!board_.valid) board_ = b;
        else {
            board_.xmin = std::min(board_.xmin, b.xmin);
            board_.ymin = std::min(board_.ymin, b.ymin);
            board_.xmax = std::max(board_.xmax, b.xmax);
            board_.ymax = std::max(board_.ymax, b.ymax);
        }
    }
    if (board_.valid) board_ = board_.expanded(2.0);

    connect(thicknessSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double) {
                thickness_ = thicknessSpin_->value();
                rebuildFaces();
                update();
            });
    connect(copperSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double) {
                copperThickness_ = copperSpin_->value();
                rebuildFaces();
                update();
            });

    rebuildFaces();
}

/**
 * @brief Proyecta un punto 3D a coordenadas de pantalla normalizadas.
 *
 * Aplica la rotación de guiñada (yaw) sobre el plano XY, centra en `center_` y
 * luego la inclinación (pitch) para obtener la coordenada vertical de pantalla.
 * La profundidad, cuando se pide, se calcula con el eje de vista
 * `(sin(yaw)·cos(pitch), cos(yaw)·cos(pitch), sin(pitch))`, que es el usado
 * para ordenar las caras.
 *
 * @param x Coordenada X de mundo (mm).
 * @param y Coordenada Y de mundo.
 * @param z Altura (mm), positiva sobre la cara superior del cobre.
 * @param depth Si no es nulo, recibe la profundidad a lo largo del eje de vista.
 * @return Punto en el espacio de la vista (aún sin escala ni desplazamiento; el
 *         llamador aplica `scale_ * zoom_` y `offset_`).
 */
QPointF View3D::project(double x, double y, double z, double* depth) const {
    const double cx = x - center_.x();
    const double cy = y - center_.y();
    const double cyaw = std::cos(yaw_);
    const double syaw = std::sin(yaw_);
    const double rx = cx * cyaw - cy * syaw;
    const double ry = cx * syaw + cy * cyaw;
    const double cp = std::cos(pitch_);
    const double sp = std::sin(pitch_);
    const double sy = ry * sp - z * cp;
    if (depth) *depth = ry * cp + z * sp;
    return QPointF(rx, sy);
}

/**
 * @brief Reconstruye toda la malla de caras, su sombreado y su orden.
 *
 * Para cada polígono de cobre genera cuatro grupos de caras: tapa (z =
 * espesor de cobre), fondo (z = 0) y paredes laterales del contorno exterior y
 * de cada agujero (de z = 0 al espesor de cobre). La placa se modela como una
 * caja: tapa en z = 0, fondo en z = −espesor de placa y cuatro paredes. Cada
 * cara guarda su profundidad media y un factor de sombreado Lambert; al final
 * las tres listas se ordenan por profundidad y se recalcula el encuadre con
 * fitView().
 *
 * @note Las tapas y el fondo se descartan si el anillo tiene menos de 3 puntos,
 *       y las paredes de lados de longitud ~0 se omiten para no generar caras
 *       degeneradas. El sombreado es independiente del color final: el color
 *       base lo pone paintEvent.
 * @warning Es una reconstrucción completa (proyección de miles de vértices y
 *          tres ordenamientos) que se ejecuta también en cada evento de
 *          arrastre; en placas con mucho cobre la rotación puede perder
 *          fluidez.
 */
void View3D::rebuildFaces() {
    topFaces_.clear();
    bottomFaces_.clear();
    boardFaces_.clear();

    const double cyaw = std::cos(yaw_);
    const double syaw = std::sin(yaw_);
    const double cp = std::cos(pitch_);
    const double sp = std::sin(pitch_);

    /// Iluminación Lambert con dirección de luz fija (0.25, -0.35, 0.90),
    /// normal rotada por guiñada/pitch y contraste acotado a [0.25, 1.05].
    auto shade = [&](double nx, double ny, double nz) {
        const double rx = nx * cyaw - ny * syaw;
        const double ry = nx * syaw + ny * cyaw;
        const double sy = ry * sp - nz * cp;
        const double dz = ry * cp + nz * sp;
        const double lambert = std::fabs(rx * 0.25 - sy * 0.35 + dz * 0.90);
        return std::clamp(0.35 + 0.65 * lambert, 0.25, 1.05);
    };

    /// Proyecta una tapa/fondo: contorno exterior con profundidad media y
    /// agujeros proyectados; descarta el contorno si tiene menos de 3 puntos.
    auto makeFace = [&](const fc::PolygonContours& pc, double z, bool top, double normalZ,
                        bool copper, std::vector<Face>& target) {
        Face face;
        face.top = top;
        face.copper = copper;
        double depthSum = 0.0;
        for (const fc::Pt& p : pc.outer) {
            double depth = 0.0;
            face.points.push_back(project(p.x, p.y, z, &depth));
            depthSum += depth;
        }
        if (face.points.size() < 3) return;
        for (const fc::Ring& hole : pc.holes) {
            std::vector<QPointF> ring;
            for (const fc::Pt& p : hole)
                ring.push_back(project(p.x, p.y, z, nullptr));
            if (ring.size() >= 3) face.holes.push_back(std::move(ring));
        }
        face.depth = depthSum / face.points.size();
        face.shade = shade(0.0, 0.0, normalZ);
        target.push_back(std::move(face));
    };

    /// Genera un cuadrilátero vertical por cada lado del anillo, de z = 0 a la
    /// altura del material, con la normal exterior y la profundidad del centro.
    auto addWalls = [&](const fc::Ring& ring, bool copper, std::vector<Face>& target) {
        if (ring.size() < 2) return;
        const double wallHeight = copper ? copperThickness_ : thickness_;
        for (std::size_t i = 0; i < ring.size(); ++i) {
            const fc::Pt& a = ring[i];
            const fc::Pt& b = ring[(i + 1) % ring.size()];
            const double dx = b.x - a.x;
            const double dy = b.y - a.y;
            const double length = std::hypot(dx, dy);
            if (length < 1e-9) continue;
            const double nx = dy / length;
            const double ny = -dx / length;
            Face wall;
            wall.top = false;
            wall.copper = copper;
            wall.points.push_back(project(a.x, a.y, 0.0, nullptr));
            wall.points.push_back(project(b.x, b.y, 0.0, nullptr));
            wall.points.push_back(project(b.x, b.y, wallHeight, nullptr));
            wall.points.push_back(project(a.x, a.y, wallHeight, nullptr));
            double depth = 0.0;
            project((a.x + b.x) / 2.0, (a.y + b.y) / 2.0, wallHeight / 2.0, &depth);
            wall.depth = depth;
            wall.shade = shade(nx, ny, 0.0);
            target.push_back(std::move(wall));
        }
    };

    for (const fc::Geom& solid : solids_) {
        for (const fc::PolygonContours& pc : solid.polygons()) {
            makeFace(pc, copperThickness_, true, 1.0, true, topFaces_);
            makeFace(pc, 0.0, false, -1.0, true, bottomFaces_);
            addWalls(pc.outer, true, topFaces_);
            for (const fc::Ring& hole : pc.holes) addWalls(hole, true, topFaces_);
        }
    }

    if (board_.valid) {
        const double cornerX[4] = {board_.xmin, board_.xmax, board_.xmax, board_.xmin};
        const double cornerY[4] = {board_.ymin, board_.ymin, board_.ymax, board_.ymax};
        fc::PolygonContours outline;
        for (int i = 0; i < 4; ++i) outline.outer.push_back({cornerX[i], cornerY[i]});
        makeFace(outline, 0.0, true, 1.0, false, boardFaces_);
        makeFace(outline, -thickness_, false, -1.0, false, boardFaces_);
        for (int i = 0; i < 4; ++i) {
            const int j = (i + 1) % 4;
            const double dx = cornerX[j] - cornerX[i];
            const double dy = cornerY[j] - cornerY[i];
            const double length = std::hypot(dx, dy);
            if (length < 1e-9) continue;
            Face wall;
            wall.top = false;
            wall.copper = false;
            wall.points.push_back(project(cornerX[i], cornerY[i], -thickness_, nullptr));
            wall.points.push_back(project(cornerX[j], cornerY[j], -thickness_, nullptr));
            wall.points.push_back(project(cornerX[j], cornerY[j], 0.0, nullptr));
            wall.points.push_back(project(cornerX[i], cornerY[i], 0.0, nullptr));
            double depth = 0.0;
            project((cornerX[i] + cornerX[j]) / 2.0, (cornerY[i] + cornerY[j]) / 2.0,
                    -thickness_ / 2.0, &depth);
            wall.depth = depth;
            wall.shade = shade(dy / length, -dx / length, 0.0);
            boardFaces_.push_back(std::move(wall));
        }
    }

    auto byDepth = [](const Face& a, const Face& b) { return a.depth < b.depth; };
    std::sort(topFaces_.begin(), topFaces_.end(), byDepth);
    std::sort(bottomFaces_.begin(), bottomFaces_.end(), byDepth);
    std::sort(boardFaces_.begin(), boardFaces_.end(), byDepth);
    fitView();
}

/**
 * @brief Encaja la escena proyectada en el área útil del diálogo.
 *
 * Recorre las caras ya proyectadas para obtener la caja 2D, reserva 80 px de
 * margen horizontal y 120 px vertical, y calcula escala y desplazamiento para
 * centrar el dibujo con un pequeño sesgo hacia arriba (30 px).
 *
 * @note Si no hay caras (caja vacía) retorna sin tocar la vista. Como se llama
 *       desde rebuildFaces, la escala se recalcula en cada rotación: el zoom
 *       manual se conserva (vive en `zoom_`) pero el centro siempre queda en el
 *       centro del diálogo.
 */
void View3D::fitView() {
    double xmin = 1e18, ymin = 1e18, xmax = -1e18, ymax = -1e18;
    /// Extiende la caja 2D de encuadre con los puntos proyectados de la lista.
    auto extend = [&](const std::vector<Face>& faces) {
        for (const Face& f : faces)
            for (const QPointF& p : f.points) {
                xmin = std::min(xmin, p.x());
                xmax = std::max(xmax, p.x());
                ymin = std::min(ymin, p.y());
                ymax = std::max(ymax, p.y());
            }
    };
    extend(topFaces_);
    extend(bottomFaces_);
    extend(boardFaces_);
    if (xmin > xmax) return;
    const double w = std::max(1.0, width() - 80.0);
    const double h = std::max(1.0, height() - 120.0);
    scale_ = std::min(w / std::max(1e-6, xmax - xmin), h / std::max(1e-6, ymax - ymin));
    offset_ = QPointF(width() / 2.0 - (xmin + xmax) / 2.0 * scale_,
                      height() / 2.0 + 30.0 - (ymin + ymax) / 2.0 * scale_);
}

/**
 * @brief Dibuja la escena 3D completa sobre el diálogo.
 *
 * Pinta el fondo oscuro y, si no hay caras, un mensaje. Después dibuja en
 * orden: placa (si está visible), fondo del cobre, tapa del cobre y taladros
 * (siempre que el cobre esté visible). Cada cara se convierte a `QPainterPath`
 * con sus agujeros y regla par-impar, se rellena con el color base modulado por
 * el sombreado (las caras no superiores se oscurecen un 18 %) y se perfila con
 * un trazo fino para separar planos.
 *
 * @param event Evento de pintado (no se usa; se redibuja todo).
 * @note Usa alfa 230-235, por lo que el orden de dibujado importa. Los taladros
 *       se proyectan a `copperThickness_ + 0.02` mm para quedar por encima del
 *       cobre y evitar que el relleno los tape.
 */
void View3D::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(16, 20, 24));
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (topFaces_.empty() && boardFaces_.empty()) {
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, tr("Sin geometría visible"));
        return;
    }

    /// Convierte un punto del espacio de vista a píxeles del diálogo.
    auto screen = [&](const QPointF& p) {
        return QPointF(offset_.x() + p.x() * scale_ * zoom_,
                       offset_.y() + p.y() * scale_ * zoom_);
    };

    /// Aplica el factor de sombreado al color base con saturación a 0..255.
    auto shaded = [](const QColor& base, double shade, int alpha) {
        return QColor(std::clamp(static_cast<int>(base.red() * shade), 0, 255),
                      std::clamp(static_cast<int>(base.green() * shade), 0, 255),
                      std::clamp(static_cast<int>(base.blue() * shade), 0, 255), alpha);
    };

    /// Dibuja una lista de caras ya ordenada, con agujeros par-impar y perfil.
    auto drawFaces = [&](const std::vector<Face>& faces, const QColor& base, int alpha,
                         const QColor& edge) {
        for (const Face& face : faces) {
            QPainterPath path;
            QPolygonF outer;
            for (const QPointF& p : face.points) outer << screen(p);
            path.addPolygon(outer);
            for (const std::vector<QPointF>& hole : face.holes) {
                QPolygonF inner;
                for (const QPointF& p : hole) inner << screen(p);
                path.addPolygon(inner);
            }
            path.setFillRule(Qt::OddEvenFill);
            const double shade = face.top ? face.shade : face.shade * 0.82;
            painter.setBrush(shaded(base, shade, alpha));
            painter.setPen(QPen(edge, 0.5));
            painter.drawPath(path);
        }
    };

    if (showBoard_ && board_.valid) {
        drawFaces(boardFaces_, QColor(45, 105, 65), 235, QColor(28, 66, 43));
    }

    if (showCopper_) {
        drawFaces(bottomFaces_, QColor(92, 124, 156), 230, QColor(140, 175, 205));
        drawFaces(topFaces_, QColor(217, 164, 65), 235, QColor(240, 196, 106));

        if (!holes_.empty()) {
            for (const fc::Geom& hole : holes_) {
                if (!hole.valid() || hole.empty()) continue;
                for (const fc::PolygonContours& pc : hole.polygons()) {
                    if (pc.outer.size() < 3) continue;
                    QPainterPath path;
                    QPolygonF poly;
                    for (const fc::Pt& p : pc.outer) {
                        const QPointF q = project(p.x, p.y, copperThickness_ + 0.02, nullptr);
                        poly << screen(q);
                    }
                    path.addPolygon(poly);
                    for (const fc::Ring& ring : pc.holes) {
                        QPolygonF inner;
                        for (const fc::Pt& p : ring) {
                            const QPointF q = project(p.x, p.y, copperThickness_ + 0.02, nullptr);
                            inner << screen(q);
                        }
                        path.addPolygon(inner);
                    }
                    path.setFillRule(Qt::OddEvenFill);
                    painter.setPen(QPen(QColor(20, 26, 30), 0.7));
                    painter.setBrush(QColor(18, 22, 26));
                    painter.drawPath(path);
                }
            }
        }
    }

    painter.setPen(QColor(200, 210, 220));
    painter.drawText(10, height() - 12,
                     tr("Arrastrar: rotar · Rueda: zoom · Espesor de placa arriba"));
}

/**
 * @brief Inicia la rotación con el botón izquierdo.
 *
 * @param event Evento de presión; otros botones se ignoran.
 */
void View3D::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragging_ = true;
        lastMouse_ = event->pos();
    }
}

/**
 * @brief Rota la escena siguiendo el arrastre del ratón.
 *
 * El desplazamiento horizontal (sin límite) suma a la guiñada y el vertical a
 * la inclinación, que se limita al rango [0.15, 1.5] rad para no cruzar el
 * cénit ni el horizonte. Tras cada movimiento se reconstruyen las caras.
 *
 * @param event Evento de movimiento.
 * @note El factor 0.01 rad/px equivale a unos 0.57° por píxel.
 */
void View3D::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_) return;
    const QPoint delta = event->pos() - lastMouse_;
    lastMouse_ = event->pos();
    yaw_ += delta.x() * 0.01;
    pitch_ = std::clamp(pitch_ + delta.y() * 0.01, 0.15, 1.5);
    rebuildFaces();
    update();
}

/**
 * @brief Acerca o aleja la escena con la rueda.
 *
 * Multiplica `zoom_` por 1.1 o su inverso según el signo del giro y lo limita a
 * [0.1, 20] para evitar escalas degeneradas.
 *
 * @param event Evento de rueda.
 * @note No se ancla al cursor (el zoom es siempre respecto del centro del
 *       diálogo) y se ignora la magnitud del delta.
 */
void View3D::wheelEvent(QWheelEvent* event) {
    zoom_ *= event->angleDelta().y() > 0 ? 1.1 : 1.0 / 1.1;
    zoom_ = std::clamp(zoom_, 0.1, 20.0);
    update();
}
