#include "Canvas.h"

#include <QMouseEvent>
#include <QPainterPathStroker>
#include <QPainter>
#include <QPainterPath>
#include <QSvgGenerator>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>

Canvas::Canvas(QWidget* parent) : QWidget(parent) {
    setMinimumSize(400, 300);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setBackgroundRole(QPalette::Base);
}

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

QPainterPath Canvas::buildLinePath(const std::vector<fc::Geom>& geoms) {
    QPainterPath path;
    for (const fc::Geom& g : geoms) appendLinePath(path, g);
    return path;
}

Canvas::Layer* Canvas::findLayer(int id) {
    for (Layer& layer : layers_) {
        if (layer.id == id) return &layer;
    }
    return nullptr;
}

const Canvas::Layer* Canvas::findLayer(int id) const {
    for (const Layer& layer : layers_) {
        if (layer.id == id) return &layer;
    }
    return nullptr;
}

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

void Canvas::beginGeometry() {
    layers_.clear();
    bounds_ = fc::BBox();
    hasData_ = false;
}

int Canvas::appendGeometry(const std::vector<fc::Geom>& solids,
                           const std::vector<fc::Geom>& follows, bool keepGeometry) {
    Layer layer;
    layer.id = nextId_++;
    layer.solid = buildPath(solids);
    layer.follow = buildLinePath(follows);
    layer.solids = solids;
    layer.follows = follows;
    layers_.push_back(std::move(layer));

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

void Canvas::endGeometry() {
    fit();
    update();
}

std::vector<int> Canvas::layerIds() const {
    std::vector<int> ids;
    ids.reserve(layers_.size());
    for (const Layer& layer : layers_) ids.push_back(layer.id);
    return ids;
}

void Canvas::setLayerOrder(const std::vector<int>& ids) {
    std::vector<Layer> ordered;
    ordered.reserve(layers_.size());
    for (int id : ids) {
        for (const Layer& layer : layers_) {
            if (layer.id == id) {
                ordered.push_back(layer);
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

void Canvas::setLayerVisible(int id, bool visible) {
    Layer* layer = findLayer(id);
    if (!layer) return;
    layer->visible = visible;
    update();
}

void Canvas::removeLayer(int id) {
    layers_.erase(std::remove_if(layers_.begin(), layers_.end(),
                                 [id](const Layer& layer) { return layer.id == id; }),
                  layers_.end());
    update();
}

int Canvas::duplicateLayer(int id) {
    const Layer* source = findLayer(id);
    if (!source) return -1;
    Layer copy = *source;
    copy.id = nextId_++;
    layers_.push_back(copy);
    update();
    return copy.id;
}

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
    update();
}

std::vector<fc::Geom> Canvas::layerSolids(int id) const {
    const Layer* layer = findLayer(id);
    return layer ? layer->solids : std::vector<fc::Geom>{};
}

std::vector<fc::Geom> Canvas::layerFollows(int id) const {
    const Layer* layer = findLayer(id);
    return layer ? layer->follows : std::vector<fc::Geom>{};
}

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

int Canvas::mergeVisibleLayers() {
    QPainterPath mergedSolid;
    QPainterPath mergedFollow;
    for (const Layer& layer : layers_) {
        if (!layer.visible) continue;
        mergedSolid = mergedSolid.united(layer.solid);
        mergedFollow.addPath(layer.follow);
    }
    if (mergedSolid.isEmpty() && mergedFollow.isEmpty()) return -1;

    Layer layer;
    layer.id = nextId_++;
    layer.solid = mergedSolid;
    layer.follow = mergedFollow;
    layers_.push_back(std::move(layer));
    update();
    return layer.id;
}

void Canvas::setLayerFilled(int id, bool filled) {
    Layer* layer = findLayer(id);
    if (!layer) return;
    layer->filled = filled;
    update();
}

bool Canvas::layerFilled(int id) const {
    const Layer* layer = findLayer(id);
    return layer ? layer->filled : true;
}

bool Canvas::layerMulticolor(int id) const {
    const Layer* layer = findLayer(id);
    return layer ? layer->multicolor : false;
}

void Canvas::setLayerMulticolor(int id, bool multicolor) {
    Layer* layer = findLayer(id);
    if (!layer) return;
    layer->multicolor = multicolor;
    update();
}

void Canvas::transformLayer(int id, const QTransform& transform) {
    Layer* layer = findLayer(id);
    if (!layer) return;
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
    update();
}

bool Canvas::exportSvg(const QString& path) const {
    if (!hasData_) return false;

    const double w = std::max(1e-6, bounds_.width());
    const double h = std::max(1e-6, bounds_.height());
    const int pixelWidth = 1400;
    const int pixelHeight = std::max(1, static_cast<int>(std::lround(pixelWidth * h / w)));

    QSvgGenerator generator;
    generator.setFileName(path);
    generator.setSize(QSize(pixelWidth, pixelHeight));
    generator.setViewBox(QRect(static_cast<int>(std::floor(bounds_.xmin)),
                               static_cast<int>(std::floor(bounds_.ymin)),
                               static_cast<int>(std::ceil(w)), static_cast<int>(std::ceil(h))));
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

void Canvas::clearData() {
    layers_.clear();
    toolpathPath_ = QPainterPath();
    bounds_ = fc::BBox();
    hasData_ = false;
    update();
}

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

void Canvas::setColors(const QColor& background, const QColor& copper, const QColor& follow,
                       const QColor& toolpath) {
    background_ = background;
    copperFill_ = copper;
    copperEdge_ = copper.lighter(130);
    followColor_ = follow;
    toolpathColor_ = toolpath;
    update();
}

std::vector<fc::Geom> Canvas::visibleSolids() const {
    std::vector<fc::Geom> out;
    for (const Layer& layer : layers_) {
        if (!layer.visible) continue;
        out.insert(out.end(), layer.solids.begin(), layer.solids.end());
    }
    return out;
}

void Canvas::setView(double scale, const QPointF& offset) {
    if (scale <= 0.0) return;
    scale_ = scale;
    offset_ = offset;
    update();
}

void Canvas::centerOn(double x, double y) {
    offset_ = QPointF(width() / 2.0 - x * scale_, height() / 2.0 + y * scale_);
    update();
}

void Canvas::setGridVisible(bool visible) {
    gridVisible_ = visible;
    update();
}

void Canvas::fitTo(const fc::BBox& bounds) {
    if (!bounds.valid) return;
    bounds_ = bounds;
    hasData_ = true;
    fit();
    update();
}

void Canvas::resizeEvent(QResizeEvent*) {
    if (hasData_) fit();
}

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

    if (gridVisible_) {
        double step = gridStep_ > 0.0 ? gridStep_ : 10.0;
        if (gridStep_ <= 0.0) {
            while (step * scale_ < 40.0) step *= 10.0;
            while (step * scale_ > 200.0) step /= 10.0;
        }
        p.setPen(QPen(QColor(38, 46, 54), 0.0));
        const double x0 = std::floor(bounds_.xmin / step) * step;
        const double y0 = std::floor(bounds_.ymin / step) * step;
        for (double x = x0; x <= bounds_.xmax + step; x += step)
            p.drawLine(QPointF(x, bounds_.ymin - step), QPointF(x, bounds_.ymax + step));
        for (double y = y0; y <= bounds_.ymax + step; y += step)
            p.drawLine(QPointF(bounds_.xmin - step, y), QPointF(bounds_.xmax + step, y));
    }

    p.setPen(QPen(QColor(90, 100, 110), 0.0));
    p.drawLine(QPointF(bounds_.xmin, displayOrigin_.y), QPointF(bounds_.xmax, displayOrigin_.y));
    p.drawLine(QPointF(displayOrigin_.x, bounds_.ymin), QPointF(displayOrigin_.x, bounds_.ymax));

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
        p.setPen(QPen(followColor_, 0.0));
        p.drawPath(layer.follow);
    }

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(toolpathColor_, 0.0));
    p.drawPath(toolpathPath_);

    p.setTransform(QTransform());
    if (hudVisible_ && hasCursor_) {
        const QString text = QString("X %1  Y %2")
                                 .arg(cursorWorld_.x(), 0, 'f', 3)
                                 .arg(cursorWorld_.y(), 0, 'f', 3);
        const QRect box(10, 10, 180, 26);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 170));
        p.drawRoundedRect(box, 5, 5);
        p.setPen(QColor(220, 230, 240));
        p.drawText(box.adjusted(8, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
    }
}

void Canvas::setToolpaths(const std::vector<fc::Geom>& paths) {
    toolpathPath_ = buildLinePath(paths);
    update();
}

void Canvas::zoomAt(const QPointF& anchor, double factor) {
    const double wx = (anchor.x() - offset_.x()) / scale_;
    const double wy = -(anchor.y() - offset_.y()) / scale_;
    scale_ *= factor;
    if (scale_ < 1e-9) scale_ = 1e-9;
    offset_ = QPointF(anchor.x() - wx * scale_, anchor.y() + wy * scale_);
    update();
}

void Canvas::zoomIn() {
    zoomAt(QPointF(width() / 2.0, height() / 2.0), 1.25);
}

void Canvas::zoomOut() {
    zoomAt(QPointF(width() / 2.0, height() / 2.0), 0.8);
}

void Canvas::wheelEvent(QWheelEvent* event) {
    const double factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    zoomAt(event->position(), factor);
}

void Canvas::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        panning_ = true;
        pressPos_ = event->pos();
        dragged_ = false;
        lastMouse_ = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

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

void Canvas::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
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
}

void Canvas::leaveEvent(QEvent*) {
    hasCursor_ = false;
    if (hudVisible_) update();
    emit cursorMoved(std::numeric_limits<double>::quiet_NaN(),
                     std::numeric_limits<double>::quiet_NaN());
}
