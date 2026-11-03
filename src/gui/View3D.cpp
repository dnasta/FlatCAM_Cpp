#include "View3D.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QDoubleSpinBox>
#include <QSettings>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

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
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Guardar vista 3D"), QFileInfo::exists(".") ? "vista3d.png" : "vista3d.png",
            tr("PNG (*.png)"));
        if (path.isEmpty()) return;
        grab().save(path, "PNG");
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

void View3D::rebuildFaces() {
    topFaces_.clear();
    bottomFaces_.clear();
    boardFaces_.clear();

    const double cyaw = std::cos(yaw_);
    const double syaw = std::sin(yaw_);
    const double cp = std::cos(pitch_);
    const double sp = std::sin(pitch_);

    auto shade = [&](double nx, double ny, double nz) {
        const double rx = nx * cyaw - ny * syaw;
        const double ry = nx * syaw + ny * cyaw;
        const double sy = ry * sp - nz * cp;
        const double dz = ry * cp + nz * sp;
        const double lambert = std::fabs(rx * 0.25 - sy * 0.35 + dz * 0.90);
        return std::clamp(0.35 + 0.65 * lambert, 0.25, 1.05);
    };

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

void View3D::fitView() {
    double xmin = 1e18, ymin = 1e18, xmax = -1e18, ymax = -1e18;
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

void View3D::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(16, 20, 24));
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (topFaces_.empty() && boardFaces_.empty()) {
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, tr("Sin geometría visible"));
        return;
    }

    auto screen = [&](const QPointF& p) {
        return QPointF(offset_.x() + p.x() * scale_ * zoom_,
                       offset_.y() + p.y() * scale_ * zoom_);
    };

    auto shaded = [](const QColor& base, double shade, int alpha) {
        return QColor(std::clamp(static_cast<int>(base.red() * shade), 0, 255),
                      std::clamp(static_cast<int>(base.green() * shade), 0, 255),
                      std::clamp(static_cast<int>(base.blue() * shade), 0, 255), alpha);
    };

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

void View3D::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragging_ = true;
        lastMouse_ = event->pos();
    }
}

void View3D::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_) return;
    const QPoint delta = event->pos() - lastMouse_;
    lastMouse_ = event->pos();
    yaw_ += delta.x() * 0.01;
    pitch_ = std::clamp(pitch_ + delta.y() * 0.01, 0.15, 1.5);
    rebuildFaces();
    update();
}

void View3D::wheelEvent(QWheelEvent* event) {
    zoom_ *= event->angleDelta().y() > 0 ? 1.1 : 1.0 / 1.1;
    zoom_ = std::clamp(zoom_, 0.1, 20.0);
    update();
}
