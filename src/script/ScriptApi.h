#pragma once

#include <QObject>

#include <functional>
#include <vector>
#include <QString>

#include "flatcam/excellon.hpp"
#include "flatcam/gerber.hpp"
#include "flatcam/gcode.hpp"

class QJSEngine;

namespace fc {

class ScriptApi : public QObject {
    Q_OBJECT

public:
    explicit ScriptApi(QObject* parent = nullptr);

    Q_INVOKABLE bool openGerber(const QString& path);
    Q_INVOKABLE bool openExcellon(const QString& path);
    Q_INVOKABLE int isolate(double toolDiameter, int passes = 1, double overlapPercent = 0.0);
    Q_INVOKABLE int cutout(double toolDiameter, double margin = 0.0, int tabs = 0,
                           double tabWidth = 0.0);
    Q_INVOKABLE int ncc(double toolDiameter, double stepOverPercent = 50.0);
    Q_INVOKABLE int paint(double toolDiameter, double stepOverPercent = 50.0);
    Q_INVOKABLE int mill(double toolDiameter);
    Q_INVOKABLE int extractDrills(double maxDiameter = 3.0);
    Q_INVOKABLE int punch(double margin = 0.0);
    Q_INVOKABLE int panelize(int rows, int cols, double gapX = 2.0, double gapY = 2.0,
                             double angle = 0.0);
    Q_INVOKABLE int qrCode(const QString& text, double module = 1.0, int border = 4);
    Q_INVOKABLE int cornerMarks(double length = 3.0, double margin = 0.0);
    Q_INVOKABLE double optimalToolDiameter(double maxDiameter = 10.0);
    Q_INVOKABLE int rulesCheck(double clearance = 0.2);
    Q_INVOKABLE bool exportGcode(const QString& path);
    Q_INVOKABLE QString info() const;
    Q_INVOKABLE QString lastError() const { return error_; }
    Q_INVOKABLE void log(const QString& text);

    using LayerSink = std::function<void(const QString&, const QString&,
                                        const std::vector<Geom>&, const std::vector<Geom>&)>;
    void setLayerSink(LayerSink sink) { sink_ = std::move(sink); }

private:
    GerberResult gerber_;
    bool hasGerber_ = false;
    ExcellonResult excellon_;
    bool hasExcellon_ = false;
    PathJob paths_;
    DrillJob drills_;
    bool hasPathJob_ = false;
    bool hasDrillJob_ = false;
    QString error_;
    LayerSink sink_;
};

void installScriptApi(QJSEngine& engine, ScriptApi* api);

}  // namespace fc
