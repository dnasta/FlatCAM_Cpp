/**
 * @file MainWindow.cpp
 * @brief Implementación de la ventana principal y sus comandos Tcl.
 *
 * Este archivo orquesta:
 * - la construcción de menús, barra, paneles y consolas;
 * - la apertura y el guardado de archivos y proyectos;
 * - el árbol de objetos, las propiedades y las capas del canvas;
 * - los paneles CAM, las tablas de herramientas y la generación de G-code;
 * - los editores y las operaciones geométricas;
 * - el registro de los 71 comandos Tcl.
 *
 * La lógica geométrica/CAM vive en `fc`; aquí sólo se compone la interfaz y se
 * mantiene el estado de la sesión.
 */
#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QSysInfo>
#include <QUrl>
#include <QDesktopServices>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QToolBox>
#include <QToolButton>
#include <QTabWidget>
#include <QToolBar>
#include <QGroupBox>
#include <QDoubleSpinBox>
#include <QUndoCommand>
#include <QUndoStack>
#include <QTransform>
#include <QDialog>
#include <QDockWidget>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QScrollArea>
#include <QLineEdit>
#include <QListWidget>
#include <QTextEdit>
#include <QStyle>
#include <QStyleFactory>
#include <QPalette>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QTimer>
#include <QPainter>
#include <QFont>
#include <QPixmap>
#include <QIcon>
#include <QIconEngine>
#include <QSettings>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QSpinBox>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QMenu>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPdfWriter>
#include <QPrintDialog>
#include <QProcess>
#include <QPrinter>
#include <QImage>
#include <QImageWriter>
#include <QRegularExpression>
#include <QPainterPath>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <memory>
#include <set>
#include <sstream>
#include <utility>

#include "Canvas.h"
#include "Theme.h"
#include "View3D.h"
#include "GCodeEditor.h"
#include "PdfPages.h"
#include "TextGeometry.h"
#include "UnitSpinBox.h"
#include "flatcam/pdf.hpp"
#include "flatcam/importers.hpp"
#include "flatcam/dxf.hpp"
#include "flatcam/thieving.hpp"
#include "flatcam/tools.hpp"
#include "flatcam/qr.hpp"

#ifdef FC_HAVE_TCL
#include <tcl.h>
#endif

using namespace fc;
#include "flatcam/gcode.hpp"
#include "ScriptApi.h"

#include <QJSEngine>
#include <QJSValue>

namespace {

#ifdef FC_HAVE_TCL
/// @name Adaptadores de comandos Tcl
/// Cada `tclCmd*` valida argumentos y delega en un método `tcl*`.
/// @{

int tclCmdExtractDrills(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc < 2 || objc > 2) {
        Tcl_WrongNumArgs(ip, 1, objv, "maxDiameter");
        return TCL_ERROR;
    }
    double maxDia = 3.0;
    Tcl_GetDoubleFromObj(ip, objv[1], &maxDia);
    Tcl_SetObjResult(ip, Tcl_NewIntObj(w->tclExtractDrills(maxDia)));
    return TCL_OK;
}

int tclCmdPunch(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc > 2) {
        Tcl_WrongNumArgs(ip, 1, objv, "?margin?");
        return TCL_ERROR;
    }
    double margin = 0.0;
    if (objc > 1) Tcl_GetDoubleFromObj(ip, objv[1], &margin);
    Tcl_SetObjResult(ip, Tcl_NewIntObj(w->tclPunch(margin)));
    return TCL_OK;
}

int tclCmdPanelize(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc < 3 || objc > 5) {
        Tcl_WrongNumArgs(ip, 1, objv, "rows cols ?gapX? ?gapY?");
        return TCL_ERROR;
    }
    int rows = 1, cols = 1;
    double gapX = 2.0, gapY = 2.0;
    Tcl_GetIntFromObj(ip, objv[1], &rows);
    Tcl_GetIntFromObj(ip, objv[2], &cols);
    if (objc > 3) Tcl_GetDoubleFromObj(ip, objv[3], &gapX);
    if (objc > 4) Tcl_GetDoubleFromObj(ip, objv[4], &gapY);
    Tcl_SetObjResult(ip, Tcl_NewIntObj(w->tclPanelize(rows, cols, gapX, gapY)));
    return TCL_OK;
}

int tclCmdQr(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc < 2 || objc > 4) {
        Tcl_WrongNumArgs(ip, 1, objv, "text ?module? ?border?");
        return TCL_ERROR;
    }
    double module = 1.0;
    int border = 4;
    if (objc > 2) Tcl_GetDoubleFromObj(ip, objv[2], &module);
    if (objc > 3) Tcl_GetIntFromObj(ip, objv[3], &border);
    const int n = w->tclQr(QString::fromUtf8(Tcl_GetString(objv[1])), module, border);
    Tcl_SetObjResult(ip, Tcl_NewIntObj(n));
    return TCL_OK;
}

int tclCmdCorners(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc < 2 || objc > 3) {
        Tcl_WrongNumArgs(ip, 1, objv, "length ?margin?");
        return TCL_ERROR;
    }
    double length = 3.0, margin = 0.0;
    Tcl_GetDoubleFromObj(ip, objv[1], &length);
    if (objc > 2) Tcl_GetDoubleFromObj(ip, objv[2], &margin);
    Tcl_SetObjResult(ip, Tcl_NewIntObj(w->tclCorners(length, margin)));
    return TCL_OK;
}

int tclCmdOptimal(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc < 2 || objc > 2) {
        Tcl_WrongNumArgs(ip, 1, objv, "maxDiameter");
        return TCL_ERROR;
    }
    double maxDia = 10.0;
    Tcl_GetDoubleFromObj(ip, objv[1], &maxDia);
    Tcl_SetObjResult(ip, Tcl_NewDoubleObj(w->tclOptimal(maxDia)));
    return TCL_OK;
}

int tclCmdNew(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 2) {
        Tcl_WrongNumArgs(ip, 1, objv, "name");
        return TCL_ERROR;
    }
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(
        w->tclNew(QString::fromUtf8(Tcl_GetString(objv[1])))));
    return TCL_OK;
}

int tclCmdRings(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[], bool holes) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 2) {
        Tcl_WrongNumArgs(ip, 1, objv, "objectName");
        return TCL_ERROR;
    }
    Tcl_SetObjResult(ip, Tcl_NewIntObj(
        w->tclRings(QString::fromUtf8(Tcl_GetString(objv[1])), holes)));
    return TCL_OK;
}

int tclCmdExteriors(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    return tclCmdRings(cd, ip, objc, objv, false);
}

int tclCmdInteriors(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    return tclCmdRings(cd, ip, objc, objv, true);
}

int tclCmdJoin(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 3) {
        Tcl_WrongNumArgs(ip, 1, objv, "nameA nameB");
        return TCL_ERROR;
    }
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(
        w->tclJoin(QString::fromUtf8(Tcl_GetString(objv[1])),
                   QString::fromUtf8(Tcl_GetString(objv[2])))));
    return TCL_OK;
}

int tclCmdGeoUnion(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 3) {
        Tcl_WrongNumArgs(ip, 1, objv, "nameA nameB");
        return TCL_ERROR;
    }
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(
        w->tclGeoUnion(QString::fromUtf8(Tcl_GetString(objv[1])),
                       QString::fromUtf8(Tcl_GetString(objv[2])))));
    return TCL_OK;
}

int tclCmdExportExcellon(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 2) {
        Tcl_WrongNumArgs(ip, 1, objv, "path");
        return TCL_ERROR;
    }
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(
        w->tclExportExcellon(QString::fromUtf8(Tcl_GetString(objv[1])))));
    return TCL_OK;
}

int tclCmdPlotObjects(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc < 2) {
        Tcl_WrongNumArgs(ip, 1, objv, "name ?name ...?");
        return TCL_ERROR;
    }
    QStringList names;
    for (int i = 1; i < objc; ++i)
        names << QString::fromUtf8(Tcl_GetString(objv[i]));
    w->tclPlotObjects(names);
    return TCL_OK;
}

int tclCmdQuit(ClientData cd, Tcl_Interp*, int, Tcl_Obj* const[]) {
    if (auto* w = static_cast<MainWindow*>(cd)) w->close();
    else QCoreApplication::quit();
    return TCL_OK;
}

int tclCmdNewGerber(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 2) {
        Tcl_WrongNumArgs(ip, 1, objv, "name");
        return TCL_ERROR;
    }
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(
        w->tclNewObject(QString::fromUtf8(Tcl_GetString(objv[1])), "gerber")));
    return TCL_OK;
}

int tclCmdNewExcellon(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 2) {
        Tcl_WrongNumArgs(ip, 1, objv, "name");
        return TCL_ERROR;
    }
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(
        w->tclNewObject(QString::fromUtf8(Tcl_GetString(objv[1])), "excellon")));
    return TCL_OK;
}

int tclCmdNewGeometry(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 2) {
        Tcl_WrongNumArgs(ip, 1, objv, "name");
        return TCL_ERROR;
    }
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(
        w->tclNewObject(QString::fromUtf8(Tcl_GetString(objv[1])), "geometry")));
    return TCL_OK;
}

int tclCmdNewObject(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 3) {
        Tcl_WrongNumArgs(ip, 1, objv, "kind name");
        return TCL_ERROR;
    }
    const QString kind = QString::fromUtf8(Tcl_GetString(objv[1]));
    const QString name = QString::fromUtf8(Tcl_GetString(objv[2]));
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(w->tclNewObject(name, kind)));
    return TCL_OK;
}

int tclCmdAddCircle(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 5) {
        Tcl_WrongNumArgs(ip, 1, objv, "name cx cy diameter");
        return TCL_ERROR;
    }
    double cx = 0, cy = 0, dia = 0;
    Tcl_GetDoubleFromObj(ip, objv[2], &cx);
    Tcl_GetDoubleFromObj(ip, objv[3], &cy);
    Tcl_GetDoubleFromObj(ip, objv[4], &dia);
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(
        w->tclAddCircle(QString::fromUtf8(Tcl_GetString(objv[1])), cx, cy, dia)));
    return TCL_OK;
}

int tclCmdAddRectangle(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 6) {
        Tcl_WrongNumArgs(ip, 1, objv, "name x1 y1 x2 y2");
        return TCL_ERROR;
    }
    double v[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; ++i) Tcl_GetDoubleFromObj(ip, objv[2 + i], &v[i]);
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(
        w->tclAddRectangle(QString::fromUtf8(Tcl_GetString(objv[1])), v[0], v[1], v[2], v[3])));
    return TCL_OK;
}

int tclCmdAddPolygon(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc < 8 || (objc - 2) % 2 != 0) {
        Tcl_WrongNumArgs(ip, 1, objv, "name x1 y1 x2 y2 x3 y3 ?x4 y4 ...?");
        return TCL_ERROR;
    }
    std::vector<double> coords;
    for (int i = 2; i < objc; ++i) {
        double value = 0;
        Tcl_GetDoubleFromObj(ip, objv[i], &value);
        coords.push_back(value);
    }
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(
        w->tclAddPolygon(QString::fromUtf8(Tcl_GetString(objv[1])), coords)));
    return TCL_OK;
}

int tclCmdAddPolyline(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc < 6 || (objc - 2) % 2 != 0) {
        Tcl_WrongNumArgs(ip, 1, objv, "name x1 y1 x2 y2 ?x3 y3 ...?");
        return TCL_ERROR;
    }
    std::vector<double> coords;
    for (int i = 2; i < objc; ++i) {
        double value = 0;
        Tcl_GetDoubleFromObj(ip, objv[i], &value);
        coords.push_back(value);
    }
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(
        w->tclAddPolyline(QString::fromUtf8(Tcl_GetString(objv[1])), coords)));
    return TCL_OK;
}

int tclCmdGetPath(ClientData cd, Tcl_Interp* ip, int, Tcl_Obj* const[]) {
    auto* w = static_cast<MainWindow*>(cd);
    Tcl_SetObjResult(ip, Tcl_NewStringObj(w->tclGetPath().toUtf8().constData(), -1));
    return TCL_OK;
}

int tclCmdSetPath(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 2) { Tcl_WrongNumArgs(ip, 1, objv, "path"); return TCL_ERROR; }
    w->tclSetPath(QString::fromUtf8(Tcl_GetString(objv[1])));
    return TCL_OK;
}

int tclCmdGetSys(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 2) { Tcl_WrongNumArgs(ip, 1, objv, "key"); return TCL_ERROR; }
    Tcl_SetObjResult(ip, Tcl_NewStringObj(
        w->tclGetSys(QString::fromUtf8(Tcl_GetString(objv[1]))).toUtf8().constData(), -1));
    return TCL_OK;
}

int tclCmdSetSys(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 3) { Tcl_WrongNumArgs(ip, 1, objv, "key value"); return TCL_ERROR; }
    w->tclSetSys(QString::fromUtf8(Tcl_GetString(objv[1])),
                 QString::fromUtf8(Tcl_GetString(objv[2])));
    return TCL_OK;
}

int tclCmdListSys(ClientData cd, Tcl_Interp* ip, int, Tcl_Obj* const[]) {
    auto* w = static_cast<MainWindow*>(cd);
    Tcl_SetObjResult(ip, Tcl_NewStringObj(w->tclListSys().toUtf8().constData(), -1));
    return TCL_OK;
}

int tclCmdSaveSys(ClientData cd, Tcl_Interp* ip, int, Tcl_Obj* const[]) {
    static_cast<MainWindow*>(cd)->tclSaveSys();
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(1));
    return TCL_OK;
}

int tclCmdOptions(ClientData cd, Tcl_Interp* ip, int, Tcl_Obj* const[]) {
    auto* w = static_cast<MainWindow*>(cd);
    Tcl_SetObjResult(ip, Tcl_NewStringObj(w->tclOptions().toUtf8().constData(), -1));
    return TCL_OK;
}

int tclCmdNRegions(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 2) { Tcl_WrongNumArgs(ip, 1, objv, "name"); return TCL_ERROR; }
    Tcl_SetObjResult(ip, Tcl_NewIntObj(
        w->tclNRegions(QString::fromUtf8(Tcl_GetString(objv[1])))));
    return TCL_OK;
}

/// @brief Convierte una lista de objetos Tcl en doubles a partir de `from`.
static std::vector<double> tclDoubles(Tcl_Interp* ip, int objc, Tcl_Obj* const objv[], int from) {
    std::vector<double> values;
    for (int i = from; i < objc; ++i) {
        double value = 0;
        Tcl_GetDoubleFromObj(ip, objv[i], &value);
        values.push_back(value);
    }
    return values;
}

int tclCmdSubtractPoly(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc < 8) { Tcl_WrongNumArgs(ip, 1, objv, "name x1 y1 x2 y2 x3 y3 ..."); return TCL_ERROR; }
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(
        w->tclSubtractShape(QString::fromUtf8(Tcl_GetString(objv[1])),
                            tclDoubles(ip, objc, objv, 2), false)));
    return TCL_OK;
}

int tclCmdSubtractRect(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 6) { Tcl_WrongNumArgs(ip, 1, objv, "name x1 y1 x2 y2"); return TCL_ERROR; }
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(
        w->tclSubtractShape(QString::fromUtf8(Tcl_GetString(objv[1])),
                            tclDoubles(ip, objc, objv, 2), true)));
    return TCL_OK;
}

int tclCmdFollow(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc < 2 || objc > 3) { Tcl_WrongNumArgs(ip, 1, objv, "name ?diameter?"); return TCL_ERROR; }
    double dia = 0.2;
    if (objc > 2) Tcl_GetDoubleFromObj(ip, objv[2], &dia);
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(
        w->tclFollow(QString::fromUtf8(Tcl_GetString(objv[1])), dia)));
    return TCL_OK;
}

int tclCmdAlignDrill(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc < 2 || objc > 3) { Tcl_WrongNumArgs(ip, 1, objv, "name ?reference?"); return TCL_ERROR; }
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(
        w->tclAlignDrill(QString::fromUtf8(Tcl_GetString(objv[1])),
                         objc > 2 ? QString::fromUtf8(Tcl_GetString(objv[2])) : QString())));
    return TCL_OK;
}

int tclCmdAlignDrillGrid(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 3) { Tcl_WrongNumArgs(ip, 1, objv, "name grid"); return TCL_ERROR; }
    double grid = 1.0;
    Tcl_GetDoubleFromObj(ip, objv[2], &grid);
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(
        w->tclAlignDrillGrid(QString::fromUtf8(Tcl_GetString(objv[1])), grid)));
    return TCL_OK;
}

int tclCmdCncJob(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc > 2) { Tcl_WrongNumArgs(ip, 1, objv, "?name?"); return TCL_ERROR; }
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(
        w->tclCreateCncJob(objc > 1 ? QString::fromUtf8(Tcl_GetString(objv[1])) : QString(), false)));
    return TCL_OK;
}

int tclCmdDrillCncJob(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc > 2) { Tcl_WrongNumArgs(ip, 1, objv, "?name?"); return TCL_ERROR; }
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(
        w->tclCreateCncJob(objc > 1 ? QString::fromUtf8(Tcl_GetString(objv[1])) : QString(), true)));
    return TCL_OK;
}

int tclCmdCopperClear(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc < 2 || objc > 3) { Tcl_WrongNumArgs(ip, 1, objv, "diameter ?overlap?"); return TCL_ERROR; }
    double dia = 0.5, overlap = 80.0;
    Tcl_GetDoubleFromObj(ip, objv[1], &dia);
    if (objc > 2) Tcl_GetDoubleFromObj(ip, objv[2], &overlap);
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(w->applyNcc(dia, overlap, 0.1, 0.0, 1.0, false)));
    return TCL_OK;
}

int tclCmdGeoCutout(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc < 2 || objc > 3) { Tcl_WrongNumArgs(ip, 1, objv, "diameter ?margin?"); return TCL_ERROR; }
    double dia = 1.2, margin = 0.0;
    Tcl_GetDoubleFromObj(ip, objv[1], &dia);
    if (objc > 2) Tcl_GetDoubleFromObj(ip, objv[2], &margin);
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(w->applyCutout(dia, margin, true)));
    return TCL_OK;
}

int tclCmdMillDrills(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc < 2 || objc > 3) { Tcl_WrongNumArgs(ip, 1, objv, "name ?diameter?"); return TCL_ERROR; }
    double dia = 0.8;
    if (objc > 2) Tcl_GetDoubleFromObj(ip, objv[2], &dia);
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(
        w->tclMillDrills(QString::fromUtf8(Tcl_GetString(objv[1])), dia)));
    return TCL_OK;
}

int tclCmdMillSlots(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc < 2 || objc > 3) { Tcl_WrongNumArgs(ip, 1, objv, "name ?diameter?"); return TCL_ERROR; }
    double dia = 0.8;
    if (objc > 2) Tcl_GetDoubleFromObj(ip, objv[2], &dia);
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(
        w->tclMillSlots(QString::fromUtf8(Tcl_GetString(objv[1])), dia)));
    return TCL_OK;
}

int tclCmdVersion(ClientData cd, Tcl_Interp* ip, int, Tcl_Obj* const[]) {
    auto* w = static_cast<MainWindow*>(cd);
    Tcl_SetObjResult(ip, Tcl_NewStringObj(w->tclVersion().toUtf8().constData(), -1));
    return TCL_OK;
}

int tclCmdHelp(ClientData cd, Tcl_Interp* ip, int, Tcl_Obj* const[]) {
    auto* w = static_cast<MainWindow*>(cd);
    Tcl_SetObjResult(ip, Tcl_NewStringObj(w->tclHelp().toUtf8().constData(), -1));
    return TCL_OK;
}

int tclCmdGetNames(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc > 2) {
        Tcl_WrongNumArgs(ip, 1, objv, "?kind?");
        return TCL_ERROR;
    }
    const QString kind = objc > 1 ? QString::fromUtf8(Tcl_GetString(objv[1])) : QString();
    Tcl_SetObjResult(ip, Tcl_NewStringObj(w->tclGetNames(kind).toUtf8().constData(), -1));
    return TCL_OK;
}

int tclCmdBBox(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 2) {
        Tcl_WrongNumArgs(ip, 1, objv, "objectName");
        return TCL_ERROR;
    }
    const QString result = w->tclBBox(QString::fromUtf8(Tcl_GetString(objv[1])));
    Tcl_SetObjResult(ip, Tcl_NewStringObj(result.toUtf8().constData(), -1));
    return TCL_OK;
}

int tclCmdSetActive(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 2) {
        Tcl_WrongNumArgs(ip, 1, objv, "objectName");
        return TCL_ERROR;
    }
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(
        w->tclSetActive(QString::fromUtf8(Tcl_GetString(objv[1])))));
    return TCL_OK;
}

int tclCmdOffset(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 4) {
        Tcl_WrongNumArgs(ip, 1, objv, "objectName dx dy");
        return TCL_ERROR;
    }
    double dx = 0.0, dy = 0.0;
    Tcl_GetDoubleFromObj(ip, objv[2], &dx);
    Tcl_GetDoubleFromObj(ip, objv[3], &dy);
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(
        w->tclOffset(QString::fromUtf8(Tcl_GetString(objv[1])), dx, dy)));
    return TCL_OK;
}

int tclCmdScale(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 3) {
        Tcl_WrongNumArgs(ip, 1, objv, "objectName factor");
        return TCL_ERROR;
    }
    double factor = 1.0;
    Tcl_GetDoubleFromObj(ip, objv[2], &factor);
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(
        w->tclScale(QString::fromUtf8(Tcl_GetString(objv[1])), factor)));
    return TCL_OK;
}

int tclCmdMirror(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 3) {
        Tcl_WrongNumArgs(ip, 1, objv, "objectName x|y");
        return TCL_ERROR;
    }
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(
        w->tclMirror(QString::fromUtf8(Tcl_GetString(objv[1])),
                     QString::fromUtf8(Tcl_GetString(objv[2])))));
    return TCL_OK;
}

int tclCmdDelete(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 2) {
        Tcl_WrongNumArgs(ip, 1, objv, "objectName");
        return TCL_ERROR;
    }
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(
        w->tclDelete(QString::fromUtf8(Tcl_GetString(objv[1])))));
    return TCL_OK;
}

int tclCmdSaveProject(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 2) {
        Tcl_WrongNumArgs(ip, 1, objv, "path");
        return TCL_ERROR;
    }
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(
        w->tclSaveProject(QString::fromUtf8(Tcl_GetString(objv[1])))));
    return TCL_OK;
}

int tclCmdExportDxf(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 2) {
        Tcl_WrongNumArgs(ip, 1, objv, "path");
        return TCL_ERROR;
    }
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(
        w->tclExportDxf(QString::fromUtf8(Tcl_GetString(objv[1])))));
    return TCL_OK;
}

int tclCmdExportSvg(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 2) {
        Tcl_WrongNumArgs(ip, 1, objv, "path");
        return TCL_ERROR;
    }
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(
        w->tclExportSvg(QString::fromUtf8(Tcl_GetString(objv[1])))));
    return TCL_OK;
}

int tclCmdPlotAll(ClientData cd, Tcl_Interp* ip, int, Tcl_Obj* const[]) {
    auto* w = static_cast<MainWindow*>(cd);
    w->tclPlotAll();
    return TCL_OK;
}

int tclCmdSetOrigin(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 3) {
        Tcl_WrongNumArgs(ip, 1, objv, "x y");
        return TCL_ERROR;
    }
    double x = 0.0, y = 0.0;
    Tcl_GetDoubleFromObj(ip, objv[1], &x);
    Tcl_GetDoubleFromObj(ip, objv[2], &y);
    w->tclSetOrigin(x, y);
    return TCL_OK;
}

int tclCmdOpenPath(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 2) {
        Tcl_WrongNumArgs(ip, 1, objv, "path");
        return TCL_ERROR;
    }
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(
        w->openPath(QString::fromUtf8(Tcl_GetString(objv[1])))));
    return TCL_OK;
}

int tclCmdOpenGerber(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 2) {
        Tcl_WrongNumArgs(ip, 1, objv, "file");
        return TCL_ERROR;
    }
    const bool ok = w->tclOpen(QString::fromUtf8(Tcl_GetString(objv[1])), false);
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(ok ? 1 : 0));
    return ok ? TCL_OK : TCL_ERROR;
}

int tclCmdOpenExcellon(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 2) {
        Tcl_WrongNumArgs(ip, 1, objv, "file");
        return TCL_ERROR;
    }
    const bool ok = w->tclOpen(QString::fromUtf8(Tcl_GetString(objv[1])), true);
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(ok ? 1 : 0));
    return ok ? TCL_OK : TCL_ERROR;
}

int tclCmdIsolate(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc < 2 || objc > 4) {
        Tcl_WrongNumArgs(ip, 1, objv, "diameter ?passes? ?overlapPercent?");
        return TCL_ERROR;
    }
    double dia = 0.2, overlap = 0.0;
    int passes = 1;
    Tcl_GetDoubleFromObj(ip, objv[1], &dia);
    if (objc > 2) Tcl_GetIntFromObj(ip, objv[2], &passes);
    if (objc > 3) Tcl_GetDoubleFromObj(ip, objv[3], &overlap);
    Tcl_SetObjResult(ip, Tcl_NewIntObj(w->tclIsolate(dia, passes, overlap)));
    return TCL_OK;
}

int tclCmdCutout(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc < 2 || objc > 5) {
        Tcl_WrongNumArgs(ip, 1, objv, "diameter ?margin? ?tabs? ?tabWidth?");
        return TCL_ERROR;
    }
    double dia = 1.2, margin = 0.0, width = 1.8;
    int tabs = 0;
    Tcl_GetDoubleFromObj(ip, objv[1], &dia);
    if (objc > 2) Tcl_GetDoubleFromObj(ip, objv[2], &margin);
    if (objc > 3) Tcl_GetIntFromObj(ip, objv[3], &tabs);
    if (objc > 4) Tcl_GetDoubleFromObj(ip, objv[4], &width);
    Tcl_SetObjResult(ip, Tcl_NewIntObj(w->tclCutout(dia, margin, tabs, width)));
    return TCL_OK;
}

int tclCmdNcc(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc < 2 || objc > 3) {
        Tcl_WrongNumArgs(ip, 1, objv, "diameter ?overlapPercent?");
        return TCL_ERROR;
    }
    double dia = 0.5, overlap = 80.0;
    Tcl_GetDoubleFromObj(ip, objv[1], &dia);
    if (objc > 2) Tcl_GetDoubleFromObj(ip, objv[2], &overlap);
    Tcl_SetObjResult(ip, Tcl_NewIntObj(w->tclNcc(dia, overlap)));
    return TCL_OK;
}

int tclCmdPaint(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc < 2 || objc > 3) {
        Tcl_WrongNumArgs(ip, 1, objv, "diameter ?overlapPercent?");
        return TCL_ERROR;
    }
    double dia = 0.1, overlap = 80.0;
    Tcl_GetDoubleFromObj(ip, objv[1], &dia);
    if (objc > 2) Tcl_GetDoubleFromObj(ip, objv[2], &overlap);
    Tcl_SetObjResult(ip, Tcl_NewIntObj(w->tclPaint(dia, overlap)));
    return TCL_OK;
}

int tclCmdExportGcode(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 2) {
        Tcl_WrongNumArgs(ip, 1, objv, "file");
        return TCL_ERROR;
    }
    const bool ok = w->tclExportGcode(QString::fromUtf8(Tcl_GetString(objv[1])));
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(ok ? 1 : 0));
    return ok ? TCL_OK : TCL_ERROR;
}

int tclCmdExportGerber(ClientData cd, Tcl_Interp* ip, int objc, Tcl_Obj* const objv[]) {
    auto* w = static_cast<MainWindow*>(cd);
    if (objc != 2) {
        Tcl_WrongNumArgs(ip, 1, objv, "file");
        return TCL_ERROR;
    }
    const bool ok = w->tclExportGerber(QString::fromUtf8(Tcl_GetString(objv[1])));
    Tcl_SetObjResult(ip, Tcl_NewBooleanObj(ok ? 1 : 0));
    return ok ? TCL_OK : TCL_ERROR;
}

int tclCmdInfo(ClientData cd, Tcl_Interp* ip, int, Tcl_Obj* const[]) {
    auto* w = static_cast<MainWindow*>(cd);
    Tcl_SetObjResult(ip, Tcl_NewStringObj(w->tclInfo().toUtf8().constData(), -1));
    return TCL_OK;
}
/// @}
#endif

/**
 * @brief Asigna un tooltip al widget y a su etiqueta de formulario.
 * @param widget Control.
 * @param text Texto HTML del globo.
 */
void applyTip(QWidget* widget, const QString& text) {
    if (!widget) return;
    widget->setToolTip(text);
    if (auto* page = widget->parentWidget()) {
        if (auto* form = qobject_cast<QFormLayout*>(page->layout())) {
            if (QWidget* label = form->labelForField(widget)) label->setToolTip(text);
        }
    }
}

/**
 * @brief Dibuja el icono circular de deshacer/rehacer.
 * @param redo `true` para rehacer (flecha espejada).
 */
QIcon historyIcon(bool redo) {
    constexpr int size = 32;
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor color(60, 60, 60);
    const double center = size / 2.0;
    const double radius = 10.5;
    painter.setPen(QPen(color, 3.0, Qt::SolidLine, Qt::RoundCap));
    painter.setBrush(Qt::NoBrush);
    // Leave a wide gap on the right for the arrow head.
    painter.drawArc(QRectF(center - radius, center - radius, radius * 2, radius * 2),
                    35 * 16, 290 * 16);

    const double angle = 35.0 * M_PI / 180.0;
    const QPointF tip(center + radius * std::cos(angle), center - radius * std::sin(angle));
    const QPointF tangent(-std::sin(angle), -std::cos(angle));
    const QPointF normal(-tangent.y(), tangent.x());
    QPolygonF head;
    head << tip + tangent * 3.5
         << tip - tangent * 8.0 + normal * 6.0
         << tip - tangent * 8.0 - normal * 6.0;
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPolygon(head);
    painter.end();

    if (!redo) return QIcon(pix);
    return QIcon(pix.transformed(QTransform().scale(-1, 1)));
}

/// @name Helpers de postprocesadores y propiedades
/// @{

/// @return Etiquetas de los 18 postprocesadores, en el orden de la UI.
QStringList gcodeFlavorLabels() {
    return {QObject::tr("Default (MACH3)"),
            QObject::tr("Default sin M6"),
            QObject::tr("GRBL"),
            QObject::tr("GRBL sin M6"),
            QObject::tr("GRBL láser"),
            QObject::tr("Marlin"),
            QObject::tr("Marlin láser (ventilador)"),
            QObject::tr("Marlin láser (spindle)"),
            QObject::tr("Repetier"),
            QObject::tr("Berta CNC"),
            QObject::tr("ISEL CNC"),
            QObject::tr("ISEL ICP CNC"),
            QObject::tr("Roland MDX-20"),
            QObject::tr("MACH3 cambio manual"),
            QObject::tr("MACH3 con sonda"),
            QObject::tr("Láser por Z"),
            QObject::tr("HPGL (plotter)"),
            QObject::tr("line_xyz"),
            QObject::tr("Personalizado (toolchange)")};
}

/// @return `true` si el índice de postprocesador corresponde a un láser.
bool isLaserFlavorIndex(int index) {
    return index == 4 || index == 6 || index == 7 || index == 15;
}

/// @brief Traduce el índice del combo al enum de postprocesador.
fc::GCodeFlavor gcodeFlavorFromIndex(int index) {
    switch (index) {
        case 1: return fc::GCodeFlavor::DefaultNoM6;
        case 2: return fc::GCodeFlavor::Grbl;
        case 3: return fc::GCodeFlavor::GrblNoM6;
        case 4: return fc::GCodeFlavor::GrblLaser;
        case 5: return fc::GCodeFlavor::Marlin;
        case 6: return fc::GCodeFlavor::MarlinLaserFan;
        case 7: return fc::GCodeFlavor::MarlinLaserSpindle;
        case 8: return fc::GCodeFlavor::Repetier;
        case 9: return fc::GCodeFlavor::Berta;
        case 10: return fc::GCodeFlavor::Isel;
        case 11: return fc::GCodeFlavor::IselIcp;
        case 12: return fc::GCodeFlavor::Roland;
        case 13: return fc::GCodeFlavor::ToolchangeManual;
        case 14: return fc::GCodeFlavor::ToolchangeProbe;
        case 15: return fc::GCodeFlavor::ZLaser;
        case 16: return fc::GCodeFlavor::Hpgl;
        case 17: return fc::GCodeFlavor::LineXyz;
        case 18: return fc::GCodeFlavor::ToolchangeCustom;
        default: return fc::GCodeFlavor::Default;
    }
}

/// @return Etiqueta legible de un postprocesador.
QString gcodeFlavorLabel(fc::GCodeFlavor flavor) {
    const QStringList labels = gcodeFlavorLabels();
    const int index = static_cast<int>(flavor);
    return index >= 0 && index < labels.size() ? labels[index] : QString();
}

/**
 * @brief Construye las propiedades visibles de un CNCJob.
 * @param opt Parámetros de G-code usados.
 * @param kind Tipo de trabajo ("aislamiento", "taladrado", ...).
 * @return Mapa clave/valor para el panel de propiedades.
 */
std::map<QString, QString> cncProps(const fc::GCodeOptions& opt, const QString& kind) {
    std::map<QString, QString> props;
    props[QObject::tr("Trabajo")] = kind;
    props[QObject::tr("Feed XY")] = QString::number(opt.feedXY, 'f', 2) + " mm/min";
    props[QObject::tr("Feed Z")] = QString::number(opt.feedZ, 'f', 2) + " mm/min";
    props[QObject::tr("Z corte")] = QString::number(opt.zCut, 'f', 3) + " mm";
    props[QObject::tr("Z viaje")] = QString::number(opt.zMove, 'f', 3) + " mm";
    props[QObject::tr("Husillo")] = QString::number(opt.spindleSpeed, 'f', 0) + " RPM";
    props[QObject::tr("Postprocesador")] = gcodeFlavorLabel(opt.flavor);
    if (opt.depthPerPass > 0.0)
        props[QObject::tr("Profundidad/pasada")] = QString::number(opt.depthPerPass, 'f', 3) + " mm";
    return props;
}

/// @brief Carga un icono original de FlatCAM desde el `.qrc`.
/// @brief Aclara los trazos oscuros de un icono para el tema oscuro.
QPixmap lightenIconForDark(const QPixmap& source) {
    QImage image = source.toImage().convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = line[x];
            const int alpha = qAlpha(pixel);
            if (alpha == 0) continue;
            const int r = qRed(pixel);
            const int g = qGreen(pixel);
            const int b = qBlue(pixel);
            const int high = std::max({r, g, b});
            const int low = std::min({r, g, b});
            if (high - low <= 42 && high < 165) {
                // Trazo neutro oscuro: se reemplaza por gris claro.
                line[x] = qRgba(231, 233, 236, alpha);
            } else if (high < 110) {
                // Color muy oscuro: se aclara conservando el tono.
                const QColor lifted = QColor::fromRgb(pixel).lighter(190);
                line[x] = qRgba(lifted.red(), lifted.green(), lifted.blue(), alpha);
            }
        }
    }
    return QPixmap::fromImage(image);
}

/// @brief Icono que adapta sus trazos al tema activo en tiempo de dibujo.
class ThemedIconEngine : public QIconEngine {
public:
    explicit ThemedIconEngine(QString name) : name_(std::move(name)) {}

    QIconEngine* clone() const override { return new ThemedIconEngine(*this); }

    QString key() const override {
        return QStringLiteral("fc-icon:%1:%2").arg(name_, fc::theme::isDark() ? "d" : "l");
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State) override {
        QPixmap pixmap(":/icons/" + name_ + ".png");
        if (pixmap.isNull()) return pixmap;
        if (fc::theme::isDark()) pixmap = lightenIconForDark(pixmap);
        if (mode == QIcon::Disabled) {
            QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
            for (int y = 0; y < image.height(); ++y) {
                auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
                for (int x = 0; x < image.width(); ++x) {
                    const QRgb pixel = line[x];
                    line[x] = qRgba(qRed(pixel), qGreen(pixel), qBlue(pixel),
                                    qAlpha(pixel) * 45 / 100);
                }
            }
            pixmap = QPixmap::fromImage(image);
        }
        if (size.isValid() && !size.isEmpty() &&
            (pixmap.width() > size.width() || pixmap.height() > size.height())) {
            pixmap = pixmap.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        return pixmap;
    }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode,
               QIcon::State state) override {
        painter->drawPixmap(rect, pixmap(rect.size(), mode, state));
    }

private:
    QString name_;
};

QIcon originalIcon(const QString& name) {
    return QIcon(new ThemedIconEngine(name));
}

/// @brief Dibuja un icono circular con una letra (fallback).
QIcon toolIcon(const QString& letter, const QColor& color) {
    QPixmap pix(24, 24);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(color);
    painter.setPen(QPen(color.darker(140), 1));
    painter.drawEllipse(1, 1, 22, 22);
    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(13);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(pix.rect(), Qt::AlignCenter, letter);
    painter.end();
    return QIcon(pix);
}

/// @brief Acorta el título de una herramienta para la pestaña.
QString shortToolTitle(QString title) {
    title.remove("Herramienta de ");
    title.remove("Herramienta ");
    return title;
}

/// @brief Devuelve el icono correspondiente a una herramienta.
QIcon toolIconForTitle(const QString& title) {
    if (title.contains("aislamiento")) return originalIcon("isolate");
    if (title.contains("NCC")) return originalIcon("ncc");
    if (title.contains("corte")) return originalIcon("cutout");
    if (title.contains("pintura")) return originalIcon("paint");
    if (title.contains("perforación")) return originalIcon("drill");
    if (title.contains("fresado")) return originalIcon("milling");
    if (title.contains("Thieving")) return originalIcon("clean");
    if (title.contains("Solder")) return originalIcon("solderpaste");
    if (title.contains("QR")) return originalIcon("qrcode");
    if (title.contains("Film")) return originalIcon("film");
    return QIcon();
}

/// @brief Genera el icono verde/gris de la columna Vis.
QIcon visibilityIcon(bool visible) {
    static QIcon yes;
    static QIcon no;
    QIcon& icon = visible ? yes : no;
    if (!icon.isNull()) return icon;
    QPixmap pix(16, 16);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(visible ? QColor(46, 204, 113) : QColor(170, 170, 170));
    painter.setPen(QPen(visible ? QColor(30, 132, 73) : QColor(110, 110, 110), 1));
    painter.drawEllipse(1, 1, 14, 14);
    painter.setPen(QPen(Qt::white, 2));
    if (visible) {
        painter.drawLine(4, 8, 7, 11);
        painter.drawLine(7, 11, 12, 5);
    } else {
        painter.drawLine(5, 5, 11, 11);
        painter.drawLine(11, 5, 5, 11);
    }
    painter.end();
    icon = QIcon(pix);
    return icon;
}

class TreeDropFilter : public QObject {
public:
    QTreeWidget* tree = nullptr;
    std::function<void()> onReorder;

    bool eventFilter(QObject* object, QEvent* event) override {
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            event->accept();
            return true;
        }
        if (event->type() != QEvent::Drop) return QObject::eventFilter(object, event);
        auto* dropEvent = static_cast<QDropEvent*>(event);
        QTreeWidgetItem* dragged = tree ? tree->currentItem() : nullptr;
        if (!tree || !dragged) {
            dropEvent->ignore();
            return true;
        }
        const QPoint pos = dropEvent->position().toPoint();
        QTreeWidgetItem* target = tree->itemAt(pos);
        int from = tree->indexOfTopLevelItem(dragged);
        if (from < 0) {
            dropEvent->ignore();
            return true;
        }
        int to = target ? tree->indexOfTopLevelItem(target) : tree->topLevelItemCount() - 1;
        if (target) {
            const QRect rect = tree->visualItemRect(target);
            if (pos.y() > rect.center().y()) ++to;
        }
        if (to > from) --to;
        to = std::clamp(to, 0, tree->topLevelItemCount() - 1);
        if (to != from) {
            QTreeWidgetItem* item = tree->takeTopLevelItem(from);
            tree->insertTopLevelItem(to, item);
            tree->setCurrentItem(item);
            if (onReorder) onReorder();
        }
        dropEvent->acceptProposedAction();
        return true;
    }
};

class LambdaCommand : public QUndoCommand {
public:
    LambdaCommand(std::function<void()> undo, std::function<void()> redo, const QString& text)
        : undo_(std::move(undo)), redo_(std::move(redo)) {
        setText(text);
    }
    void undo() override { undo_(); }
    void redo() override { redo_(); }

private:
    std::function<void()> undo_;
    std::function<void()> redo_;
};

/// @brief Heurística por extensión y cabecera para distinguir Excellon.
bool looksLikeExcellon(const QString& path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == "drl" || suffix == "xln" || suffix == "exc" || suffix == "ncd") return true;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    const QString head = QString::fromUtf8(f.read(4096));
    return head.contains("M48") || head.contains("METRIC,") || head.contains("INCH,") ||
           head.contains("T01C");
}

/// @brief Formatea un número sin ceros innecesarios.
QString formatDouble(double v) {
    std::ostringstream os;
    os.precision(6);
    os << v;
    return QString::fromStdString(os.str());
}

}  // namespace

/// @}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    undoStack_ = new QUndoStack(this);
    connect(undoStack_, &QUndoStack::indexChanged, this, [this](int) { markProjectDirty(); });
    setWindowTitle(tr("FlatCAM C++"));
    resize(1200, 800);
    setAcceptDrops(true);
    buildLayout();
    loadPreferences();
    applyUiLevel();
    buildMenus();
    rebuildRecentMenu();
    rebuildBookmarksMenu();
    statusBar()->showMessage(tr("Listo"));
}

// ============================================================================
// Construcción de la interfaz
// ============================================================================

/// Construye menús, barra, docks, canvas y consola.
void MainWindow::buildLayout() {
    canvas_ = new Canvas(this);
    canvas_->setToolTip(tr("Visor. Rueda: zoom. Arrastrar: desplazar. Barra inferior: coordenadas."));
    setCentralWidget(canvas_);

    info_ = new QPlainTextEdit(this);
    info_->setToolTip(tr("Resumen de archivos, aperturas, herramientas, áreas y advertencias."));
    info_->setReadOnly(true);
    info_->setMaximumBlockCount(500);
    info_->setPlainText(tr("Arrastre uno o varios Gerber (.gbr, .gtl, .gbl) o Excellon (.drl, .xln)."));

    bottomTabs_ = new QTabWidget(this);
    bottomTabs_->tabBar()->setDrawBase(false);
    bottomTabs_->setProperty("flat", true);
    buildTclConsole(bottomTabs_);
    buildConsole(bottomTabs_);
    gcodeView_ = new QPlainTextEdit(this);
    gcodeView_->setReadOnly(true);
    gcodeView_->setToolTip(tr("Último G-code generado. Doble clic en un elemento del grupo "
                              "G-code abre el editor con resaltado."));
    gcodeView_->setStyleSheet("QPlainTextEdit { font-family: Menlo; font-size: 12px; }");
    bottomTabs_->addTab(gcodeView_, tr("G-code"));
    bottomTabs_->addTab(info_, tr("Informe"));
    shellDock_ = new QDockWidget(tr("Shell"), this);
    shellDock_->setObjectName("shellDock");
    shellDock_->setTitleBarWidget(new QWidget(shellDock_));
    shellDock_->setWidget(bottomTabs_);
    addDockWidget(Qt::BottomDockWidgetArea, shellDock_);

    auto* closeConsole = new QToolButton(bottomTabs_);
    closeConsole->setText("×");
    closeConsole->setAutoRaise(true);
    closeConsole->setToolTip(tr("Ocultar la consola (Ver → Consola para mostrarla)."));
    closeConsole->setFixedSize(24, 24);
    bottomTabs_->setCornerWidget(closeConsole, Qt::TopRightCorner);
    connect(closeConsole, &QToolButton::clicked, this, [this] {
        if (shellDock_) shellDock_->hide();
    });

    objects_ = new QTreeWidget(this);
    objects_->setToolTip(tr("Objetos cargados. Checkbox: visibilidad. Clic: seleccionar/zoom. "
                            "Clic derecho: duplicar o eliminar."));
    objects_->setHeaderLabels({tr("Vis"), tr("Objeto"), tr("Tipo"), tr("Área")});
    objects_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    objects_->setRootIsDecorated(false);
    objects_->header()->setSectionResizeMode(QHeaderView::Interactive);
    objects_->header()->setStretchLastSection(false);
    objects_->header()->setSectionsMovable(true);
    objects_->header()->setMinimumSectionSize(40);
    objects_->setColumnWidth(0, 36);
    if (auto* headerItem = objects_->headerItem()) {
        headerItem->setToolTip(0, tr("El checkbox de la izquierda muestra u oculta la capa en el "
                                     "visor. Todos los archivos se cargan visibles."));
        headerItem->setToolTip(1, tr("Nombre del archivo, de la capa generada o del G-code."));
        headerItem->setToolTip(2, tr("Tipo de objeto: gerber, excellon, herramienta o gcode."));
        headerItem->setToolTip(3, tr("Área del objeto en mm²."));
    }
    objects_->setColumnWidth(1, 170);
    objects_->setColumnWidth(2, 78);

    properties_ = new QTreeWidget(this);
    properties_->setToolTip(tr("Propiedades del objeto seleccionado. Nombre editable con doble clic."));
    properties_->setHeaderLabels({tr("Propiedad"), tr("Valor")});
    properties_->setRootIsDecorated(false);
    properties_->header()->setSectionResizeMode(QHeaderView::Interactive);
    properties_->header()->setStretchLastSection(true);
    properties_->setColumnWidth(0, 120);
    properties_->setEditTriggers(QAbstractItemView::DoubleClicked |
                                 QAbstractItemView::EditKeyPressed);

    leftTabs_ = new QTabWidget(this);
    leftTabs_->setMinimumWidth(370);
    leftTabs_->tabBar()->setDrawBase(false);
    leftTabs_->setProperty("flat", true);
    leftTabs_->setIconSize(QSize(16, 16));
    leftTabs_->setTabsClosable(true);
    leftTabs_->setDocumentMode(false);
    leftTabs_->addTab(objects_, tr("Proyecto"));
    leftTabs_->addTab(properties_, tr("Propiedades"));
    objectToolPage_ = new QWidget(this);
    objectToolLayout_ = new QVBoxLayout(objectToolPage_);
    objectToolLayout_->setContentsMargins(6, 6, 6, 6);
    objectToolLayout_->setSpacing(6);
    auto* objectToolScroll = new QScrollArea(this);
    objectToolScroll->setWidgetResizable(true);
    objectToolScroll->setFrameShape(QFrame::NoFrame);
    objectToolScroll->setWidget(objectToolPage_);
    leftTabs_->addTab(objectToolScroll, tr("Herramienta"));
    for (int i = 0; i < 3; ++i) {
        leftTabs_->tabBar()->setTabButton(i, QTabBar::RightSide, nullptr);
        leftTabs_->tabBar()->setTabButton(i, QTabBar::LeftSide, nullptr);
    }
    connect(leftTabs_, &QTabWidget::tabCloseRequested, this, [this](int index) {
        if (index >= 3) leftTabs_->removeTab(index);
    });
    buildToolPanel(leftTabs_);
    resetObjectGroups();
    auto* leftDock = new QDockWidget(tr("Panel"), this);
    leftDock->setTitleBarWidget(new QWidget(leftDock));
    leftDock->setWidget(leftTabs_);
    addDockWidget(Qt::LeftDockWidgetArea, leftDock);

    statusLabel_ = new QLabel(tr("Sin archivo"), this);
    coordsLabel_ = new QLabel(tr("X: —   Y: —"), this);
    coordsLabel_->setMinimumWidth(170);
    unitsLabel_ = new QLabel(tr("mm"), this);
    unitsLabel_->setToolTip(tr("Unidades de visualización (Opciones → Mostrar en pulgadas)."));
    auto* idleLabel = new QLabel(tr("Ocioso"), this);
    idleLabel->setToolTip(tr("Estado de la aplicación."));
    statusBar()->addPermanentWidget(unitsLabel_);
    statusBar()->addPermanentWidget(coordsLabel_);
    statusBar()->addPermanentWidget(idleLabel);
    statusBar()->addPermanentWidget(statusLabel_);

    connect(objects_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int column) {
        if (!item || column != 0) return;
        const bool visible = !item->data(0, Qt::UserRole + 1).toBool();
        setObjectVisible(item, visible);
        statusBar()->showMessage(visible ? tr("Capa visible") : tr("Capa oculta"), 2000);
        refreshProperties();
    });
    connect(objects_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        const QString path = item->data(0, Qt::UserRole + 2).toString();
        if (!path.isEmpty()) {
            openGCodeEditor(path, this);
            return;
        }
        objects_->setCurrentItem(item);
        const QString kind = item->text(2).toLower();
        if (kind == "document" || kind == "script" || kind == "cncjob") {
            openTextObject(item->data(0, Qt::UserRole).toInt());
            return;
        }
        refreshObjectToolPage();
        if (leftTabs_) leftTabs_->setCurrentIndex(2);
    });
    connect(objects_, &QTreeWidget::itemClicked, this, &MainWindow::projectObjectSelected);
    connect(objects_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem*, QTreeWidgetItem*) {
                refreshProperties();
                refreshObjectToolPage();
            });
    connect(objects_, &QTreeWidget::itemChanged, this, &MainWindow::objectItemChanged);
    objects_->setDragEnabled(false);
    objects_->setAcceptDrops(false);
    objects_->viewport()->setAcceptDrops(false);
    objects_->setDragDropMode(QAbstractItemView::NoDragDrop);
    auto syncLayerOrder = [this] {
        if (!canvas_ || !objects_) return;
        std::vector<int> ids;
        for (QTreeWidgetItem* item : objectItems()) {
            ids.push_back(item->data(0, Qt::UserRole).toInt());
        }
        canvas_->setLayerOrder(ids);
    };
    connect(objects_->model(), &QAbstractItemModel::rowsInserted, this,
            [this, syncLayerOrder](const QModelIndex&, int, int) {
                QTimer::singleShot(0, this, syncLayerOrder);
            });
    connect(objects_->model(), &QAbstractItemModel::rowsRemoved, this,
            [this, syncLayerOrder](const QModelIndex&, int, int) {
                QTimer::singleShot(0, this, syncLayerOrder);
            });
    objects_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(objects_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QTreeWidgetItem* item = objects_->itemAt(pos);
        if (!item) return;
        QMenu menu(this);
        QAction* duplicate = menu.addAction(tr("Duplicar objeto"));
        QAction* remove = menu.addAction(tr("Eliminar objeto"));
        QAction* chosen = menu.exec(objects_->mapToGlobal(pos));
        if (chosen == remove) {
            objects_->setCurrentItem(item);
            deleteSelectedObject();
        } else if (chosen == duplicate) {
            objects_->setCurrentItem(item);
            duplicateSelectedObject();
        }
    });

    connect(properties_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* row, int column) {
        if (!row || column != 1) return;
        QTreeWidgetItem* item = objects_->currentItem();
        if (!item) return;
        const int layerId = item->data(0, Qt::UserRole).toInt();
        const auto props = objectProps_.find(layerId);
        if (props != objectProps_.end()) {
            const auto value = props->second.find(row->text(0));
            if (value != props->second.end()) {
                value->second = row->text(1);
                return;
            }
        }
        if (row->text(0) != tr("Nombre")) return;
        const QString oldName = item->text(1);
        const QString newName = row->text(1);
        if (oldName == newName) return;
        updatingTree_ = true;
        item->setText(1, newName);
        updatingTree_ = false;
        undoStack_->push(new LambdaCommand(
            [item, oldName] { item->setText(1, oldName); },
            [item, newName] { item->setText(1, newName); }, tr("Renombrar objeto")));
    });

    connect(canvas_, &Canvas::layerPicked, this, [this](int layerId) {
        if (layerId <= 0) return;
        if (QTreeWidgetItem* item = findObjectItem(layerId)) {
            objects_->setCurrentItem(item);
            refreshProperties();
        }
    });
    connect(canvas_, &Canvas::layerActivated, this, [this](int layerId) {
        if (layerId <= 0) return;
        const BBox b = canvas_->layerBounds(layerId);
        if (b.valid) canvas_->fitTo(b);
    });
    connect(canvas_, &Canvas::cursorMoved, this, [this](double x, double y) {
        if (std::isnan(x) || std::isnan(y)) {
            coordsLabel_->setText(tr("X: —   Y: —"));
        } else {
            coordsLabel_->setText(tr("X: %1   Y: %2")
                                      .arg(formatLength(x), formatLength(y)));
        }
    });
}

/// Construye la barra de menús y sus acciones/atajos.
void MainWindow::buildMenus() {
    QMenu* file = menuBar()->addMenu(tr("&Archivo"));
    QAction* newProject = file->addAction(tr("&Nuevo proyecto"));
    newProject->setShortcut(QKeySequence::New);
    newProject->setIcon(originalIcon("new_file"));
    connect(newProject, &QAction::triggered, this, &MainWindow::newProject);
    QAction* saveProject = file->addAction(tr("&Guardar proyecto como..."));
    saveProject->setShortcut(QKeySequence::Save);
    saveProject->setIcon(originalIcon("save"));
    connect(saveProject, &QAction::triggered, this, &MainWindow::saveProjectAs);
    QAction* saveConfig = file->addAction(tr("Guardar &configuración..."));
    saveConfig->setIcon(originalIcon("save_as"));
    connect(saveConfig, &QAction::triggered, this, &MainWindow::saveConfiguration);
    QAction* loadConfig = file->addAction(tr("Abrir configuración..."));
    loadConfig->setIcon(originalIcon("folder"));
    connect(loadConfig, &QAction::triggered, this, &MainWindow::loadConfiguration);
    QAction* backupProject = file->addAction(tr("&Copia de seguridad del proyecto"));
    backupProject->setIcon(originalIcon("save_as"));
    connect(backupProject, &QAction::triggered, this, [this] {
        if (projectPath_.isEmpty()) {
            QMessageBox::information(this, tr("Copia de seguridad"),
                                     tr("Guardá el proyecto primero."));
            return;
        }
        const QString backup = projectPath_ + ".bak";
        if (QFile::exists(backup)) QFile::remove(backup);
        if (QFile::copy(projectPath_, backup))
            statusBar()->showMessage(tr("Copia de seguridad: %1").arg(backup), 5000);
        else
            showError(tr("No se pudo crear la copia de seguridad."));
    });
    file->addSeparator();
    QAction* open = file->addAction(tr("&Abrir..."));
    open->setShortcut(QKeySequence::Open);
    open->setIcon(originalIcon("folder_gerber"));
    connect(open, &QAction::triggered, this, &MainWindow::chooseFile);
    recentMenu_ = file->addMenu(originalIcon("recent"), tr("Abrir &recientes"));
    QAction* openProject = file->addAction(tr("Abrir &proyecto (.FlatPrj)..."));
    openProject->setIcon(originalIcon("folder"));
    connect(openProject, &QAction::triggered, this, &MainWindow::chooseProject);
    QAction* importSvgAction = file->addAction(tr("Importar &SVG..."));
    importSvgAction->setIcon(originalIcon("svg"));
    connect(importSvgAction, &QAction::triggered, this, [this] { importVector(true); });
    QAction* importDxfAction = file->addAction(tr("Importar &DXF..."));
    importDxfAction->setIcon(originalIcon("dxf"));
    connect(importDxfAction, &QAction::triggered, this, [this] { importVector(false); });
    file->addSeparator();
    QAction* exportSvgAction = file->addAction(tr("Exportar &SVG..."));
    exportSvgAction->setIcon(originalIcon("svg"));
    connect(exportSvgAction, &QAction::triggered, this, &MainWindow::exportFilm);
    QAction* printAction = file->addAction(tr("&Imprimir..."));
    printAction->setIcon(originalIcon("print"));
    connect(printAction, &QAction::triggered, this, &MainWindow::printView);
    QAction* exportPngAction = file->addAction(tr("Exportar &PNG..."));
    exportPngAction->setIcon(originalIcon("png"));
    connect(exportPngAction, &QAction::triggered, this, &MainWindow::exportPng);
    QAction* exportDxfAction = file->addAction(tr("Exportar &DXF..."));
    exportDxfAction->setIcon(originalIcon("dxf"));
    connect(exportDxfAction, &QAction::triggered, this, &MainWindow::exportDxfFile);
    QAction* exportHpglAction = file->addAction(tr("Exportar &HPGL..."));
    exportHpglAction->setIcon(originalIcon("image"));
    connect(exportHpglAction, &QAction::triggered, this, &MainWindow::exportHpglFile);
    file->addSeparator();
    QAction* newDocumentAction = file->addAction(tr("Nuevo &documento..."));
    newDocumentAction->setIcon(originalIcon("file"));
    connect(newDocumentAction, &QAction::triggered, this, [this] { createTextObject("document"); });
    QAction* newProjectScriptAction = file->addAction(tr("Nuevo script de &proyecto..."));
    newProjectScriptAction->setIcon(originalIcon("script"));
    connect(newProjectScriptAction, &QAction::triggered, this, [this] { createTextObject("script"); });
    QAction* newScriptAction = file->addAction(tr("Nuevo &script..."));
    newScriptAction->setIcon(originalIcon("script_new"));
    connect(newScriptAction, &QAction::triggered, this, &MainWindow::newScript);
    QAction* editScriptAction = file->addAction(tr("&Editar script..."));
    editScriptAction->setIcon(originalIcon("open_script"));
    connect(editScriptAction, &QAction::triggered, this, &MainWindow::editScript);
    QAction* runScriptAction = file->addAction(tr("Ejecutar &script JavaScript..."));
    runScriptAction->setIcon(originalIcon("script"));
    connect(runScriptAction, &QAction::triggered, this, &MainWindow::runScript);
    QAction* openObj = file->addAction(tr("Abrir &objeto de proyecto..."));
    openObj->setIcon(originalIcon("folder_excellon"));
    connect(openObj, &QAction::triggered, this, &MainWindow::chooseProjectObject);
    QAction* exportG = file->addAction(tr("&Exportar G-code (taladrado)..."));
    exportG->setShortcut(QKeySequence("Ctrl+E"));
    exportG->setIcon(originalIcon("save_as"));
    connect(exportG, &QAction::triggered, this, &MainWindow::exportGCode);
    QAction* exportPdfAction = file->addAction(tr("Exportar &PDF..."));
    exportPdfAction->setIcon(originalIcon("pdf"));
    connect(exportPdfAction, &QAction::triggered, this, &MainWindow::exportPdf);
    QAction* importHpglAction = file->addAction(tr("Importar &HPGL2..."));
    importHpglAction->setIcon(originalIcon("image"));
    connect(importHpglAction, &QAction::triggered, this, &MainWindow::importHpglFile);
    QAction* importPdfAction = file->addAction(tr("Importar &PDF..."));
    importPdfAction->setIcon(originalIcon("pdf"));
    connect(importPdfAction, &QAction::triggered, this, &MainWindow::importPdfFile);
    QAction* importImageAction = file->addAction(tr("&Importar imagen..."));
    importImageAction->setIcon(originalIcon("image"));
    connect(importImageAction, &QAction::triggered, this, &MainWindow::importImage);
    QAction* loadInfAction = file->addAction(tr("Cargar tabla &INF (PcbWizard)..."));
    loadInfAction->setIcon(originalIcon("drill"));
    connect(loadInfAction, &QAction::triggered, this, &MainWindow::loadInfFile);
    QAction* openGCodeAction = file->addAction(tr("Abrir &G-code..."));
    openGCodeAction->setIcon(originalIcon("code_editor"));
    connect(openGCodeAction, &QAction::triggered, this, &MainWindow::openGCodeFile);
    file->addSeparator();
    QAction* quit = file->addAction(tr("&Salir"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);

    QMenu* edit = menuBar()->addMenu(tr("&Editar"));
    QAction* undo = edit->addAction(tr("&Deshacer"));
    undo->setShortcut(QKeySequence::Undo);
    undo->setIcon(historyIcon(false));
    connect(undo, &QAction::triggered, undoStack_, &QUndoStack::undo);
    QAction* redo = edit->addAction(tr("&Rehacer"));
    redo->setShortcut(QKeySequence::Redo);
    redo->setIcon(historyIcon(true));
    connect(redo, &QAction::triggered, undoStack_, &QUndoStack::redo);
    edit->addSeparator();
    QAction* selectAll = edit->addAction(tr("Seleccionar &todo"));
    selectAll->setShortcut(QKeySequence::SelectAll);
    selectAll->setIcon(originalIcon("select_all"));
    connect(selectAll, &QAction::triggered, this, &MainWindow::selectAllObjects);
    QAction* deselectAll = edit->addAction(tr("&Deseleccionar todo"));
    deselectAll->setIcon(originalIcon("deselect_all"));
    connect(deselectAll, &QAction::triggered, this, &MainWindow::deselectAllObjects);

    QMenu* view = menuBar()->addMenu(tr("&Ver"));
    QAction* fit = view->addAction(tr("&Ajustar a la ventana"));
    fit->setShortcut(Qt::Key_F);
    fit->setIcon(originalIcon("zoom_fit"));
    connect(fit, &QAction::triggered, this, &MainWindow::fitView);
    QAction* zin = view->addAction(tr("Acercar"));
    zin->setShortcut(QKeySequence::ZoomIn);
    zin->setIcon(originalIcon("zoom_in"));
    connect(zin, &QAction::triggered, canvas_, &Canvas::zoomIn);
    QAction* zout = view->addAction(tr("Alejar"));
    zout->setShortcut(QKeySequence::ZoomOut);
    zout->setIcon(originalIcon("zoom_out"));
    connect(zout, &QAction::triggered, canvas_, &Canvas::zoomOut);
    view->addSeparator();
    bookmarksMenu_ = view->addMenu(originalIcon("bookmarks"), tr("&Marcadores"));
    QAction* addBookmarkAction = bookmarksMenu_->addAction(tr("Agregar marcador..."));
    connect(addBookmarkAction, &QAction::triggered, this, &MainWindow::addBookmark);
    bookmarksMenu_->addSeparator();
    rebuildBookmarksMenu();

    QMenu* tools = menuBar()->addMenu(tr("&Herramienta"));
    QAction* isolate = tools->addAction(tr("&Aislar cobre..."));
    isolate->setShortcut(QKeySequence("Ctrl+I"));
    isolate->setIcon(originalIcon("isolate"));
    connect(isolate, &QAction::triggered, this, &MainWindow::isolateDialog);
    QAction* exportIso = tools->addAction(tr("Exportar &G-code de aislamiento..."));
    exportIso->setIcon(originalIcon("save_as"));
    connect(exportIso, &QAction::triggered, this, &MainWindow::exportIsolationGCode);
    tools->addSeparator();
    QAction* cutout = tools->addAction(tr("&Cortar contorno..."));
    cutout->setShortcut(QKeySequence("Ctrl+K"));
    cutout->setIcon(originalIcon("cutout"));
    connect(cutout, &QAction::triggered, this, &MainWindow::cutoutDialog);
    QAction* exportCut = tools->addAction(tr("Exportar G-code de &corte..."));
    exportCut->setIcon(originalIcon("save_as"));
    connect(exportCut, &QAction::triggered, this, &MainWindow::exportCutoutGCode);
    tools->addSeparator();
    QAction* ncc = tools->addAction(tr("Despejar cobre (&NCC)..."));
    ncc->setShortcut(QKeySequence("Ctrl+N"));
    ncc->setIcon(originalIcon("ncc"));
    connect(ncc, &QAction::triggered, this, &MainWindow::nccDialog);
    QAction* exportNcc = tools->addAction(tr("Exportar G-code de &NCC..."));
    exportNcc->setIcon(originalIcon("save_as"));
    connect(exportNcc, &QAction::triggered, this, &MainWindow::exportNccGCode);
    tools->addSeparator();
    QAction* paint = tools->addAction(tr("&Pintar área..."));
    paint->setShortcut(QKeySequence("Ctrl+P"));
    paint->setIcon(originalIcon("paint"));
    connect(paint, &QAction::triggered, this, &MainWindow::paintDialog);
    QAction* exportPaint = tools->addAction(tr("Exportar G-code de &pintado..."));
    exportPaint->setIcon(originalIcon("save_as"));
    connect(exportPaint, &QAction::triggered, this, &MainWindow::exportPaintGCode);
    tools->addSeparator();
    QAction* editGeometry = tools->addAction(tr("&Editor de geometría..."));
    editGeometry->setIcon(originalIcon("properties"));
    connect(editGeometry, &QAction::triggered, this, &MainWindow::editGeometry);
    QAction* booleans = tools->addAction(tr("Operaciones &booleanas..."));
    booleans->setIcon(originalIcon("union"));
    connect(booleans, &QAction::triggered, this, &MainWindow::booleanDialog);
    QAction* moveAction = tools->addAction(tr("&Mover objeto..."));
    moveAction->setIcon(originalIcon("move"));
    connect(moveAction, &QAction::triggered, this, &MainWindow::moveObject);
    QAction* transform = tools->addAction(tr("&Transformar objeto..."));
    transform->setIcon(originalIcon("transform"));
    connect(transform, &QAction::triggered, this, &MainWindow::transformSelectedObject);
    QAction* film = tools->addAction(tr("Crear &film (combinar capas)"));
    film->setIcon(originalIcon("film"));
    connect(film, &QAction::triggered, this, &MainWindow::createFilm);
    QAction* exportFilmAction = tools->addAction(tr("Exportar film &SVG..."));
    exportFilmAction->setIcon(originalIcon("save_as"));
    connect(exportFilmAction, &QAction::triggered, this, &MainWindow::exportFilm);

    auto tipAction = [](QAction* action, const QString& text) {
        action->setToolTip(text);
        action->setStatusTip(text);
    };
    tipAction(open, tr("Abrir uno o varios Gerber/Excellon. Cada archivo se agrega como capa."));
    tipAction(openProject, tr("Abrir un proyecto FlatCAM .FlatPrj (incluye proyectos comprimidos)."));
    tipAction(openObj, tr("Elegir un objeto concreto dentro del proyecto abierto."));
    tipAction(newScriptAction, tr("Crear un script JavaScript nuevo y ejecutarlo."));
    tipAction(editScriptAction, tr("Abrir y editar un script JavaScript existente."));
    tipAction(runScriptAction, tr("Ejecutar un script JavaScript (Qt JS) que use la API flatcam."));
    tipAction(exportG, tr("Exportar G-code de taladrado del archivo Excellon cargado."));
    tipAction(importImageAction, tr("Convertir una imagen (PNG/JPG) en cobre Gerber por umbral."));
    tipAction(importHpglAction, tr("Importa un archivo HPGL/PLT como recorridos."));
    tipAction(importPdfAction, tr("Importa la geometría vectorial del PDF (si no la hay, rasteriza las páginas)."));
    tipAction(loadInfAction, tr("Aplica los diámetros de herramienta de un archivo .INF al Excellon seleccionado."));
    tipAction(openGCodeAction, tr("Carga un programa G-code y lo dibuja como capa de recorridos."));
    tipAction(importSvgAction, tr("Importa un SVG como capa de cobre/recorridos."));
    tipAction(importDxfAction, tr("Importa un DXF (líneas, polilíneas y círculos)."));
    tipAction(exportSvgAction, tr("Exporta las capas visibles como SVG."));
    tipAction(exportPngAction, tr("Exporta una imagen PNG de la vista actual."));
    tipAction(exportDxfAction, tr("Exporta las capas visibles como DXF."));
    tipAction(printAction, tr("Imprime la vista actual en cualquier impresora o PDF del sistema."));
    tipAction(quit, tr("Cerrar la aplicación."));
    tipAction(undo, tr("Deshacer la última operación (borrar, duplicar, transformar, editar)."));
    tipAction(redo, tr("Rehacer la operación deshecha."));
    tipAction(fit, tr("Ajustar el zoom para ver todos los objetos cargados."));
    tipAction(zin, tr("Acercar el visor."));
    tipAction(zout, tr("Alejar el visor."));
    tipAction(isolate, tr("Generar recorridos de aislamiento alrededor del cobre del objeto."));
    tipAction(exportIso, tr("Guardar el G-code del último aislamiento."));
    tipAction(cutout, tr("Generar el corte de contorno de la placa, con puentes opcionales."));
    tipAction(exportCut, tr("Guardar el G-code del último corte de contorno."));
    tipAction(ncc, tr("Despejar el cobre sobrante (NCC) con hatch o contornos."));
    tipAction(exportNcc, tr("Guardar el G-code del último despeje NCC."));
    tipAction(paint, tr("Rellenar un área con contornos concéntricos (paint)."));
    tipAction(exportPaint, tr("Guardar el G-code del último pintado."));
    tipAction(transform, tr("Mover, rotar o escalar el objeto seleccionado."));
    tipAction(film, tr("Combinar las capas visibles en un objeto Film."));
    tipAction(exportFilmAction, tr("Exportar las capas visibles como SVG para imprimir."));
    tipAction(editGeometry, tr("Editar los vértices de contornos y recorridos del objeto."));
    tipAction(booleans, tr("Unión, diferencia o intersección entre dos objetos."));
    for (QMenu* menu : menuBar()->findChildren<QMenu*>()) menu->setToolTipsVisible(true);

    tools->addSeparator();
    auto addToolAction = [this, tools](const QString& text, const QString& title) {
        QAction* action = tools->addAction(text);
        action->setIcon(toolIconForTitle(title));
        connect(action, &QAction::triggered, this, [this, title] { showToolTab(title); });
    };
    addToolAction(tr("&Aislamiento"), tr("Herramienta de aislamiento"));
    addToolAction(tr("&NCC"), tr("Herramienta NCC"));
    addToolAction(tr("&Corte"), tr("Herramienta de corte"));
    addToolAction(tr("&Pintura"), tr("Herramienta de pintura"));
    addToolAction(tr("&Perforación"), tr("Herramienta de perforación"));
    addToolAction(tr("&Fresado"), tr("Herramienta de fresado"));
    addToolAction(tr("Copper &Thieving"), tr("Copper Thieving"));
    addToolAction(tr("Solder &Paste"), tr("Solder Paste"));
    addToolAction(tr("Código &QR"), tr("QR Code"));
    addToolAction(tr("&Film"), tr("Film"));
    tools->addSeparator();
    QAction* calculators = tools->addAction(tr("&Calculadoras..."));
    calculators->setIcon(originalIcon("calculator"));
    connect(calculators, &QAction::triggered, this, &MainWindow::showCalculators);
    tools->addSeparator();
    QAction* panelizeAction = tools->addAction(tr("&Panelizar..."));
    panelizeAction->setIcon(originalIcon("panelize"));
    connect(panelizeAction, &QAction::triggered, this, &MainWindow::panelize);
    QAction* fiducialsAction = tools->addAction(tr("Añadir &fiduciales..."));
    fiducialsAction->setIcon(originalIcon("fiducials"));
    connect(fiducialsAction, &QAction::triggered, this, &MainWindow::addFiducials);
    QAction* calibrateAction = tools->addAction(tr("&Calibrar escala..."));
    calibrateAction->setIcon(originalIcon("calibrate"));
    connect(calibrateAction, &QAction::triggered, this, &MainWindow::calibrate);
    QAction* filmAdvanced = tools->addAction(tr("Film a&vanzado..."));
    filmAdvanced->setIcon(originalIcon("film"));
    connect(filmAdvanced, &QAction::triggered, this, &MainWindow::advancedFilm);
    tools->addSeparator();
    QAction* etch = tools->addAction(tr("Compensación de &grabado..."));
    etch->setIcon(originalIcon("etch"));
    connect(etch, &QAction::triggered, this, &MainWindow::etchCompensation);
    QAction* invert = tools->addAction(tr("&Invertir Gerber..."));
    invert->setIcon(originalIcon("invert"));
    connect(invert, &QAction::triggered, this, &MainWindow::invertGerber);
    QAction* distance = tools->addAction(tr("Medir &distancia mínima..."));
    distance->setIcon(originalIcon("distance_min"));
    connect(distance, &QAction::triggered, this, &MainWindow::distanceTool);
    QAction* optimal = tools->addAction(tr("Herramienta &óptima..."));
    optimal->setIcon(originalIcon("optimal"));
    connect(optimal, &QAction::triggered, this, &MainWindow::optimalTool);
    QAction* corners = tools->addAction(tr("Marcas de &esquina..."));
    corners->setIcon(originalIcon("corners"));
    connect(corners, &QAction::triggered, this, &MainWindow::addCornerMarks);
    QAction* punch = tools->addAction(tr("&Punch Gerber..."));
    punch->setIcon(originalIcon("punch"));
    connect(punch, &QAction::triggered, this, &MainWindow::punchGerberAction);
    QAction* pcbWizard = tools->addAction(tr("&PcbWizard (Excellon + INF)..."));
    pcbWizard->setIcon(originalIcon("drill"));
    connect(pcbWizard, &QAction::triggered, this, &MainWindow::pcbWizardImport);
    QAction* loadInf = tools->addAction(tr("Aplicar tabla &INF al Excellon..."));
    loadInf->setIcon(originalIcon("drill"));
    connect(loadInf, &QAction::triggered, this, &MainWindow::loadInfFile);
    QAction* extract = tools->addAction(tr("&Extraer taladros..."));
    extract->setIcon(originalIcon("extract_drill"));
    connect(extract, &QAction::triggered, this, &MainWindow::extractDrillsAction);
    QAction* rules = tools->addAction(tr("Comprobar &reglas..."));
    rules->setIcon(originalIcon("rules"));
    connect(rules, &QAction::triggered, this, &MainWindow::rulesCheck);
    QAction* align = tools->addAction(tr("&Alinear objetos..."));
    align->setIcon(originalIcon("align"));
    connect(align, &QAction::triggered, this, &MainWindow::alignObjects);
    QAction* dbl = tools->addAction(tr("Doble &faz (espejar)..."));
    dbl->setIcon(originalIcon("doubleside"));
    connect(dbl, &QAction::triggered, this, &MainWindow::doubleSided);

    tipAction(exportPdfAction, tr("Exportar la vista actual como PDF vectorial."));
    tipAction(distance, tr("Distancia mínima entre dos objetos y su línea de unión."));
    tipAction(optimal, tr("Calcula el diámetro de fresa más grande que todavía aísla el cobre."));
    tipAction(corners, tr("Agrega marcas en L en las esquinas para alinear la placa."));
    tipAction(punch, tr("Resta los taladros Excellon al cobre del Gerber."));
    tipAction(extract, tr("Extrae los pads circulares de un Gerber como archivo Excellon."));
    tipAction(pcbWizard, tr("Importa un Excellon y le aplica la tabla INF en un solo paso."));
    tipAction(loadInf, tr("Aplica diámetros desde un INF al Excellon seleccionado."));
    tipAction(rules, tr("Busca pares de objetos más cercanos que la separación indicada."));

    auto* toolbar = addToolBar(tr("Principal"));
    toolbar->setMovable(false);
    open->setIcon(originalIcon("folder_gerber"));
    openProject->setIcon(originalIcon("folder"));
    openObj->setIcon(originalIcon("folder_excellon"));
    fit->setIcon(originalIcon("zoom_fit"));
    zin->setIcon(originalIcon("zoom_in"));
    zout->setIcon(originalIcon("zoom_out"));
    isolate->setIcon(originalIcon("isolate"));
    cutout->setIcon(originalIcon("cutout"));
    ncc->setIcon(originalIcon("ncc"));
    paint->setIcon(originalIcon("paint"));
    toolbar->setIconSize(QSize(22, 22));

    toolbar->addAction(newProject);
    toolbar->addAction(saveProject);
    toolbar->addAction(open);
    toolbar->addAction(openProject);
    toolbar->addAction(openObj);
    toolbar->addSeparator();
    toolbar->addAction(fit);
    toolbar->addAction(zin);
    toolbar->addAction(zout);
    toolbar->addSeparator();
    toolbar->addAction(undo);
    toolbar->addAction(redo);

    QMenu* objectsMenu = menuBar()->addMenu(tr("&Objetos"));
    QAction* duplicateObj = objectsMenu->addAction(tr("&Duplicar objeto"));
    duplicateObj->setShortcut(QKeySequence("Ctrl+D"));
    connect(duplicateObj, &QAction::triggered, this, &MainWindow::duplicateSelectedObject);
    QAction* deleteObj = objectsMenu->addAction(tr("&Eliminar objeto"));
    connect(deleteObj, &QAction::triggered, this, &MainWindow::deleteSelectedObject);
    objectsMenu->addSeparator();
    QAction* objectEdit = objectsMenu->addAction(tr("&Editor de geometría..."));
    objectEdit->setIcon(originalIcon("properties"));
    connect(objectEdit, &QAction::triggered, this, &MainWindow::editGeometry);
    QAction* geometryOps = objectsMenu->addAction(tr("&Operaciones de geometría..."));
    geometryOps->setIcon(originalIcon("transform"));
    connect(geometryOps, &QAction::triggered, this, &MainWindow::geometryOperations);
    QAction* excellonEditorAction = objectsMenu->addAction(tr("&Editor Excellon..."));
    connect(excellonEditorAction, &QAction::triggered, this, &MainWindow::excellonEditor);
    QAction* gerberEditorAction =
        objectsMenu->addAction(tr("Editor de &primitivas (Gerber/Geometría)..."));
    connect(gerberEditorAction, &QAction::triggered, this, &MainWindow::gerberEditor);
    QAction* sourceEditor = objectsMenu->addAction(tr("&Editor de fuente..."));
    sourceEditor->setIcon(originalIcon("code_editor"));
    connect(sourceEditor, &QAction::triggered, this, &MainWindow::editSource);
    QAction* moveOrigin = objectsMenu->addAction(tr("Mover a &origen"));
    moveOrigin->setIcon(originalIcon("origin"));
    connect(moveOrigin, &QAction::triggered, this, &MainWindow::moveToOrigin);
    QAction* joinAction = objectsMenu->addAction(tr("&Unir objetos..."));
    joinAction->setIcon(originalIcon("join"));
    connect(joinAction, &QAction::triggered, this, &MainWindow::joinObjects);
    QAction* explodeAction = objectsMenu->addAction(tr("&Explotar objeto"));
    explodeAction->setIcon(originalIcon("explode"));
    connect(explodeAction, &QAction::triggered, this, &MainWindow::explodeObject);
    QAction* jumpAction = objectsMenu->addAction(tr("&Ir a coordenadas..."));
    jumpAction->setIcon(originalIcon("jump_to"));
    connect(jumpAction, &QAction::triggered, this, &MainWindow::jumpToLocation);
    objectsMenu->addSeparator();
    QAction* objectBool = objectsMenu->addAction(tr("&Operaciones booleanas..."));
    objectBool->setIcon(originalIcon("union"));
    connect(objectBool, &QAction::triggered, this, &MainWindow::booleanDialog);

    QMenu* options = menuBar()->addMenu(tr("&Opciones"));
    QAction* prefs = options->addAction(tr("&Preferencias..."));
    prefs->setIcon(originalIcon("pref"));
    connect(prefs, &QAction::triggered, this, &MainWindow::showPreferences);
    options->addSeparator();
    QAction* view3d = view->addAction(tr("Vista &3D..."));
    view3d->setIcon(originalIcon("image"));
    view->addSeparator();
    connect(view3d, &QAction::triggered, this, &MainWindow::show3D);
    QAction* gridAction = options->addAction(tr("&Mostrar rejilla"));
    gridAction->setIcon(originalIcon("grid"));
    gridAction->setCheckable(true);
    gridAction->setChecked(canvas_->gridVisible());
    connect(gridAction, &QAction::toggled, this, [this](bool on) { canvas_->setGridVisible(on); });
    QAction* toolDbAction = options->addAction(tr("Base de datos de &herramientas..."));
    toolDbAction->setIcon(originalIcon("database"));
    connect(toolDbAction, &QAction::triggered, this, &MainWindow::showToolDb);
    options->addSeparator();
    QAction* resetPrefs = options->addAction(tr("&Restablecer preferencias"));
    connect(resetPrefs, &QAction::triggered, this, [this] {
        if (QMessageBox::question(this, tr("Restablecer preferencias"),
                                  tr("¿Volver a los valores predeterminados de colores, "
                                     "unidades, rejilla y tema?")) != QMessageBox::Yes)
            return;
        QSettings settings;
        settings.remove("preferences");
        decimals_ = 3;
        stepsPerCircle_ = 64;
        defaultUnits_ = Units::Mm;
        background_ = QColor(16, 20, 24);
        copperColor_ = QColor(217, 164, 65);
        followColor_ = QColor(111, 168, 220);
        toolpathColor_ = QColor(102, 214, 132);
        canvas_->setColors(background_, copperColor_, followColor_, toolpathColor_);
        canvas_->setGridStep(0.0);
        canvas_->setGridVisible(true);
        canvas_->setWorkspace(BBox{});
        canvas_->setHudVisible(false);
        statusBar()->showMessage(tr("Preferencias restablecidas (el tema se aplica al reiniciar)"), 5000);
    });
    QAction* workspaceAction = options->addAction(tr("Área de &trabajo..."));
    connect(workspaceAction, &QAction::triggered, this, [this] {
        QSettings settings;
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Área de trabajo"));
        auto* form = new QFormLayout(&dialog);
        auto* width = new UnitSpinBox(&dialog);
        width->setRange(0.0, 5000.0);
        width->setDecimals(2);
        width->setSuffix(" mm");
        width->setValue(settings.value("preferences/workspaceW", 200.0).toDouble());
        auto* height = new UnitSpinBox(&dialog);
        height->setRange(0.0, 5000.0);
        height->setDecimals(2);
        height->setSuffix(" mm");
        height->setValue(settings.value("preferences/workspaceH", 200.0).toDouble());
        auto* show = new QCheckBox(tr("Mostrar área de trabajo"), &dialog);
        show->setChecked(canvas_->workspace().valid);
        form->addRow(tr("Ancho:"), width);
        form->addRow(tr("Alto:"), height);
        form->addRow(QString(), show);
        auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        form->addRow(box);
        connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) return;
        settings.setValue("preferences/workspaceW", width->value());
        settings.setValue("preferences/workspaceH", height->value());
        canvas_->setWorkspace(show->isChecked() ? BBox{0.0, 0.0, width->value(), height->value()}
                                                : BBox{});
    });
    QAction* hudAction = options->addAction(tr("Mostrar &HUD"));
    hudAction->setCheckable(true);
    hudAction->setChecked(canvas_->hudVisible());
    connect(hudAction, &QAction::toggled, this, [this](bool on) {
        canvas_->setHudVisible(on);
        QSettings().setValue("preferences/hud", on);
    });
    QAction* advancedAction = options->addAction(tr("Nivel &avanzado"));
    advancedAction->setCheckable(true);
    advancedAction->setChecked(advancedLevel_);
    connect(advancedAction, &QAction::toggled, this, [this](bool on) {
        advancedLevel_ = on;
        savePreferences();
        applyUiLevel();
    });
    QAction* snapAction = options->addAction(tr("Snap a &rejilla"));
    snapAction->setCheckable(true);
    snapAction->setChecked(snapGrid_);
    connect(snapAction, &QAction::toggled, this, [this](bool on) {
        snapGrid_ = on;
        savePreferences();
        statusBar()->showMessage(on ? tr("Snap a rejilla activado") : tr("Snap a rejilla desactivado"), 3000);
    });
    QAction* inchAction = options->addAction(tr("Mostrar en &pulgadas"));
    inchAction->setCheckable(true);
    inchAction->setChecked(displayInch_);
    connect(inchAction, &QAction::toggled, this, [this](bool on) {
        const bool wasInch = displayInch_;
        displayInch_ = on;
        UnitSpinBox::setDisplayInch(on);
        if (wasInch != on) convertToolTableUnits(on);
        if (unitsLabel_) unitsLabel_->setText(on ? "in" : "mm");
        savePreferences();
        refreshProperties();
        statusBar()->showMessage(on ? tr("Unidades: pulgadas") : tr("Unidades: mm"), 3000);
    });
    QAction* darkAction = options->addAction(tr("Tema &oscuro"));
    darkAction->setCheckable(true);
    darkAction->setChecked(darkTheme_);
    connect(darkAction, &QAction::toggled, this, [this](bool on) {
        applyTheme(on);
        savePreferences();
    });

    QMenu* help = menuBar()->addMenu(tr("A&yuda"));
    QAction* about = help->addAction(tr("Acerca de"));
    about->setIcon(originalIcon("about"));
    about->setToolTip(tr("Información de la aplicación y licencia."));
    about->setStatusTip(tr("Información de la aplicación y licencia."));
    connect(about, &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("FlatCAM C++"),
                           tr("<b>FlatCAM C++ 0.3.0</b> · build 2026-11-04<br>"
                              "Port nativo de FlatCAM 2024.4 a C++20 + Qt6 + GEOS.<br>"
                              "Gerber, Excellon, G-code, importadores y herramientas CAM.<br><br>"
                              "Licencia GPLv3, igual que el proyecto original."));
    });

    // --- Barra de herramientas CAM/edición (iconos originales de FlatCAM)
    auto* toolsBar = addToolBar(tr("Herramientas CAM"));
    toolsBar->setMovable(false);
    toolsBar->setIconSize(QSize(22, 22));
    auto addBar = [this, toolsBar](const QString& icon, const QString& text, auto slot,
                                   const QString& tip = QString()) {
        QAction* action = toolsBar->addAction(originalIcon(icon), text);
        connect(action, &QAction::triggered, this, slot);
        action->setToolTip(tip.isEmpty() ? text : tip);
        action->setStatusTip(action->toolTip());
        return action;
    };

    addBar("edit", tr("Editor de geometría"), &MainWindow::editGeometry,
           tr("Abrir el editor de primitivas del objeto seleccionado."));
    addBar("copy", tr("Duplicar"), &MainWindow::duplicateSelectedObject,
           tr("Duplicar el objeto seleccionado (Ctrl+D)."));
    addBar("trash", tr("Eliminar"), &MainWindow::deleteSelectedObject,
           tr("Eliminar el objeto seleccionado."));
    addBar("distance_min", tr("Distancia mínima"), &MainWindow::distanceTool,
           tr("Medir la distancia mínima entre dos objetos."));
    addBar("origin", tr("Mover a origen"), &MainWindow::moveToOrigin,
           tr("Llevar el objeto seleccionado a (0, 0)."));
    addBar("jump_to", tr("Ir a coordenadas"), &MainWindow::jumpToLocation,
           tr("Centrar la vista en una coordenada."));
    addBar("distance", tr("Medir distancia"), &MainWindow::distanceMeasure,
           tr("Medir una distancia entre dos coordenadas."));
    addBar("locate", tr("Definir origen"), &MainWindow::setDisplayOrigin,
           tr("Mover el origen de visualización del visor."));
    addBar("close_edit_file", tr("Cerrar editor"), &MainWindow::closeToolTab,
           tr("Cerrar la pestaña de herramienta activa."));
    addBar("select_all", tr("Seleccionar todo"), &MainWindow::selectAllObjects,
           tr("Seleccionar todos los objetos del proyecto."));
    toolsBar->addSeparator();
    addBar("set_color", tr("Color del cobre"), [this] {
        const QColor color = QColorDialog::getColor(copperColor_, this, tr("Color del cobre"));
        if (!color.isValid()) return;
        copperColor_ = color;
        canvas_->setColors(background_, copperColor_, followColor_, toolpathColor_);
        savePreferences();
    }, tr("Cambiar el color de relleno del cobre."));
    addBar("bookmarks", tr("Marcador"), &MainWindow::addBookmark,
           tr("Guardar la vista actual como marcador."));
    addBar("locate", tr("Localizar objeto"), [this] {
        auto* item = objects_->currentItem();
        if (!item) return;
        const BBox b = canvas_->layerBounds(item->data(0, Qt::UserRole).toInt());
        if (b.valid) canvas_->fitTo(b);
    }, tr("Hacer zoom al objeto seleccionado."));
    toolsBar->addSeparator();
    addBar("isolate", tr("Aislamiento"), [this] {
        showToolTab(tr("Herramienta de aislamiento"));
    }, tr("Abrir la herramienta de aislamiento."));
    addBar("ncc", tr("NCC"), [this] { showToolTab(tr("Herramienta NCC")); },
           tr("Abrir la herramienta NCC."));
    addBar("cutout", tr("Corte"), [this] { showToolTab(tr("Herramienta de corte")); },
           tr("Abrir la herramienta de corte."));
    addBar("paint", tr("Pintura"), [this] { showToolTab(tr("Herramienta de pintura")); },
           tr("Abrir la herramienta de pintura."));
    addBar("drill", tr("Perforación"), [this] {
        showToolTab(tr("Herramienta de perforación"));
    }, tr("Abrir la herramienta de perforación."));
    addBar("milling", tr("Fresado"), [this] { showToolTab(tr("Herramienta de fresado")); },
           tr("Abrir la herramienta de fresado."));
    toolsBar->addSeparator();
    addBar("panelize", tr("Panelizar"), &MainWindow::panelize,
           tr("Duplicar el objeto en una grilla de panel."));
    addBar("film", tr("Film"), &MainWindow::createFilm,
           tr("Combinar las capas visibles en un film."));
    addBar("solderpaste", tr("Solder Paste"), [this] { showToolTab(tr("Solder Paste")); },
           tr("Abrir la herramienta de pasta de soldar."));
    addBar("clean", tr("Thieving"), [this] { showToolTab(tr("Copper Thieving")); },
           tr("Abrir Copper Thieving."));
    addBar("rules", tr("Reglas"), &MainWindow::rulesCheck,
           tr("Comprobar separaciones mínimas."));
    addBar("print", tr("Imprimir"), &MainWindow::printView,
           tr("Imprimir la vista actual."));
    toolsBar->addSeparator();
    addBar("calculator", tr("Calculadoras"), &MainWindow::showCalculators,
           tr("Abrir las calculadoras."));
    addBar("transform", tr("Transformaciones"), &MainWindow::transformSelectedObject,
           tr("Abrir el diálogo de transformaciones."));
    addBar("qrcode", tr("QR Code"), [this] { showToolTab(tr("QR Code")); },
           tr("Abrir el generador de códigos QR."));
    addBar("punch", tr("Punch Gerber"), &MainWindow::punchGerberAction,
           tr("Restar los taladros al cobre."));
    addBar("extract_drill", tr("Extraer taladros"), &MainWindow::extractDrillsAction,
           tr("Extraer pads circulares como Excellon."));
    addBar("fiducials", tr("Fiduciales"), &MainWindow::addFiducials,
           tr("Agregar fiduciales al contorno."));
    addBar("calibrate", tr("Calibrar"), &MainWindow::calibrate,
           tr("Calibrar la escala del objeto."));

    help->addSeparator();
    QAction* reportBug = help->addAction(tr("&Reportar un problema"));
    connect(reportBug, &QAction::triggered, this, [this] {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Reportar un problema"));
        auto* layout = new QVBoxLayout(&dialog);
        auto* message = new QPlainTextEdit(&dialog);
        message->setPlaceholderText(tr("Describí qué estabas haciendo y qué falló..."));
        message->setMaximumHeight(140);
        layout->addWidget(message);
        auto* details = new QPlainTextEdit(&dialog);
        details->setReadOnly(true);
        details->setPlainText(
            tr("FlatCAM C++ 0.3.0\nBuild: 2026-11-04\n"
               "Qt: %1\nSistema: %2\nConfig: %3")
                .arg(QString::fromLatin1(qVersion()))
                .arg(QSysInfo::prettyProductName())
                .arg(QSettings().fileName()));
        layout->addWidget(details);
        auto* copy = new QPushButton(tr("Copiar informe"), &dialog);
        layout->addWidget(copy);
        auto* box = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        layout->addWidget(box);
        connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(copy, &QPushButton::clicked, &dialog, [message, details] {
            QApplication::clipboard()->setText(message->toPlainText() + "\n\n" +
                                               details->toPlainText());
        });
        dialog.resize(640, 420);
        dialog.exec();
    });
    QAction* bookmarkManager = help->addAction(tr("&Gestor de marcadores"));
    connect(bookmarkManager, &QAction::triggered, this, [this] {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Marcadores"));
        auto* layout = new QVBoxLayout(&dialog);
        auto* list = new QListWidget(&dialog);
        layout->addWidget(list);
        QStringList entries = QSettings().value("bookmarks/list").toStringList();
        for (const QString& entry : entries) list->addItem(entry.section('|', 0, 0));
        auto* buttons = new QHBoxLayout();
        auto* go = new QPushButton(tr("Ir"), &dialog);
        auto* remove = new QPushButton(tr("Eliminar"), &dialog);
        buttons->addWidget(go);
        buttons->addWidget(remove);
        layout->addLayout(buttons);
        auto* box = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        layout->addWidget(box);
        connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(go, &QPushButton::clicked, &dialog, [this, list, entries] {
            const int index = list->currentRow();
            if (index < 0 || index >= entries.size()) return;
            const QStringList parts = entries[index].split('|');
            if (parts.size() == 4)
                canvas_->setView(parts[3].toDouble(),
                                 QPointF(parts[1].toDouble(), parts[2].toDouble()));
        });
        connect(remove, &QPushButton::clicked, &dialog, [this, list, &entries] {
            const int index = list->currentRow();
            if (index < 0 || index >= entries.size()) return;
            entries.removeAt(index);
            delete list->takeItem(index);
            QSettings().setValue("bookmarks/list", entries);
            rebuildBookmarksMenu();
        });
        dialog.resize(520, 380);
        dialog.exec();
    });
    QAction* gerberSpec = help->addAction(tr("Especificación &Gerber"));
    connect(gerberSpec, &QAction::triggered, this, [] {
        QDesktopServices::openUrl(QUrl("https://www.ucamco.com/files/downloads/file_en/456/gerber-layer-format-specification-revision-2024-05_en.pdf"));
    });
    QAction* excellonSpec = help->addAction(tr("Especificación &Excellon"));
    connect(excellonSpec, &QAction::triggered, this, [] {
        QDesktopServices::openUrl(QUrl("https://web.archive.org/web/20210127112422/https://www.excellon.com/manuals/program.htm"));
    });
    QAction* shortcutsAction = help->addAction(tr("&Atajos de teclado"));
    connect(shortcutsAction, &QAction::triggered, this, [this] {
        QMessageBox box(this);
        box.setWindowTitle(tr("Atajos de teclado"));
        box.setTextFormat(Qt::RichText);
        box.setText(tr(
            "<b>Archivo</b><br>"
            "Ctrl+O: abrir · Ctrl+S: guardar proyecto · Ctrl+N: nuevo<br><br>"
            "<b>Edición</b><br>"
            "Ctrl+Z / Ctrl+Y: deshacer/rehacer · Ctrl+A: seleccionar todo · "
            "Ctrl+D: duplicar · Supr: eliminar<br><br>"
            "<b>Herramientas</b><br>"
            "Ctrl+I: aislamiento · Ctrl+K: corte · Ctrl+N: NCC · Ctrl+P: pintura · "
            "Ctrl+E: exportar G-code<br><br>"
            "<b>Vista</b><br>"
            "F: ajustar · Ctrl++ / Ctrl+-: zoom · Rueda: zoom · Arrastrar: desplazar"));
        box.exec();
    });
    QAction* manualAction = help->addAction(tr("&Manual (README)"));
    connect(manualAction, &QAction::triggered, this, [] {
        const QString local =
            QFileInfo(QCoreApplication::applicationDirPath() + "/../../README.md")
                .absoluteFilePath();
        if (QFileInfo::exists(local))
            QDesktopServices::openUrl(QUrl::fromLocalFile(local));
        else
            QDesktopServices::openUrl(QUrl("https://github.com/dnasta/FlatCAM_Cpp"));
    });
}

// ============================================================================
// Archivos y proyectos
// ============================================================================

/// @brief Abre el diálogo para seleccionar un proyecto `.FlatPrj`.
void MainWindow::chooseProject() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Abrir proyecto"), lastDir_,
        tr("Proyecto FlatCAM (*.FlatPrj);;Todos (*)"));
    if (!path.isEmpty()) openProject(path);
}

/// @brief Abre el diálogo de archivos y carga la selección.
void MainWindow::chooseFile() {
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Abrir archivos"), lastDir_,
        tr("Archivos PCB (*.gbr *.ger *.gtl *.gbl *.gts *.gbs *.gko *.drl *.xln *.exc *.ncd "
           "*.svg *.dxf *.pdf *.hpgl *.hpg *.plt *.nc *.gcode);;Todos (*)"));
    if (!paths.isEmpty()) openFiles(paths);
}

/// @brief Abre un archivo individual detectando su tipo.
bool MainWindow::openPath(const QString& path) { return openFiles(QStringList{path}); }

/// @brief Carga varios archivos como capas y actualiza el árbol.
bool MainWindow::openFiles(const QStringList& paths) {
    if (paths.isEmpty()) return false;
    lastDir_ = QFileInfo(paths.first()).absolutePath();

    // A single .FlatPrj keeps its dedicated streaming loader.
    if (paths.size() == 1 && QFileInfo(paths.first()).suffix().compare("FlatPrj", Qt::CaseInsensitive) == 0)
        return openProject(paths.first());

    lastExcellon_.reset();
    lastGerber_.reset();
    lastIsolation_.reset();
    lastCutout_.reset();
    lastNcc_.reset();
    lastPaint_.reset();
    lastDrillJob_.reset();
    gerberLayers_.clear();
    excellonLayers_.clear();
    projectPath_.clear();
    projectDirty_ = false;
    resetObjectGroups();
    canvas_->beginGeometry();

    int loaded = 0;
    QStringList errors;
    for (const QString& path : paths) {
        if (QFileInfo(path).suffix().compare("FlatPrj", Qt::CaseInsensitive) == 0) {
            errors << tr("%1: abra los proyectos de a uno").arg(QFileInfo(path).fileName());
            continue;
        }
        try {
            const QString suffix = QFileInfo(path).suffix().toLower();
            const bool gcodeFile = suffix == "nc" || suffix == "gcode" || suffix == "tap" ||
                                   suffix == "ngc";
            const bool hpglFile = suffix == "hpgl" || suffix == "hpg" || suffix == "plt";
            bool ok = false;
            if (gcodeFile) ok = addGCodeLayer(path);
            else if (suffix == "pdf") ok = importPdfPath(path);
            else if (suffix == "svg" || suffix == "dxf")
                ok = importVectorPath(path, suffix == "svg");
            else if (hpglFile) {
                addVectorGeometry(QFileInfo(path).fileName(), importHpgl(path.toStdString()));
                ok = true;
            } else {
                ok = looksLikeExcellon(path) ? addExcellon(path) : addGerber(path);
            }
            if (ok) {
                ++loaded;
                recordRecent(path);
            }
        } catch (const std::exception& e) {
            errors << tr("%1: %2").arg(QFileInfo(path).fileName(), QString::fromUtf8(e.what()));
        }
    }

    canvas_->endGeometry();
    canvas_->setToolpaths({});
    if (!objectItems().empty()) objects_->setCurrentItem(objectItems().front());

    setWindowTitle(loaded == 1
                       ? tr("FlatCAM C++ — %1").arg(QFileInfo(paths.first()).fileName())
                       : tr("FlatCAM C++ — %1 archivos").arg(loaded));
    statusLabel_->setText(tr("%1 archivos · %2 objetos").arg(loaded).arg(objectItems().size()));
    statusBar()->showMessage(tr("Cargados %1 archivos").arg(loaded), 5000);

    if (!errors.isEmpty()) {
        info_->setPlainText(info_->toPlainText() + "\n--- ERRORES ---\n" + errors.join("\n"));
        QMessageBox::warning(this, tr("Algunos archivos no se cargaron"), errors.join("\n"));
    }
    return loaded > 0;
}

/// @brief Carga un proyecto, sus objetos y sus opciones.
bool MainWindow::openProject(const QString& path) {
    try {
        projectPath_ = path;
        const ProjectInfo projectInfo = inspectProject(path.toStdString());
        if (!projectInfo.options.empty())
            loadToolTablesJson(QString::fromStdString(projectInfo.options));
        lastExcellon_.reset();
        lastGerber_.reset();
        lastIsolation_.reset();
        lastCutout_.reset();
        lastNcc_.reset();
        lastPaint_.reset();
        lastDrillJob_.reset();
        gerberLayers_.clear();
        excellonLayers_.clear();
        resetObjectGroups();

        canvas_->beginGeometry();
        std::ostringstream os;
        os << "PROYECTO\n";
        os << "archivo     : " << QFileInfo(path).fileName().toStdString() << "\n";

        int index = 0;
        const auto summaries = fc::forEachProjectObject(
            path.toStdString(), [&](fc::ProjectObject&& obj) {
                const int layerId = canvas_->appendGeometry(obj.solids, obj.follows, false);

                double area = 0.0;
                for (const fc::Geom& g : obj.solids) area += g.area();

                addObjectRow(QString::fromStdString(obj.name), QString::fromStdString(obj.kind),
                             QString::number(area, 'f', 2), layerId);
                if (!obj.text.empty()) objectText_[layerId] = QString::fromStdString(obj.text);
                if (!obj.properties.empty()) {
                    const QJsonDocument doc = QJsonDocument::fromJson(
                        QByteArray::fromStdString(obj.properties));
                    if (doc.isObject()) {
                        for (auto it = doc.object().begin(); it != doc.object().end(); ++it)
                            objectProps_[layerId][it.key()] = it.value().toString();
                    }
                }
                if (!obj.sourceFile.empty())
                    sourcePaths_[layerId] = QString::fromStdString(obj.sourceFile);

                if (obj.kind == "gerber" && !lastGerber_) {
                    fc::GerberResult gerber;
                    gerber.solids = obj.solids;
                    gerber.follows = obj.follows;
                    gerber.solid = fc::Geom::unionOf(obj.solids);
                    gerber.bounds = gerber.solid.bounds();
                    gerber.fileUnits = obj.units;
                    lastGerber_ = gerber;
                }

                ++index;
                QApplication::processEvents();
                return true;
            });

        canvas_->endGeometry();
        canvas_->setToolpaths({});
        if (!objectItems().empty())
            objects_->setCurrentItem(objectItems().front());

        os << "objetos     : " << summaries.size() << "\n";
        for (const fc::ProjectObjectSummary& summary : summaries) {
            os << "  [" << summary.kind << "] " << summary.name << "  (" << fc::toString(summary.units)
               << ", área " << summary.area << ")\n";
        }
        setInfo(QString::fromStdString(os.str()));

        setWindowTitle(tr("FlatCAM C++ — %1").arg(QFileInfo(path).fileName()));
        statusLabel_->setText(tr("%1 · %2 objetos").arg(QFileInfo(path).fileName()).arg(summaries.size()));
        statusBar()->showMessage(tr("Proyecto cargado (objetos en streaming)"), 5000);
        recordRecent(path);
        projectDirty_ = false;
        return true;
    } catch (const std::exception& e) {
        showError(QString::fromUtf8(e.what()));
        return false;
    }
}

/// @brief Muestra la lista de objetos de un proyecto para abrir uno.
void MainWindow::chooseProjectObject() {
    QString path = projectPath_;
    if (path.isEmpty()) {
        path = QFileDialog::getOpenFileName(this, tr("Proyecto FlatCAM"), lastDir_,
                                            tr("Proyecto FlatCAM (*.FlatPrj);;Todos (*)"));
        if (path.isEmpty()) return;
        projectPath_ = path;
    }

    try {
        const fc::ProjectInfo info = fc::inspectProject(path.toStdString());
        QStringList names;
        for (const fc::ProjectObjectInfo& obj : info.objects) {
            names << QString("[%1] %2").arg(QString::fromStdString(obj.kind),
                                           QString::fromStdString(obj.name));
        }
        if (names.isEmpty()) return;

        bool ok = false;
        const QString chosen = QInputDialog::getItem(this, tr("Abrir objeto de proyecto"),
                                                     tr("Objeto:"), names, 0, false, &ok);
        if (!ok) return;
        const int index = names.indexOf(chosen);
        if (index < 0) return;

        const fc::ProjectObject obj =
            fc::loadProjectObject(path.toStdString(), static_cast<std::size_t>(index));

        canvas_->setGeometryData(obj.solids, obj.follows);
        canvas_->setToolpaths({});
        lastIsolation_.reset();
        lastCutout_.reset();
        lastNcc_.reset();
        lastPaint_.reset();

        std::ostringstream os;
        os << "OBJETO DE PROYECTO\n";
        os << "nombre : " << obj.name << "\n";
        os << "tipo   : " << obj.kind << "\n";
        os << "sólidos: " << obj.solids.size() << "   recorridos: " << obj.follows.size() << "\n";

        if (obj.kind == "gerber") {
            fc::GerberResult gerber;
            gerber.solids = obj.solids;
            gerber.follows = obj.follows;
            gerber.solid = fc::Geom::unionOf(obj.solids);
            gerber.bounds = gerber.solid.bounds();
            gerber.fileUnits = obj.units;
            lastGerber_ = gerber;
            lastExcellon_.reset();
            os << "área   : " << gerber.solid.area() << "\n";
        } else {
            lastGerber_.reset();
            lastExcellon_.reset();
        }
        setInfo(QString::fromStdString(os.str()));
        statusBar()->showMessage(tr("Objeto cargado: %1").arg(QString::fromStdString(obj.name)), 5000);
    } catch (const std::exception& e) {
        showError(QString::fromUtf8(e.what()));
    }
}

/// @brief Reacciona a cambios de nombre o visibilidad en el árbol.
void MainWindow::objectItemChanged(QTreeWidgetItem* item, int column) {
    if (updatingTree_ || !item) return;
    const int layer = item->data(0, Qt::UserRole).toInt();
    refreshProperties();
}

// ============================================================================
// Objetos, propiedades y capas
// ============================================================================

/// @brief Reconstruye el panel de propiedades del objeto activo.
void MainWindow::refreshProperties() {
    if (!properties_) return;
    properties_->clear();
    QTreeWidgetItem* item = objects_->currentItem();

    auto addRow = [this](const QString& key, const QString& value, bool editable = false) {
        auto* row = new QTreeWidgetItem(properties_);
        row->setText(0, key);
        row->setText(1, value);
        if (editable) row->setFlags(row->flags() | Qt::ItemIsEditable);
    };

    if (!item) {
        addRow(tr("Estado"), tr("sin selección"));
        return;
    }

    addRow(tr("Nombre"), item->text(1), true);
    addRow(tr("Tipo"), item->text(2));
    addRow(tr("Área"), item->text(3) + " mm²");
    addRow(tr("Visible"), item->data(0, Qt::UserRole + 1).toBool() ? tr("sí") : tr("no"));

    const int layer = item->data(0, Qt::UserRole).toInt();
    const fc::BBox b = canvas_->layerBounds(layer);
    if (b.valid) {
        addRow(tr("X"), QString("%1 … %2").arg(formatLength(b.xmin, 3), formatLength(b.xmax, 3)));
        addRow(tr("Y"), QString("%1 … %2").arg(formatLength(b.ymin, 3), formatLength(b.ymax, 3)));
        addRow(tr("Ancho"), lengthText(b.width(), 3) + (displayInch_ ? " in" : " mm"));
        addRow(tr("Alto"), lengthText(b.height(), 3) + (displayInch_ ? " in" : " mm"));
    }
    const auto git = gerberLayers_.find(layer);
    if (git != gerberLayers_.end()) {
        const fc::GerberResult& g = git->second;
        addRow(tr("Unidades"), QString::fromStdString(fc::toString(g.fileUnits)));
        addRow(tr("Aperturas"), QString::number(g.apertures.size()));
        addRow(tr("Flashes"), QString::number(g.flashes));
        addRow(tr("Segmentos"), QString::number(g.segments));
        addRow(tr("Regiones"), QString::number(g.regions));
        addRow(tr("Sólidos"), QString::number(g.solids.size()));
        addRow(tr("Recorridos"), QString::number(g.follows.size()));
        addRow(tr("Área cobre"), QString::number(g.solid.area(), 'f', 3) + " mm²");
        for (const std::string& warning : g.warnings) addRow(tr("Aviso"), QString::fromStdString(warning));
    }
    const auto eit = excellonLayers_.find(layer);
    if (eit != excellonLayers_.end()) {
        const fc::ExcellonResult& e = eit->second;
        addRow(tr("Unidades"), QString::fromStdString(fc::toString(e.fileUnits)));
        addRow(tr("Taladros"), QString::number(e.drillCount));
        addRow(tr("Ranuras"), QString::number(e.slotCount));
        addRow(tr("Herramientas"), QString::number(e.tools.size()));
        for (const auto& [number, tool] : e.tools) {
            addRow(tr("T%1").arg(number),
                   tr("Ø %1 %2 · %3 taladros · %4 ranuras")
                       .arg(lengthText(tool.diameter, 3), displayInch_ ? "in" : "mm")
                       .arg(tool.drills.size())
                       .arg(tool.slotSegments.size()));
        }
        for (const std::string& warning : e.warnings) addRow(tr("Aviso"), QString::fromStdString(warning));
    }
    if (item->text(2) == "gcode") {
        addRow(tr("Recorridos"), QString::number(canvas_->layerFollows(layer).size()));
    }
    const auto props = objectProps_.find(layer);
    if (props != objectProps_.end()) {
        for (const auto& [key, value] : props->second) {
            if (key.startsWith("__")) continue;
            addRow(key, value, true);
        }
    }
}

/// @brief Elimina los objetos seleccionados con undo.
void MainWindow::deleteSelectedObject() {
    const auto selected = objects_->selectedItems();
    if (selected.size() > 1) {
        struct Entry {
            int id = 0;
            QString name;
            QString kind;
            QString area;
            bool visible = true;
            Canvas::LayerSnapshot snapshot;
        };
        std::vector<Entry> entries;
        for (QTreeWidgetItem* row : selected) {
            const int id = row->data(0, Qt::UserRole).toInt();
            if (id <= 0) continue;
            entries.push_back({id, row->text(1), row->text(2), row->text(3),
                               row->data(0, Qt::UserRole + 1).toBool(),
                               canvas_->layerSnapshot(id)});
        }
        if (entries.empty()) return;
        auto remove = [this, entries] {
            for (const Entry& entry : entries) {
                canvas_->removeLayer(entry.id);
                gerberLayers_.erase(entry.id);
                excellonLayers_.erase(entry.id);
                sourcePaths_.erase(entry.id);
                if (QTreeWidgetItem* row = findObjectItem(entry.id)) delete row;
            }
            refreshSourceCombos();
            refreshProperties();
        };
        auto restore = [this, entries] {
            for (const Entry& entry : entries) {
                canvas_->insertLayer(entry.snapshot);
                addObjectRow(entry.name, entry.kind, entry.area, entry.id, entry.visible);
            }
            refreshProperties();
        };
        undoStack_->push(new LambdaCommand(restore, remove, tr("Eliminar objetos")));
        return;
    }
    QTreeWidgetItem* item = objects_->currentItem();
    if (!item) return;

    const int id = item->data(0, Qt::UserRole).toInt();
    if (id <= 0) return;
    const Canvas::LayerSnapshot snapshot = canvas_->layerSnapshot(id);
    const QString name = item->text(1);
    const QString kind = item->text(2);
    const QString area = item->text(3);
    const bool checked = item->data(0, Qt::UserRole + 1).toBool();

    auto remove = [this, id] {
        canvas_->removeLayer(id);
        gerberLayers_.erase(id);
        excellonLayers_.erase(id);
        if (QTreeWidgetItem* candidate = findObjectItem(id)) delete candidate;
        refreshSourceCombos();
        refreshProperties();
    };

    auto restore = [this, snapshot, name, kind, area, checked] {
        canvas_->insertLayer(snapshot);
        auto* restored = addObjectRow(name, kind, area, snapshot.id, checked);
        objects_->setCurrentItem(restored);
        refreshProperties();
    };

    undoStack_->push(new LambdaCommand(restore, remove, tr("Eliminar objeto")));
}

/// @brief Duplica el objeto activo y su geometría.
void MainWindow::duplicateSelectedObject() {
    QTreeWidgetItem* item = objects_->currentItem();
    if (!item) return;

    const int sourceId = item->data(0, Qt::UserRole).toInt();
    if (sourceId <= 0) return;
    const QString name = item->text(1) + tr(" (copia)");
    const QString kind = item->text(2);
    const QString area = item->text(3);
    const bool checked = item->data(0, Qt::UserRole + 1).toBool();
    auto newId = std::make_shared<int>(0);

    auto addCopy = [this, sourceId, newId, name, kind, area, checked] {
        const int id = canvas_->duplicateLayer(sourceId);
        if (id < 0) return;
        *newId = id;
        if (gerberLayers_.count(sourceId)) gerberLayers_[id] = gerberLayers_[sourceId];
        if (excellonLayers_.count(sourceId)) excellonLayers_[id] = excellonLayers_[sourceId];
        auto* duplicated = addObjectRow(name, kind, area, id, checked);
        objects_->setCurrentItem(duplicated);
    };

    auto removeCopy = [this, newId] {
        canvas_->removeLayer(*newId);
        if (QTreeWidgetItem* candidate = findObjectItem(*newId)) delete candidate;
        refreshProperties();
    };

    undoStack_->push(new LambdaCommand(removeCopy, addCopy, tr("Duplicar objeto")));
}

// ============================================================================
// Editores y operaciones geométricas
// ============================================================================

/// @brief Abre el editor de vértices de la geometría activa.
void MainWindow::editGeometry() {
    auto* item = objects_->currentItem();
    if (!item) {
        QMessageBox::information(this, tr("Editor de geometría"), tr("Seleccione un objeto."));
        return;
    }
    const int id = item->data(0, Qt::UserRole).toInt();
    const std::vector<Geom> beforeSolids = canvas_->layerSolids(id);
    const std::vector<Geom> beforeFollows = canvas_->layerFollows(id);
    if (beforeSolids.empty() && beforeFollows.empty()) {
        QMessageBox::information(this, tr("Editor de geometría"),
                                 tr("Este objeto no tiene geometría editable (por ejemplo, "
                                    "cargado desde un proyecto grande)."));
        return;
    }

    struct ContourRef {
        int kind = 0;  // 0 outer, 1 hole, 2 line
        std::size_t solid = 0;
        std::size_t part = 0;
        std::size_t hole = 0;
        std::size_t line = 0;
        QString label;
    };

    std::vector<std::vector<PolygonContours>> parts(beforeSolids.size());
    for (std::size_t i = 0; i < beforeSolids.size(); ++i) parts[i] = beforeSolids[i].polygons();

    std::vector<Ring> lines;
    for (const Geom& g : beforeFollows) {
        if (!g.valid() || g.empty()) continue;
        if (g.typeId() == GEOS_LINESTRING || g.typeId() == GEOS_LINEARRING) {
            lines.push_back(g.coords());
        } else {
            for (int k = 0; k < g.numGeometries(); ++k) {
                const Geom part = g.geometryN(k);
                if (part.typeId() == GEOS_LINESTRING || part.typeId() == GEOS_LINEARRING)
                    lines.push_back(part.coords());
            }
        }
    }

    std::vector<ContourRef> refs;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        for (std::size_t p = 0; p < parts[i].size(); ++p) {
            ContourRef ref;
            ref.kind = 0;
            ref.solid = i;
            ref.part = p;
            ref.label = tr("Sólido %1 · contorno %2").arg(i).arg(p);
            refs.push_back(ref);
            for (std::size_t h = 0; h < parts[i][p].holes.size(); ++h) {
                ContourRef hole;
                hole.kind = 1;
                hole.solid = i;
                hole.part = p;
                hole.hole = h;
                hole.label = tr("Sólido %1 · agujero %2").arg(i).arg(h);
                refs.push_back(hole);
            }
        }
    }
    for (std::size_t l = 0; l < lines.size(); ++l) {
        ContourRef ref;
        ref.kind = 2;
        ref.line = l;
        ref.label = tr("Recorrido %1").arg(l);
        refs.push_back(ref);
    }
    if (refs.empty()) {
        QMessageBox::information(this, tr("Editor de geometría"), tr("No hay contornos editables."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Editor de geometría"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* combo = new QComboBox(&dialog);
    for (const ContourRef& ref : refs) combo->addItem(ref.label);
    applyTip(combo, tr("Elegí el contorno o recorrido cuyos vértices querés editar."));
    layout->addWidget(combo);

    auto* table = new QTableWidget(0, 2, &dialog);
    table->setHorizontalHeaderLabels({tr("X"), tr("Y")});
    table->horizontalHeader()->setStretchLastSection(true);
    applyTip(table, tr("Coordenadas de los vértices. Doble clic para editar; se reconstruye la "
                       "geometría al aceptar."));
    layout->addWidget(table);

    auto* buttonsRow = new QHBoxLayout();
    auto* addPoint = new QPushButton(tr("Añadir punto"), &dialog);
    auto* removePoint = new QPushButton(tr("Eliminar punto"), &dialog);
    auto* undoPoint = new QPushButton(tr("Deshacer"), &dialog);
    auto* redoPoint = new QPushButton(tr("Rehacer"), &dialog);
    applyTip(addPoint, tr("Agrega un vértice al final del contorno actual."));
    applyTip(removePoint, tr("Elimina la fila/vértice seleccionado."));
    buttonsRow->addWidget(addPoint);
    buttonsRow->addWidget(removePoint);
    buttonsRow->addWidget(undoPoint);
    buttonsRow->addWidget(redoPoint);
    layout->addLayout(buttonsRow);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    std::size_t current = 0;

    auto ringFor = [&](std::size_t index) -> Ring& {
        const ContourRef& ref = refs[index];
        if (ref.kind == 0) return parts[ref.solid][ref.part].outer;
        if (ref.kind == 1) return parts[ref.solid][ref.part].holes[ref.hole];
        return lines[ref.line];
    };

    auto loadTable = [&](std::size_t index) {
        const Ring& ring = ringFor(index);
        table->setRowCount(static_cast<int>(ring.size()));
        for (std::size_t i = 0; i < ring.size(); ++i) {
            table->setItem(static_cast<int>(i), 0, new QTableWidgetItem(QString::number(ring[i].x, 'f', 4)));
            table->setItem(static_cast<int>(i), 1, new QTableWidgetItem(QString::number(ring[i].y, 'f', 4)));
        }
    };

    auto saveTable = [&](std::size_t index) {
        Ring& ring = ringFor(index);
        ring.clear();
        for (int i = 0; i < table->rowCount(); ++i) {
            auto* x = table->item(i, 0);
            auto* y = table->item(i, 1);
            if (!x || !y) continue;
            ring.push_back({x->text().toDouble(), y->text().toDouble()});
        }
    };

    using GeometrySnapshot =
        std::pair<std::vector<std::vector<PolygonContours>>, std::vector<Ring>>;
    std::vector<GeometrySnapshot> history;
    int historyPos = -1;
    auto pushHistory = [&] {
        history.resize(static_cast<std::size_t>(historyPos + 1));
        history.push_back({parts, lines});
        historyPos = static_cast<int>(history.size()) - 1;
    };
    auto restoreHistory = [&](int pos) {
        if (pos < 0 || pos >= static_cast<int>(history.size())) return;
        historyPos = pos;
        parts = history[static_cast<std::size_t>(pos)].first;
        lines = history[static_cast<std::size_t>(pos)].second;
        loadTable(current);
    };
    pushHistory();
    connect(undoPoint, &QPushButton::clicked, this, [&] {
        if (historyPos > 0) restoreHistory(historyPos - 1);
    });
    connect(redoPoint, &QPushButton::clicked, this, [&] {
        if (historyPos + 1 < static_cast<int>(history.size())) restoreHistory(historyPos + 1);
    });

    loadTable(0);
    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [&](int index) {
                if (index < 0) return;
                pushHistory();
                saveTable(current);
                current = static_cast<std::size_t>(index);
                loadTable(current);
            });

    connect(addPoint, &QPushButton::clicked, &dialog, [&] {
        pushHistory();
        const int row = table->rowCount();
        table->insertRow(row);
        table->setItem(row, 0, new QTableWidgetItem("0"));
        table->setItem(row, 1, new QTableWidgetItem("0"));
    });
    connect(removePoint, &QPushButton::clicked, &dialog, [&] {
        pushHistory();
        const int row = table->currentRow();
        if (row >= 0) table->removeRow(row);
    });

    if (dialog.exec() != QDialog::Accepted) return;
    saveTable(current);

    std::vector<Geom> afterSolids;
    for (const auto& solidParts : parts) {
        std::vector<Geom> pieces;
        for (const PolygonContours& pc : solidParts) {
            if (pc.outer.size() < 3) continue;
            pieces.push_back(Geom::polygon(pc.outer, pc.holes).fixed());
        }
        if (!pieces.empty()) afterSolids.push_back(Geom::unionOf(pieces));
    }
    std::vector<Geom> afterFollows;
    for (const Ring& line : lines) {
        if (line.size() >= 2) afterFollows.push_back(Geom::line(line));
    }

    auto apply = [this, id](const std::vector<Geom>& solids, const std::vector<Geom>& follows) {
        canvas_->setLayerGeometry(id, solids, follows);
        refreshProperties();
    };

    undoStack_->push(new LambdaCommand([apply, beforeSolids, beforeFollows] {
                                          apply(beforeSolids, beforeFollows);
                                      },
                                      [apply, afterSolids, afterFollows] {
                                          apply(afterSolids, afterFollows);
                                      },
                                      tr("Editar geometría")));
}

/// @brief Abre el diálogo de operaciones geométricas (buffer, escalar...).
void MainWindow::geometryOperations() {
    auto* item = objects_->currentItem();
    if (!item) {
        QMessageBox::information(this, tr("Operaciones de geometría"),
                                 tr("Seleccioná un objeto."));
        return;
    }
    const int id = item->data(0, Qt::UserRole).toInt();
    const std::vector<Geom> beforeSolids = canvas_->layerSolids(id);
    const std::vector<Geom> beforeFollows = canvas_->layerFollows(id);
    if (beforeSolids.empty() && beforeFollows.empty()) {
        QMessageBox::information(this, tr("Operaciones de geometría"),
                                 tr("El objeto no tiene geometría editable."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Operaciones de geometría"));
    auto* form = new QFormLayout(&dialog);
    auto* simplify = new UnitSpinBox(&dialog);
    simplify->setRange(0.0, 10.0);
    simplify->setDecimals(4);
    simplify->setValue(0.0);
    simplify->setSuffix(" mm");
    auto* buffer = new UnitSpinBox(&dialog);
    buffer->setRange(-20.0, 20.0);
    buffer->setDecimals(3);
    buffer->setValue(0.0);
    buffer->setSuffix(" mm");
    auto* scale = new UnitSpinBox(&dialog);
    scale->setRange(0.01, 100.0);
    scale->setDecimals(4);
    scale->setValue(1.0);
    auto* rotate = new UnitSpinBox(&dialog);
    rotate->setRange(-360.0, 360.0);
    rotate->setDecimals(2);
    rotate->setSuffix(" °");
    auto* skewX = new UnitSpinBox(&dialog);
    skewX->setRange(-10.0, 10.0);
    skewX->setDecimals(4);
    skewX->setValue(0.0);
    auto* skewY = new UnitSpinBox(&dialog);
    skewY->setRange(-10.0, 10.0);
    skewY->setDecimals(4);
    skewY->setValue(0.0);
    auto* booleanOp = new QComboBox(&dialog);
    booleanOp->addItems({tr("Ninguna"), tr("Unión"), tr("Diferencia"), tr("Intersección")});
    auto* comboB = new QComboBox(&dialog);
    std::vector<int> idsB;
    for (QTreeWidgetItem* row : objectItems()) {
        const int other = row->data(0, Qt::UserRole).toInt();
        if (other == id) continue;
        if (canvas_->layerSolids(other).empty()) continue;
        comboB->addItem(row->text(1), other);
        idsB.push_back(other);
    }
    form->addRow(tr("Simplificar:"), simplify);
    form->addRow(tr("Buffer/offset:"), buffer);
    form->addRow(tr("Escalar:"), scale);
    form->addRow(tr("Rotar:"), rotate);
    form->addRow(tr("Skew X:"), skewX);
    form->addRow(tr("Skew Y:"), skewY);
    form->addRow(tr("Booleana:"), booleanOp);
    form->addRow(tr("Objeto B:"), comboB);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(box);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    Geom work = Geom::unionOf(beforeSolids);
    if (!work.valid() || work.empty()) {
        work = Geom::unionOf(beforeFollows);
        if (!work.valid()) return;
    }
    const BBox bounds = canvas_->layerBounds(id);
    const Pt center{bounds.valid ? (bounds.xmin + bounds.xmax) * 0.5 : 0.0,
                    bounds.valid ? (bounds.ymin + bounds.ymax) * 0.5 : 0.0};
    if (simplify->value() > 0.0) work = work.simplify(simplify->value()).fixed();
    if (std::fabs(buffer->value()) > 1e-9) work = work.buffer(buffer->value(), 16);
    if (std::fabs(scale->value() - 1.0) > 1e-9)
        work = work.scale(scale->value(), scale->value(), center);
    if (std::fabs(rotate->value()) > 1e-9) work = work.rotateDeg(rotate->value(), center);
    if (std::fabs(skewX->value()) > 1e-9 || std::fabs(skewY->value()) > 1e-9) {
        const double sx = skewX->value();
        const double sy = skewY->value();
        work = work.transformXY([center, sx, sy](Pt p) {
            const double x = p.x - center.x;
            const double y = p.y - center.y;
            return Pt{center.x + x + sx * y, center.y + y + sy * x};
        });
    }
    if (booleanOp->currentIndex() > 0 && comboB->count() > 0) {
        const int other = comboB->currentData().toInt();
        const Geom b = Geom::unionOf(canvas_->layerSolids(other));
        if (b.valid() && !b.empty()) {
            if (booleanOp->currentIndex() == 1) work = work.unite(b);
            else if (booleanOp->currentIndex() == 2) work = work.difference(b);
            else work = work.intersection(b);
        }
    }
    if (!work.valid() || work.empty()) {
        QMessageBox::information(this, tr("Operaciones de geometría"),
                                 tr("El resultado quedó vacío."));
        return;
    }

    std::vector<Geom> afterSolids;
    std::vector<Geom> afterFollows;
    if (beforeSolids.empty()) {
        afterFollows.push_back(work);
    } else {
        for (const PolygonContours& pc : work.polygons()) {
            if (pc.outer.size() >= 3) afterSolids.push_back(Geom::polygon(pc.outer, pc.holes));
        }
        if (afterSolids.empty()) afterSolids.push_back(work);
    }
    if (!beforeFollows.empty()) {
        for (const Geom& g : beforeFollows) {
            Geom transformed = g;
            if (std::fabs(scale->value() - 1.0) > 1e-9)
                transformed = transformed.scale(scale->value(), scale->value(), center);
            if (std::fabs(rotate->value()) > 1e-9)
                transformed = transformed.rotateDeg(rotate->value(), center);
            if (std::fabs(skewX->value()) > 1e-9 || std::fabs(skewY->value()) > 1e-9) {
                const double sx = skewX->value();
                const double sy = skewY->value();
                transformed = transformed.transformXY([center, sx, sy](Pt p) {
                    const double x = p.x - center.x;
                    const double y = p.y - center.y;
                    return Pt{center.x + x + sx * y, center.y + y + sy * x};
                });
            }
            afterFollows.push_back(transformed);
        }
    }

    canvas_->setLayerGeometry(id, afterSolids, afterFollows);
    auto apply = [this, id](const std::vector<Geom>& solids, const std::vector<Geom>& follows) {
        canvas_->setLayerGeometry(id, solids, follows);
        refreshProperties();
    };
    undoStack_->push(new LambdaCommand([apply, beforeSolids, beforeFollows] {
                                          apply(beforeSolids, beforeFollows);
                                      },
                                      [apply, afterSolids, afterFollows] {
                                          apply(afterSolids, afterFollows);
                                      },
                                      tr("Operaciones de geometría")));
    statusBar()->showMessage(tr("Geometría transformada"), 4000);
}

/// @brief Abre el diálogo de unión, diferencia e intersección.
void MainWindow::booleanDialog() {
    auto* item = objects_->currentItem();
    if (!item) {
        QMessageBox::information(this, tr("Booleanas"), tr("Seleccione el objeto A."));
        return;
    }
    const int idA = item->data(0, Qt::UserRole).toInt();
    const std::vector<Geom> solidsA = canvas_->layerSolids(idA);
    if (solidsA.empty()) {
        QMessageBox::information(this, tr("Booleanas"),
                                 tr("El objeto A no tiene geometría editable."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Operaciones booleanas"));
    auto* form = new QFormLayout(&dialog);

    auto* operation = new QComboBox(&dialog);
    operation->addItem(originalIcon("union"), tr("Unión (A + B)"));
    operation->addItem(originalIcon("subtract"), tr("Diferencia (A - B)"));
    operation->addItem(originalIcon("intersection"), tr("Intersección (A ∩ B)"));
    applyTip(operation, tr("Unión: suma A y B. Diferencia: quita B de A. Intersección: sólo lo común."));
    form->addRow(tr("Operación:"), operation);

    auto* comboB = new QComboBox(&dialog);
    std::vector<int> idsB;
    for (const int otherId : canvas_->layerIds()) {
        if (otherId == idA) continue;
        if (canvas_->layerSolids(otherId).empty()) continue;
        for (QTreeWidgetItem* candidate : objectItems()) {
            if (candidate->data(0, Qt::UserRole).toInt() == otherId) {
                comboB->addItem(candidate->text(1));
                idsB.push_back(otherId);
                break;
            }
        }
    }
    if (comboB->count() == 0) {
        QMessageBox::information(this, tr("Booleanas"),
                                 tr("No hay otro objeto con geometría editable."));
        return;
    }
    applyTip(comboB, tr("Segundo objeto con el que se combina el seleccionado."));
    form->addRow(tr("Objeto B:"), comboB);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(box);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    const int idB = idsB[static_cast<std::size_t>(comboB->currentIndex())];
    const std::vector<Geom> solidsB = canvas_->layerSolids(idB);
    const Geom a = Geom::unionOf(solidsA);
    const Geom b = Geom::unionOf(solidsB);

    Geom result;
    switch (operation->currentIndex()) {
        case 0: result = a.unite(b); break;
        case 1: result = a.difference(b); break;
        default: result = a.intersection(b); break;
    }
    if (!result.valid()) {
        showError(tr("La operación no produjo geometría"));
        return;
    }

    const std::vector<Geom> afterA{result};
    const bool wasVisibleB = true;
    auto applyA = [this, idA](const std::vector<Geom>& solids) {
        canvas_->setLayerGeometry(idA, solids, {});
        refreshProperties();
    };
    auto setBVisible = [this, idB](bool visible) {
        if (QTreeWidgetItem* candidate = findObjectItem(idB)) {
            updatingTree_ = true;
            setObjectVisible(candidate, visible);
            updatingTree_ = false;
        }
    };

    undoStack_->push(new LambdaCommand(
        [applyA, setBVisible, wasVisibleB, solidsA] {
            applyA(solidsA);
            setBVisible(wasVisibleB);
        },
        [applyA, setBVisible, afterA] {
            applyA(afterA);
            setBVisible(false);
        },
        tr("Operación booleana")));
}

/// @brief Mueve, rota o escala el objeto activo.
void MainWindow::transformSelectedObject() {
    QTreeWidgetItem* item = objects_->currentItem();
    if (!item) return;

    const int id = item->data(0, Qt::UserRole).toInt();
    if (id <= 0) return;
    const Canvas::LayerSnapshot before = canvas_->layerSnapshot(id);
    if (before.id == 0) return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Transformar objeto"));
    auto* form = new QFormLayout(&dialog);

    auto* dx = new UnitSpinBox(&dialog);
    dx->setRange(-1000, 1000);
    dx->setDecimals(3);
    dx->setSuffix(" mm");
    applyTip(dx, tr("Desplazamiento horizontal del objeto."));
    form->addRow(tr("Mover X:"), dx);

    auto* dy = new UnitSpinBox(&dialog);
    dy->setRange(-1000, 1000);
    dy->setDecimals(3);
    dy->setSuffix(" mm");
    applyTip(dy, tr("Desplazamiento vertical del objeto."));
    form->addRow(tr("Mover Y:"), dy);

    auto* angle = new UnitSpinBox(&dialog);
    angle->setRange(-360, 360);
    angle->setDecimals(2);
    angle->setSuffix(" °");
    applyTip(angle, tr("Rotación en grados alrededor del centro del objeto."));
    form->addRow(tr("Rotar:"), angle);

    auto* scale = new UnitSpinBox(&dialog);
    scale->setRange(0.01, 100.0);
    scale->setDecimals(3);
    scale->setValue(1.0);
    applyTip(scale, tr("Factor de escala. 1 = sin cambios, 2 = doble tamaño."));
    form->addRow(tr("Escalar:"), scale);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    const fc::BBox b = canvas_->layerBounds(id);
    const double cx = b.valid ? (b.xmin + b.xmax) / 2.0 : 0.0;
    const double cy = b.valid ? (b.ymin + b.ymax) / 2.0 : 0.0;

    QTransform transform;
    transform.translate(dx->value(), dy->value());
    transform.translate(cx, cy);
    transform.rotate(angle->value());
    transform.scale(scale->value(), scale->value());
    transform.translate(-cx, -cy);

    const std::vector<Geom> beforeSolids = canvas_->layerSolids(id);
    const std::vector<Geom> beforeFollows = canvas_->layerFollows(id);
    auto applyTransform = [&transform](const std::vector<Geom>& input) {
        std::vector<Geom> output;
        output.reserve(input.size());
        for (const Geom& g : input) {
            output.push_back(g.transformXY([&transform](Pt p) {
                const QPointF q = transform.map(QPointF(p.x, p.y));
                return Pt{q.x(), q.y()};
            }));
        }
        return output;
    };
    const std::vector<Geom> afterSolids = applyTransform(beforeSolids);
    const std::vector<Geom> afterFollows = applyTransform(beforeFollows);
    canvas_->setLayerGeometry(id, afterSolids, afterFollows);
    const auto apply = [this, id](const std::vector<Geom>& solids, const std::vector<Geom>& follows) {
        canvas_->setLayerGeometry(id, solids, follows);
        refreshProperties();
    };
    undoStack_->push(new LambdaCommand(
        [apply, beforeSolids, beforeFollows] { apply(beforeSolids, beforeFollows); },
        [apply, afterSolids, afterFollows] { apply(afterSolids, afterFollows); },
        tr("Transformar objeto")));
}

/// @brief Combina las capas visibles en un film.
void MainWindow::createFilm() {
    const int id = canvas_->mergeVisibleLayers();
    if (id < 0) return;
    auto* item = addObjectRow(tr("Film"), "film", "-", id);
    objects_->setCurrentItem(item);
    statusBar()->showMessage(tr("Film creado"), 3000);
}

/// @brief Exporta las capas visibles a SVG.
void MainWindow::exportFilm() {
    const QString path = QFileDialog::getSaveFileName(this, tr("Exportar film SVG"),
                                                      QFileInfo(lastDir_).absolutePath() + "/film.svg",
                                                      tr("SVG (*.svg);;Todos (*)"));
    if (path.isEmpty()) return;
    if (!canvas_->exportSvg(path)) {
        showError(tr("No hay geometría visible para exportar"));
        return;
    }
    statusBar()->showMessage(tr("Film exportado: %1").arg(QFileInfo(path).fileName()), 5000);
}

/// @brief Ejecuta un script JavaScript en el contexto de la aplicación.
bool MainWindow::runScriptFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        showError(tr("No se pudo abrir el script"));
        return false;
    }
    const QString code = QString::fromUtf8(file.readAll());

    QJSEngine engine;
    fc::ScriptApi api;
    api.setLayerSink([this](const QString& name, const QString& kind,
                            const std::vector<Geom>& solids,
                            const std::vector<Geom>& follows) {
        const int id = canvas_->appendGeometry(solids, follows, true);
        canvas_->endGeometry();
        addObjectRow(name, kind, "-", id);
        if (kind == "gerber") {
            GerberResult r;
            r.solids = solids;
            r.follows = follows;
            r.solid = solids.empty() ? Geom() : Geom::unionOf(solids);
            r.bounds = r.solid.valid() ? r.solid.bounds() : BBox{};
            gerberLayers_[id] = r;
        } else if (kind == "excellon") {
            ExcellonResult r;
            r.geometry = solids;
            r.solid = solids.empty() ? Geom() : Geom::unionOf(solids);
            r.bounds = r.solid.valid() ? r.solid.bounds() : BBox{};
            excellonLayers_[id] = r;
        }
        if (!canvas_->hasData()) canvas_->fitTo(canvas_->layerBounds(id));
    });
    fc::installScriptApi(engine, &api);
    const QJSValue result = engine.evaluate(code, path);
    if (result.isError()) {
        info_->setPlainText(info_->toPlainText() + "\n--- SCRIPT ERROR ---\n" + result.toString());
        showError(result.toString());
        return false;
    }

    info_->setPlainText(info_->toPlainText() + "\n--- SCRIPT ---\n" + path + "\n" + api.info());
    statusBar()->showMessage(tr("Script ejecutado: %1").arg(QFileInfo(path).fileName()), 5000);
    return true;
}

/// @brief Crea un objeto Script vacío.
void MainWindow::newScript() { scriptEditor(QString()); }

/// @brief Abre el editor del script activo.
void MainWindow::editScript() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Editar script"), lastDir_, tr("JavaScript (*.js);;Todos (*)"));
    if (!path.isEmpty()) scriptEditor(path);
}

/// @brief Editor de texto para scripts, con guardar y ejecutar.
void MainWindow::scriptEditor(const QString& initialPath) {
    QString path = initialPath;
    QString content;
    if (!path.isEmpty()) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
            content = QString::fromUtf8(file.readAll());
    }

    QDialog dialog(this);
    dialog.resize(900, 640);
    dialog.setWindowTitle(path.isEmpty() ? tr("Nuevo script") : tr("Script — %1").arg(QFileInfo(path).fileName()));
    auto* layout = new QVBoxLayout(&dialog);
    auto* editor = new QPlainTextEdit(&dialog);
    editor->setPlainText(content.isEmpty() ? "// Script FlatCAM C++ (Qt JS)\n// flatcam.openGerber(\"...\");\n// flatcam.isolate(0.2, 1, 0);\n" : content);
    editor->setStyleSheet("QPlainTextEdit { font-family: Menlo; font-size: 12px; }");
    layout->addWidget(editor);
    auto* buttons = new QHBoxLayout();
    auto* save = new QPushButton(originalIcon("save"), tr("Guardar"), &dialog);
    auto* run = new QPushButton(originalIcon("script"), tr("Guardar y ejecutar"), &dialog);
    buttons->addStretch();
    buttons->addWidget(save);
    buttons->addWidget(run);
    layout->addLayout(buttons);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(box);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    connect(save, &QPushButton::clicked, &dialog, [this, &path, editor, &dialog] {
        if (path.isEmpty()) {
            path = QFileDialog::getSaveFileName(
                this, tr("Guardar script"), QFileInfo(lastDir_).absolutePath() + "/script.js",
                tr("JavaScript (*.js)"));
            if (path.isEmpty()) return;
        }
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
        file.write(editor->toPlainText().toUtf8());
        dialog.setWindowTitle(tr("Script — %1").arg(QFileInfo(path).fileName()));
    });
    connect(run, &QPushButton::clicked, &dialog, [this, &path, editor, &dialog] {
        if (path.isEmpty()) {
            path = QFileDialog::getSaveFileName(
                this, tr("Guardar script"), QFileInfo(lastDir_).absolutePath() + "/script.js",
                tr("JavaScript (*.js)"));
            if (path.isEmpty()) return;
        }
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
        file.write(editor->toPlainText().toUtf8());
        file.close();
        runScriptFile(path);
    });
    dialog.exec();
}

/// @brief Ejecuta el script activo.
void MainWindow::runScript() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Ejecutar script JavaScript"),
                                                      lastDir_, tr("JavaScript (*.js);;Todos (*)"));
    if (!path.isEmpty()) runScriptFile(path);
}

/// @brief Carga el objeto de proyecto seleccionado.
void MainWindow::projectObjectSelected() {
    auto* item = objects_->currentItem();
    if (!item) return;
    const int id = item->data(0, Qt::UserRole).toInt();
    const auto objectProps = objectProps_.find(id);
    if (objectProps != objectProps_.end()) {
        const auto tables = objectProps->second.find(QStringLiteral("__toolTables"));
        if (tables != objectProps->second.end()) loadToolTablesJson(tables->second);
    }

    if (gerberLayers_.count(id)) {
        lastGerber_ = gerberLayers_[id];
        lastExcellon_.reset();
        lastDrillJob_.reset();
    } else if (excellonLayers_.count(id)) {
        lastExcellon_ = excellonLayers_[id];
        lastDrillJob_ = fc::drillJobFromExcellon(lastExcellon_.value());
        lastGerber_.reset();
    }

    const fc::BBox b = canvas_->layerBounds(id);
    if (b.valid) canvas_->fitTo(b);
    refreshProperties();
}

/// Abre la vista 3D con el cobre visible y los taladros del proyecto.
void MainWindow::show3D() {
    const std::vector<Geom> solids = canvas_->visibleSolids();
    if (solids.empty()) {
        QMessageBox::information(this, tr("Vista 3D"), tr("No hay capas visibles."));
        return;
    }
    std::vector<Geom> holes;
    for (QTreeWidgetItem* item : objectItems()) {
        if (!item->data(0, Qt::UserRole + 1).toBool()) continue;
        if (item->text(2).toLower() != "excellon") continue;
        const auto h = canvas_->layerSolids(item->data(0, Qt::UserRole).toInt());
        holes.insert(holes.end(), h.begin(), h.end());
    }
    auto* dialog = new View3D(solids, holes, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    dialog->raise();
}

/// @brief Construye la consola Tcl y registra los 71 comandos.
void MainWindow::buildTclConsole(QTabWidget* tabs) {
#ifdef FC_HAVE_TCL
    auto* page = new QWidget(tabs);
    auto* layout = new QVBoxLayout(page);
    tclOutput_ = new QPlainTextEdit(page);
    tclOutput_->setReadOnly(true);
    tclOutput_->setMaximumBlockCount(1000);
    tclOutput_->setStyleSheet("QPlainTextEdit { font-family: Menlo; font-size: 12px; "
                              "background:#141a1f; color:#c9d1d9; }");
    tclOutput_->setPlainText(tr("Consola Tcl integrada.\n"
                                "Comandos: open_gerber, open_excellon, isolate, cutout, ncc, "
                                "paint, export_gcode, export_gerber, info.\n"
                                "Ejemplo: isolate 0.2 1 0 · export_gcode /tmp/iso.nc\n"));
    tclInput_ = new QLineEdit(page);
    tclInput_->setPlaceholderText(tr("Comando Tcl y Enter (clear para limpiar)"));
    layout->addWidget(tclOutput_);
    layout->addWidget(tclInput_);
    tabs->addTab(page, tr("Shell Tcl"));

    Tcl_Interp* interp = Tcl_CreateInterp();
    tclInterp_ = interp;
    Tcl_Init(interp);
    Tcl_CreateObjCommand(interp, "open_gerber", tclCmdOpenGerber, this, nullptr);
    Tcl_CreateObjCommand(interp, "open_excellon", tclCmdOpenExcellon, this, nullptr);
    Tcl_CreateObjCommand(interp, "isolate", tclCmdIsolate, this, nullptr);
    Tcl_CreateObjCommand(interp, "cutout", tclCmdCutout, this, nullptr);
    Tcl_CreateObjCommand(interp, "ncc", tclCmdNcc, this, nullptr);
    Tcl_CreateObjCommand(interp, "paint", tclCmdPaint, this, nullptr);
    Tcl_CreateObjCommand(interp, "export_gcode", tclCmdExportGcode, this, nullptr);
    Tcl_CreateObjCommand(interp, "export_gerber", tclCmdExportGerber, this, nullptr);
    Tcl_CreateObjCommand(interp, "extract_drills", tclCmdExtractDrills, this, nullptr);
    Tcl_CreateObjCommand(interp, "punch", tclCmdPunch, this, nullptr);
    Tcl_CreateObjCommand(interp, "panelize", tclCmdPanelize, this, nullptr);
    Tcl_CreateObjCommand(interp, "qr", tclCmdQr, this, nullptr);
    Tcl_CreateObjCommand(interp, "corners", tclCmdCorners, this, nullptr);
    Tcl_CreateObjCommand(interp, "optimal", tclCmdOptimal, this, nullptr);
    Tcl_CreateObjCommand(interp, "version", tclCmdVersion, this, nullptr);
    Tcl_CreateObjCommand(interp, "help", tclCmdHelp, this, nullptr);
    Tcl_CreateObjCommand(interp, "get_names", tclCmdGetNames, this, nullptr);
    Tcl_CreateObjCommand(interp, "bbox", tclCmdBBox, this, nullptr);
    Tcl_CreateObjCommand(interp, "bounds", tclCmdBBox, this, nullptr);
    Tcl_CreateObjCommand(interp, "set_active", tclCmdSetActive, this, nullptr);
    Tcl_CreateObjCommand(interp, "offset", tclCmdOffset, this, nullptr);
    Tcl_CreateObjCommand(interp, "scale", tclCmdScale, this, nullptr);
    Tcl_CreateObjCommand(interp, "mirror", tclCmdMirror, this, nullptr);
    Tcl_CreateObjCommand(interp, "delete", tclCmdDelete, this, nullptr);
    Tcl_CreateObjCommand(interp, "save_project", tclCmdSaveProject, this, nullptr);
    Tcl_CreateObjCommand(interp, "export_dxf", tclCmdExportDxf, this, nullptr);
    Tcl_CreateObjCommand(interp, "export_svg", tclCmdExportSvg, this, nullptr);
    Tcl_CreateObjCommand(interp, "plot_all", tclCmdPlotAll, this, nullptr);
    Tcl_CreateObjCommand(interp, "set_origin", tclCmdSetOrigin, this, nullptr);
    Tcl_CreateObjCommand(interp, "open_path", tclCmdOpenPath, this, nullptr);
    Tcl_CreateObjCommand(interp, "open_dxf", tclCmdOpenPath, this, nullptr);
    Tcl_CreateObjCommand(interp, "open_svg", tclCmdOpenPath, this, nullptr);
    Tcl_CreateObjCommand(interp, "open_gcode", tclCmdOpenPath, this, nullptr);
    Tcl_CreateObjCommand(interp, "open_project", tclCmdOpenPath, this, nullptr);
    Tcl_CreateObjCommand(interp, "new_gerber", tclCmdNewGerber, this, nullptr);
    Tcl_CreateObjCommand(interp, "new_excellon", tclCmdNewExcellon, this, nullptr);
    Tcl_CreateObjCommand(interp, "new_geometry", tclCmdNewGeometry, this, nullptr);
    Tcl_CreateObjCommand(interp, "add_circle", tclCmdAddCircle, this, nullptr);
    Tcl_CreateObjCommand(interp, "add_rectangle", tclCmdAddRectangle, this, nullptr);
    Tcl_CreateObjCommand(interp, "add_polygon", tclCmdAddPolygon, this, nullptr);
    Tcl_CreateObjCommand(interp, "add_polyline", tclCmdAddPolyline, this, nullptr);
    Tcl_CreateObjCommand(interp, "new", tclCmdNew, this, nullptr);
    Tcl_CreateObjCommand(interp, "exteriors", tclCmdExteriors, this, nullptr);
    Tcl_CreateObjCommand(interp, "interiors", tclCmdInteriors, this, nullptr);
    Tcl_CreateObjCommand(interp, "join_geometry", tclCmdJoin, this, nullptr);
    Tcl_CreateObjCommand(interp, "join_excellon", tclCmdJoin, this, nullptr);
    Tcl_CreateObjCommand(interp, "geo_union", tclCmdGeoUnion, this, nullptr);
    Tcl_CreateObjCommand(interp, "export_excellon", tclCmdExportExcellon, this, nullptr);
    Tcl_CreateObjCommand(interp, "plot_objects", tclCmdPlotObjects, this, nullptr);
    Tcl_CreateObjCommand(interp, "quit", tclCmdQuit, this, nullptr);
    Tcl_CreateObjCommand(interp, "get_path", tclCmdGetPath, this, nullptr);
    Tcl_CreateObjCommand(interp, "set_path", tclCmdSetPath, this, nullptr);
    Tcl_CreateObjCommand(interp, "get_sys", tclCmdGetSys, this, nullptr);
    Tcl_CreateObjCommand(interp, "set_sys", tclCmdSetSys, this, nullptr);
    Tcl_CreateObjCommand(interp, "list_sys", tclCmdListSys, this, nullptr);
    Tcl_CreateObjCommand(interp, "save_sys", tclCmdSaveSys, this, nullptr);
    Tcl_CreateObjCommand(interp, "options", tclCmdOptions, this, nullptr);
    Tcl_CreateObjCommand(interp, "nregions", tclCmdNRegions, this, nullptr);
    Tcl_CreateObjCommand(interp, "subtract_poly", tclCmdSubtractPoly, this, nullptr);
    Tcl_CreateObjCommand(interp, "subtract_rectangle", tclCmdSubtractRect, this, nullptr);
    Tcl_CreateObjCommand(interp, "follow", tclCmdFollow, this, nullptr);
    Tcl_CreateObjCommand(interp, "align_drill", tclCmdAlignDrill, this, nullptr);
    Tcl_CreateObjCommand(interp, "align_drill_grid", tclCmdAlignDrillGrid, this, nullptr);
    Tcl_CreateObjCommand(interp, "cncjob", tclCmdCncJob, this, nullptr);
    Tcl_CreateObjCommand(interp, "drillcncjob", tclCmdDrillCncJob, this, nullptr);
    Tcl_CreateObjCommand(interp, "copperclear", tclCmdCopperClear, this, nullptr);
    Tcl_CreateObjCommand(interp, "geo_cutout", tclCmdGeoCutout, this, nullptr);
    Tcl_CreateObjCommand(interp, "mill_drills", tclCmdMillDrills, this, nullptr);
    Tcl_CreateObjCommand(interp, "mill_slots", tclCmdMillSlots, this, nullptr);
    Tcl_CreateObjCommand(interp, "write_gcode", tclCmdExportGcode, this, nullptr);
    Tcl_CreateObjCommand(interp, "info", tclCmdInfo, this, nullptr);

    connect(tclInput_, &QLineEdit::returnPressed, this, [this] {
        const QString line = tclInput_->text().trimmed();
        if (line.isEmpty()) return;
        tclInput_->clear();
        if (line == "clear") {
            tclOutput_->clear();
            return;
        }
        auto* interp = static_cast<Tcl_Interp*>(tclInterp_);
        tclOutput_->appendPlainText("> " + line);
        if (Tcl_Eval(interp, line.toUtf8().constData()) != TCL_OK)
            tclOutput_->appendPlainText(QString("ERROR: ") + Tcl_GetStringResult(interp));
        else {
            const QString result = Tcl_GetStringResult(interp);
            if (!result.isEmpty()) tclOutput_->appendPlainText(result);
        }
    });
#endif
}

/// @brief Adaptador Tcl del comando `open`.
bool MainWindow::tclOpen(const QString& path, bool excellon) {
    try {
        const bool ok = excellon ? addExcellon(path) : addGerber(path);
        if (ok) {
            canvas_->endGeometry();
            if (!objectItems().empty()) objects_->setCurrentItem(objectItems().back());
        }
        return ok;
    } catch (const std::exception& e) {
        showError(QString::fromUtf8(e.what()));
        return false;
    }
}

/// @brief Adaptador Tcl del comando `isolate`.
int MainWindow::tclIsolate(double dia, int passes, double overlap) {
    GCodeOptions opt;
    if (!applyIsolation(dia, passes, overlap, false, opt)) return 0;
    return static_cast<int>(lastIsoMulti_.size());
}

/// @brief Adaptador Tcl del comando `cutout`.
int MainWindow::tclCutout(double dia, double margin, int tabs, double width) {
    GCodeOptions opt;
    if (!applyCutout(dia, margin, true, opt)) return 0;
    return static_cast<int>(lastCutMulti_.size());
}

/// @brief Adaptador Tcl del comando `ncc`.
int MainWindow::tclNcc(double dia, double overlapPercent) {
    GCodeOptions opt;
    if (!applyNcc(dia, 100.0 - overlapPercent, 0.1, 0.0, 1.0, false, opt)) return 0;
    return static_cast<int>(lastNccMulti_.size());
}

/// @brief Adaptador Tcl del comando `paint`.
int MainWindow::tclPaint(double dia, double overlapPercent) {
    GCodeOptions opt;
    if (!applyPaint(dia, 100.0 - overlapPercent, 0.0, opt)) return 0;
    return static_cast<int>(lastPaintMulti_.size());
}

/// @brief Adaptador Tcl del comando `extractdrills`.
int MainWindow::tclExtractDrills(double maxDiameter) {
    auto* item = objects_ ? objects_->currentItem() : nullptr;
    int id = item ? item->data(0, Qt::UserRole).toInt() : 0;
    auto it = gerberLayers_.find(id);
    GerberResult source;
    if (it != gerberLayers_.end()) source = it->second;
    else if (lastGerber_) source = *lastGerber_;
    else return -1;
    const ExcellonResult drills = extractDrills(source, maxDiameter, defaultUnits_, stepsPerCircle_);
    if (drills.drillCount == 0) return 0;
    const int layerId = addGeneratedSolid(tr("Taladros extraídos"), drills.geometry, id, "excellon");
    if (layerId > 0) {
        excellonLayers_[layerId] = drills;
        lastExcellon_ = drills;
        refreshSourceCombos();
    }
    return static_cast<int>(drills.drillCount);
}

/// @brief Adaptador Tcl del comando `punch`.
int MainWindow::tclPunch(double margin) {
    auto* item = objects_ ? objects_->currentItem() : nullptr;
    const int id = item ? item->data(0, Qt::UserRole).toInt() : 0;
    auto git = gerberLayers_.find(id);
    GerberResult source;
    if (git != gerberLayers_.end()) source = git->second;
    else if (lastGerber_) source = *lastGerber_;
    else return -1;
    if (!lastExcellon_) return -1;
    const Geom punched = punchGerber(source, *lastExcellon_, margin);
    if (!punched.valid() || punched.empty()) return 0;
    addGeneratedSolid(tr("Punch Gerber"), {punched}, id, "gerber");
    return 1;
}

/// @brief Adaptador Tcl del comando `panelize`.
int MainWindow::tclPanelize(int rows, int cols, double gapX, double gapY) {
    auto* item = objects_ ? objects_->currentItem() : nullptr;
    if (!item || rows < 1 || cols < 1) return -1;
    const int id = item->data(0, Qt::UserRole).toInt();
    const std::vector<Geom> solids = canvas_->layerSolids(id);
    const BBox b = canvas_->layerBounds(id);
    if (solids.empty() || !b.valid) return -1;
    const double dx = b.width() + gapX;
    const double dy = b.height() + gapY;
    std::vector<Geom> out;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            for (const Geom& g : solids)
                out.push_back(g.translate(c * dx, r * dy));
    addGeneratedSolid(tr("Panel %1x%2").arg(rows).arg(cols), out, id, "panel");
    return static_cast<int>(out.size());
}

/// @brief Adaptador Tcl del comando `qr`.
int MainWindow::tclQr(const QString& text, double module, int border) {
    const std::vector<Geom> geom = qrCodeGeometry(text.toStdString(), module, border);
    if (geom.empty()) return -1;
    addGeneratedSolid(tr("QR Code"), geom, 0, "gerber");
    return static_cast<int>(geom.size());
}

/// @brief Adaptador Tcl del comando `corners`.
int MainWindow::tclCorners(double length, double margin) {
    auto* item = objects_ ? objects_->currentItem() : nullptr;
    if (!item) return -1;
    const int id = item->data(0, Qt::UserRole).toInt();
    const BBox b = canvas_->layerBounds(id);
    if (!b.valid) return -1;
    const std::vector<Geom> marks = cornerMarks(b, length, margin, false);
    addGeneratedLayer(tr("Marcas de esquina"), marks, id);
    return static_cast<int>(marks.size());
}

/// @brief Adaptador Tcl del comando `optimal`.
double MainWindow::tclOptimal(double maxDiameter) {
    auto* item = objects_ ? objects_->currentItem() : nullptr;
    const int id = item ? item->data(0, Qt::UserRole).toInt() : 0;
    auto it = gerberLayers_.find(id);
    Geom copper;
    if (it != gerberLayers_.end()) copper = it->second.solid;
    else if (lastGerber_) copper = lastGerber_->solid;
    if (!copper.valid() || copper.empty()) return -1.0;
    const OptimalTool opt = optimalIsolationDiameter(copper, maxDiameter);
    return opt.valid ? opt.diameter : maxDiameter;
}

/// @brief Adaptador Tcl del comando `exportgcode`.
bool MainWindow::tclExportGcode(const QString& path) {
    std::string code;
    if (lastDrillJob_)
        code = drillingGCode(*lastDrillJob_, lastDrillHeader_);
    else if (!lastIsoMulti_.empty())
        code = pathsGCodeMulti(lastIsoMulti_, lastIsoHeader_);
    else if (!lastNccMulti_.empty())
        code = pathsGCodeMulti(lastNccMulti_, lastNccHeader_);
    else if (!lastCutMulti_.empty())
        code = pathsGCodeMulti(lastCutMulti_, lastCutHeader_);
    else if (!lastPaintMulti_.empty())
        code = pathsGCodeMulti(lastPaintMulti_, lastPaintHeader_);
    else
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    file.write(code.c_str());
    return true;
}

/// @brief Adaptador Tcl del comando `exportgerber`.
bool MainWindow::tclExportGerber(const QString& path) {
    if (!lastGerber_) return false;
    try {
        writeGerber(path.toStdString(), lastGerber_->solids, lastGerber_->follows, defaultUnits_);
        return true;
    } catch (...) {
        return false;
    }
}

/// @brief Ejecuta una cadena Tcl y devuelve el resultado.
QString MainWindow::runTclCommand(const QString& command) {
#ifdef FC_HAVE_TCL
    if (!tclInterp_) return tr("Tcl no inicializado");
    auto* interp = static_cast<Tcl_Interp*>(tclInterp_);
    const int code = Tcl_Eval(interp, command.toUtf8().constData());
    const QString result = QString::fromUtf8(Tcl_GetStringResult(interp));
    return code == TCL_OK ? result : tr("ERROR: %1").arg(result);
#else
    Q_UNUSED(command);
    return tr("Tcl no disponible");
#endif
}

/// @brief Adaptador Tcl del comando `version`.
QString MainWindow::tclVersion() const {
    return QStringLiteral("FlatCAM C++ 0.3.0");
}

/// @brief Adaptador Tcl del comando `help`.
QString MainWindow::tclHelp() const {
    return QStringLiteral(
        "open_gerber path | open_excellon path | open_dxf path | open_svg path | "
        "open_gcode path | open_project path\n"
        "isolate dia ?passes? ?overlap? | cutout dia ?margin? ?tabs? ?width? | "
        "ncc dia ?overlap? | paint dia ?overlap?\n"
        "extract_drills maxDia | punch ?margin? | panelize rows cols ?gapX? ?gapY? | "
        "qr text ?module? ?border? | corners length ?margin? | optimal maxDia\n"
        "export_gcode path | export_gerber path | export_dxf path | export_svg path | "
        "save_project path\n"
        "get_names ?kind? | bbox name | set_active name | delete name | "
        "offset name dx dy | scale name factor | mirror name x|y\n"
        "plot_all | set_origin x y | version | info | help");
}

/// @brief Adaptador Tcl del comando `getnames`.
QString MainWindow::tclGetNames(const QString& kind) const {
    QStringList names;
    for (QTreeWidgetItem* item : objectItems()) {
        if (!kind.isEmpty() && item->text(2).compare(kind, Qt::CaseInsensitive) != 0) continue;
        names << item->text(1);
    }
    return names.join('\n');
}

/// @brief Adaptador Tcl del comando `bbox`.
QString MainWindow::tclBBox(const QString& name) const {
    QTreeWidgetItem* item = findObjectByName(name);
    if (!item) return {};
    const BBox b = canvas_->layerBounds(item->data(0, Qt::UserRole).toInt());
    if (!b.valid) return {};
    return QString("%1 %2 %3 %4").arg(b.xmin, 0, 'f', 6).arg(b.ymin, 0, 'f', 6)
        .arg(b.xmax, 0, 'f', 6).arg(b.ymax, 0, 'f', 6);
}

/// @brief Adaptador Tcl del comando `setactive`.
bool MainWindow::tclSetActive(const QString& name) {
    QTreeWidgetItem* item = findObjectByName(name);
    if (!item) return false;
    objects_->setCurrentItem(item);
    refreshProperties();
    refreshObjectToolPage();
    return true;
}

/// @brief Adaptador Tcl del comando `offset`.
bool MainWindow::tclOffset(const QString& name, double dx, double dy) {
    QTreeWidgetItem* item = findObjectByName(name);
    if (!item) return false;
    const int id = item->data(0, Qt::UserRole).toInt();
    std::vector<Geom> solids;
    std::vector<Geom> follows;
    for (const Geom& g : canvas_->layerSolids(id)) solids.push_back(g.translate(dx, dy));
    for (const Geom& g : canvas_->layerFollows(id)) follows.push_back(g.translate(dx, dy));
    canvas_->setLayerGeometry(id, solids, follows);
    const auto it = gerberLayers_.find(id);
    if (it != gerberLayers_.end()) {
        it->second.solids = solids;
        it->second.follows = follows;
        it->second.solid = Geom::unionOf(solids);
        it->second.bounds = it->second.solid.valid() ? it->second.solid.bounds() : BBox{};
    }
    refreshProperties();
    return true;
}

/// @brief Adaptador Tcl del comando `scale`.
bool MainWindow::tclScale(const QString& name, double factor) {
    if (factor <= 0.0) return false;
    QTreeWidgetItem* item = findObjectByName(name);
    if (!item) return false;
    const int id = item->data(0, Qt::UserRole).toInt();
    const BBox b = canvas_->layerBounds(id);
    if (!b.valid) return false;
    const Pt center{(b.xmin + b.xmax) * 0.5, (b.ymin + b.ymax) * 0.5};
    std::vector<Geom> solids;
    std::vector<Geom> follows;
    for (const Geom& g : canvas_->layerSolids(id)) solids.push_back(g.scale(factor, factor, center));
    for (const Geom& g : canvas_->layerFollows(id)) follows.push_back(g.scale(factor, factor, center));
    canvas_->setLayerGeometry(id, solids, follows);
    const auto it = gerberLayers_.find(id);
    if (it != gerberLayers_.end()) {
        it->second.solids = solids;
        it->second.follows = follows;
        it->second.solid = Geom::unionOf(solids);
        it->second.bounds = it->second.solid.valid() ? it->second.solid.bounds() : BBox{};
    }
    refreshProperties();
    return true;
}

/// @brief Adaptador Tcl del comando `mirror`.
bool MainWindow::tclMirror(const QString& name, const QString& axis) {
    QTreeWidgetItem* item = findObjectByName(name);
    if (!item) return false;
    const int id = item->data(0, Qt::UserRole).toInt();
    const BBox b = canvas_->layerBounds(id);
    if (!b.valid) return false;
    const Pt center{(b.xmin + b.xmax) * 0.5, (b.ymin + b.ymax) * 0.5};
    const bool mirrorX = axis.compare("x", Qt::CaseInsensitive) == 0;
    std::vector<Geom> solids;
    std::vector<Geom> follows;
    for (const Geom& g : canvas_->layerSolids(id))
        solids.push_back(mirrorX ? g.scale(-1.0, 1.0, center) : g.scale(1.0, -1.0, center));
    for (const Geom& g : canvas_->layerFollows(id))
        follows.push_back(mirrorX ? g.scale(-1.0, 1.0, center) : g.scale(1.0, -1.0, center));
    canvas_->setLayerGeometry(id, solids, follows);
    const auto it = gerberLayers_.find(id);
    if (it != gerberLayers_.end()) {
        it->second.solids = solids;
        it->second.follows = follows;
        it->second.solid = Geom::unionOf(solids);
        it->second.bounds = it->second.solid.valid() ? it->second.solid.bounds() : BBox{};
    }
    refreshProperties();
    return true;
}

/// @brief Adaptador Tcl del comando `delete`.
bool MainWindow::tclDelete(const QString& name) {
    QTreeWidgetItem* item = findObjectByName(name);
    if (!item) return false;
    const int id = item->data(0, Qt::UserRole).toInt();
    canvas_->removeLayer(id);
    gerberLayers_.erase(id);
    excellonLayers_.erase(id);
    sourcePaths_.erase(id);
    delete item;
    refreshSourceCombos();
    refreshProperties();
    return true;
}

/// @brief Adaptador Tcl del comando `saveproject`.
bool MainWindow::tclSaveProject(const QString& path) {
    return saveProjectToPath(path);
}

/// @brief Adaptador Tcl del comando `exportdxf`.
bool MainWindow::tclExportDxf(const QString& path) {
    std::vector<Geom> solids;
    std::vector<Geom> follows;
    for (QTreeWidgetItem* item : objectItems()) {
        if (!item->data(0, Qt::UserRole + 1).toBool()) continue;
        const int id = item->data(0, Qt::UserRole).toInt();
        const auto s = canvas_->layerSolids(id);
        const auto f = canvas_->layerFollows(id);
        solids.insert(solids.end(), s.begin(), s.end());
        follows.insert(follows.end(), f.begin(), f.end());
    }
    if (solids.empty() && follows.empty()) return false;
    try {
        exportDxf(path.toStdString(), solids, follows);
        return true;
    } catch (const std::exception& e) {
        showError(QString::fromUtf8(e.what()));
        return false;
    }
}

/// @brief Adaptador Tcl del comando `exportsvg`.
bool MainWindow::tclExportSvg(const QString& path) {
    return canvas_->exportSvg(path);
}

/// @brief Adaptador Tcl del comando `plotall`.
void MainWindow::tclPlotAll() {
    for (QTreeWidgetItem* item : objectItems()) {
        updatingTree_ = true;
        setObjectVisible(item, true);
        updatingTree_ = false;
    }
}

/// @brief Adaptador Tcl del comando `newobject`.
bool MainWindow::tclNewObject(const QString& name, const QString& kind) {
    if (findObjectByName(name)) return false;
    const int id = canvas_->appendGeometry({}, {}, true);
    canvas_->endGeometry();
    addObjectRow(name, kind, "0.00", id);
    if (kind == "gerber" || kind == "geometry") {
        GerberResult r;
        r.fileUnits = defaultUnits_;
        gerberLayers_[id] = r;
    } else if (kind == "excellon") {
        ExcellonResult r;
        excellonLayers_[id] = r;
    }
    return true;
}

/// @brief Adaptador Tcl del comando `addcircle`.
bool MainWindow::tclAddCircle(const QString& name, double cx, double cy, double dia) {
    if (dia <= 0.0) return false;
    QTreeWidgetItem* item = findObjectByName(name);
    if (!item) return false;
    const int id = item->data(0, Qt::UserRole).toInt();
    std::vector<Geom> solids = canvas_->layerSolids(id);
    solids.push_back(Geom::circle({cx, cy}, dia / 2.0, 32));
    canvas_->setLayerGeometry(id, solids, canvas_->layerFollows(id));
    const auto it = gerberLayers_.find(id);
    if (it != gerberLayers_.end()) {
        it->second.solids = solids;
        it->second.solid = Geom::unionOf(solids);
        it->second.bounds = it->second.solid.valid() ? it->second.solid.bounds() : BBox{};
    }
    item->setText(3, QString::number(Geom::unionOf(solids).area(), 'f', 2));
    canvas_->fitTo(canvas_->layerBounds(id));
    return true;
}

/// @brief Adaptador Tcl del comando `addrectangle`.
bool MainWindow::tclAddRectangle(const QString& name, double x1, double y1, double x2, double y2) {
    if (x1 == x2 || y1 == y2) return false;
    QTreeWidgetItem* item = findObjectByName(name);
    if (!item) return false;
    const int id = item->data(0, Qt::UserRole).toInt();
    std::vector<Geom> solids = canvas_->layerSolids(id);
    solids.push_back(Geom::box(std::min(x1, x2), std::min(y1, y2),
                               std::max(x1, x2), std::max(y1, y2)));
    canvas_->setLayerGeometry(id, solids, canvas_->layerFollows(id));
    const auto it = gerberLayers_.find(id);
    if (it != gerberLayers_.end()) {
        it->second.solids = solids;
        it->second.solid = Geom::unionOf(solids);
        it->second.bounds = it->second.solid.valid() ? it->second.solid.bounds() : BBox{};
    }
    item->setText(3, QString::number(Geom::unionOf(solids).area(), 'f', 2));
    canvas_->fitTo(canvas_->layerBounds(id));
    return true;
}

/// @brief Adaptador Tcl del comando `addpolygon`.
bool MainWindow::tclAddPolygon(const QString& name, const std::vector<double>& coords) {
    if (coords.size() < 6 || coords.size() % 2 != 0) return false;
    QTreeWidgetItem* item = findObjectByName(name);
    if (!item) return false;
    Ring ring;
    for (std::size_t i = 0; i + 1 < coords.size(); i += 2) ring.push_back({coords[i], coords[i + 1]});
    if (ring.size() < 3) return false;
    const int id = item->data(0, Qt::UserRole).toInt();
    std::vector<Geom> solids = canvas_->layerSolids(id);
    solids.push_back(Geom::polygon(ring).fixed());
    canvas_->setLayerGeometry(id, solids, canvas_->layerFollows(id));
    const auto it = gerberLayers_.find(id);
    if (it != gerberLayers_.end()) {
        it->second.solids = solids;
        it->second.solid = Geom::unionOf(solids);
        it->second.bounds = it->second.solid.valid() ? it->second.solid.bounds() : BBox{};
    }
    item->setText(3, QString::number(Geom::unionOf(solids).area(), 'f', 2));
    canvas_->fitTo(canvas_->layerBounds(id));
    return true;
}

/// @brief Adaptador Tcl del comando `addpolyline`.
bool MainWindow::tclAddPolyline(const QString& name, const std::vector<double>& coords) {
    if (coords.size() < 4 || coords.size() % 2 != 0) return false;
    QTreeWidgetItem* item = findObjectByName(name);
    if (!item) return false;
    Ring ring;
    for (std::size_t i = 0; i + 1 < coords.size(); i += 2) ring.push_back({coords[i], coords[i + 1]});
    if (ring.size() < 2) return false;
    const int id = item->data(0, Qt::UserRole).toInt();
    std::vector<Geom> follows = canvas_->layerFollows(id);
    follows.push_back(Geom::line(ring));
    canvas_->setLayerGeometry(id, canvas_->layerSolids(id), follows);
    const auto it = gerberLayers_.find(id);
    if (it != gerberLayers_.end()) {
        it->second.follows = follows;
    }
    canvas_->fitTo(canvas_->layerBounds(id));
    return true;
}

/// @brief Adaptador Tcl del comando `new`.
bool MainWindow::tclNew(const QString& name) {
    return tclNewObject(name, "geometry");
}

/// @brief Adaptador Tcl del comando `rings`.
int MainWindow::tclRings(const QString& name, bool holes) {
    QTreeWidgetItem* item = findObjectByName(name);
    if (!item) return -1;
    const int id = item->data(0, Qt::UserRole).toInt();
    const auto it = gerberLayers_.find(id);
    Geom source;
    if (it != gerberLayers_.end()) source = it->second.solid;
    else source = Geom::unionOf(canvas_->layerSolids(id));
    if (!source.valid() || source.empty()) return -1;
    std::vector<Geom> rings;
    for (const PolygonContours& pc : source.polygons()) {
        if (!holes && pc.outer.size() >= 3) rings.push_back(Geom::polygon(pc.outer));
        if (holes)
            for (const Ring& h : pc.holes)
                if (h.size() >= 3) rings.push_back(Geom::polygon(h));
    }
    if (rings.empty()) return -1;
    const QString outName = name + (holes ? "_interiors" : "_exteriors");
    const int newId = addGeneratedSolid(outName, rings, id, "geometry");
    return newId > 0 ? static_cast<int>(rings.size()) : -1;
}

/// @brief Adaptador Tcl del comando `join`.
bool MainWindow::tclJoin(const QString& a, const QString& b) {
    QTreeWidgetItem* ia = findObjectByName(a);
    QTreeWidgetItem* ib = findObjectByName(b);
    if (!ia || !ib) return false;
    const int ida = ia->data(0, Qt::UserRole).toInt();
    const int idb = ib->data(0, Qt::UserRole).toInt();
    std::vector<Geom> solids = canvas_->layerSolids(ida);
    std::vector<Geom> follows = canvas_->layerFollows(ida);
    const auto sb = canvas_->layerSolids(idb);
    const auto fb = canvas_->layerFollows(idb);
    solids.insert(solids.end(), sb.begin(), sb.end());
    follows.insert(follows.end(), fb.begin(), fb.end());
    const int id = canvas_->appendGeometry(solids, follows, true);
    canvas_->endGeometry();
    addObjectRow(a + "+" + b, "geometry", "-", id, true, toolsGroup_);
    return true;
}

/// @brief Adaptador Tcl del comando `geounion`.
bool MainWindow::tclGeoUnion(const QString& a, const QString& b) {
    QTreeWidgetItem* ia = findObjectByName(a);
    QTreeWidgetItem* ib = findObjectByName(b);
    if (!ia || !ib) return false;
    const Geom ga = Geom::unionOf(canvas_->layerSolids(ia->data(0, Qt::UserRole).toInt()));
    const Geom gb = Geom::unionOf(canvas_->layerSolids(ib->data(0, Qt::UserRole).toInt()));
    if (!ga.valid() || !gb.valid()) return false;
    const Geom united = ga.unite(gb);
    if (!united.valid() || united.empty()) return false;
    addGeneratedSolid(a + "|" + b, {united}, 0, "geometry");
    return true;
}

/// @brief Adaptador Tcl del comando `exportexcellon`.
bool MainWindow::tclExportExcellon(const QString& path) {
    auto* item = objects_->currentItem();
    if (!item) return false;
    const int id = item->data(0, Qt::UserRole).toInt();
    const auto it = excellonLayers_.find(id);
    if (it == excellonLayers_.end()) return false;
    try {
        writeExcellon(path.toStdString(), it->second, defaultUnits_);
        return true;
    } catch (const std::exception& e) {
        showError(QString::fromUtf8(e.what()));
        return false;
    }
}

/// @brief Adaptador Tcl del comando `plotobjects`.
void MainWindow::tclPlotObjects(const QStringList& names) {
    for (QTreeWidgetItem* item : objectItems()) {
        const bool show = names.contains(item->text(1));
        updatingTree_ = true;
        setObjectVisible(item, show);
        updatingTree_ = false;
    }
}

/// @brief Adaptador Tcl del comando `getpath`.
QString MainWindow::tclGetPath() const { return lastDir_; }
/// @brief Adaptador Tcl del comando `setpath`.
void MainWindow::tclSetPath(const QString& path) { lastDir_ = path; }

/// @brief Adaptador Tcl del comando `getsys`.
QString MainWindow::tclGetSys(const QString& key) const {
    return QSettings().value("sys/" + key).toString();
}

/// @brief Adaptador Tcl del comando `setsys`.
void MainWindow::tclSetSys(const QString& key, const QString& value) {
    QSettings().setValue("sys/" + key, value);
}

/// @brief Adaptador Tcl del comando `listsys`.
QString MainWindow::tclListSys() const {
    QStringList keys;
    for (const QString& key : QSettings().allKeys()) {
        if (key.startsWith("sys/")) keys << key.mid(4);
    }
    return keys.join('\n');
}

/// @brief Adaptador Tcl del comando `savesys`.
void MainWindow::tclSaveSys() { QSettings().sync(); }

/// @brief Convierte milímetros a la unidad de visualización.
double MainWindow::displayLength(double mm) const {
    return displayInch_ ? mm / 25.4 : mm;
}

/// @brief Convierte un valor de la unidad activa a milímetros.
double MainWindow::lengthToMm(double value) const {
    return displayInch_ ? value * 25.4 : value;
}

/// @brief Formatea una longitud con la unidad y decimales activos.
QString MainWindow::activeObjectBase() const {
    QTreeWidgetItem* item = objects_ ? objects_->currentItem() : nullptr;
    if (!item) return {};
    return QFileInfo(item->text(1)).completeBaseName();
}

QString MainWindow::toolLayerName(const QString& operation) const {
    const QString base = activeObjectBase();
    return base.isEmpty() ? operation : tr("%1 · %2").arg(operation, base);
}

QString MainWindow::lengthText(double mm, int decimals) const {
    return QString::number(displayLength(mm), 'f', displayInch_ ? decimals + 4 : decimals);
}

/// @brief Convierte las tablas de herramientas entre mm y pulgadas.
void MainWindow::convertToolTableUnits(bool toInch) {
    const double factor = toInch ? 1.0 / 25.4 : 25.4;
    const int decimals = toInch ? 7 : 3;
    for (QTableWidget* table : {isoTable_, nccTable_, cutTable_, paintTable_, drillTable_}) {
        if (!table) continue;
        for (int row = 0; row < table->rowCount(); ++row) {
            for (int column : {1, 6, 7}) {
                if (auto* item = table->item(row, column)) {
                    bool ok = false;
                    const double v = item->text().toDouble(&ok);
                    if (ok) item->setText(QString::number(v * factor, 'f', decimals));
                }
            }
        }
        table->setColumnWidth(1, toInch ? 78 : 42);
        table->setColumnWidth(6, toInch ? 84 : 50);
        table->setColumnWidth(7, toInch ? 84 : 48);
    }
}

/// @brief Adaptador Tcl del comando `options`.
QString MainWindow::tclOptions() const {
    return QString("units=%1 decimals=%2 steps=%3 displayInch=%4 simplify=%5 tolerance=%6")
        .arg(defaultUnits_ == Units::Inch ? "in" : "mm")
        .arg(decimals_)
        .arg(stepsPerCircle_)
        .arg(displayInch_ ? 1 : 0)
        .arg(gerberSimplify_ ? 1 : 0)
        .arg(gerberTolerance_, 0, 'f', 4);
}

/// @brief Adaptador Tcl del comando `nregions`.
int MainWindow::tclNRegions(const QString& name) {
    QTreeWidgetItem* item = findObjectByName(name);
    if (!item) return -1;
    const int id = item->data(0, Qt::UserRole).toInt();
    const auto it = gerberLayers_.find(id);
    const Geom solid = it != gerberLayers_.end() ? it->second.solid
                                                 : Geom::unionOf(canvas_->layerSolids(id));
    if (!solid.valid() || solid.empty()) return 0;
    return static_cast<int>(solid.polygons().size());
}

/// @brief Adaptador Tcl del comando `subtractshape`.
bool MainWindow::tclSubtractShape(const QString& name, const std::vector<double>& coords,
                                  bool rectangle) {
    QTreeWidgetItem* item = findObjectByName(name);
    if (!item) return false;
    const int id = item->data(0, Qt::UserRole).toInt();
    const Geom source = Geom::unionOf(canvas_->layerSolids(id));
    if (!source.valid() || source.empty()) return false;
    Geom cutter;
    if (rectangle) {
        if (coords.size() < 4) return false;
        cutter = Geom::box(std::min(coords[0], coords[2]), std::min(coords[1], coords[3]),
                           std::max(coords[0], coords[2]), std::max(coords[1], coords[3]));
    } else {
        if (coords.size() < 6 || coords.size() % 2 != 0) return false;
        Ring ring;
        for (std::size_t i = 0; i + 1 < coords.size(); i += 2)
            ring.push_back({coords[i], coords[i + 1]});
        cutter = Geom::polygon(ring).fixed();
    }
    const Geom result = source.difference(cutter);
    if (!result.valid() || result.empty()) return false;
    addGeneratedSolid(name + "_sub", {result}, id, "geometry");
    return true;
}

/// @brief Adaptador Tcl del comando `follow`.
bool MainWindow::tclFollow(const QString& name, double diameter) {
    QTreeWidgetItem* item = findObjectByName(name);
    if (!item) return false;
    const int id = item->data(0, Qt::UserRole).toInt();
    const auto it = gerberLayers_.find(id);
    if (it == gerberLayers_.end() || it->second.follows.empty()) return false;
    std::vector<Geom> paths;
    for (const Geom& g : it->second.follows) paths.push_back(g.buffer(diameter / 2.0, 8));
    addGeneratedSolid(name + "_follow", paths, id, "geometry");
    return true;
}

/// @brief Adaptador Tcl del comando `aligndrill`.
bool MainWindow::tclAlignDrill(const QString& name, const QString& reference) {
    QTreeWidgetItem* item = findObjectByName(name);
    if (!item) return false;
    const int id = item->data(0, Qt::UserRole).toInt();
    auto it = excellonLayers_.find(id);
    if (it == excellonLayers_.end()) return false;
    QTreeWidgetItem* ref = reference.isEmpty() ? nullptr : findObjectByName(reference);
    BBox target;
    if (ref) target = canvas_->layerBounds(ref->data(0, Qt::UserRole).toInt());
    if (!target.valid) {
        for (const auto& [gid, gerber] : gerberLayers_) {
            (void)gid;
            if (gerber.bounds.valid) { target = gerber.bounds; break; }
        }
    }
    if (!target.valid) return false;
    const BBox b = it->second.bounds;
    if (!b.valid) return false;
    const double dx = target.xmin - b.xmin;
    const double dy = target.ymin - b.ymin;
    for (auto& [number, tool] : it->second.tools) {
        (void)number;
        for (Pt& hole : tool.drills) { hole.x += dx; hole.y += dy; }
        for (auto& [a, c] : tool.slotSegments) {
            a.x += dx; a.y += dy; c.x += dx; c.y += dy;
        }
        for (Geom& g : tool.geometry) g = g.translate(dx, dy);
    }
    for (Geom& g : it->second.geometry) g = g.translate(dx, dy);
    it->second.solid = Geom::unionOf(it->second.geometry);
    it->second.bounds = it->second.solid.valid() ? it->second.solid.bounds() : BBox{};
    canvas_->setLayerGeometry(id, it->second.geometry, {});
    lastExcellon_ = it->second;
    lastDrillJob_ = drillJobFromExcellon(it->second);
    refreshProperties();
    return true;
}

/// @brief Adaptador Tcl del comando `aligndrillgrid`.
bool MainWindow::tclAlignDrillGrid(const QString& name, double grid) {
    if (grid <= 0.0) return false;
    QTreeWidgetItem* item = findObjectByName(name);
    if (!item) return false;
    const int id = item->data(0, Qt::UserRole).toInt();
    auto it = excellonLayers_.find(id);
    if (it == excellonLayers_.end()) return false;
    std::vector<Geom> geometry;
    for (auto& [number, tool] : it->second.tools) {
        (void)number;
        for (Pt& hole : tool.drills) {
            hole.x = std::round(hole.x / grid) * grid;
            hole.y = std::round(hole.y / grid) * grid;
            geometry.push_back(Geom::circle(hole, tool.diameter / 2.0, stepsPerCircle_));
        }
        for (const auto& [a, b] : tool.slotSegments)
            geometry.push_back(Geom::line({{a.x, a.y}, {b.x, b.y}})
                                   .buffer(tool.diameter / 2.0, 16));
    }
    it->second.geometry = geometry;
    it->second.solid = Geom::unionOf(geometry);
    it->second.bounds = it->second.solid.valid() ? it->second.solid.bounds() : BBox{};
    canvas_->setLayerGeometry(id, geometry, {});
    lastExcellon_ = it->second;
    lastDrillJob_ = drillJobFromExcellon(it->second);
    refreshProperties();
    return true;
}

/// @brief Adaptador Tcl del comando `createcncjob`.
bool MainWindow::tclCreateCncJob(const QString& name, bool drillJob) {
    GCodeOptions opt;
    opt.units = defaultUnits_;
    std::string code;
    if (drillJob) {
        if (!lastExcellon_) return false;
        code = drillingGCode(drillJobFromExcellon(*lastExcellon_), opt);
    } else {
        if (!lastGerber_) return false;
        PathJob job;
        job.bounds = lastGerber_->bounds;
        for (const Geom& g : lastGerber_->follows) {
            Ring ring = g.coords();
            if (ring.size() >= 2) job.paths.emplace_back(ring.begin(), ring.end());
        }
        if (job.paths.empty()) return false;
        code = pathsGCode(job, opt);
    }
    addCncJobObject(name.isEmpty() ? tr("CNCJob") : name, QString::fromStdString(code));
    return true;
}

/// @brief Adaptador Tcl del comando `milldrills`.
bool MainWindow::tclMillDrills(const QString& name, double diameter) {
    if (!lastExcellon_ || diameter <= 0.0) return false;
    PathJob job;
    job.toolDiameter = diameter;
    BBox bounds;
    for (const auto& [number, tool] : lastExcellon_->tools) {
        (void)number;
        for (const Pt& hole : tool.drills) {
            const double radius = tool.diameter / 2.0 + diameter / 2.0;
            std::vector<Pt> ring;
            for (int i = 0; i < 32; ++i) {
                const double a = 2.0 * M_PI * i / 32.0;
                ring.push_back({hole.x + radius * std::cos(a), hole.y + radius * std::sin(a)});
            }
            job.paths.push_back(ring);
            if (!bounds.valid) bounds = BBox{hole.x - radius, hole.y - radius,
                                             hole.x + radius, hole.y + radius};
        }
    }
    if (job.paths.empty()) return false;
    job.bounds = bounds;
    GCodeOptions opt;
    opt.units = defaultUnits_;
    addCncJobObject(name.isEmpty() ? tr("Mill drills") : name,
                    QString::fromStdString(pathsGCode(job, opt)));
    return true;
}

/// @brief Adaptador Tcl del comando `millslots`.
bool MainWindow::tclMillSlots(const QString& name, double diameter) {
    if (!lastExcellon_ || diameter <= 0.0) return false;
    PathJob job;
    job.toolDiameter = diameter;
    for (const auto& [number, tool] : lastExcellon_->tools) {
        (void)number;
        for (const auto& [a, b] : tool.slotSegments) {
            const double radius = tool.diameter / 2.0 + diameter / 2.0;
            std::vector<Pt> ring;
            const double angle = std::atan2(b.y - a.y, b.x - a.x);
            for (int i = 0; i <= 16; ++i) {
                const double t = M_PI * i / 16.0;
                ring.push_back({b.x + radius * std::cos(angle + t),
                                b.y + radius * std::sin(angle + t)});
            }
            for (int i = 0; i <= 16; ++i) {
                const double t = M_PI * i / 16.0;
                ring.push_back({a.x + radius * std::cos(angle + M_PI + t),
                                a.y + radius * std::sin(angle + M_PI + t)});
            }
            job.paths.push_back(ring);
        }
    }
    if (job.paths.empty()) return false;
    GCodeOptions opt;
    opt.units = defaultUnits_;
    addCncJobObject(name.isEmpty() ? tr("Mill slots") : name,
                    QString::fromStdString(pathsGCode(job, opt)));
    return true;
}

/// @brief Adaptador Tcl del comando `setorigin`.
void MainWindow::tclSetOrigin(double x, double y) {
    canvas_->setDisplayOrigin({x, y});
}

/// @brief Adaptador Tcl del comando `info`.
QString MainWindow::tclInfo() const {
    return tr("objetos=%1 gerber=%2 excellon=%3\n")
        .arg(objectItems().size())
        .arg(lastGerber_ ? lastGerber_->solid.area() : 0.0)
        .arg(lastExcellon_ ? lastExcellon_->drillCount : 0);
}

// ============================================================================
// Preferencias, tema y nivel de interfaz
// ============================================================================

/// @brief Carga preferencias y aplica unidad, tema y nivel.
void MainWindow::loadPreferences() {
    QSettings settings;
    decimals_ = settings.value("preferences/decimals", 3).toInt();
    stepsPerCircle_ = settings.value("preferences/steps", 64).toInt();
    defaultUnits_ = settings.value("preferences/units", "mm").toString() == "in"
                        ? fc::Units::Inch
                        : fc::Units::Mm;
    background_ = settings.value("preferences/bg", background_).value<QColor>();
    copperColor_ = settings.value("preferences/copper", copperColor_).value<QColor>();
    followColor_ = settings.value("preferences/follow", followColor_).value<QColor>();
    toolpathColor_ = settings.value("preferences/toolpath", toolpathColor_).value<QColor>();
    darkTheme_ = settings.value("preferences/darkTheme", false).toBool();
    customToolchange_ = settings.value("preferences/customToolchange").toString();
    {
        const bool wasInch = displayInch_;
        displayInch_ = settings.value("preferences/displayInch", false).toBool();
        if (wasInch != displayInch_) convertToolTableUnits(displayInch_);
    }
    snapGrid_ = settings.value("preferences/snapGrid", false).toBool();
    advancedLevel_ = settings.value("preferences/advancedLevel", true).toBool();
    UnitSpinBox::setDisplayInch(displayInch_);
    if (unitsLabel_) unitsLabel_->setText(displayInch_ ? "in" : "mm");
    gerberSimplify_ = settings.value("preferences/gerberSimplify", false).toBool();
    gerberTolerance_ = settings.value("preferences/gerberTolerance", 0.005).toDouble();
    geometrySimplify_ = settings.value("preferences/geometrySimplify", false).toBool();
    geometryTolerance_ = settings.value("preferences/geometryTolerance", 0.01).toDouble();
    excellonExtractMaxDia_ = settings.value("preferences/excellonExtractMaxDia", 3.0).toDouble();
    cncDefaultFeedXY_ = settings.value("preferences/cncFeedXY", 300.0).toDouble();
    cncDefaultFeedZ_ = settings.value("preferences/cncFeedZ", 60.0).toDouble();
    cncDefaultRPM_ = settings.value("preferences/cncRPM", 10000.0).toDouble();
    cncDefaultZCut_ = settings.value("preferences/cncZCut", -1.5).toDouble();
    const double wsW = settings.value("preferences/workspaceW", 0.0).toDouble();
    const double wsH = settings.value("preferences/workspaceH", 0.0).toDouble();
    if (canvas_ && wsW > 0.0 && wsH > 0.0) canvas_->setWorkspace(BBox{0.0, 0.0, wsW, wsH});
    if (canvas_) canvas_->setHudVisible(settings.value("preferences/hud", false).toBool());
    if (canvas_) canvas_->setColors(background_, copperColor_, followColor_, toolpathColor_);
}

/// @brief Persiste las preferencias en QSettings.
void MainWindow::savePreferences() {
    QSettings settings;
    settings.setValue("preferences/decimals", decimals_);
    settings.setValue("preferences/steps", stepsPerCircle_);
    settings.setValue("preferences/units", defaultUnits_ == fc::Units::Inch ? "in" : "mm");
    settings.setValue("preferences/bg", background_);
    settings.setValue("preferences/copper", copperColor_);
    settings.setValue("preferences/follow", followColor_);
    settings.setValue("preferences/toolpath", toolpathColor_);
    settings.setValue("preferences/darkTheme", darkTheme_);
    settings.setValue("preferences/displayInch", displayInch_);
    settings.setValue("preferences/snapGrid", snapGrid_);
    settings.setValue("preferences/advancedLevel", advancedLevel_);
    settings.setValue("preferences/gerberSimplify", gerberSimplify_);
    settings.setValue("preferences/gerberTolerance", gerberTolerance_);
    settings.setValue("preferences/geometrySimplify", geometrySimplify_);
    settings.setValue("preferences/geometryTolerance", geometryTolerance_);
    settings.setValue("preferences/excellonExtractMaxDia", excellonExtractMaxDia_);
    settings.setValue("preferences/cncFeedXY", cncDefaultFeedXY_);
    settings.setValue("preferences/cncFeedZ", cncDefaultFeedZ_);
    settings.setValue("preferences/cncRPM", cncDefaultRPM_);
    settings.setValue("preferences/cncZCut", cncDefaultZCut_);
}

namespace {

QSqlDatabase toolDatabase() {
    static bool initialized = false;
    static const QString connection = "flatcam_tools";
    QSqlDatabase db = QSqlDatabase::contains(connection)
                          ? QSqlDatabase::database(connection)
                          : QSqlDatabase::addDatabase("QSQLITE", connection);
    if (!initialized) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dir);
        db.setDatabaseName(dir + "/tools.db");
        if (db.open()) {
            QSqlQuery query(db);
            query.exec("CREATE TABLE IF NOT EXISTS tools ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "name TEXT NOT NULL,"
                       "diameter REAL NOT NULL,"
                       "feedXY REAL NOT NULL,"
                       "feedZ REAL NOT NULL,"
                       "rpm REAL NOT NULL,"
                       "laser REAL NOT NULL,"
                       "zCut REAL NOT NULL,"
                       "depth REAL NOT NULL)");
        }
        initialized = true;
    }
    return db;
}

}  // namespace

/// @brief Lee la base SQLite de herramientas.
QJsonArray MainWindow::toolDbEntries() const {
    QJsonArray out;
    QSqlDatabase db = toolDatabase();
    if (!db.isOpen()) return out;
    QSqlQuery query(db);
    if (!query.exec("SELECT name, diameter, feedXY, feedZ, rpm, laser, zCut, depth "
                    "FROM tools ORDER BY diameter, name"))
        return out;
    while (query.next()) {
        QJsonObject entry;
        entry["name"] = query.value(0).toString();
        entry["diameter"] = query.value(1).toDouble();
        entry["feedXY"] = query.value(2).toDouble();
        entry["feedZ"] = query.value(3).toDouble();
        entry["rpm"] = query.value(4).toDouble();
        entry["laser"] = query.value(5).toDouble();
        entry["zCut"] = query.value(6).toDouble();
        entry["depth"] = query.value(7).toDouble();
        out.append(entry);
    }
    if (out.isEmpty()) {
        QJsonArray seed;
        const double diameters[] = {0.1, 0.2, 0.3, 0.4, 0.5, 0.8, 1.0, 2.0, 3.0};
        for (double dia : diameters) {
            QJsonObject entry;
            entry["name"] = tr("Fresa %1 mm").arg(dia, 0, 'f', 2);
            entry["diameter"] = dia;
            entry["feedXY"] = 300.0;
            entry["feedZ"] = 60.0;
            entry["rpm"] = 10000.0;
            entry["laser"] = 300.0;
            entry["zCut"] = -1.5;
            entry["depth"] = 0.0;
            seed.append(entry);
        }
        setToolDbEntries(seed);
        return seed;
    }
    return out;
}

/// @brief Reemplaza la base de herramientas.
void MainWindow::setToolDbEntries(const QJsonArray& entries) const {
    QSqlDatabase db = toolDatabase();
    if (!db.isOpen()) return;
    db.transaction();
    QSqlQuery clear(db);
    clear.exec("DELETE FROM tools");
    QSqlQuery insert(db);
    insert.prepare("INSERT INTO tools (name, diameter, feedXY, feedZ, rpm, laser, zCut, depth) "
                   "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    for (const QJsonValue& value : entries) {
        const QJsonObject entry = value.toObject();
        insert.addBindValue(entry.value("name").toString());
        insert.addBindValue(entry.value("diameter").toDouble());
        insert.addBindValue(entry.value("feedXY").toDouble());
        insert.addBindValue(entry.value("feedZ").toDouble());
        insert.addBindValue(entry.value("rpm").toDouble());
        insert.addBindValue(entry.value("laser").toDouble());
        insert.addBindValue(entry.value("zCut").toDouble());
        insert.addBindValue(entry.value("depth").toDouble());
        insert.exec();
    }
    db.commit();
}

/// @brief Abre el editor de la base de herramientas.
void MainWindow::showToolDb() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Base de datos de herramientas"));
    dialog.resize(900, 520);
    auto* layout = new QVBoxLayout(&dialog);
    auto* table = new QTableWidget(0, 8, &dialog);
    table->setHorizontalHeaderLabels({tr("Nombre"), tr("Ø"), tr("Av. XY"), tr("Av. Z"),
                                      tr("RPM"), tr("S"), tr("Z corte"), tr("Prof.")});
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    layout->addWidget(table);

    auto fill = [table](const QJsonArray& entries) {
        table->setRowCount(0);
        for (const QJsonValue& value : entries) {
            const QJsonObject entry = value.toObject();
            const int row = table->rowCount();
            table->insertRow(row);
            const QStringList values{
                entry.value("name").toString(),
                QString::number(entry.value("diameter").toDouble(), 'f', 3),
                QString::number(entry.value("feedXY").toDouble(), 'f', 1),
                QString::number(entry.value("feedZ").toDouble(), 'f', 1),
                QString::number(entry.value("rpm").toDouble(), 'f', 0),
                QString::number(entry.value("laser").toDouble(), 'f', 1),
                QString::number(entry.value("zCut").toDouble(), 'f', 3),
                QString::number(entry.value("depth").toDouble(), 'f', 3)};
            for (int c = 0; c < values.size(); ++c)
                table->setItem(row, c, new QTableWidgetItem(values[c]));
        }
    };
    fill(toolDbEntries());

    auto* buttons = new QHBoxLayout();
    auto* add = new QPushButton(originalIcon("plus"), tr("Agregar"), &dialog);
    auto* remove = new QPushButton(originalIcon("trash"), tr("Eliminar"), &dialog);
    auto* save = new QPushButton(originalIcon("save"), tr("Guardar"), &dialog);
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch();
    buttons->addWidget(save);
    layout->addLayout(buttons);

    connect(add, &QPushButton::clicked, &dialog, [table] {
        const int row = table->rowCount();
        table->insertRow(row);
        const QStringList values{tr("Nueva herramienta"), "0.200", "300.0", "60.0",
                                 "10000", "300.0", "-1.500", "0.000"};
        for (int c = 0; c < values.size(); ++c)
            table->setItem(row, c, new QTableWidgetItem(values[c]));
    });
    connect(remove, &QPushButton::clicked, &dialog, [table] {
        if (table->currentRow() >= 0) table->removeRow(table->currentRow());
    });
    connect(save, &QPushButton::clicked, &dialog, [this, table] {
        QJsonArray entries;
        for (int row = 0; row < table->rowCount(); ++row) {
            auto cell = [table, row](int column) {
                auto* item = table->item(row, column);
                return item ? item->text() : QString();
            };
            QJsonObject entry;
            entry["name"] = cell(0);
            entry["diameter"] = cell(1).toDouble();
            entry["feedXY"] = cell(2).toDouble();
            entry["feedZ"] = cell(3).toDouble();
            entry["rpm"] = cell(4).toDouble();
            entry["laser"] = cell(5).toDouble();
            entry["zCut"] = cell(6).toDouble();
            entry["depth"] = cell(7).toDouble();
            entries.append(entry);
        }
        setToolDbEntries(entries);
    });

    auto* box = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(box);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

/// @brief Agrega una herramienta de la base a una tabla.
void MainWindow::addToolFromDb(QTableWidget* table) {
    if (!table) return;
    const QJsonArray entries = toolDbEntries();
    if (entries.isEmpty()) {
        QMessageBox::information(this, tr("Base de datos"),
                                 tr("La base de datos está vacía."));
        return;
    }
    QStringList labels;
    for (const QJsonValue& value : entries) {
        const QJsonObject entry = value.toObject();
        labels << tr("%1 · Ø %2 mm · %3/%4 mm/min · %5 RPM")
                      .arg(entry.value("name").toString())
                      .arg(entry.value("diameter").toDouble(), 0, 'f', 3)
                      .arg(entry.value("feedXY").toDouble(), 0, 'f', 0)
                      .arg(entry.value("feedZ").toDouble(), 0, 'f', 0)
                      .arg(entry.value("rpm").toDouble(), 0, 'f', 0);
    }
    bool ok = false;
    const QString chosen = QInputDialog::getItem(
        this, tr("Herramienta desde la base de datos"), tr("Herramienta:"), labels, 0, false, &ok);
    if (!ok) return;
    const int index = labels.indexOf(chosen);
    if (index < 0 || index >= entries.size()) return;
    const QJsonObject entry = entries[index].toObject();
    const int row = table->rowCount();
    table->insertRow(row);
    const QStringList values{
        QString::number(row + 1),
        QString::number(entry.value("diameter").toDouble(), 'f', 3),
        QString::number(entry.value("feedXY").toDouble(), 'f', 1),
        QString::number(entry.value("feedZ").toDouble(), 'f', 1),
        QString::number(entry.value("rpm").toDouble(), 'f', 0),
        QString::number(entry.value("laser").toDouble(), 'f', 1),
        QString::number(entry.value("zCut").toDouble(), 'f', 3),
        QString::number(entry.value("depth").toDouble(), 'f', 3)};
    for (int c = 0; c < values.size(); ++c) {
        auto* item = new QTableWidgetItem(values[c]);
        if (c == 0) item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, c, item);
    }
}

/// @brief Muestra u oculta los controles avanzados.
void MainWindow::applyUiLevel() {
    for (QWidget* widget : advancedWidgets_) {
        if (widget) widget->setVisible(advancedLevel_);
    }
}

/// @brief Aplica el tema claro u oscuro en caliente.
void MainWindow::applyTheme(bool dark) {
    darkTheme_ = dark;
    QSettings().setValue("preferences/darkTheme", dark);
    if (qApp) {
        fc::theme::apply(*qApp, dark);
        update();
    }
}

/// @brief Agrega un objeto al árbol del proyecto.
QTreeWidgetItem* MainWindow::addObjectRow(const QString& name, const QString& kind,
                                          const QString& area, int layerId, bool visible,
                                          QTreeWidgetItem* parent) {
    if (!parent) {
        if (kind == "gerber" || kind == "excellon") parent = filesGroup_;
        else if (kind == "gcode" || kind == "cncjob") parent = gcodeGroup_;
        else parent = toolsGroup_;
    }
    auto* item = new QTreeWidgetItem(parent);
    item->setFlags((item->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsSelectable |
                    Qt::ItemIsEnabled) & ~Qt::ItemIsDropEnabled);
    item->setText(1, name);
    item->setText(2, kind);
    item->setText(3, area);
    item->setData(0, Qt::UserRole, layerId);
    setObjectVisible(item, visible);
    refreshSourceCombos();
    markProjectDirty();
    return item;
}

/// @brief Agrega una capa generada al grupo Herramientas.
int MainWindow::addGeneratedLayer(const QString& name, const std::vector<Geom>& paths, int sourceId) {
    if (paths.empty()) return -1;
    const int id = canvas_->appendGeometry({}, paths, true);
    canvas_->endGeometry();
    addObjectRow(name, "herramienta", "-", id, true, toolsGroup_);
    if (sourceId > 0) generatedFrom_[sourceId].push_back(id);
    if (toolsGroup_) toolsGroup_->setExpanded(true);
    refreshSourceCombos();
    if (statusLabel_)
        statusLabel_->setText(tr("%1 archivos · %2 objetos")
                                  .arg(filesGroup_ ? filesGroup_->childCount() : 0)
                                  .arg(objectItems().size()));
    if (!canvas_->hasData()) canvas_->fitTo(canvas_->layerBounds(id));
    return id;
}

/// @brief Agrega un sólido generado como capa nueva.
int MainWindow::addGeneratedSolid(const QString& name, const std::vector<Geom>& solids,
                                 int sourceId, const QString& kind) {
    if (solids.empty()) return -1;
    const int id = canvas_->appendGeometry(solids, {}, true);
    canvas_->endGeometry();
    double area = 0.0;
    for (const Geom& g : solids) area += g.area();
    addObjectRow(name, kind, QString::number(area, 'f', 2), id, true, toolsGroup_);
    if (sourceId > 0) generatedFrom_[sourceId].push_back(id);
    if (toolsGroup_) toolsGroup_->setExpanded(true);
    refreshSourceCombos();
    if (statusLabel_)
        statusLabel_->setText(tr("%1 archivos · %2 objetos")
                                  .arg(filesGroup_ ? filesGroup_->childCount() : 0)
                                  .arg(objectItems().size()));
    if (!canvas_->hasData()) canvas_->fitTo(canvas_->layerBounds(id));
    return id;
}

/// @brief Reúne la geometría de una capa para exportar o editar.
std::vector<Geom> MainWindow::layerGeometries(int layerId) const {
    std::vector<Geom> out = canvas_->layerSolids(layerId);
    const std::vector<Geom> follows = canvas_->layerFollows(layerId);
    out.insert(out.end(), follows.begin(), follows.end());
    return out;
}

/// @brief Agrega un CNCJob al grupo G-code.
void MainWindow::addGCodeRow(const QString& path) {
    if (!gcodeGroup_) return;
    auto* item = new QTreeWidgetItem(gcodeGroup_);
    item->setText(1, QFileInfo(path).fileName());
    item->setText(2, "gcode");
    item->setText(3, "-");
    item->setData(0, Qt::UserRole + 2, path);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    gcodeGroup_->setExpanded(true);
    if (gcodeView_) {
        QFile in(path);
        if (in.open(QIODevice::ReadOnly | QIODevice::Text))
            gcodeView_->setPlainText(QString::fromUtf8(in.readAll()));
    }
}

/// @brief Lista los ítems de objeto del árbol.
std::vector<QTreeWidgetItem*> MainWindow::objectItems() const {
    std::vector<QTreeWidgetItem*> out;
    for (QTreeWidgetItem* group : {filesGroup_, toolsGroup_, gcodeGroup_}) {
        if (!group) continue;
        for (int i = 0; i < group->childCount(); ++i) {
            QTreeWidgetItem* child = group->child(i);
            if (child->data(0, Qt::UserRole).toInt() > 0) out.push_back(child);
        }
    }
    return out;
}

/// @brief Busca el ítem del árbol por id de capa.
QTreeWidgetItem* MainWindow::findObjectItem(int layerId) const {
    for (QTreeWidgetItem* item : objectItems()) {
        if (item->data(0, Qt::UserRole).toInt() == layerId) return item;
    }
    return nullptr;
}

/// @brief Busca un objeto por nombre.
QTreeWidgetItem* MainWindow::findObjectByName(const QString& name) const {
    for (QTreeWidgetItem* item : objectItems()) {
        if (item->text(1) == name) return item;
    }
    for (QTreeWidgetItem* item : objectItems()) {
        if (item->text(1).compare(name, Qt::CaseInsensitive) == 0) return item;
    }
    return nullptr;
}

/// @brief Reinicia los grupos Archivos/Herramientas/G-code.
void MainWindow::resetObjectGroups() {
    objects_->clear();
    filesGroup_ = new QTreeWidgetItem(objects_);
    filesGroup_->setText(1, tr("Archivos"));
    filesGroup_->setFlags(Qt::ItemIsEnabled);
    toolsGroup_ = new QTreeWidgetItem(objects_);
    toolsGroup_->setText(1, tr("Herramientas"));
    toolsGroup_->setFlags(Qt::ItemIsEnabled);
    gcodeGroup_ = new QTreeWidgetItem(objects_);
    gcodeGroup_->setText(1, tr("G-code"));
    gcodeGroup_->setFlags(Qt::ItemIsEnabled);
    objects_->expandAll();
    generatedFrom_.clear();
    refreshSourceCombos();
}

/// @brief Muestra u oculta un objeto y sus capas derivadas.
void MainWindow::setObjectVisible(QTreeWidgetItem* item, bool visible) {
    if (!item) return;
    item->setIcon(0, visibilityIcon(visible));
    item->setData(0, Qt::UserRole + 1, visible);
    const int layerId = item->data(0, Qt::UserRole).toInt();
    if (canvas_) canvas_->setLayerVisible(layerId, visible);
    const auto it = generatedFrom_.find(layerId);
    if (it == generatedFrom_.end()) return;
    for (const int childId : it->second) {
        if (QTreeWidgetItem* child = findObjectItem(childId)) {
            child->setIcon(0, visibilityIcon(visible));
            child->setData(0, Qt::UserRole + 1, visible);
            canvas_->setLayerVisible(childId, visible);
        }
    }
}

/// @brief Construye la consola JavaScript inferior.
void MainWindow::buildConsole(QTabWidget* tabs) {
    auto* page = new QWidget(tabs);
    auto* layout = new QVBoxLayout(page);
    consoleOutput_ = new QPlainTextEdit(page);
    consoleOutput_->setReadOnly(true);
    consoleOutput_->setMaximumBlockCount(1000);
    consoleOutput_->setStyleSheet("QPlainTextEdit { font-family: Menlo; font-size: 12px; "
                                  "background:#141a1f; color:#c9d1d9; }");
    consoleOutput_->setPlainText(tr("Consola JavaScript de Qt.\n"
                                    "Ejemplos:\n"
                                    "  flatcam.info()\n"
                                    "  flatcam.openGerber('archivo.gbr')\n"
                                    "  flatcam.isolate(0.2, 1, 0)\n"
                                    "  flatcam.exportGcode('/tmp/salida.nc')\n"));
    consoleInput_ = new QLineEdit(page);
    consoleInput_->setPlaceholderText(tr("Escribí una expresión y Enter (clear para limpiar)"));
    layout->addWidget(consoleOutput_);
    layout->addWidget(consoleInput_);
    tabs->addTab(page, tr("Consola JS"));

    connect(consoleInput_, &QLineEdit::returnPressed, this, [this] {
        const QString line = consoleInput_->text().trimmed();
        if (line.isEmpty()) return;
        consoleInput_->clear();
        if (line == "clear") {
            consoleOutput_->clear();
            return;
        }
        if (!consoleEngine_) {
            consoleEngine_ = new QJSEngine(this);
            consoleApi_ = new fc::ScriptApi(this);
            consoleApi_->setLayerSink([this](const QString& name, const QString& kind,
                                             const std::vector<Geom>& solids,
                                             const std::vector<Geom>& follows) {
                const int id = canvas_->appendGeometry(solids, follows, true);
                canvas_->endGeometry();
                addObjectRow(name, kind, "-", id);
                if (kind == "gerber") {
                    GerberResult r;
                    r.solids = solids;
                    r.follows = follows;
                    r.solid = solids.empty() ? Geom() : Geom::unionOf(solids);
                    r.bounds = r.solid.valid() ? r.solid.bounds() : BBox{};
                    gerberLayers_[id] = r;
                } else if (kind == "excellon") {
                    ExcellonResult r;
                    r.geometry = solids;
                    r.solid = solids.empty() ? Geom() : Geom::unionOf(solids);
                    r.bounds = r.solid.valid() ? r.solid.bounds() : BBox{};
                    excellonLayers_[id] = r;
                }
                if (!canvas_->hasData()) canvas_->fitTo(canvas_->layerBounds(id));
            });
            fc::installScriptApi(*consoleEngine_, consoleApi_);
        }
        const QJSValue result = consoleEngine_->evaluate(line);
        consoleOutput_->appendPlainText("> " + line);
        if (result.isError())
            consoleOutput_->appendPlainText("ERROR: " + result.toString());
        else if (!result.isUndefined())
            consoleOutput_->appendPlainText(result.toString());
    });
}

/// @brief Paneliza el objeto activo en filas/columnas.
void MainWindow::panelize() {
    auto* item = objects_->currentItem();
    if (!item) return;
    const int id = item->data(0, Qt::UserRole).toInt();
    const std::vector<Geom> solids = canvas_->layerSolids(id);
    if (solids.empty()) {
        QMessageBox::information(this, tr("Panelizar"),
                                 tr("El objeto no tiene geometría editable."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Panelizar"));
    auto* form = new QFormLayout(&dialog);
    auto* reference = new QComboBox(&dialog);
    for (QTreeWidgetItem* row : objectItems()) {
        reference->addItem(row->text(1), row->data(0, Qt::UserRole).toInt());
    }
    const int currentIndex = reference->findData(
        objects_->currentItem() ? objects_->currentItem()->data(0, Qt::UserRole).toInt() : 0);
    if (currentIndex >= 0) reference->setCurrentIndex(currentIndex);
    auto* panelType = new QComboBox(&dialog);
    panelType->addItems({tr("Grilla"), tr("Fila"), tr("Columna")});
    auto* rows = new QSpinBox(&dialog);
    rows->setRange(1, 50);
    rows->setValue(2);
    auto* cols = new QSpinBox(&dialog);
    cols->setRange(1, 50);
    cols->setValue(2);
    auto* gapX = new UnitSpinBox(&dialog);
    gapX->setRange(0.0, 500.0);
    gapX->setDecimals(2);
    gapX->setValue(2.0);
    gapX->setSuffix(" mm");
    auto* gapY = new UnitSpinBox(&dialog);
    gapY->setRange(0.0, 500.0);
    gapY->setDecimals(2);
    gapY->setValue(2.0);
    gapY->setSuffix(" mm");
    form->addRow(tr("Referencia:"), reference);
    form->addRow(tr("Tipo:"), panelType);
    form->addRow(tr("Filas:"), rows);
    form->addRow(tr("Columnas:"), cols);
    auto* angle = new UnitSpinBox(&dialog);
    angle->setRange(-180.0, 180.0);
    angle->setDecimals(1);
    angle->setValue(0.0);
    angle->setSuffix(" °");
    auto* outline = new QCheckBox(tr("Agregar contorno del panel"), &dialog);
    outline->setChecked(true);
    auto* fillet = new UnitSpinBox(&dialog);
    fillet->setRange(0.0, 20.0);
    fillet->setDecimals(2);
    fillet->setSuffix(" mm");
    fillet->setToolTip(tr("Radio de las esquinas del contorno del panel."));
    form->addRow(tr("Separación X:"), gapX);
    form->addRow(tr("Separación Y:"), gapY);
    form->addRow(tr("Rotación:"), angle);
    form->addRow(QString(), outline);
    form->addRow(tr("Radio esquina:"), fillet);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(box);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    BBox b = canvas_->layerBounds(id);
    const int refId = reference->currentData().toInt();
    if (refId > 0 && refId != id) {
        const BBox rb = canvas_->layerBounds(refId);
        if (rb.valid) b = rb;
    }
    if (!b.valid) return;
    const Pt center{(b.xmin + b.xmax) * 0.5, (b.ymin + b.ymax) * 0.5};
    std::vector<Geom> rotated;
    rotated.reserve(solids.size());
    for (const Geom& g : solids) rotated.push_back(g.rotateDeg(angle->value(), center));
    BBox rb;
    for (const Geom& g : rotated) {
        const BBox gb = g.bounds();
        if (!gb.valid) continue;
        if (!rb.valid) rb = gb;
        else {
            rb.xmin = std::min(rb.xmin, gb.xmin);
            rb.ymin = std::min(rb.ymin, gb.ymin);
            rb.xmax = std::max(rb.xmax, gb.xmax);
            rb.ymax = std::max(rb.ymax, gb.ymax);
        }
    }
    if (!rb.valid) return;
    int gridRows = rows->value();
    int gridCols = cols->value();
    if (panelType->currentIndex() == 1) gridRows = 1;
    if (panelType->currentIndex() == 2) gridCols = 1;
    const double dx = rb.width() + gapX->value();
    const double dy = rb.height() + gapY->value();
    std::vector<Geom> out;
    for (int r = 0; r < gridRows; ++r) {
        for (int c = 0; c < gridCols; ++c) {
            const double ox = b.xmin - rb.xmin + c * dx;
            const double oy = b.ymin - rb.ymin + r * dy;
            for (const Geom& g : rotated) out.push_back(g.translate(ox, oy));
        }
    }
    std::vector<Geom> follows;
    if (outline->isChecked() && !out.empty()) {
        BBox pb;
        for (const Geom& g : out) {
            const BBox gb = g.bounds();
            if (!gb.valid) continue;
            if (!pb.valid) pb = gb;
            else {
                pb.xmin = std::min(pb.xmin, gb.xmin);
                pb.ymin = std::min(pb.ymin, gb.ymin);
                pb.xmax = std::max(pb.xmax, gb.xmax);
                pb.ymax = std::max(pb.ymax, gb.ymax);
            }
        }
        const double m = 2.0;
        pb = pb.expanded(m);
        Geom outlineGeom = Geom::box(pb.xmin, pb.ymin, pb.xmax, pb.ymax);
        if (fillet->value() > 0.0) {
            const double r = std::min({fillet->value(), pb.width() / 2.0, pb.height() / 2.0});
            outlineGeom = outlineGeom.buffer(-r, 16).buffer(r, 16);
        }
        bool added = false;
        for (const PolygonContours& pc : outlineGeom.polygons()) {
            if (pc.outer.size() >= 3) {
                Ring ring = pc.outer;
                ring.push_back(ring.front());
                follows.push_back(Geom::line(ring));
                added = true;
                break;
            }
        }
        if (!added)
            follows.push_back(Geom::line({{pb.xmin, pb.ymin}, {pb.xmax, pb.ymin},
                                          {pb.xmax, pb.ymax}, {pb.xmin, pb.ymax},
                                          {pb.xmin, pb.ymin}}));
    }
    const int newId = canvas_->appendGeometry(out, follows, true);
    addObjectRow(tr("Panel %1x%2").arg(gridRows).arg(gridCols), "panel", "-", newId);
    canvas_->endGeometry();
    statusBar()->showMessage(tr("Panel generado"), 4000);
}

/// @brief Agrega cuatro marcas fiduciales en las esquinas.
void MainWindow::addFiducials() {
    auto* item = objects_->currentItem();
    if (!item) return;
    const int id = item->data(0, Qt::UserRole).toInt();
    const BBox b = canvas_->layerBounds(id);
    if (!b.valid) return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Añadir fiduciales"));
    auto* form = new QFormLayout(&dialog);
    auto* diameter = new UnitSpinBox(&dialog);
    diameter->setRange(0.1, 10.0);
    diameter->setDecimals(2);
    diameter->setValue(1.0);
    diameter->setSuffix(" mm");
    auto* inset = new UnitSpinBox(&dialog);
    inset->setRange(0.0, 50.0);
    inset->setDecimals(2);
    inset->setValue(3.0);
    inset->setSuffix(" mm");
    form->addRow(tr("Diámetro:"), diameter);
    form->addRow(tr("Distancia al borde:"), inset);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(box);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    const double x0 = b.xmin + inset->value();
    const double y0 = b.ymin + inset->value();
    const double x1 = b.xmax - inset->value();
    const double y1 = b.ymax - inset->value();
    std::vector<Geom> marks = {Geom::circle({x0, y0}, diameter->value() / 2, 32),
                               Geom::circle({x1, y0}, diameter->value() / 2, 32),
                               Geom::circle({x0, y1}, diameter->value() / 2, 32),
                               Geom::circle({x1, y1}, diameter->value() / 2, 32)};
    const int newId = canvas_->appendGeometry(marks, {}, true);
    addObjectRow(tr("Fiduciales"), "gerber", "-", newId, true, toolsGroup_);
    canvas_->endGeometry();
    statusBar()->showMessage(tr("Fiduciales agregados"), 4000);
}

/// @brief Abre la calibración de escala por distancia medida/real.
void MainWindow::calibrate() {
    auto* item = objects_->currentItem();
    if (!item) return;
    const int id = item->data(0, Qt::UserRole).toInt();
    if (id <= 0) return;
    const Canvas::LayerSnapshot before = canvas_->layerSnapshot(id);
    if (before.id == 0) return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Calibrar escala"));
    auto* form = new QFormLayout(&dialog);
    auto* measured = new UnitSpinBox(&dialog);
    measured->setRange(0.001, 10000.0);
    measured->setDecimals(4);
    measured->setValue(10.0);
    measured->setSuffix(" mm");
    auto* real = new UnitSpinBox(&dialog);
    real->setRange(0.001, 10000.0);
    real->setDecimals(4);
    real->setValue(10.0);
    real->setSuffix(" mm");
    auto* usePoints = new QCheckBox(tr("Calcular desde dos puntos"), &dialog);
    auto* x1 = new UnitSpinBox(&dialog);
    auto* y1 = new UnitSpinBox(&dialog);
    auto* x2 = new UnitSpinBox(&dialog);
    auto* y2 = new UnitSpinBox(&dialog);
    for (QDoubleSpinBox* spin : {x1, y1, x2, y2}) {
        spin->setRange(-100000, 100000);
        spin->setDecimals(3);
        spin->setSuffix(" mm");
        spin->setEnabled(false);
    }
    x2->setValue(10.0);
    auto updateFromPoints = [=] {
        const double d = std::hypot(x2->value() - x1->value(), y2->value() - y1->value());
        measured->setValue(std::max(0.001, d));
    };
    connect(usePoints, &QCheckBox::toggled, this, [=](bool on) {
        for (QDoubleSpinBox* spin : {x1, y1, x2, y2}) spin->setEnabled(on);
        measured->setEnabled(!on);
        if (on) updateFromPoints();
    });
    for (QDoubleSpinBox* spin : {x1, y1, x2, y2})
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [=](double) { if (usePoints->isChecked()) updateFromPoints(); });
    form->addRow(tr("Distancia medida en el objeto:"), measured);
    form->addRow(QString(), usePoints);
    form->addRow(tr("Punto 1 X:"), x1);
    form->addRow(tr("Punto 1 Y:"), y1);
    form->addRow(tr("Punto 2 X:"), x2);
    form->addRow(tr("Punto 2 Y:"), y2);
    form->addRow(tr("Distancia real:"), real);
    auto* verify = new QCheckBox(tr("Generar G-code de verificación"), &dialog);
    verify->setChecked(true);
    form->addRow(QString(), verify);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(box);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    const double measuredValue = usePoints->isChecked()
        ? std::max(0.001, std::hypot(x2->value() - x1->value(), y2->value() - y1->value()))
        : measured->value();
    const double factor = real->value() / measuredValue;
    const BBox b = canvas_->layerBounds(id);
    const double cx = b.valid ? (b.xmin + b.xmax) / 2.0 : 0.0;
    const double cy = b.valid ? (b.ymin + b.ymax) / 2.0 : 0.0;
    QTransform transform;
    transform.translate(cx, cy);
    transform.scale(factor, factor);
    transform.translate(-cx, -cy);

    const std::vector<Geom> beforeSolids = canvas_->layerSolids(id);
    const std::vector<Geom> beforeFollows = canvas_->layerFollows(id);
    std::vector<Geom> afterSolids;
    std::vector<Geom> afterFollows;
    for (const Geom& g : beforeSolids)
        afterSolids.push_back(g.scale(factor, factor, {cx, cy}));
    for (const Geom& g : beforeFollows)
        afterFollows.push_back(g.scale(factor, factor, {cx, cy}));
    auto apply = [this, id](const std::vector<Geom>& solids, const std::vector<Geom>& follows) {
        canvas_->setLayerGeometry(id, solids, follows);
        refreshProperties();
    };
    apply(afterSolids, afterFollows);
    undoStack_->push(new LambdaCommand([apply, beforeSolids, beforeFollows] {
                                          apply(beforeSolids, beforeFollows);
                                      },
                                      [apply, afterSolids, afterFollows] {
                                          apply(afterSolids, afterFollows);
                                      },
                                      tr("Calibrar escala")));
    if (verify->isChecked()) {
        std::ostringstream code;
        code << "(Calibration verification)\n";
        code << "G21\nG90\nG00 Z5.000\n";
        code << "G00 X0.000 Y0.000\nG01 Z-0.100 F60.00\n";
        code << "G01 X" << std::fixed << std::setprecision(4) << real->value()
             << " Y0.000 F100.00\n";
        code << "G00 Z5.000\nM02\n";
        addCncJobObject(tr("Calibración"), QString::fromStdString(code.str()));
    }
    statusBar()->showMessage(tr("Escala aplicada: x%1").arg(factor), 4000);
}

/// @brief Abre el film avanzado con margen, marco y punched.
void MainWindow::advancedFilm() {
    const std::vector<Geom> solids = canvas_->visibleSolids();
    if (solids.empty()) {
        QMessageBox::information(this, tr("Film"), tr("No hay capas visibles."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Film avanzado"));
    auto* form = new QFormLayout(&dialog);
    auto* margin = new UnitSpinBox(&dialog);
    margin->setRange(0.0, 100.0);
    margin->setDecimals(2);
    margin->setValue(2.0);
    margin->setSuffix(" mm");
    auto* negative = new QCheckBox(tr("Negativo (fondo sólido)"), &dialog);
    auto* withFrame = new QCheckBox(tr("Incluir marco"), &dialog);
    withFrame->setChecked(true);
    auto* punched = new QCheckBox(tr("Punched (restar taladros)"), &dialog);
    auto* skew = new UnitSpinBox(&dialog);
    skew->setRange(-60.0, 60.0);
    skew->setDecimals(2);
    skew->setSuffix(" °");
    skew->setToolTip(tr("Inclinación (skew) del film."));
    auto* filmType = new QComboBox(&dialog);
    filmType->addItems({tr("Positivo"), tr("Negativo"), tr("Punched (restar taladros)")});
    auto* refCombo = new QComboBox(&dialog);
    refCombo->addItem(tr("Área combinada"), 0);
    for (QTreeWidgetItem* row : objectItems())
        refCombo->addItem(row->text(1), row->data(0, Qt::UserRole).toInt());
    form->addRow(tr("Margen:"), margin);
    form->addRow(QString(), negative);
    form->addRow(QString(), withFrame);
    form->addRow(QString(), punched);
    form->addRow(tr("Tipo de film:"), filmType);
    form->addRow(tr("Skew:"), skew);
    form->addRow(tr("Referencia:"), refCombo);
    connect(filmType, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog,
            [negative, punched](int index) {
                negative->setChecked(index >= 1);
                punched->setChecked(index == 2);
            });
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(box);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    Geom merged = Geom::unionOf(solids);
    if (punched->isChecked()) {
        std::vector<Geom> holes;
        for (QTreeWidgetItem* row : objectItems()) {
            if (!row->data(0, Qt::UserRole + 1).toBool()) continue;
            if (row->text(2).toLower() != "excellon") continue;
            const auto h = canvas_->layerSolids(row->data(0, Qt::UserRole).toInt());
            holes.insert(holes.end(), h.begin(), h.end());
        }
        if (!holes.empty()) merged = merged.difference(Geom::unionOf(holes));
    }
    Geom skewed = merged;
    const BBox b = merged.bounds();
    if (!b.valid) return;
    if (std::fabs(skew->value()) > 1e-9) {
        const double k = std::tan(skew->value() * M_PI / 180.0);
        const Pt center{(b.xmin + b.xmax) * 0.5, (b.ymin + b.ymax) * 0.5};
        skewed = merged.transformXY([center, k](Pt p) {
            return Pt{p.x + k * (p.y - center.y), p.y};
        });
    }
    BBox frameBase = b;
    const int refId = refCombo->currentData().toInt();
    if (refId > 0) {
        const BBox rb = canvas_->layerBounds(refId);
        if (rb.valid) frameBase = rb;
    }
    const BBox frame = frameBase.expanded(margin->value());
    const Geom frameBox = Geom::box(frame.xmin, frame.ymin, frame.xmax, frame.ymax);
    std::vector<Geom> out;
    out.push_back(negative->isChecked() ? frameBox.difference(skewed) : skewed);
    if (withFrame->isChecked() && !negative->isChecked())
        out.push_back(Geom::line({{frame.xmin, frame.ymin}, {frame.xmax, frame.ymin},
                                  {frame.xmax, frame.ymax}, {frame.xmin, frame.ymax},
                                  {frame.xmin, frame.ymin}}));
    const int newId = canvas_->appendGeometry(out, {}, true);
    addObjectRow(tr("Film"), "film", "-", newId);
    canvas_->endGeometry();
    statusBar()->showMessage(tr("Film creado"), 4000);
}

/// @brief Construye la página de calculadoras.
QWidget* MainWindow::buildCalculatorPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    // Units
    auto* unitsGroup = new QGroupBox(tr("Conversor de unidades"), page);
    auto* unitsForm = new QFormLayout(unitsGroup);
    auto* value = new UnitSpinBox(unitsGroup);
    value->setRange(-1e9, 1e9);
    value->setDecimals(4);
    value->setValue(1.0);
    auto* from = new QComboBox(unitsGroup);
    from->addItems({"mm", "mil", "inch", "cm"});
    auto* to = new QComboBox(unitsGroup);
    to->addItems({"mm", "mil", "inch", "cm"});
    to->setCurrentIndex(1);
    auto* unitsResult = new QLabel(tr("—"), unitsGroup);
    auto* unitsButton = new QPushButton(tr("Convertir"), unitsGroup);
    unitsForm->addRow(tr("Valor:"), value);
    unitsForm->addRow(tr("De:"), from);
    unitsForm->addRow(tr("A:"), to);
    unitsForm->addRow(unitsButton);
    unitsForm->addRow(tr("Resultado:"), unitsResult);
    layout->addWidget(unitsGroup);
    auto factor = [](const QString& unit) {
        if (unit == "mm") return 1.0;
        if (unit == "mil") return 0.0254;
        if (unit == "inch") return 25.4;
        return 10.0;  // cm
    };
    connect(unitsButton, &QPushButton::clicked, this, [=] {
        const double mm = value->value() * factor(from->currentText());
        const double out = mm / factor(to->currentText());
        unitsResult->setText(QString::number(out, 'f', 6) + " " + to->currentText());
    });

    // Trace width (IPC-2221)
    auto* traceGroup = new QGroupBox(tr("Ancho de pista (IPC-2221)"), page);
    auto* traceForm = new QFormLayout(traceGroup);
    auto* current = new UnitSpinBox(traceGroup);
    current->setRange(0.1, 200.0);
    current->setValue(1.0);
    current->setSuffix(" A");
    auto* deltaT = new UnitSpinBox(traceGroup);
    deltaT->setRange(1.0, 200.0);
    deltaT->setValue(10.0);
    deltaT->setSuffix(" °C");
    auto* copper = new QComboBox(traceGroup);
    copper->addItems({"0.5 oz", "1 oz", "2 oz"});
    copper->setCurrentIndex(1);
    auto* layer = new QComboBox(traceGroup);
    layer->addItems({tr("Externa"), tr("Interna")});
    auto* traceResult = new QLabel(tr("—"), traceGroup);
    auto* traceButton = new QPushButton(tr("Calcular"), traceGroup);
    traceForm->addRow(tr("Corriente:"), current);
    traceForm->addRow(tr("ΔT admisible:"), deltaT);
    traceForm->addRow(tr("Cobre:"), copper);
    traceForm->addRow(tr("Capa:"), layer);
    traceForm->addRow(traceButton);
    traceForm->addRow(tr("Ancho:"), traceResult);
    layout->addWidget(traceGroup);
    connect(traceButton, &QPushButton::clicked, this, [=] {
        const double k = layer->currentIndex() == 0 ? 0.048 : 0.024;
        const double areaMil2 =
            std::pow(current->value() / (k * std::pow(deltaT->value(), 0.44)), 1.0 / 0.725);
        const double oz[3] = {0.5, 1.0, 2.0};
        const double thicknessMil = oz[copper->currentIndex()] * 1.378;
        const double widthMil = areaMil2 / thicknessMil;
        traceResult->setText(QString("%1 mil  (%2 mm)")
                                 .arg(widthMil, 0, 'f', 2)
                                 .arg(widthMil * 0.0254, 0, 'f', 3));
    });

    // Drill speed
    auto* drillGroup = new QGroupBox(tr("Velocidad de taladrado"), page);
    auto* drillForm = new QFormLayout(drillGroup);
    auto* diameter = new UnitSpinBox(drillGroup);
    diameter->setRange(0.1, 10.0);
    diameter->setDecimals(2);
    diameter->setValue(0.8);
    diameter->setSuffix(" mm");
    auto* speed = new UnitSpinBox(drillGroup);
    speed->setRange(1.0, 300.0);
    speed->setValue(60.0);
    speed->setSuffix(" m/min");
    auto* drillResult = new QLabel(tr("—"), drillGroup);
    auto* drillButton = new QPushButton(tr("Calcular"), drillGroup);
    drillForm->addRow(tr("Diámetro:"), diameter);
    drillForm->addRow(tr("Velocidad de corte:"), speed);
    drillForm->addRow(drillButton);
    drillForm->addRow(tr("RPM:"), drillResult);
    layout->addWidget(drillGroup);
    connect(drillButton, &QPushButton::clicked, this, [=] {
        const double rpm = (1000.0 * speed->value()) / (M_PI * diameter->value());
        drillResult->setText(QString("%1 RPM").arg(rpm, 0, 'f', 0));
    });

    layout->addStretch();
    return page;
}

/// @brief Abre el diálogo de calculadoras.
void MainWindow::showCalculators() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Calculadoras"));
    dialog.resize(420, 650);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(buildCalculatorPage());
    auto* box = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(box);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

/// @brief Abre una pestaña de herramienta.
void MainWindow::openToolTab(const QString& title) { showToolTab(title); }

/// @brief Muestra o crea la pestaña de una herramienta.
void MainWindow::showToolTab(const QString& title) {
    if (!leftTabs_) return;
    const auto it = toolTabs_.find(title);
    if (it == toolTabs_.end()) return;
    QWidget* page = it->second;
    int index = leftTabs_->indexOf(page);
    if (index < 0) {
        index = leftTabs_->addTab(page, shortToolTitle(title));
        leftTabs_->setTabIcon(index, toolIconForTitle(title));
        leftTabs_->setTabToolTip(index, title);
    }
    leftTabs_->setCurrentIndex(index);
}

/// @brief Muestra la pestaña Herramienta del objeto.
void MainWindow::showObjectTool() {
    refreshObjectToolPage();
    if (leftTabs_) leftTabs_->setCurrentIndex(2);
}

/// @brief Reconstruye la página de herramienta del objeto activo.
void MainWindow::refreshObjectToolPage() {
    if (!objectToolLayout_) return;
    while (QLayoutItem* child = objectToolLayout_->takeAt(0)) {
        if (QWidget* widget = child->widget()) delete widget;
        delete child;
    }

    auto* item = objects_ ? objects_->currentItem() : nullptr;
    if (!item) {
        auto* hint = new QLabel(tr("Seleccioná o hacé doble clic en un objeto del proyecto."),
                                objectToolPage_);
        hint->setWordWrap(true);
        objectToolLayout_->addWidget(hint);
        objectToolLayout_->addStretch();
        return;
    }

    const int id = item->data(0, Qt::UserRole).toInt();
    if (id <= 0) {
        auto* hint = new QLabel(tr("Este elemento no tiene una herramienta asociada."),
                                objectToolPage_);
        hint->setWordWrap(true);
        objectToolLayout_->addWidget(hint);
        objectToolLayout_->addStretch();
        return;
    }
    const QString kind = item->text(2).toLower();
    const bool isGerber = kind == "gerber";
    const bool isExcellon = kind == "excellon";

    auto* header = new QWidget(objectToolPage_);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    auto* logo = new QLabel(header);
    const QString iconName = isExcellon ? "drill" : (isGerber ? "folder_gerber" : "tool");
    logo->setPixmap(originalIcon(iconName).pixmap(28, 28));
    headerLayout->addWidget(logo);
    auto* title = new QLabel(isGerber ? tr("Objeto Gerber")
                                      : isExcellon ? tr("Objeto Excellon")
                                                   : tr("Objeto"), header);
    QFont titleFont = title->font();
    titleFont.setPointSize(18);
    titleFont.setBold(true);
    title->setFont(titleFont);
    headerLayout->addWidget(title);
    headerLayout->addStretch();
    auto* level = new QLabel(tr("BASIC"), header);
    level->setProperty("role", "badge");
    headerLayout->addWidget(level);
    objectToolLayout_->addWidget(header);

    auto* options = new QWidget(objectToolPage_);
    auto* optionsLayout = new QHBoxLayout(options);
    optionsLayout->setContentsMargins(0, 0, 0, 0);
    optionsLayout->addWidget(new QLabel(tr("Opciones de parcela:"), options));
    auto* solid = new QCheckBox(tr("Sólido"), options);
    solid->setChecked(canvas_->layerFilled(id));
    auto* multicolor = new QCheckBox(tr("Multicolor"), options);
    multicolor->setChecked(canvas_->layerMulticolor(id));
    optionsLayout->addWidget(solid);
    optionsLayout->addWidget(multicolor);
    optionsLayout->addStretch();
    objectToolLayout_->addWidget(options);
    connect(solid, &QCheckBox::toggled, this, [this, id](bool on) {
        canvas_->setLayerFilled(id, on);
    });
    connect(multicolor, &QCheckBox::toggled, this, [this, id](bool on) {
        canvas_->setLayerMulticolor(id, on);
    });

    auto* nameRow = new QWidget(objectToolPage_);
    auto* nameLayout = new QHBoxLayout(nameRow);
    nameLayout->setContentsMargins(0, 0, 0, 0);
    nameLayout->addWidget(new QLabel(tr("Nombre:"), nameRow));
    auto* nameEdit = new QLineEdit(item->text(1), nameRow);
    nameLayout->addWidget(nameEdit, 1);
    objectToolLayout_->addWidget(nameRow);
    connect(nameEdit, &QLineEdit::editingFinished, this, [this, item, nameEdit] {
        const QString name = nameEdit->text().trimmed();
        if (name.isEmpty() || name == item->text(1)) return;
        item->setText(1, name);
        refreshProperties();
    });

    auto* graphRow = new QWidget(objectToolPage_);
    auto* graphLayout = new QHBoxLayout(graphRow);
    graphLayout->setContentsMargins(0, 0, 0, 0);
    graphLayout->addWidget(new QLabel(tr("Gráfico:"), graphRow));
    auto* graph = new QCheckBox(graphRow);
    graph->setChecked(item->data(0, Qt::UserRole + 1).toBool());
    graphLayout->addWidget(graph);
    graphLayout->addStretch();
    objectToolLayout_->addWidget(graphRow);
    connect(graph, &QCheckBox::toggled, this, [this, item](bool on) {
        updatingTree_ = true;
        setObjectVisible(item, on);
        updatingTree_ = false;
    });

    auto makeButton = [this](const QString& icon, const QString& text, QWidget* parent) {
        auto* button = new QPushButton(originalIcon(icon), text, parent);
        button->setMinimumHeight(32);
        button->setStyleSheet("QPushButton { text-align:center; padding:4px 8px; }");
        objectToolLayout_->addWidget(button);
        return button;
    };

    if (isGerber) {
        auto* editor = makeButton("edit", tr("Gerber Editor"), objectToolPage_);
        connect(editor, &QPushButton::clicked, this, &MainWindow::gerberEditor);
        auto* props = makeButton("properties", tr("PROPIEDADES"), objectToolPage_);
        connect(props, &QPushButton::clicked, this, [this] {
            if (leftTabs_) leftTabs_->setCurrentIndex(1);
        });
    } else if (isExcellon) {
        auto* editor = makeButton("drill", tr("Excellon Editor"), objectToolPage_);
        connect(editor, &QPushButton::clicked, this, &MainWindow::excellonEditor);
        auto* props = makeButton("properties", tr("PROPIEDADES"), objectToolPage_);
        connect(props, &QPushButton::clicked, this, [this] {
            if (leftTabs_) leftTabs_->setCurrentIndex(1);
        });

        auto* tableLabel = new QLabel(tr("Tabla de herramientas"), objectToolPage_);
        QFont tableFont = tableLabel->font();
        tableFont.setBold(true);
        tableLabel->setFont(tableFont);
        objectToolLayout_->addWidget(tableLabel);

        const auto it = excellonLayers_.find(id);
        if (it != excellonLayers_.end()) {
            auto* table = new QTableWidget(0, 4, objectToolPage_);
            table->setHorizontalHeaderLabels(
                {tr("#"), tr("Diámetro"), tr("Taladros"), tr("Muesca")});
            table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
            table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
            table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
            table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
            table->verticalHeader()->setVisible(false);
            table->setEditTriggers(QAbstractItemView::NoEditTriggers);
            std::size_t totalDrills = 0;
            std::size_t totalSlots = 0;
            for (const auto& [number, tool] : it->second.tools) {
                const int row = table->rowCount();
                table->insertRow(row);
                table->setItem(row, 0, new QTableWidgetItem(QString::number(number)));
                table->setItem(row, 1, new QTableWidgetItem(
                    QString::number(tool.diameter, 'f', 4)));
                table->setItem(row, 2, new QTableWidgetItem(
                    QString::number(tool.drills.size())));
                table->setItem(row, 3, new QTableWidgetItem(
                    QString::number(tool.slotSegments.size())));
                totalDrills += tool.drills.size();
                totalSlots += tool.slotSegments.size();
            }
            const int drillsRow = table->rowCount();
            table->insertRow(drillsRow);
            auto* drillsLabel = new QTableWidgetItem(tr("Taladros totales"));
            drillsLabel->setForeground(QColor(150, 40, 190));
            drillsLabel->setFont(tableFont);
            table->setItem(drillsRow, 0, drillsLabel);
            table->setSpan(drillsRow, 0, 1, 2);
            table->setItem(drillsRow, 2, new QTableWidgetItem(QString::number(totalDrills)));
            const int slotsRow = table->rowCount();
            table->insertRow(slotsRow);
            auto* slotsLabel = new QTableWidgetItem(tr("Ranuras totales"));
            slotsLabel->setForeground(QColor(40, 90, 200));
            slotsLabel->setFont(tableFont);
            table->setItem(slotsRow, 0, slotsLabel);
            table->setSpan(slotsRow, 0, 1, 2);
            table->setItem(slotsRow, 2, new QTableWidgetItem(QString()));
            table->setItem(slotsRow, 3, new QTableWidgetItem(QString::number(totalSlots)));
            table->setMinimumHeight(180);
            objectToolLayout_->addWidget(table);
        }
    }

    auto* toolsLabel = new QLabel(tr("HERRAMIENTAS"), objectToolPage_);
    QFont toolsFont = toolsLabel->font();
    toolsFont.setBold(true);
    toolsLabel->setFont(toolsFont);
    objectToolLayout_->addWidget(toolsLabel);

    if (isGerber) {
        auto* iso = makeButton("isolate", tr("Enrutamiento de aislamiento"), objectToolPage_);
        connect(iso, &QPushButton::clicked, this,
                [this] { showToolTab(tr("Herramienta de aislamiento")); });
        auto* ncc = makeButton("ncc", tr("Herramienta NCC"), objectToolPage_);
        connect(ncc, &QPushButton::clicked, this,
                [this] { showToolTab(tr("Herramienta NCC")); });
        auto* cut = makeButton("cutout", tr("Herramienta de Corte"), objectToolPage_);
        connect(cut, &QPushButton::clicked, this,
                [this] { showToolTab(tr("Herramienta de corte")); });
    } else if (isExcellon) {
        auto* drill = makeButton("drill", tr("Herramienta de Perforación"), objectToolPage_);
        connect(drill, &QPushButton::clicked, this,
                [this] { showToolTab(tr("Herramienta de perforación")); });
        auto* mill = makeButton("milling", tr("Herramienta de fresado"), objectToolPage_);
        mill->setEnabled(false);
        mill->setToolTip(tr("No aplicable a un objeto Excellon."));
    } else {
        auto* iso = makeButton("geometry", tr("Herramientas de mecanizado"), objectToolPage_);
        connect(iso, &QPushButton::clicked, this, [this] {
            showToolTab(tr("Herramienta de aislamiento"));
        });
    }

    auto* utilities = new QPushButton(originalIcon("pref"), tr("UTILIDADES"), objectToolPage_);
    utilities->setMinimumHeight(32);
    utilities->setStyleSheet("QPushButton { text-align:center; padding:4px 8px; }");
    auto* utilitiesMenu = new QMenu(utilities);
    auto addUtility = [this, utilitiesMenu](const QString& text, auto slot) {
        QAction* action = utilitiesMenu->addAction(text);
        connect(action, &QAction::triggered, this, slot);
    };
    addUtility(tr("Panelizar..."), &MainWindow::panelize);
    addUtility(tr("Fiduciales..."), &MainWindow::addFiducials);
    addUtility(tr("Copper Thieving"), [this] { showToolTab(tr("Copper Thieving")); });
    addUtility(tr("Film"), &MainWindow::createFilm);
    addUtility(tr("Punch Gerber..."), &MainWindow::punchGerberAction);
    addUtility(tr("Extraer taladros..."), &MainWindow::extractDrillsAction);
    addUtility(tr("Comprobar reglas..."), &MainWindow::rulesCheck);
    addUtility(tr("Calculadoras..."), &MainWindow::showCalculators);
    utilities->setMenu(utilitiesMenu);
    objectToolLayout_->addWidget(utilities);

    auto* transformLabel = new QLabel(tr("Transformaciones"), objectToolPage_);
    QFont transformFont = transformLabel->font();
    transformFont.setBold(true);
    transformLabel->setFont(transformFont);
    objectToolLayout_->addWidget(transformLabel);

    auto applyGeometry = [this, id](const std::vector<Geom>& solids,
                                    const std::vector<Geom>& follows) {
        canvas_->setLayerGeometry(id, solids, follows);
        refreshProperties();
    };

    auto* scaleRow = new QWidget(objectToolPage_);
    auto* scaleLayout = new QHBoxLayout(scaleRow);
    scaleLayout->setContentsMargins(0, 0, 0, 0);
    auto* scaleEdit = new QLineEdit("1.0000", scaleRow);
    auto* scaleButton = new QPushButton(tr("Escala"), scaleRow);
    scaleLayout->addWidget(scaleEdit, 1);
    scaleLayout->addWidget(scaleButton);
    objectToolLayout_->addWidget(scaleRow);
    connect(scaleButton, &QPushButton::clicked, this,
            [this, id, scaleEdit, applyGeometry] {
                bool ok = false;
                const double factor = scaleEdit->text().toDouble(&ok);
                if (!ok || factor <= 0.0) return;
                const std::vector<Geom> beforeSolids = canvas_->layerSolids(id);
                const std::vector<Geom> beforeFollows = canvas_->layerFollows(id);
                const BBox b = canvas_->layerBounds(id);
                const Pt center{(b.xmin + b.xmax) * 0.5, (b.ymin + b.ymax) * 0.5};
                std::vector<Geom> solids;
                std::vector<Geom> follows;
                for (const Geom& g : beforeSolids) solids.push_back(g.scale(factor, factor, center));
                for (const Geom& g : beforeFollows) follows.push_back(g.scale(factor, factor, center));
                applyGeometry(solids, follows);
                undoStack_->push(new LambdaCommand(
                    [applyGeometry, beforeSolids, beforeFollows] {
                        applyGeometry(beforeSolids, beforeFollows);
                    },
                    [applyGeometry, solids, follows] { applyGeometry(solids, follows); },
                    tr("Escalar objeto")));
            });

    auto* offsetRow = new QWidget(objectToolPage_);
    auto* offsetLayout = new QHBoxLayout(offsetRow);
    offsetLayout->setContentsMargins(0, 0, 0, 0);
    auto* offsetEdit = new QLineEdit("(0.0000, 0.0000)", offsetRow);
    auto* offsetButton = new QPushButton(tr("Compensar"), offsetRow);
    offsetLayout->addWidget(offsetEdit, 1);
    offsetLayout->addWidget(offsetButton);
    objectToolLayout_->addWidget(offsetRow);
    connect(offsetButton, &QPushButton::clicked, this,
            [this, id, offsetEdit, applyGeometry] {
                const std::vector<Geom> beforeSolids = canvas_->layerSolids(id);
                const std::vector<Geom> beforeFollows = canvas_->layerFollows(id);
                QString text = offsetEdit->text().trimmed();
                text.remove('(');
                text.remove(')');
                const QStringList parts = text.split(',');
                if (parts.size() != 2) return;
                bool okX = false;
                bool okY = false;
                const double dx = parts[0].trimmed().toDouble(&okX);
                const double dy = parts[1].trimmed().toDouble(&okY);
                if (!okX || !okY) return;
                std::vector<Geom> solids;
                std::vector<Geom> follows;
                for (const Geom& g : beforeSolids) solids.push_back(g.translate(dx, dy));
                for (const Geom& g : beforeFollows) follows.push_back(g.translate(dx, dy));
                applyGeometry(solids, follows);
                undoStack_->push(new LambdaCommand(
                    [applyGeometry, beforeSolids, beforeFollows] {
                        applyGeometry(beforeSolids, beforeFollows);
                    },
                    [applyGeometry, solids, follows] { applyGeometry(solids, follows); },
                    tr("Compensar objeto")));
                offsetEdit->setText("(0.0000, 0.0000)");
            });

    auto* transformButton = makeButton("transform", tr("Transformaciones"), objectToolPage_);
    connect(transformButton, &QPushButton::clicked, this,
            &MainWindow::transformSelectedObject);

    objectToolLayout_->addStretch();
}

/// @brief Abre el diálogo de preferencias.
void MainWindow::showPreferences() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Preferencias"));
    dialog.resize(560, 600);
    auto* outer = new QVBoxLayout(&dialog);
    auto* tabs = new QTabWidget(&dialog);
    outer->addWidget(tabs);

    // ---------------------------------------------------------------- General
    auto* generalPage = new QWidget(&dialog);
    auto* form = new QFormLayout(generalPage);

    auto* decimals = new QSpinBox(&dialog);
    decimals->setRange(0, 6);
    decimals->setValue(decimals_);
    applyTip(decimals, tr("<b>Decimales</b><br>Cantidad de decimales mostrados en coordenadas."));
    form->addRow(tr("Decimales:"), decimals);

    auto* steps = new QSpinBox(&dialog);
    steps->setRange(8, 512);
    steps->setValue(stepsPerCircle_);
    applyTip(steps, tr("<b>Pasos por círculo</b><br>Segmentos usados para aproximar círculos y "
                       "arcos en Gerber/Excellon. Más pasos = más precisión y más lento."));
    form->addRow(tr("Pasos por círculo:"), steps);

    auto* units = new QComboBox(&dialog);
    units->addItems({tr("Milímetros (mm)"), tr("Pulgadas (in)")});
    units->setCurrentIndex(defaultUnits_ == fc::Units::Mm ? 0 : 1);
    applyTip(units, tr("<b>Unidades de trabajo</b><br>Se aplican a los archivos que se carguen "
                       "después de aceptar."));
    form->addRow(tr("Unidades:"), units);

    auto* gridStep = new UnitSpinBox(&dialog);
    gridStep->setRange(0.01, 100.0);
    gridStep->setDecimals(3);
    gridStep->setValue(canvas_->gridStep());
    gridStep->setSuffix(" mm");
    applyTip(gridStep, tr("<b>Paso de rejilla</b><br>Separación entre líneas de la rejilla."));
    form->addRow(tr("Paso de rejilla:"), gridStep);

    auto* showGrid = new QCheckBox(tr("Mostrar rejilla"), &dialog);
    showGrid->setChecked(canvas_->gridVisible());
    form->addRow(QString(), showGrid);

    tabs->addTab(generalPage, originalIcon("pref"), tr("General"));

    // ----------------------------------------------------------------- Gerber
    auto* gerberPage = new QWidget(&dialog);
    auto* gerberForm = new QFormLayout(gerberPage);
    auto* simplify = new QCheckBox(tr("Simplificar geometría al importar"), &dialog);
    simplify->setChecked(gerberSimplify_);
    auto* tolerance = new UnitSpinBox(&dialog);
    tolerance->setRange(0.0001, 1.0);
    tolerance->setDecimals(4);
    tolerance->setValue(gerberTolerance_);
    tolerance->setSuffix(" mm");
    tolerance->setEnabled(gerberSimplify_);
    connect(simplify, &QCheckBox::toggled, tolerance, &QWidget::setEnabled);
    applyTip(tolerance, tr("<b>Tolerancia</b><br>Mayor valor = menos vértices y más rápido."));
    gerberForm->addRow(QString(), simplify);
    gerberForm->addRow(tr("Tolerancia:"), tolerance);
    tabs->addTab(gerberPage, originalIcon("folder_gerber"), tr("Gerber"));

    // ----------------------------------------------------------------- Geometry
    auto* geometryPage = new QWidget(&dialog);
    auto* geometryForm = new QFormLayout(geometryPage);
    auto* geomSimplify = new QCheckBox(tr("Simplificar geometría al importar SVG/DXF"), &dialog);
    geomSimplify->setChecked(geometrySimplify_);
    auto* geomTolerance = new UnitSpinBox(&dialog);
    geomTolerance->setRange(0.0001, 1.0);
    geomTolerance->setDecimals(4);
    geomTolerance->setValue(geometryTolerance_);
    geomTolerance->setSuffix(" mm");
    geomTolerance->setEnabled(geometrySimplify_);
    connect(geomSimplify, &QCheckBox::toggled, geomTolerance, &QWidget::setEnabled);
    geometryForm->addRow(QString(), geomSimplify);
    geometryForm->addRow(tr("Tolerancia:"), geomTolerance);
    tabs->addTab(geometryPage, originalIcon("geometry"), tr("Geometry"));

    // ----------------------------------------------------------------- Excellon
    auto* excellonPage = new QWidget(&dialog);
    auto* excellonForm = new QFormLayout(excellonPage);
    auto* extractDia = new UnitSpinBox(&dialog);
    extractDia->setRange(0.05, 20.0);
    extractDia->setDecimals(2);
    extractDia->setValue(excellonExtractMaxDia_);
    extractDia->setSuffix(" mm");
    excellonForm->addRow(tr("Ø máximo para extraer taladros:"), extractDia);
    tabs->addTab(excellonPage, originalIcon("drill"), tr("Excellon"));

    // ----------------------------------------------------------------- CNCJob
    auto* cncPage = new QWidget(&dialog);
    auto* cncForm = new QFormLayout(cncPage);
    auto* cncFeedXY = new UnitSpinBox(&dialog);
    cncFeedXY->setRange(1.0, 20000.0);
    cncFeedXY->setDecimals(1);
    cncFeedXY->setValue(cncDefaultFeedXY_);
    cncFeedXY->setSuffix(" mm/min");
    auto* cncFeedZ = new UnitSpinBox(&dialog);
    cncFeedZ->setRange(0.1, 5000.0);
    cncFeedZ->setDecimals(1);
    cncFeedZ->setValue(cncDefaultFeedZ_);
    cncFeedZ->setSuffix(" mm/min");
    auto* cncRPM = new UnitSpinBox(&dialog);
    cncRPM->setRange(0.0, 100000.0);
    cncRPM->setDecimals(0);
    cncRPM->setValue(cncDefaultRPM_);
    cncRPM->setSuffix(" RPM");
    auto* cncZCut = new UnitSpinBox(&dialog);
    cncZCut->setRange(-50.0, 0.0);
    cncZCut->setDecimals(3);
    cncZCut->setValue(cncDefaultZCut_);
    cncZCut->setSuffix(" mm");
    auto* applyCnc = new QCheckBox(tr("Aplicar a las tablas actuales"), &dialog);
    cncForm->addRow(tr("Avance XY:"), cncFeedXY);
    cncForm->addRow(tr("Avance Z:"), cncFeedZ);
    cncForm->addRow(tr("Husillo:"), cncRPM);
    cncForm->addRow(tr("Z de corte:"), cncZCut);
    cncForm->addRow(QString(), applyCnc);
    tabs->addTab(cncPage, originalIcon("cnc"), tr("CNCJob"));

    // ----------------------------------------------------------------- Colors
    auto* colorsPage = new QWidget(&dialog);
    auto* colorsForm = new QFormLayout(colorsPage);
    auto colorButton = [this](const QColor& color) {
        auto* button = new QPushButton(this);
        button->setStyleSheet(QString("background:%1; min-width:80px; min-height:20px;")
                                  .arg(color.name()));
        return button;
    };
    auto* bgButton = colorButton(background_);
    auto* copperButton = colorButton(copperColor_);
    auto* followButton = colorButton(followColor_);
    auto* toolpathButton = colorButton(toolpathColor_);
    applyTip(bgButton, tr("<b>Color de fondo</b><br>Clic para elegir el color del visor."));
    applyTip(copperButton, tr("<b>Color del cobre</b><br>Clic para elegir el relleno del cobre."));
    applyTip(followButton, tr("<b>Color de recorridos</b><br>Clic para elegir las líneas de "
                              "recorrido del Gerber."));
    applyTip(toolpathButton, tr("<b>Color de trayectorias</b><br>Clic para elegir el color de las "
                                "herramientas previsualizadas."));
    colorsForm->addRow(tr("Fondo:"), bgButton);
    colorsForm->addRow(tr("Cobre:"), copperButton);
    colorsForm->addRow(tr("Recorridos:"), followButton);
    colorsForm->addRow(tr("Trayectorias:"), toolpathButton);
    connect(bgButton, &QPushButton::clicked, this, [this, bgButton] {
        const QColor c = QColorDialog::getColor(background_, this, tr("Color de fondo"));
        if (c.isValid()) {
            background_ = c;
            bgButton->setStyleSheet(QString("background:%1; min-width:80px;").arg(c.name()));
        }
    });
    connect(copperButton, &QPushButton::clicked, this, [this, copperButton] {
        const QColor c = QColorDialog::getColor(copperColor_, this, tr("Color del cobre"));
        if (c.isValid()) {
            copperColor_ = c;
            copperButton->setStyleSheet(QString("background:%1; min-width:80px;").arg(c.name()));
        }
    });
    connect(followButton, &QPushButton::clicked, this, [this, followButton] {
        const QColor c = QColorDialog::getColor(followColor_, this, tr("Color de recorridos"));
        if (c.isValid()) {
            followColor_ = c;
            followButton->setStyleSheet(QString("background:%1; min-width:80px;").arg(c.name()));
        }
    });
    connect(toolpathButton, &QPushButton::clicked, this, [this, toolpathButton] {
        const QColor c = QColorDialog::getColor(toolpathColor_, this, tr("Color de trayectorias"));
        if (c.isValid()) {
            toolpathColor_ = c;
            toolpathButton->setStyleSheet(QString("background:%1; min-width:80px;").arg(c.name()));
        }
    });
    auto* resetColors = new QPushButton(tr("Restablecer colores predeterminados"), &dialog);
    applyTip(resetColors, tr("Vuelve al esquema oscuro original de FlatCAM C++."));
    colorsForm->addRow(resetColors);
    connect(resetColors, &QPushButton::clicked, this,
            [this, bgButton, copperButton, followButton, toolpathButton] {
                background_ = QColor(16, 20, 24);
                copperColor_ = QColor(217, 164, 65);
                followColor_ = QColor(111, 168, 220);
                toolpathColor_ = QColor(102, 214, 132);
                bgButton->setStyleSheet(QString("background:%1; min-width:80px;").arg(background_.name()));
                copperButton->setStyleSheet(QString("background:%1; min-width:80px;").arg(copperColor_.name()));
                followButton->setStyleSheet(QString("background:%1; min-width:80px;").arg(followColor_.name()));
                toolpathButton->setStyleSheet(QString("background:%1; min-width:80px;").arg(toolpathColor_.name()));
            });
    tabs->addTab(colorsPage, originalIcon("image"), tr("Colores"));

    // --------------------------------------------------------------- Files
    auto* filesPage = new QWidget(&dialog);
    auto* filesForm = new QFormLayout(filesPage);
    auto* recentLimit = new QSpinBox(&dialog);
    recentLimit->setRange(1, 50);
    recentLimit->setValue(12);
    applyTip(recentLimit, tr("<b>Archivos recientes</b><br>Cantidad máxima en el menú Archivo."));
    filesForm->addRow(tr("Máx. recientes:"), recentLimit);
    auto* clearRecent = new QPushButton(tr("Borrar lista de recientes"), &dialog);
    filesForm->addRow(clearRecent);
    connect(clearRecent, &QPushButton::clicked, this, [this] {
        QSettings().remove("recent/files");
        rebuildRecentMenu();
    });
    tabs->addTab(filesPage, originalIcon("folder"), tr("Archivos"));

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    outer->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    decimals_ = decimals->value();
    stepsPerCircle_ = steps->value();
    defaultUnits_ = units->currentIndex() == 0 ? fc::Units::Mm : fc::Units::Inch;
    gerberSimplify_ = simplify->isChecked();
    gerberTolerance_ = tolerance->value();
    geometrySimplify_ = geomSimplify->isChecked();
    geometryTolerance_ = geomTolerance->value();
    excellonExtractMaxDia_ = extractDia->value();
    cncDefaultFeedXY_ = cncFeedXY->value();
    cncDefaultFeedZ_ = cncFeedZ->value();
    cncDefaultRPM_ = cncRPM->value();
    cncDefaultZCut_ = cncZCut->value();
    if (applyCnc->isChecked()) {
        auto applyToTable = [this](QTableWidget* table) {
            if (!table) return;
            for (int row = 0; row < table->rowCount(); ++row) {
                auto set = [&](int col, const QString& text) {
                    if (auto* item = table->item(row, col)) item->setText(text);
                    else table->setItem(row, col, new QTableWidgetItem(text));
                };
                set(2, QString::number(cncDefaultFeedXY_, 'f', 1));
                set(3, QString::number(cncDefaultFeedZ_, 'f', 1));
                set(4, QString::number(cncDefaultRPM_, 'f', 0));
                set(6, lengthText(cncDefaultZCut_, 3));
            }
        };
        applyToTable(isoTable_);
        applyToTable(nccTable_);
        applyToTable(cutTable_);
        applyToTable(paintTable_);
        applyToTable(drillTable_);
    }
    canvas_->setGridStep(gridStep->value());
    canvas_->setGridVisible(showGrid->isChecked());
    canvas_->setColors(background_, copperColor_, followColor_, toolpathColor_);
    QSettings settings;
    settings.setValue("preferences/recentLimit", recentLimit->value());
    savePreferences();
    statusBar()->showMessage(tr("Preferencias aplicadas y guardadas"), 5000);
}

/// @brief Abre el editor gráfico de Excellon.
void MainWindow::excellonEditor() {
    auto* item = objects_->currentItem();
    if (!item) {
        QMessageBox::information(this, tr("Editor Excellon"), tr("Seleccione un objeto."));
        return;
    }
    const int id = item->data(0, Qt::UserRole).toInt();
    auto it = excellonLayers_.find(id);
    if (it == excellonLayers_.end()) {
        QMessageBox::information(this, tr("Editor Excellon"),
                                 tr("El objeto seleccionado no es un Excellon editable."));
        return;
    }
    ExcellonResult result = it->second;

    QDialog dialog(this);
    dialog.resize(950, 700);
    dialog.setWindowTitle(tr("Editor Excellon — %1").arg(item->text(0)));
    auto* layout = new QVBoxLayout(&dialog);

    auto* toolsTitle = new QLabel(tr("Herramientas (diámetro editable)"), &dialog);
    layout->addWidget(toolsTitle);
    auto* tools = new QTableWidget(0, 3, &dialog);
    tools->setHorizontalHeaderLabels(
        {tr("#"), tr("Ø ") + (displayInch_ ? QStringLiteral("in") : QStringLiteral("mm")), tr("Tipo")});
    tools->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(tools);
    auto* toolButtons = new QHBoxLayout();
    auto* addTool = new QPushButton(tr("Añadir herramienta"), &dialog);
    auto* removeTool = new QPushButton(tr("Eliminar herramienta"), &dialog);
    auto* undoEdit = new QPushButton(tr("Deshacer"), &dialog);
    auto* redoEdit = new QPushButton(tr("Rehacer"), &dialog);
    toolButtons->addWidget(addTool);
    toolButtons->addWidget(removeTool);
    toolButtons->addWidget(undoEdit);
    toolButtons->addWidget(redoEdit);
    toolButtons->addStretch();
    layout->addLayout(toolButtons);

    auto* holesTitle = new QLabel(tr("Taladros y ranuras"), &dialog);
    layout->addWidget(holesTitle);
    auto* holes = new QTableWidget(0, 7, &dialog);
    holes->setHorizontalHeaderLabels(
        {tr("Herramienta"), tr("Tipo"), tr("X"), tr("Y"), tr("X2"), tr("Y2"), tr("")});
    holes->horizontalHeader()->setStretchLastSection(true);
    holes->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(holes);
    auto* holeButtons = new QHBoxLayout();
    auto* copyRows = new QPushButton(tr("Copiar filas"), &dialog);
    auto* pasteRows = new QPushButton(tr("Pegar filas"), &dialog);
    auto* removeRows = new QPushButton(tr("Eliminar seleccionadas"), &dialog);
    holeButtons->addWidget(copyRows);
    holeButtons->addWidget(pasteRows);
    holeButtons->addWidget(removeRows);
    holeButtons->addStretch();
    layout->addLayout(holeButtons);

    auto* addBox = new QGroupBox(tr("Agregar"), &dialog);
    auto* addForm = new QHBoxLayout(addBox);
    auto* toolCombo = new QComboBox(addBox);
    auto* typeCombo = new QComboBox(addBox);
    typeCombo->addItems({tr("Taladro"), tr("Ranura")});
    auto* ax = new UnitSpinBox(addBox);
    ax->setRange(-10000, 10000);
    ax->setDecimals(3);
    ax->setSuffix(" mm");
    auto* ay = new UnitSpinBox(addBox);
    ay->setRange(-10000, 10000);
    ay->setDecimals(3);
    ay->setSuffix(" mm");
    auto* bx = new UnitSpinBox(addBox);
    bx->setRange(-10000, 10000);
    bx->setDecimals(3);
    bx->setSuffix(" mm");
    auto* by = new UnitSpinBox(addBox);
    by->setRange(-10000, 10000);
    by->setDecimals(3);
    by->setSuffix(" mm");
    auto* add = new QPushButton(tr("Agregar"), addBox);
    auto* remove = new QPushButton(tr("Eliminar"), addBox);
    addForm->addWidget(new QLabel(tr("T"), addBox));
    addForm->addWidget(toolCombo);
    addForm->addWidget(typeCombo);
    addForm->addWidget(new QLabel(tr("X"), addBox));
    addForm->addWidget(ax);
    addForm->addWidget(new QLabel(tr("Y"), addBox));
    addForm->addWidget(ay);
    addForm->addWidget(new QLabel(tr("X2"), addBox));
    addForm->addWidget(bx);
    addForm->addWidget(new QLabel(tr("Y2"), addBox));
    addForm->addWidget(by);
    addForm->addWidget(add);
    addForm->addWidget(remove);
    layout->addWidget(addBox);

    auto* editBox = new QGroupBox(tr("Editar agujero seleccionado"), &dialog);
    auto* editForm = new QHBoxLayout(editBox);
    auto* edx = new UnitSpinBox(editBox);
    edx->setRange(-1000, 1000);
    edx->setDecimals(3);
    edx->setSuffix(" mm");
    auto* edy = new UnitSpinBox(editBox);
    edy->setRange(-1000, 1000);
    edy->setDecimals(3);
    edy->setSuffix(" mm");
    auto* moveHole = new QPushButton(tr("Mover"), editBox);
    auto* duplicateHole = new QPushButton(tr("Duplicar"), editBox);
    auto* allDx = new UnitSpinBox(editBox);
    allDx->setRange(-1000, 1000);
    allDx->setDecimals(3);
    allDx->setSuffix(" mm");
    auto* allDy = new UnitSpinBox(editBox);
    allDy->setRange(-1000, 1000);
    allDy->setDecimals(3);
    allDy->setSuffix(" mm");
    auto* shiftAll = new QPushButton(tr("Desplazar todo"), editBox);
    editForm->addWidget(new QLabel(tr("ΔX"), editBox));
    editForm->addWidget(edx);
    editForm->addWidget(new QLabel(tr("ΔY"), editBox));
    editForm->addWidget(edy);
    editForm->addWidget(moveHole);
    editForm->addWidget(duplicateHole);
    editForm->addWidget(new QLabel(tr("Todo ΔX"), editBox));
    editForm->addWidget(allDx);
    editForm->addWidget(new QLabel(tr("ΔY"), editBox));
    editForm->addWidget(allDy);
    editForm->addWidget(shiftAll);
    layout->addWidget(editBox);

    auto* bottom = new QHBoxLayout();
    auto* save = new QPushButton(tr("Guardar como Excellon..."), &dialog);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    bottom->addWidget(save);
    bottom->addWidget(box);
    layout->addLayout(bottom);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto refreshTools = [&] {
        tools->setRowCount(0);
        toolCombo->clear();
        for (const auto& [number, tool] : result.tools) {
            const int row = tools->rowCount();
            tools->insertRow(row);
            auto* n = new QTableWidgetItem(QString::number(number));
            n->setFlags(n->flags() & ~Qt::ItemIsEditable);
            tools->setItem(row, 0, n);
            tools->setItem(row, 1, new QTableWidgetItem(lengthText(tool.diameter, 3)));
            QString type = tr("Taladro");
            if (!tool.drills.empty() && !tool.slotSegments.empty()) type = tr("Mixta");
            else if (tool.drills.empty() && !tool.slotSegments.empty()) type = tr("Ranura");
            auto* typeItem = new QTableWidgetItem(type);
            typeItem->setFlags(typeItem->flags() & ~Qt::ItemIsEditable);
            tools->setItem(row, 2, typeItem);
            toolCombo->addItem(QString("T%1").arg(number), number);
        }
    };
    auto refreshHoles = [&] {
        holes->setRowCount(0);
        for (const auto& [number, tool] : result.tools) {
            for (const Pt& hole : tool.drills) {
                const int row = holes->rowCount();
                holes->insertRow(row);
                holes->setItem(row, 0, new QTableWidgetItem(QString::number(number)));
                holes->setItem(row, 1, new QTableWidgetItem(tr("Taladro")));
                holes->setItem(row, 2, new QTableWidgetItem(lengthText(hole.x, 3)));
                holes->setItem(row, 3, new QTableWidgetItem(lengthText(hole.y, 3)));
                holes->setItem(row, 4, new QTableWidgetItem(""));
                holes->setItem(row, 5, new QTableWidgetItem(""));
            }
            for (const auto& [a, b] : tool.slotSegments) {
                const int row = holes->rowCount();
                holes->insertRow(row);
                holes->setItem(row, 0, new QTableWidgetItem(QString::number(number)));
                holes->setItem(row, 1, new QTableWidgetItem(tr("Ranura")));
                holes->setItem(row, 2, new QTableWidgetItem(lengthText(a.x, 3)));
                holes->setItem(row, 3, new QTableWidgetItem(lengthText(a.y, 3)));
                holes->setItem(row, 4, new QTableWidgetItem(lengthText(b.x, 3)));
                holes->setItem(row, 5, new QTableWidgetItem(lengthText(b.y, 3)));
            }
        }
    };
    refreshTools();
    refreshHoles();

    struct Snapshot {
        ExcellonResult result;
        std::vector<std::vector<QString>> rows;
    };
    std::vector<Snapshot> history;
    int historyPos = -1;
    auto capture = [&] {
        Snapshot snapshot;
        snapshot.result = result;
        for (int row = 0; row < holes->rowCount(); ++row) {
            std::vector<QString> values;
            for (int c = 0; c < 6; ++c)
                values.push_back(holes->item(row, c) ? holes->item(row, c)->text() : QString());
            snapshot.rows.push_back(std::move(values));
        }
        history.resize(static_cast<std::size_t>(historyPos + 1));
        history.push_back(std::move(snapshot));
        historyPos = static_cast<int>(history.size()) - 1;
    };
    auto restore = [&](int pos) {
        if (pos < 0 || pos >= static_cast<int>(history.size())) return;
        historyPos = pos;
        result = history[static_cast<std::size_t>(pos)].result;
        refreshTools();
        holes->setRowCount(0);
        for (const auto& values : history[static_cast<std::size_t>(pos)].rows) {
            const int row = holes->rowCount();
            holes->insertRow(row);
            for (int c = 0; c < 6 && c < static_cast<int>(values.size()); ++c)
                holes->setItem(row, c, new QTableWidgetItem(values[static_cast<std::size_t>(c)]));
        }
    };
    capture();
    connect(undoEdit, &QPushButton::clicked, this, [&] {
        if (historyPos > 0) restore(historyPos - 1);
    });
    connect(redoEdit, &QPushButton::clicked, this, [&] {
        if (historyPos + 1 < static_cast<int>(history.size())) restore(historyPos + 1);
    });

    connect(addTool, &QPushButton::clicked, this, [&] {
        capture();
        int next = 1;
        if (!result.tools.empty()) next = result.tools.rbegin()->first + 1;
        bool ok = false;
        const QString unit = displayInch_ ? "in" : "mm";
        const double dia = lengthToMm(QInputDialog::getDouble(
            &dialog, tr("Nueva herramienta"), tr("Diámetro (%1):").arg(unit),
            displayLength(1.0), displayLength(0.01), displayLength(20.0),
            displayInch_ ? 6 : 3, &ok));
        if (!ok) return;
        ExcellonTool tool;
        tool.number = next;
        tool.diameter = dia;
        tool.diameterKnown = true;
        result.tools[next] = tool;
        refreshTools();
        refreshHoles();
    });
    connect(removeTool, &QPushButton::clicked, this, [&] {
        capture();
        const int row = tools->currentRow();
        if (row < 0) return;
        const int number = tools->item(row, 0)->text().toInt();
        result.tools.erase(number);
        refreshTools();
        refreshHoles();
    });

    connect(add, &QPushButton::clicked, this, [&] {
        capture();
        if (toolCombo->count() == 0) return;
        const int number = toolCombo->currentData().toInt();
        ExcellonTool& tool = result.tools[number];
        if (typeCombo->currentIndex() == 0) {
            tool.drills.push_back({ax->value(), ay->value()});
        } else {
            tool.slotSegments.emplace_back(Pt{ax->value(), ay->value()},
                                           Pt{bx->value(), by->value()});
        }
        refreshHoles();
    });
    connect(remove, &QPushButton::clicked, this, [&] {
        capture();
        const int row = holes->currentRow();
        if (row < 0) return;
        int index = 0;
        for (auto& [number, tool] : result.tools) {
            (void)number;
            if (index + static_cast<int>(tool.drills.size()) > row) {
                tool.drills.erase(tool.drills.begin() + (row - index));
                refreshHoles();
                return;
            }
            index += static_cast<int>(tool.drills.size());
            if (index + static_cast<int>(tool.slotSegments.size()) > row) {
                tool.slotSegments.erase(tool.slotSegments.begin() + (row - index));
                refreshHoles();
                return;
            }
            index += static_cast<int>(tool.slotSegments.size());
        }
    });
    connect(moveHole, &QPushButton::clicked, this, [&] {
        capture();
        const int row = holes->currentRow();
        if (row < 0) return;
        auto shift = [&](int col) {
            if (auto* item = holes->item(row, col)) {
                const double delta = (col == 2 || col == 4) ? edx->value() : edy->value();
                item->setText(QString::number(item->text().toDouble() + displayLength(delta),
                                             'f', displayInch_ ? 7 : 3));
            }
        };
        shift(2);
        shift(3);
        if (holes->item(row, 4) && !holes->item(row, 4)->text().isEmpty()) {
            shift(4);
            shift(5);
        }
    });
    connect(duplicateHole, &QPushButton::clicked, this, [&] {
        capture();
        const int row = holes->currentRow();
        if (row < 0) return;
        const int newRow = holes->rowCount();
        holes->insertRow(newRow);
        for (int c = 0; c < 6; ++c) {
            const QString text = holes->item(row, c) ? holes->item(row, c)->text() : QString();
            double value = text.toDouble();
            const double step = displayLength(1.0);
            if (c == 2) value += step;
            if (c == 3) value += step;
            if (c == 4 && !text.isEmpty()) value += step;
            if (c == 5 && !text.isEmpty()) value += step;
            holes->setItem(newRow, c, new QTableWidgetItem(
                (c >= 2) ? QString::number(value, 'f', displayInch_ ? 7 : 3) : text));
        }
    });
    std::vector<std::array<QString, 6>> rowClipboard;
    connect(copyRows, &QPushButton::clicked, this, [&] {
        rowClipboard.clear();
        auto rows = holes->selectionModel()->selectedRows();
        std::sort(rows.begin(), rows.end(), [](const QModelIndex& a, const QModelIndex& b) {
            return a.row() < b.row();
        });
        for (const QModelIndex& index : rows) {
            std::array<QString, 6> values{};
            for (int c = 0; c < 6; ++c)
                values[static_cast<std::size_t>(c)] =
                    holes->item(index.row(), c) ? holes->item(index.row(), c)->text() : QString();
            rowClipboard.push_back(values);
        }
    });
    connect(pasteRows, &QPushButton::clicked, this, [&] {
        capture();
        for (const auto& values : rowClipboard) {
            const int row = holes->rowCount();
            holes->insertRow(row);
            for (int c = 0; c < 6; ++c) {
                QString text = values[static_cast<std::size_t>(c)];
                const double step = displayLength(1.0);
                if (c == 2 || c == 4) text = QString::number(text.toDouble() + step, 'f', displayInch_ ? 7 : 3);
                if (c == 3 || c == 5) text = QString::number(text.toDouble() + step, 'f', displayInch_ ? 7 : 3);
                holes->setItem(row, c, new QTableWidgetItem(text));
            }
        }
    });
    connect(removeRows, &QPushButton::clicked, this, [&] {
        capture();
        auto rows = holes->selectionModel()->selectedRows();
        std::sort(rows.begin(), rows.end(), [](const QModelIndex& a, const QModelIndex& b) {
            return a.row() > b.row();
        });
        for (const QModelIndex& index : rows) holes->removeRow(index.row());
    });
    connect(shiftAll, &QPushButton::clicked, this, [&] {
        capture();
        for (auto& [number, tool] : result.tools) {
            (void)number;
            for (Pt& hole : tool.drills) {
                hole.x += allDx->value();
                hole.y += allDy->value();
            }
            for (auto& [a, b] : tool.slotSegments) {
                a.x += allDx->value(); a.y += allDy->value();
                b.x += allDx->value(); b.y += allDy->value();
            }
        }
        refreshHoles();
    });
    connect(save, &QPushButton::clicked, this, [&] {
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Guardar como Excellon"), QFileInfo(lastDir_).absolutePath() + "/edited.drl",
            tr("Excellon (*.drl *.xln *.exc);;Todos (*)"));
        if (path.isEmpty()) return;
        try {
            writeExcellon(path.toStdString(), result, defaultUnits_);
            statusBar()->showMessage(tr("Excellon guardado"), 5000);
        } catch (const std::exception& e) {
            showError(QString::fromUtf8(e.what()));
        }
    });

    if (dialog.exec() != QDialog::Accepted) return;

    // Apply edited diameters.
    for (int row = 0; row < tools->rowCount(); ++row) {
        auto* n = tools->item(row, 0);
        auto* d = tools->item(row, 1);
        if (!n || !d) continue;
        const int number = n->text().toInt();
        if (result.tools.count(number)) result.tools[number].diameter = lengthToMm(d->text().toDouble());
    }

    // Rebuild drills/slots from the holes table.
    for (auto& [number, tool] : result.tools) {
        (void)number;
        tool.drills.clear();
        tool.slotSegments.clear();
    }
    for (int row = 0; row < holes->rowCount(); ++row) {
        const int number = holes->item(row, 0) ? holes->item(row, 0)->text().toInt() : 0;
        if (!result.tools.count(number)) continue;
        const QString type = holes->item(row, 1) ? holes->item(row, 1)->text() : "";
        const double x = holes->item(row, 2) ? lengthToMm(holes->item(row, 2)->text().toDouble()) : 0;
        const double y = holes->item(row, 3) ? lengthToMm(holes->item(row, 3)->text().toDouble()) : 0;
        if (type == tr("Ranura")) {
            const double x2 = holes->item(row, 4) ? lengthToMm(holes->item(row, 4)->text().toDouble()) : x;
            const double y2 = holes->item(row, 5) ? lengthToMm(holes->item(row, 5)->text().toDouble()) : y;
            result.tools[number].slotSegments.emplace_back(Pt{x, y}, Pt{x2, y2});
        } else {
            result.tools[number].drills.push_back({x, y});
        }
    }

    result.geometry.clear();
    result.drillCount = 0;
    result.slotCount = 0;
    for (auto& [number, tool] : result.tools) {
        (void)number;
        for (const Pt& hole : tool.drills) {
            result.geometry.push_back(Geom::circle(hole, tool.diameter / 2.0, 16));
            ++result.drillCount;
        }
        for (const auto& [a, b] : tool.slotSegments) {
            result.geometry.push_back(Geom::line({{a.x, a.y}, {b.x, b.y}})
                                          .buffer(tool.diameter / 2.0, 16));
            ++result.slotCount;
        }
    }
    result.solid = Geom::unionOf(result.geometry);
    result.bounds = result.solid.bounds();

    excellonLayers_[id] = result;
    lastExcellon_ = result;
    lastDrillJob_ = drillJobFromExcellon(result);
    canvas_->setLayerGeometry(id, result.geometry, {});
    item->setText(2, QString::number(result.solid.area(), 'f', 2));
    refreshDrillTable();
    refreshProperties();
    statusBar()->showMessage(tr("Excellon editado"), 4000);
}

/// @brief Aplica compensación de grabado (buffer).
void MainWindow::etchCompensation() {
    auto* item = objects_->currentItem();
    if (!item) return;
    const int id = item->data(0, Qt::UserRole).toInt();
    const std::vector<Geom> solids = canvas_->layerSolids(id);
    if (solids.empty()) {
        QMessageBox::information(this, tr("Compensación de grabado"),
                                 tr("El objeto no tiene geometría editable."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Compensación de grabado"));
    auto* form = new QFormLayout(&dialog);
    auto* value = new UnitSpinBox(&dialog);
    value->setRange(-10.0, 10.0);
    value->setDecimals(3);
    value->setValue(0.05);
    value->setSuffix(" mm");
    applyTip(value, tr("<b>Compensación</b><br>Positivo engorda el cobre (compensa "
                       "sobre-grabado); negativo lo adelgaza."));
    form->addRow(tr("Compensación:"), value);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(box);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    const std::vector<Geom> before = solids;
    std::vector<Geom> after;
    for (const Geom& g : solids) {
        Geom grown = g.buffer(value->value(), 16).fixed();
        if (grown.valid() && !grown.empty()) after.push_back(grown);
    }
    if (after.empty()) return;
    canvas_->setLayerGeometry(id, after, canvas_->layerFollows(id));
    item->setText(3, QString::number(Geom::unionOf(after).area(), 'f', 2));
    refreshProperties();
    undoStack_->push(new LambdaCommand(
        [this, id, before, follows = canvas_->layerFollows(id)] {
            canvas_->setLayerGeometry(id, before, follows);
            refreshProperties();
        },
        [this, id, after, follows = canvas_->layerFollows(id)] {
            canvas_->setLayerGeometry(id, after, follows);
            refreshProperties();
        },
        tr("Compensación de grabado")));
}

/// @brief Genera el negativo del cobre en un área.
void MainWindow::invertGerber() {
    auto* item = objects_->currentItem();
    if (!item) return;
    const int id = item->data(0, Qt::UserRole).toInt();
    const std::vector<Geom> solids = canvas_->layerSolids(id);
    const BBox b = canvas_->layerBounds(id);
    if (solids.empty() || !b.valid) {
        QMessageBox::information(this, tr("Invertir Gerber"),
                                 tr("El objeto no tiene geometría editable."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Invertir Gerber"));
    auto* form = new QFormLayout(&dialog);
    auto* margin = new UnitSpinBox(&dialog);
    margin->setRange(0.0, 50.0);
    margin->setDecimals(2);
    margin->setValue(0.0);
    margin->setSuffix(" mm");
    applyTip(margin, tr("<b>Margen</b><br>Expande el área invertida alrededor del objeto."));
    form->addRow(tr("Margen:"), margin);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(box);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    const BBox frame = b.expanded(margin->value());
    const Geom frameBox = Geom::box(frame.xmin, frame.ymin, frame.xmax, frame.ymax);
    const Geom result = frameBox.difference(Geom::unionOf(solids)).fixed();
    if (!result.valid()) return;
    const std::vector<Geom> before = solids;
    const std::vector<Geom> after{result};
    canvas_->setLayerGeometry(id, after, {});
    item->setText(3, QString::number(result.area(), 'f', 2));
    refreshProperties();
    undoStack_->push(new LambdaCommand(
        [this, id, before] {
            canvas_->setLayerGeometry(id, before, {});
            refreshProperties();
        },
        [this, id, after] {
            canvas_->setLayerGeometry(id, after, {});
            refreshProperties();
        },
        tr("Invertir Gerber")));
}

/// @brief Mide la distancia mínima entre dos objetos.
void MainWindow::distanceTool() {
    auto* item = objects_->currentItem();
    if (!item) return;
    const int idA = item->data(0, Qt::UserRole).toInt();
    const std::vector<Geom> solidsA = layerGeometries(idA);
    if (solidsA.empty()) {
        QMessageBox::information(this, tr("Distancia"), tr("El objeto A no tiene geometría."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Medir distancia mínima"));
    auto* form = new QFormLayout(&dialog);
    auto* combo = new QComboBox(&dialog);
    std::vector<int> ids;
    for (const int other : canvas_->layerIds()) {
        if (other == idA || layerGeometries(other).empty()) continue;
        for (QTreeWidgetItem* candidate : objectItems()) {
            if (candidate->data(0, Qt::UserRole).toInt() == other) {
                combo->addItem(candidate->text(1));
                ids.push_back(other);
                break;
            }
        }
    }
    if (combo->count() == 0) {
        QMessageBox::information(this, tr("Distancia"),
                                 tr("No hay otro objeto con geometría."));
        return;
    }
    form->addRow(tr("Objeto B:"), combo);
    auto* result = new QLabel(tr("—"), &dialog);
    auto* calc = new QPushButton(tr("Calcular"), &dialog);
    form->addRow(calc);
    form->addRow(tr("Distancia:"), result);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    form->addRow(box);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(calc, &QPushButton::clicked, this, [&] {
        if (ids.empty()) return;
        const int idB = ids[static_cast<std::size_t>(combo->currentIndex())];
        const auto solidsB = layerGeometries(idB);
        if (solidsB.empty()) return;
        Pt pa{};
        Pt pb{};
        const double d = layersDistance(solidsA, solidsB, &pa, &pb);
        if (d < 0.0) {
            result->setText(tr("sin geometría"));
            return;
        }
        addGeneratedLayer(tr("Distancia mínima"), {Geom::line({pa, pb})}, idA);
        result->setText(tr("%1 mm  ·  (%2, %3) → (%4, %5)")
                            .arg(d, 0, 'f', 4)
                            .arg(pa.x, 0, 'f', decimals_)
                            .arg(pa.y, 0, 'f', decimals_)
                            .arg(pb.x, 0, 'f', decimals_)
                            .arg(pb.y, 0, 'f', decimals_));
    });
    dialog.exec();
}

/// @brief Calcula la fresa máxima que aísla el cobre.
void MainWindow::optimalTool() {
    auto* item = objects_->currentItem();
    if (!item) {
        QMessageBox::information(this, tr("Herramienta óptima"),
                                 tr("Seleccioná un objeto Gerber."));
        return;
    }
    const int id = item->data(0, Qt::UserRole).toInt();
    auto it = gerberLayers_.find(id);
    Geom copper;
    if (it != gerberLayers_.end()) copper = it->second.solid;
    else copper = Geom::unionOf(canvas_->layerSolids(id));
    if (!copper.valid() || copper.empty()) {
        QMessageBox::information(this, tr("Herramienta óptima"),
                                 tr("El objeto seleccionado no tiene cobre."));
        return;
    }

    QDialog params(this);
    params.setWindowTitle(tr("Herramienta óptima"));
    auto* form = new QFormLayout(&params);
    auto* maxDiaSpin = new UnitSpinBox(&params);
    maxDiaSpin->setRange(0.05, 100.0);
    maxDiaSpin->setDecimals(3);
    maxDiaSpin->setValue(5.0);
    maxDiaSpin->setSuffix(" mm");
    auto* precision = new UnitSpinBox(&params);
    precision->setRange(0.0, 100.0);
    precision->setDecimals(3);
    precision->setValue(0.5);
    precision->setSuffix(" mm");
    auto* frequency = new QSpinBox(&params);
    frequency->setRange(1, 200);
    frequency->setValue(12);
    form->addRow(tr("Diámetro máximo:"), maxDiaSpin);
    form->addRow(tr("Precisión (separación de puntos):"), precision);
    form->addRow(tr("Frecuencia (máx. pares):"), frequency);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &params);
    form->addRow(box);
    connect(box, &QDialogButtonBox::accepted, &params, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &params, &QDialog::reject);
    if (params.exec() != QDialog::Accepted) return;
    const double maxDia = maxDiaSpin->value();
    const OptimalTool opt = optimalIsolationDiameter(copper, maxDia);
    if (!opt.valid) {
        QMessageBox::information(this, tr("Herramienta óptima"),
                                 tr("No se encontró una separación limitante; se puede usar "
                                    "hasta %1 mm.").arg(maxDia, 0, 'f', 3));
        return;
    }
    if (isoTable_ && isoTable_->rowCount() > 0) {
        if (auto* cell = isoTable_->item(0, 1))
            cell->setText(QString::number(opt.diameter, 'f', 3));
    }
    const auto gaps = optimalIsolationGaps(copper, maxDia,
                                           static_cast<std::size_t>(frequency->value()),
                                           precision->value());
    std::vector<Geom> gapLines;
    QString details;
    for (std::size_t i = 0; i < gaps.size(); ++i) {
        gapLines.push_back(Geom::line({gaps[i].pointA, gaps[i].pointB}));
        if (i < 5)
            details += tr("<br>%1 mm en (%2, %3) → (%4, %5)")
                           .arg(gaps[i].gap, 0, 'f', 4)
                           .arg(gaps[i].pointA.x, 0, 'f', 3)
                           .arg(gaps[i].pointA.y, 0, 'f', 3)
                           .arg(gaps[i].pointB.x, 0, 'f', 3)
                           .arg(gaps[i].pointB.y, 0, 'f', 3);
    }
    if (!gapLines.empty())
        addGeneratedLayer(tr("Separaciones mínimas"), gapLines, id);
    QMessageBox::information(
        this, tr("Herramienta óptima"),
        tr("Separación mínima de cobre: <b>%1 mm</b><br>"
           "Puntos: (%2, %3) → (%4, %5)<br>"
           "Diámetro óptimo de aislamiento: <b>%6 mm</b><br>"
           "Pares limitantes encontrados: %7%8<br><br>"
           "Se cargó en la primera fila de la tabla de aislamiento.")
            .arg(opt.gap, 0, 'f', 4)
            .arg(opt.pointA.x, 0, 'f', 3)
            .arg(opt.pointA.y, 0, 'f', 3)
            .arg(opt.pointB.x, 0, 'f', 3)
            .arg(opt.pointB.y, 0, 'f', 3)
            .arg(opt.diameter, 0, 'f', 4)
            .arg(gaps.size())
            .arg(details));
    openToolTab(tr("Herramienta de aislamiento"));
}

/// @brief Agrega marcas de esquina en L.
void MainWindow::addCornerMarks() {
    auto* item = objects_->currentItem();
    if (!item) return;
    const int id = item->data(0, Qt::UserRole).toInt();
    const BBox b = canvas_->layerBounds(id);
    if (!b.valid) return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Marcas de esquina"));
    auto* form = new QFormLayout(&dialog);
    auto* length = new UnitSpinBox(&dialog);
    length->setRange(0.1, 100.0);
    length->setDecimals(2);
    length->setValue(3.0);
    length->setSuffix(" mm");
    auto* margin = new UnitSpinBox(&dialog);
    margin->setRange(-50.0, 50.0);
    margin->setDecimals(2);
    margin->setValue(0.0);
    margin->setSuffix(" mm");
    auto* outside = new QCheckBox(tr("Por fuera del contorno"), &dialog);
    form->addRow(tr("Longitud:"), length);
    form->addRow(tr("Margen:"), margin);
    form->addRow(QString(), outside);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(box);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    const auto marks = cornerMarks(b, length->value(), margin->value(), outside->isChecked());
    addGeneratedLayer(tr("Marcas de esquina"), marks, id);
    statusBar()->showMessage(tr("Marcas de esquina generadas"), 4000);
}

/// @brief Resta los taladros Excellon al cobre.
void MainWindow::punchGerberAction() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Punch Gerber"));
    auto* form = new QFormLayout(&dialog);
    auto* gerberCombo = new QComboBox(&dialog);
    auto* drillCombo = new QComboBox(&dialog);
    std::vector<int> gerberIds;
    std::vector<int> drillIds;
    for (QTreeWidgetItem* row : objectItems()) {
        const int id = row->data(0, Qt::UserRole).toInt();
        const QString kind = row->text(2).toLower();
        if (gerberLayers_.count(id) && kind == "gerber") {
            gerberCombo->addItem(row->text(1));
            gerberIds.push_back(id);
        } else if (excellonLayers_.count(id) && kind == "excellon") {
            drillCombo->addItem(row->text(1));
            drillIds.push_back(id);
        }
    }
    if (gerberIds.empty() || drillIds.empty()) {
        QMessageBox::information(this, tr("Punch Gerber"),
                                 tr("Necesitás un Gerber y un Excellon cargados."));
        return;
    }
    auto* margin = new UnitSpinBox(&dialog);
    margin->setRange(0.0, 10.0);
    margin->setDecimals(3);
    margin->setValue(0.0);
    margin->setSuffix(" mm");
    auto* toolList = new QListWidget(&dialog);
    toolList->setMaximumHeight(120);
    auto refreshToolList = [&] {
        toolList->clear();
        const int did = drillIds[static_cast<std::size_t>(drillCombo->currentIndex())];
        for (const auto& [number, tool] : excellonLayers_[did].tools) {
            auto* entry = new QListWidgetItem(
                tr("T%1 · Ø %2 mm · %3 taladros").arg(number)
                    .arg(tool.diameter, 0, 'f', 3).arg(tool.drills.size()), toolList);
            entry->setFlags(entry->flags() | Qt::ItemIsUserCheckable);
            entry->setCheckState(Qt::Checked);
            entry->setData(Qt::UserRole, number);
        }
    };
    connect(drillCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [&](int) { refreshToolList(); });
    refreshToolList();
    form->addRow(tr("Gerber:"), gerberCombo);
    form->addRow(tr("Excellon:"), drillCombo);
    form->addRow(tr("Herramientas:"), toolList);
    form->addRow(tr("Holgura:"), margin);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(box);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    const int gid = gerberIds[static_cast<std::size_t>(gerberCombo->currentIndex())];
    const int did = drillIds[static_cast<std::size_t>(drillCombo->currentIndex())];
    ExcellonResult filtered = excellonLayers_[did];
    std::set<int> checked;
    for (int i = 0; i < toolList->count(); ++i) {
        if (toolList->item(i)->checkState() == Qt::Checked)
            checked.insert(toolList->item(i)->data(Qt::UserRole).toInt());
    }
    for (auto it = filtered.tools.begin(); it != filtered.tools.end();) {
        if (checked.count(it->first) == 0) it = filtered.tools.erase(it);
        else ++it;
    }
    filtered.geometry.clear();
    filtered.drillCount = 0;
    filtered.slotCount = 0;
    for (const auto& [number, tool] : filtered.tools) {
        (void)number;
        for (const Pt& hole : tool.drills) {
            filtered.geometry.push_back(Geom::circle(hole, tool.diameter / 2.0, 24));
            ++filtered.drillCount;
        }
        for (const auto& [a, b] : tool.slotSegments) {
            filtered.geometry.push_back(Geom::line({{a.x, a.y}, {b.x, b.y}})
                                            .buffer(tool.diameter / 2.0, 16));
            ++filtered.slotCount;
        }
    }
    filtered.solid = filtered.geometry.empty() ? Geom() : Geom::unionOf(filtered.geometry);
    filtered.bounds = filtered.solid.valid() ? filtered.solid.bounds() : BBox{};
    if (filtered.geometry.empty()) {
        QMessageBox::information(this, tr("Punch Gerber"),
                                 tr("Seleccioná al menos una herramienta."));
        return;
    }
    const Geom punched = punchGerber(gerberLayers_[gid], filtered, margin->value());
    if (!punched.valid() || punched.empty()) {
        QMessageBox::warning(this, tr("Punch Gerber"), tr("El resultado quedó vacío."));
        return;
    }
    addGeneratedSolid(tr("Punch Gerber"), {punched}, gid, "gerber");
    statusBar()->showMessage(tr("Punch Gerber generado"), 4000);
}

/// @brief Extrae taladros circulares del Gerber.
void MainWindow::extractDrillsAction() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Extraer taladros"));
    auto* form = new QFormLayout(&dialog);
    auto* combo = new QComboBox(&dialog);
    std::vector<int> ids;
    for (QTreeWidgetItem* row : objectItems()) {
        const int id = row->data(0, Qt::UserRole).toInt();
        if (!gerberLayers_.count(id) || row->text(2).toLower() != "gerber") continue;
        combo->addItem(row->text(1));
        ids.push_back(id);
    }
    if (ids.empty()) {
        QMessageBox::information(this, tr("Extraer taladros"),
                                 tr("Cargá un Gerber con pads circulares."));
        return;
    }
    auto* maxDia = new UnitSpinBox(&dialog);
    maxDia->setRange(0.05, 20.0);
    maxDia->setDecimals(2);
    maxDia->setValue(excellonExtractMaxDia_);
    maxDia->setSuffix(" mm");
    form->addRow(tr("Gerber:"), combo);
    form->addRow(tr("Diámetro máximo:"), maxDia);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(box);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    const int gid = ids[static_cast<std::size_t>(combo->currentIndex())];
    const ExcellonResult drills = extractDrills(gerberLayers_[gid], maxDia->value(),
                                                 defaultUnits_, stepsPerCircle_);
    if (drills.drillCount == 0) {
        QMessageBox::information(this, tr("Extraer taladros"),
                                 tr("No se encontraron pads circulares por debajo del límite."));
        return;
    }
    const int id = addGeneratedSolid(tr("Taladros extraídos"), drills.geometry, gid, "excellon");
    if (id > 0) {
        excellonLayers_[id] = drills;
        lastExcellon_ = drills;
        refreshSourceCombos();
    }
    statusBar()->showMessage(tr("Taladros extraídos: %1 en %2 herramientas")
                                 .arg(drills.drillCount)
                                 .arg(drills.tools.size()), 5000);
}

/// @brief Abre un archivo G-code como capa de recorridos.
void MainWindow::openGCodeFile() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Abrir G-code"), lastDir_,
        tr("G-code (*.nc *.gcode *.tap *.txt *.ngc);;Todos (*)"));
    if (!path.isEmpty()) addGCodeLayer(path);
}

/// @brief Agrega un programa G-code al visor.
bool MainWindow::addGCodeLayer(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        showError(tr("No se pudo abrir %1").arg(path));
        return false;
    }
    std::vector<Geom> paths;
    Ring current;
    Pt pos{0.0, 0.0};
    bool absolute = true;
    bool cutting = false;
    int moves = 0;

    auto flush = [&] {
        if (current.size() >= 2) paths.push_back(Geom::line(current));
        current.clear();
    };

    while (!file.atEnd()) {
        QString line = QString::fromUtf8(file.readLine());
        line = line.section(';', 0, 0).section('(', 0, 0).trimmed();
        if (line.isEmpty()) continue;
        const QStringList words = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        bool hasG1 = false;
        bool hasG0 = false;
        std::optional<double> x;
        std::optional<double> y;
        for (const QString& raw : words) {
            const QString word = raw.toUpper();
            if (word == "G90") absolute = true;
            else if (word == "G91") absolute = false;
            else if (word == "G0" || word == "G00") hasG0 = true;
            else if (word == "G1" || word == "G01" || word == "G2" || word == "G02" ||
                     word == "G3" || word == "G03") hasG1 = true;
            else if (word.startsWith("X")) x = word.mid(1).toDouble();
            else if (word.startsWith("Y")) y = word.mid(1).toDouble();
        }
        if (!hasG0 && !hasG1 && !x && !y) continue;
        const Pt target{(x ? (absolute ? *x : pos.x + *x) : pos.x),
                        (y ? (absolute ? *y : pos.y + *y) : pos.y)};
        const bool rapid = hasG0;
        if (rapid) {
            flush();
            pos = target;
            cutting = false;
            continue;
        }
        if (current.empty()) current.push_back(pos);
        current.push_back(target);
        pos = target;
        cutting = true;
        ++moves;
        if (moves % 2000 == 0) QApplication::processEvents();
    }
    flush();
    if (paths.empty()) {
        QMessageBox::information(this, tr("Abrir G-code"),
                                 tr("El archivo no contiene movimientos lineales G0/G1."));
        return false;
    }
    const int id = canvas_->appendGeometry({}, paths, true);
    canvas_->endGeometry();
    addObjectRow(QFileInfo(path).fileName(), "gcode", "-", id, true, toolsGroup_);
    sourcePaths_[id] = path;
    if (toolsGroup_) toolsGroup_->setExpanded(true);
    refreshSourceCombos();
    if (!canvas_->hasData()) canvas_->fitTo(canvas_->layerBounds(id));
    statusBar()->showMessage(tr("G-code cargado: %1 recorridos").arg(paths.size()), 5000);
    return true;
}

/// @brief Asistente de importación Excellon + tabla INF.
void MainWindow::pcbWizardImport() {
    const QString excellonPath = QFileDialog::getOpenFileName(
        this, tr("PcbWizard: archivo Excellon"), lastDir_,
        tr("Excellon (*.drl *.xln *.exc *.ncd);;Todos (*)"));
    if (excellonPath.isEmpty()) return;
    const QString infPath = QFileDialog::getOpenFileName(
        this, tr("PcbWizard: archivo INF"), QFileInfo(excellonPath).absolutePath(),
        tr("PcbWizard INF (*.INF *.inf);;Todos (*)"));
    if (infPath.isEmpty()) return;
    try {
        fc::ExcellonOptions opt;
        opt.appUnits = defaultUnits_;
        opt.stepsPerCircle = stepsPerCircle_;
        fc::ExcellonResult result = fc::ExcellonParser(opt).parseFile(excellonPath.toStdString());

        QFile inf(infPath);
        if (!inf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            showError(tr("No se pudo abrir el INF."));
            return;
        }
        QHash<int, double> diameters;
        bool inch = result.fileUnits == Units::Inch;
        const QRegularExpression toolRe(R"(^T(\d+)\s+(\d*\.?\d+)$)");
        const QRegularExpression formatRe(
            R"(^(\d+)\.?(\d+)\s*format,\s*(inches|metric)?,\s*(absolute|incremental)?.*$)",
            QRegularExpression::CaseInsensitiveOption);
        while (!inf.atEnd()) {
            const QString line = QString::fromUtf8(inf.readLine()).trimmed();
            auto match = toolRe.match(line);
            if (match.hasMatch()) {
                diameters[match.captured(1).toInt()] = match.captured(2).toDouble();
                continue;
            }
            match = formatRe.match(line);
            if (match.hasMatch())
                inch = match.captured(3).compare("inches", Qt::CaseInsensitive) == 0;
        }
        if (diameters.isEmpty()) {
            QMessageBox::warning(this, tr("PcbWizard"),
                                 tr("El INF no contiene herramientas Tn con diámetro."));
            return;
        }
        int applied = 0;
        for (auto& [number, tool] : result.tools) {
            const auto found = diameters.find(number);
            if (found == diameters.end()) continue;
            double dia = found.value();
            if (inch && defaultUnits_ == Units::Mm) dia *= 25.4;
            tool.diameter = dia;
            tool.diameterKnown = true;
            ++applied;
        }
        if (applied == 0) {
            QMessageBox::warning(this, tr("PcbWizard"),
                                 tr("Ninguna herramienta del INF coincide con el Excellon."));
            return;
        }
        std::vector<Geom> geometry;
        for (const auto& [number, tool] : result.tools) {
            for (const Pt& hole : tool.drills)
                geometry.push_back(Geom::circle(hole, tool.diameter / 2.0, stepsPerCircle_));
            for (const auto& [a, b] : tool.slotSegments)
                geometry.push_back(Geom::line({{a.x, a.y}, {b.x, b.y}})
                                       .buffer(tool.diameter / 2.0, 16));
        }
        result.geometry = geometry;
        result.solid = geometry.empty() ? Geom() : Geom::unionOf(geometry);
        result.bounds = result.solid.valid() ? result.solid.bounds() : BBox{};

        const int id = canvas_->appendGeometry(result.geometry, {}, true);
        canvas_->endGeometry();
        addObjectRow(QFileInfo(excellonPath).fileName(), "excellon",
                     QString::number(result.solid.area(), 'f', 2), id);
        excellonLayers_[id] = result;
        lastExcellon_ = result;
        lastDrillJob_ = drillJobFromExcellon(result);
        refreshDrillTable();
        if (!canvas_->hasData()) canvas_->fitTo(canvas_->layerBounds(id));
        statusBar()->showMessage(
            tr("PcbWizard: %1 herramientas, %2 taladros").arg(applied).arg(result.drillCount),
            6000);
        refreshObjectToolPage();
    } catch (const std::exception& e) {
        showError(QString::fromUtf8(e.what()));
    }
}

/// @brief Carga una tabla `.INF` de herramientas.
void MainWindow::loadInfFile() {
    auto* item = objects_->currentItem();
    if (!item) {
        QMessageBox::information(this, tr("Tabla INF"),
                                 tr("Seleccioná el objeto Excellon al que aplicar la tabla."));
        return;
    }
    const int id = item->data(0, Qt::UserRole).toInt();
    auto it = excellonLayers_.find(id);
    if (it == excellonLayers_.end()) {
        QMessageBox::information(this, tr("Tabla INF"),
                                 tr("El objeto seleccionado no es un Excellon."));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Cargar tabla INF"), lastDir_, tr("PcbWizard INF (*.INF *.inf);;Todos (*)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Tabla INF"), tr("No se pudo abrir el archivo."));
        return;
    }
    ExcellonResult result = it->second;
    QHash<int, double> diameters;
    bool inch = result.fileUnits == Units::Inch;
    const QRegularExpression toolRe(R"(^T(\d+)\s+(\d*\.?\d+)$)");
    const QRegularExpression formatRe(
        R"(^(\d+)\.?(\d+)\s*format,\s*(inches|metric)?,\s*(absolute|incremental)?.*$)",
        QRegularExpression::CaseInsensitiveOption);
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        auto match = toolRe.match(line);
        if (match.hasMatch()) {
            diameters[match.captured(1).toInt()] = match.captured(2).toDouble();
            continue;
        }
        match = formatRe.match(line);
        if (match.hasMatch()) {
            inch = match.captured(3).compare("inches", Qt::CaseInsensitive) == 0;
        }
    }
    if (diameters.isEmpty()) {
        QMessageBox::warning(this, tr("Tabla INF"),
                             tr("El archivo no contiene una tabla de herramientas T<n> <diámetro>."));
        return;
    }
    int applied = 0;
    for (auto& [number, tool] : result.tools) {
        const auto found = diameters.find(number);
        if (found == diameters.end()) continue;
        double dia = found.value();
        if (inch && defaultUnits_ == Units::Mm) dia *= 25.4;
        tool.diameter = dia;
        tool.diameterKnown = true;
        ++applied;
    }
    if (applied == 0) {
        QMessageBox::information(this, tr("Tabla INF"),
                                 tr("Ninguna herramienta del archivo coincide con el Excellon."));
        return;
    }

    // Rebuild the hole geometry with the corrected diameters.
    std::vector<Geom> geometry;
    for (const auto& [number, tool] : result.tools) {
        for (const Pt& hole : tool.drills) {
            geometry.push_back(Geom::circle(hole, tool.diameter / 2.0, stepsPerCircle_));
        }
        for (const auto& [a, b] : tool.slotSegments) {
            geometry.push_back(Geom::line({{a.x, a.y}, {b.x, b.y}})
                                   .buffer(tool.diameter / 2.0, 16));
        }
    }
    result.geometry = geometry;
    result.solid = geometry.empty() ? Geom() : Geom::unionOf(geometry);
    result.bounds = result.solid.valid() ? result.solid.bounds() : BBox{};

    excellonLayers_[id] = result;
    if (lastExcellon_ && canvas_->layerIds().size() && !canvas_->layerIds().empty()) {
        // Keep the "last" object usable from the Tcl/JS API.
        lastExcellon_ = result;
    }
    canvas_->setLayerGeometry(id, geometry, {});
    lastDrillJob_ = drillJobFromExcellon(result);
    refreshDrillTable();
    refreshProperties();
    statusBar()->showMessage(tr("Tabla INF aplicada: %1 herramientas").arg(applied), 5000);
}

/// @brief Importa SVG o DXF desde un diálogo.
void MainWindow::importVector(bool svg) {
    const QString path = QFileDialog::getOpenFileName(
        this, svg ? tr("Importar SVG") : tr("Importar DXF"), lastDir_,
        svg ? tr("SVG (*.svg);;Todos (*)") : tr("DXF (*.dxf);;Todos (*)"));
    if (!path.isEmpty() && importVectorPath(path, svg)) recordRecent(path);
}

/// @brief Importa un SVG/DXF concreto y agrega texto.
bool MainWindow::importVectorPath(const QString& path, bool svg) {
    try {
        std::vector<Geom> geoms = svg ? importSvg(path.toStdString())
                                      : importDxf(path.toStdString());
        const std::vector<Geom> texts =
            svg ? svgTextGeometry(path) : dxfTextGeometry(path);
        geoms.insert(geoms.end(), texts.begin(), texts.end());
        addVectorGeometry(QFileInfo(path).fileName(), geoms);
    } catch (const std::exception& e) {
        showError(QString::fromUtf8(e.what()));
        return false;
    }
    return true;
}

/// @brief Convierte geometría importada en una capa Gerber.
void MainWindow::addVectorGeometry(const QString& name, const std::vector<Geom>& geoms) {
    if (geoms.empty()) {
        QMessageBox::information(this, tr("Importar"), tr("El archivo no tiene geometría."));
        return;
    }
    std::vector<Geom> solids;
    std::vector<Geom> follows;
    for (const Geom& g : geoms) {
        if (!g.valid() || g.empty()) continue;
        const Geom geometry = geometrySimplify_
                                  ? g.simplify(geometryTolerance_).fixed()
                                  : g;
        const int type = geometry.typeId();
        if (type == GEOS_POLYGON || type == GEOS_MULTIPOLYGON) solids.push_back(geometry);
        else follows.push_back(geometry);
    }
    const int id = canvas_->appendGeometry(solids, follows, true);
    canvas_->endGeometry();
    addObjectRow(name, "gerber", QString::number(Geom::unionOf(solids).area(), 'f', 2), id);
    GerberResult r;
    r.solids = solids;
    r.follows = follows;
    r.solid = solids.empty() ? Geom() : Geom::unionOf(solids);
    r.bounds = r.solid.valid() ? r.solid.bounds() : BBox{};
    r.fileUnits = defaultUnits_;
    gerberLayers_[id] = r;
    lastGerber_ = r;
    if (!canvas_->hasData()) canvas_->fitTo(canvas_->layerBounds(id));
    statusBar()->showMessage(tr("Importado: %1 objetos").arg(geoms.size()), 5000);
}

/// @brief Importa un archivo HPGL2.
void MainWindow::importHpglFile() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Importar HPGL2"), lastDir_,
        tr("HPGL (*.hpgl *.hpg *.plt);;Todos (*)"));
    if (path.isEmpty()) return;
    try {
        addVectorGeometry(QFileInfo(path).fileName(),
                          importHpgl(path.toStdString()));
    } catch (const std::exception& e) {
        showError(QString::fromUtf8(e.what()));
    }
}

/// @brief Importa un PDF vectorial o rasterizado.
bool MainWindow::importPdfPath(const QString& path) {
    try {
        const std::vector<Geom> geoms = fc::importPdfVector(path.toStdString());
        if (!geoms.empty()) {
            addVectorGeometry(QFileInfo(path).fileName(), geoms);
            statusBar()->showMessage(tr("PDF vectorial importado: %1 objetos")
                                         .arg(geoms.size()), 5000);
            return true;
        }
    } catch (const std::exception&) {
        // Fall through to the raster conversion below.
    }
#ifdef __APPLE__
    QVector<QSizeF> pageSizes;
    const QVector<QImage> pages = renderPdfPages(path, 200, &pageSizes);
    if (!pages.isEmpty()) {
        for (int i = 0; i < pages.size(); ++i) {
            const double widthMm = i < pageSizes.size() ? pageSizes[i].width() : 200.0;
            addImageGeometry(pages[i], tr("%1 p%2").arg(QFileInfo(path).baseName()).arg(i + 1),
                             widthMm, 128, false);
        }
        statusBar()->showMessage(tr("PDF importado: %1 páginas").arg(pages.size()), 5000);
        return true;
    }
#endif
    return false;
}

// ============================================================================
// Importación y exportación
// ============================================================================

/// @brief Abre el diálogo de importación de PDF.
void MainWindow::importPdfFile() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Importar PDF"), lastDir_, tr("PDF (*.pdf);;Todos (*)"));
    if (path.isEmpty()) return;
    if (importPdfPath(path)) {
        recordRecent(path);
        return;
    }
    const QString temp = QDir::tempPath() + "/flatcam_pdf_import.png";
    QProcess process;
    process.start("/usr/bin/sips", {"-s", "format", "png", path, "--out", temp});
    if (!process.waitForFinished(30000) || process.exitCode() != 0) {
        showError(tr("No se pudo convertir el PDF a imagen."));
        return;
    }
    QImage image(temp);
    QFile::remove(temp);
    if (image.isNull()) {
        showError(tr("No se pudo leer el PDF convertido."));
        return;
    }
    bool ok = false;
    const QString unit = displayInch_ ? "in" : "mm";
    const double width = lengthToMm(QInputDialog::getDouble(
        this, tr("Importar PDF"), tr("Ancho de la página (%1):").arg(unit),
        displayLength(200.0), displayLength(1.0), displayLength(2000.0),
        displayInch_ ? 5 : 2, &ok));
    if (!ok) return;
    addImageGeometry(image, QFileInfo(path).baseName(), width, 128, false);
}

/// @brief Imprime la vista actual con QPrinter.
void MainWindow::printView() {
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer, this);
    dialog.setWindowTitle(tr("Imprimir vista"));
    if (dialog.exec() != QDialog::Accepted) return;
    QPainter painter(&printer);
    canvas_->render(&painter);
    painter.end();
    statusBar()->showMessage(tr("Vista enviada a la impresora"), 5000);
}

/// @brief Exporta la vista a HPGL.
void MainWindow::exportHpglFile() {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Exportar HPGL"), QFileInfo(lastDir_).absolutePath() + "/flatcam.hpgl",
        tr("HPGL (*.hpgl *.plt)"));
    if (path.isEmpty()) return;
    std::vector<Geom> solids;
    std::vector<Geom> follows;
    for (QTreeWidgetItem* item : objectItems()) {
        if (!item->data(0, Qt::UserRole + 1).toBool()) continue;
        const int id = item->data(0, Qt::UserRole).toInt();
        const auto s = canvas_->layerSolids(id);
        const auto f = canvas_->layerFollows(id);
        solids.insert(solids.end(), s.begin(), s.end());
        follows.insert(follows.end(), f.begin(), f.end());
    }
    if (solids.empty() && follows.empty()) {
        showError(tr("No hay geometría visible para exportar."));
        return;
    }
    try {
        exportHpgl(path.toStdString(), solids, follows);
        statusBar()->showMessage(tr("HPGL exportado: %1").arg(QFileInfo(path).fileName()), 5000);
    } catch (const std::exception& e) {
        showError(QString::fromUtf8(e.what()));
    }
}

/// @brief Exporta los objetos a DXF.
void MainWindow::exportDxfFile() {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Exportar DXF"), QFileInfo(lastDir_).absolutePath() + "/flatcam.dxf",
        tr("DXF (*.dxf)"));
    if (path.isEmpty()) return;
    std::vector<Geom> solids;
    std::vector<Geom> follows;
    for (QTreeWidgetItem* item : objectItems()) {
        if (!item->data(0, Qt::UserRole + 1).toBool()) continue;
        const int id = item->data(0, Qt::UserRole).toInt();
        const auto s = canvas_->layerSolids(id);
        const auto f = canvas_->layerFollows(id);
        solids.insert(solids.end(), s.begin(), s.end());
        follows.insert(follows.end(), f.begin(), f.end());
    }
    if (solids.empty() && follows.empty()) {
        showError(tr("No hay geometría visible para exportar."));
        return;
    }
    try {
        exportDxf(path.toStdString(), solids, follows);
        statusBar()->showMessage(tr("DXF exportado: %1").arg(QFileInfo(path).fileName()), 5000);
    } catch (const std::exception& e) {
        showError(QString::fromUtf8(e.what()));
    }
}

/// @brief Exporta la vista a PNG.
void MainWindow::exportPng() {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Exportar PNG"), QFileInfo(lastDir_).absolutePath() + "/flatcam.png",
        tr("PNG (*.png)"));
    if (path.isEmpty()) return;
    const QPixmap pixmap = canvas_->grab();
    if (!pixmap.save(path, "PNG")) {
        showError(tr("No se pudo guardar la imagen."));
        return;
    }
    statusBar()->showMessage(tr("PNG exportado: %1").arg(QFileInfo(path).fileName()), 5000);
}

/// @brief Importa una imagen y la vectoriza.
void MainWindow::importImage() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Importar imagen"), lastDir_,
        tr("Imágenes (*.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff);;Todos (*)"));
    if (path.isEmpty()) return;
    QImage image(path);
    if (image.isNull()) {
        QMessageBox::warning(this, tr("Importar imagen"), tr("No se pudo leer la imagen."));
        return;
    }
    image = image.convertToFormat(QImage::Format_Grayscale8);
    while (image.width() > 320 || image.height() > 320) {
        image = image.scaled(image.width() / 2, image.height() / 2, Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Importar imagen"));
    auto* form = new QFormLayout(&dialog);
    auto* width = new UnitSpinBox(&dialog);
    width->setRange(1.0, 1000.0);
    width->setDecimals(2);
    width->setValue(50.0);
    width->setSuffix(" mm");
    auto* threshold = new QSpinBox(&dialog);
    threshold->setRange(0, 255);
    threshold->setValue(128);
    auto* invert = new QCheckBox(tr("Invertir (blanco = cobre)"), &dialog);
    auto* colorMode = new QComboBox(&dialog);
    colorMode->addItems({tr("Blanco y negro"), tr("Color")});
    auto* colorButton = new QPushButton(tr("Color objetivo"), &dialog);
    QColor targetColor = Qt::black;
    colorButton->setStyleSheet("background:#000000; color:white;");
    auto* colorTolerance = new QSpinBox(&dialog);
    colorTolerance->setRange(0, 765);
    colorTolerance->setValue(60);
    auto* maskEnabled = new QCheckBox(tr("Máscara de color"), &dialog);
    auto* maskButton = new QPushButton(tr("Color de máscara"), &dialog);
    QColor maskColor = Qt::white;
    maskButton->setStyleSheet("background:#ffffff; color:black;");
    auto* maskTolerance = new QSpinBox(&dialog);
    maskTolerance->setRange(0, 765);
    maskTolerance->setValue(30);
    connect(maskButton, &QPushButton::clicked, &dialog, [&] {
        const QColor chosen = QColorDialog::getColor(maskColor, &dialog, tr("Color de máscara"));
        if (!chosen.isValid()) return;
        maskColor = chosen;
        maskButton->setStyleSheet(QString("background:%1; color:%2;")
                                      .arg(chosen.name(),
                                           chosen.lightness() > 128 ? "black" : "white"));
    });
    connect(colorButton, &QPushButton::clicked, &dialog, [&] {
        const QColor chosen = QColorDialog::getColor(targetColor, &dialog, tr("Color objetivo"));
        if (!chosen.isValid()) return;
        targetColor = chosen;
        colorButton->setStyleSheet(QString("background:%1; color:%2;")
                                       .arg(chosen.name(),
                                            chosen.lightness() > 128 ? "black" : "white"));
    });
    auto* dpi = new QSpinBox(&dialog);
    dpi->setRange(1, 2400);
    dpi->setValue(96);
    auto* useDpi = new QCheckBox(tr("Calcular ancho desde DPI"), &dialog);
    connect(useDpi, &QCheckBox::toggled, width, [width](bool on) { width->setEnabled(!on); });
    auto* preview = new QLabel(&dialog);
    preview->setPixmap(QPixmap::fromImage(image));
    form->addRow(tr("Ancho:"), width);
    form->addRow(tr("DPI:"), dpi);
    form->addRow(QString(), useDpi);
    form->addRow(tr("Modo:"), colorMode);
    form->addRow(tr("Color:"), colorButton);
    form->addRow(tr("Tolerancia color:"), colorTolerance);
    form->addRow(QString(), maskEnabled);
    form->addRow(tr("Máscara:"), maskButton);
    form->addRow(tr("Tolerancia máscara:"), maskTolerance);
    form->addRow(tr("Umbral:"), threshold);
    form->addRow(QString(), invert);
    form->addRow(preview);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(box);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    const double widthMm = useDpi->isChecked()
        ? image.width() * 25.4 / std::max(1, dpi->value())
        : width->value();
    addImageGeometry(image, QFileInfo(path).baseName(), widthMm, threshold->value(),
                     invert->isChecked(), colorMode->currentIndex() == 1, targetColor,
                     colorTolerance->value(), maskEnabled->isChecked(), maskColor,
                     maskTolerance->value());
}

/// @brief Convierte una imagen en capa de cobre.
void MainWindow::addImageGeometry(const QImage& image, const QString& name, double widthMm,
                                  int threshold, bool invert, bool colorMode,
                                  const QColor& target, int tolerance,
                                  bool maskEnabled, const QColor& maskColor,
                                  int maskTolerance) {
    if (image.isNull() || image.width() <= 0) return;
    QImage work = image;
    if (work.format() != QImage::Format_Grayscale8)
        work = work.convertToFormat(QImage::Format_Grayscale8);
    while (work.width() > 480 || work.height() > 480) {
        work = work.scaled(work.width() / 2, work.height() / 2, Qt::KeepAspectRatio,
                           Qt::SmoothTransformation);
    }
    const double pixel = widthMm / work.width();
    std::vector<Geom> rects;
    rects.reserve(static_cast<std::size_t>(work.width() * work.height() / 4));
    for (int y = 0; y < work.height(); ++y) {
        int start = -1;
        const double yb = (work.height() - y - 1) * pixel;
        for (int x = 0; x <= work.width(); ++x) {
            bool on = false;
            if (x < work.width()) {
                if (colorMode) {
                    const QColor pixel = work.pixelColor(x, y);
                    const int distance = std::abs(pixel.red() - target.red()) +
                                         std::abs(pixel.green() - target.green()) +
                                         std::abs(pixel.blue() - target.blue());
                    on = distance <= tolerance;
                    if (invert) on = !on;
                } else {
                    const int gray = qGray(work.pixel(x, y));
                    on = invert ? gray > threshold : gray < threshold;
                }
                if (on && maskEnabled) {
                    const QColor pixel = work.pixelColor(x, y);
                    const int distance = std::abs(pixel.red() - maskColor.red()) +
                                         std::abs(pixel.green() - maskColor.green()) +
                                         std::abs(pixel.blue() - maskColor.blue());
                    if (distance <= maskTolerance) on = false;
                }
            }
            if (on && start < 0) start = x;
            if (!on && start >= 0) {
                rects.push_back(Geom::box(start * pixel, yb, x * pixel, yb + pixel));
                start = -1;
            }
        }
    }
    if (rects.empty()) {
        QMessageBox::information(this, tr("Importar imagen"),
                                 tr("La imagen no tiene píxeles dentro del umbral."));
        return;
    }
    const Geom united = Geom::unionOf(rects);
    const int id = addGeneratedSolid(name, {united}, 0, "gerber");
    if (id > 0) {
        GerberResult r;
        r.solids = {united};
        r.solid = united;
        r.bounds = united.bounds();
        r.fileUnits = defaultUnits_;
        gerberLayers_[id] = r;
        lastGerber_ = r;
        refreshSourceCombos();
    }
    statusBar()->showMessage(tr("Imagen importada: %1 polígonos").arg(united.numGeometries()), 5000);
}

/// @brief Exporta la vista a PDF vectorial.
void MainWindow::exportPdf() {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Exportar PDF"), QFileInfo(lastDir_).absolutePath() + "/flatcam.pdf",
        tr("PDF (*.pdf)"));
    if (path.isEmpty()) return;
    QPdfWriter writer(path);
    writer.setResolution(300);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageOrientation(canvas_->width() >= canvas_->height()
                                  ? QPageLayout::Landscape
                                  : QPageLayout::Portrait);
    writer.setTitle(tr("FlatCAM C++"));
    QPainter painter(&writer);
    canvas_->render(&painter);
    painter.end();
    statusBar()->showMessage(tr("PDF exportado: %1").arg(QFileInfo(path).fileName()), 5000);
}

/// @brief Comprueba reglas de diseño (traza, taladro, anillo...).
void MainWindow::rulesCheck() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Comprobar reglas"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    auto* clearance = new UnitSpinBox(&dialog);
    clearance->setRange(0.0, 10.0);
    clearance->setDecimals(3);
    clearance->setValue(0.2);
    clearance->setSuffix(" mm");
    auto* traceCheck = new QCheckBox(tr("Ancho mínimo de traza"), &dialog);
    auto* traceMin = new UnitSpinBox(&dialog);
    traceMin->setRange(0.0, 10.0);
    traceMin->setDecimals(3);
    traceMin->setValue(0.15);
    traceMin->setSuffix(" mm");
    auto* holeCheck = new QCheckBox(tr("Diámetro mínimo de taladro"), &dialog);
    auto* holeMin = new UnitSpinBox(&dialog);
    holeMin->setRange(0.0, 10.0);
    holeMin->setDecimals(3);
    holeMin->setValue(0.3);
    holeMin->setSuffix(" mm");
    auto* holeCopperCheck = new QCheckBox(tr("Clearance taladro-cobre"), &dialog);
    auto* holeCopperMin = new UnitSpinBox(&dialog);
    holeCopperMin->setRange(0.0, 10.0);
    holeCopperMin->setDecimals(3);
    holeCopperMin->setValue(0.2);
    holeCopperMin->setSuffix(" mm");
    auto* holeClearCheck = new QCheckBox(tr("Separación entre taladros"), &dialog);
    auto* holeClearMin = new UnitSpinBox(&dialog);
    holeClearMin->setRange(0.0, 10.0);
    holeClearMin->setDecimals(3);
    holeClearMin->setValue(0.3);
    holeClearMin->setSuffix(" mm");
    auto* ringCheck = new QCheckBox(tr("Anillo anular mínimo"), &dialog);
    auto* ringMin = new UnitSpinBox(&dialog);
    ringMin->setRange(0.0, 10.0);
    ringMin->setDecimals(3);
    ringMin->setValue(0.15);
    ringMin->setSuffix(" mm");
    traceCheck->setChecked(true);
    holeCheck->setChecked(true);
    ringCheck->setChecked(false);
    form->addRow(tr("Separación mínima:"), clearance);
    form->addRow(traceCheck, traceMin);
    form->addRow(holeCheck, holeMin);
    form->addRow(holeCopperCheck, holeCopperMin);
    form->addRow(holeClearCheck, holeClearMin);
    form->addRow(ringCheck, ringMin);
    layout->addLayout(form);
    auto* table = new QTableWidget(0, 4, &dialog);
    table->setHorizontalHeaderLabels({tr("Objeto"), tr("Regla"), tr("Valor"), tr("Estado")});
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    layout->addWidget(table);
    auto* run = new QPushButton(tr("Comprobar"), &dialog);
    layout->addWidget(run);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(box);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    connect(run, &QPushButton::clicked, this,
            [this, table, clearance, traceCheck, traceMin, holeCheck, holeMin,
             holeCopperCheck, holeCopperMin, holeClearCheck, holeClearMin, ringCheck, ringMin] {
        table->setRowCount(0);
        auto addResult = [table](const QString& object, const QString& rule, double value,
                                 bool violation) {
            const int row = table->rowCount();
            table->insertRow(row);
            table->setItem(row, 0, new QTableWidgetItem(object));
            table->setItem(row, 1, new QTableWidgetItem(rule));
            table->setItem(row, 2, new QTableWidgetItem(
                QString::number(value, 'f', 4) + " mm"));
            auto* status = new QTableWidgetItem(violation ? tr("Violación") : tr("OK"));
            status->setForeground(violation ? QColor(180, 40, 40) : QColor(40, 140, 60));
            table->setItem(row, 3, status);
        };

        const auto items = objectItems();
        for (std::size_t i = 0; i < items.size(); ++i) {
            const int idA = items[i]->data(0, Qt::UserRole).toInt();
            const QString kindA = items[i]->text(2).toLower();
            if (kindA != "gerber" && kindA != "excellon") continue;
            const auto ga = layerGeometries(idA);
            if (ga.empty()) continue;
            for (std::size_t j = i + 1; j < items.size(); ++j) {
                const int idB = items[j]->data(0, Qt::UserRole).toInt();
                const QString kindB = items[j]->text(2).toLower();
                if (kindB != "gerber" && kindB != "excellon") continue;
                const auto gb = layerGeometries(idB);
                if (gb.empty()) continue;
                const double d = layersDistance(ga, gb);
                if (d < 0.0) continue;
                if (d < clearance->value())
                    addResult(items[i]->text(1) + " / " + items[j]->text(1),
                              tr("Separación"), d, true);
            }
        }

        for (QTreeWidgetItem* item : items) {
            const int id = item->data(0, Qt::UserRole).toInt();
            const QString kind = item->text(2).toLower();
            if (traceCheck->isChecked() && kind == "gerber") {
                const auto it = gerberLayers_.find(id);
                if (it != gerberLayers_.end()) {
                    double minimum = -1.0;
                    for (const auto& [code, aperture] : it->second.apertures) {
                        if (aperture.kind != GerberAperture::Kind::Circle) continue;
                        if (aperture.geometry.empty()) continue;
                        const double size = aperture.size > 0.0 ? aperture.size : aperture.diameter;
                        if (size <= 0.0) continue;
                        if (minimum < 0.0 || size < minimum) minimum = size;
                    }
                    if (minimum > 0.0 && minimum < traceMin->value())
                        addResult(item->text(1), tr("Ancho de traza"), minimum, true);
                }
            }
            if (holeCheck->isChecked() && kind == "excellon") {
                const auto it = excellonLayers_.find(id);
                if (it != excellonLayers_.end()) {
                    double minimum = -1.0;
                    for (const auto& [number, tool] : it->second.tools) {
                        if (tool.drills.empty() && tool.slotSegments.empty()) continue;
                        if (minimum < 0.0 || tool.diameter < minimum) minimum = tool.diameter;
                    }
                    if (minimum > 0.0 && minimum < holeMin->value())
                        addResult(item->text(1), tr("Taladro"), minimum, true);
                }
            }
            if (holeCopperCheck->isChecked() && kind == "excellon") {
                const auto eit = excellonLayers_.find(id);
                if (eit != excellonLayers_.end()) {
                    std::vector<Geom> copper;
                    for (const auto& [gid, gerber] : gerberLayers_) {
                        (void)gid;
                        for (const PolygonContours& pc : gerber.solid.polygons()) {
                            if (pc.outer.size() >= 3)
                                copper.push_back(Geom::polygon(pc.outer, pc.holes));
                        }
                    }
                    double minimum = 1e9;
                    for (const auto& [number, tool] : eit->second.tools) {
                        (void)number;
                        for (const Pt& hole : tool.drills) {
                            const Geom holePoint = Geom::point(hole.x, hole.y);
                            const Geom holeCircle = Geom::circle(hole, tool.diameter / 2.0, 16);
                            for (const Geom& poly : copper) {
                                if (poly.intersects(holePoint)) continue;
                                minimum = std::min(minimum, holeCircle.distance(poly));
                            }
                        }
                    }
                    if (minimum < 1e9 && minimum < holeCopperMin->value())
                        addResult(item->text(1), tr("Clearance taladro-cobre"), minimum, true);
                }
            }
            if (holeClearCheck->isChecked() && kind == "excellon") {
                const auto eit = excellonLayers_.find(id);
                if (eit != excellonLayers_.end()) {
                    std::vector<std::pair<Pt, double>> holes;
                    for (const auto& [number, tool] : eit->second.tools) {
                        (void)number;
                        for (const Pt& hole : tool.drills)
                            holes.push_back({hole, tool.diameter / 2.0});
                    }
                    double minimum = 1e9;
                    for (std::size_t a = 0; a < holes.size(); ++a) {
                        for (std::size_t b = a + 1; b < holes.size(); ++b) {
                            const double d = std::hypot(holes[a].first.x - holes[b].first.x,
                                                        holes[a].first.y - holes[b].first.y);
                            minimum = std::min(minimum, d - holes[a].second - holes[b].second);
                        }
                    }
                    if (minimum < 1e9 && minimum < holeClearMin->value())
                        addResult(item->text(1), tr("Separación de taladros"), minimum, true);
                }
            }
            if (ringCheck->isChecked() && kind == "excellon") {
                const auto eit = excellonLayers_.find(id);
                if (eit == excellonLayers_.end()) continue;
                double worst = 1e9;
                QString holeLabel;
                for (const auto& [number, tool] : eit->second.tools) {
                    for (const Pt& hole : tool.drills) {
                        for (const auto& [gid, gerber] : gerberLayers_) {
                            if (!gerber.solid.valid() || gerber.solid.empty()) continue;
                            double boundary = 1e9;
                            const Geom point = Geom::point(hole.x, hole.y);
                            for (const PolygonContours& pc : gerber.solid.polygons()) {
                                if (pc.outer.size() >= 2)
                                    boundary = std::min(boundary,
                                        point.distance(Geom::line(pc.outer)));
                                for (const Ring& h : pc.holes)
                                    boundary = std::min(boundary,
                                        point.distance(Geom::line(h)));
                            }
                            if (boundary >= 1e9) continue;
                            const double ring = boundary - tool.diameter / 2.0;
                            if (ring < worst) {
                                worst = ring;
                                holeLabel = tr("T%1 en %2").arg(number).arg(item->text(1));
                            }
                        }
                    }
                }
                if (worst < 1e9 && worst < ringMin->value())
                    addResult(holeLabel, tr("Anillo anular"), worst, true);
            }
        }
        if (table->rowCount() == 0) {
            table->insertRow(0);
            table->setItem(0, 0, new QTableWidgetItem(tr("Sin violaciones")));
        }
    });
    dialog.resize(820, 520);
    dialog.exec();
}

/// @brief Guarda la vista actual como marcador.
void MainWindow::addBookmark() {
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Agregar marcador"), tr("Nombre del marcador:"),
        QLineEdit::Normal, tr("Vista %1").arg(bookmarksMenu_ ? bookmarksMenu_->actions().size() : 1),
        &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    QSettings settings;
    QStringList list = settings.value("bookmarks/list").toStringList();
    list << QString("%1|%2|%3|%4")
                .arg(name.trimmed())
                .arg(canvas_->viewOffset().x(), 0, 'f', 3)
                .arg(canvas_->viewOffset().y(), 0, 'f', 3)
                .arg(canvas_->viewScale(), 0, 'f', 9);
    settings.setValue("bookmarks/list", list);
    rebuildBookmarksMenu();
    statusBar()->showMessage(tr("Marcador guardado: %1").arg(name.trimmed()), 4000);
}

/// @brief Reconstruye el menú de marcadores.
void MainWindow::rebuildBookmarksMenu() {
    if (!bookmarksMenu_) return;
    // Keep the first action (Add) and the separator.
    const auto actions = bookmarksMenu_->actions();
    for (int i = actions.size() - 1; i >= 2; --i) bookmarksMenu_->removeAction(actions[i]);
    const QStringList list = QSettings().value("bookmarks/list").toStringList();
    for (const QString& entry : list) {
        const QStringList parts = entry.split('|');
        if (parts.size() != 4) continue;
        const QString name = parts[0];
        const double x = parts[1].toDouble();
        const double y = parts[2].toDouble();
        const double scale = parts[3].toDouble();
        QAction* action = bookmarksMenu_->addAction(name);
        connect(action, &QAction::triggered, this, [this, x, y, scale] {
            canvas_->setView(scale, QPointF(x, y));
        });
        QAction* remove = bookmarksMenu_->addAction(tr("  Quitar «%1»").arg(name));
        connect(remove, &QAction::triggered, this, [this, entry] {
            QSettings settings;
            QStringList list = settings.value("bookmarks/list").toStringList();
            list.removeAll(entry);
            settings.setValue("bookmarks/list", list);
            rebuildBookmarksMenu();
        });
    }
    if (list.isEmpty()) {
        QAction* empty = bookmarksMenu_->addAction(tr("Sin marcadores"));
        empty->setEnabled(false);
    }
}

/// @brief Agrega un archivo a la lista de recientes.
void MainWindow::recordRecent(const QString& path) {
    if (path.isEmpty()) return;
    const QString absolute = QFileInfo(path).absoluteFilePath();
    QSettings settings;
    QStringList list = settings.value("recent/files").toStringList();
    list.removeAll(absolute);
    list.prepend(absolute);
    const int limit = settings.value("preferences/recentLimit", 12).toInt();
    while (list.size() > std::max(1, limit)) list.removeLast();
    settings.setValue("recent/files", list);
    rebuildRecentMenu();
}

/// @brief Reconstruye el menú de archivos recientes.
void MainWindow::rebuildRecentMenu() {
    if (!recentMenu_) return;
    recentMenu_->clear();
    QSettings settings;
    const QStringList list = settings.value("recent/files").toStringList();
    if (list.isEmpty()) {
        QAction* empty = recentMenu_->addAction(tr("Sin archivos recientes"));
        empty->setEnabled(false);
        return;
    }
    for (const QString& path : list) {
        QAction* action = recentMenu_->addAction(QFileInfo(path).fileName());
        action->setToolTip(path);
        connect(action, &QAction::triggered, this, [this, path] { openFiles({path}); });
    }
    recentMenu_->addSeparator();
    QAction* clear = recentMenu_->addAction(tr("Borrar lista"));
    connect(clear, &QAction::triggered, this, [this] {
        QSettings().remove("recent/files");
        rebuildRecentMenu();
    });
}

/// @brief Limpia la sesión para empezar un proyecto nuevo.
void MainWindow::newProject() {
    lastExcellon_.reset();
    lastGerber_.reset();
    lastIsolation_.reset();
    lastCutout_.reset();
    lastNcc_.reset();
    lastPaint_.reset();
    lastDrillJob_.reset();
    lastMillJob_.reset();
    gerberLayers_.clear();
    excellonLayers_.clear();
    sourcePaths_.clear();
    generatedFrom_.clear();
    projectPath_.clear();
    projectDirty_ = false;
    resetObjectGroups();
    canvas_->clearData();
    info_->clear();
    info_->setPlainText(tr("Proyecto nuevo. Arrastre archivos Gerber o Excellon."));
    setWindowTitle(tr("FlatCAM C++ — Proyecto nuevo"));
    statusBar()->showMessage(tr("Proyecto nuevo"), 3000);
}

/// @brief Exporta las preferencias a JSON.
void MainWindow::saveConfiguration() {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Guardar configuración"),
        QFileInfo(lastDir_).absolutePath() + "/flatcam_config.json",
        tr("Configuración (*.json)"));
    if (path.isEmpty()) return;
    QJsonObject root;
    QSettings settings;
    for (const QString& key : settings.allKeys()) {
        if (key.startsWith("recent/") || key.startsWith("bookmarks/") ||
            key.startsWith("toolsdb/"))
            continue;
        const QVariant value = settings.value(key);
        switch (value.typeId()) {
            case QMetaType::Bool: root[key] = value.toBool(); break;
            case QMetaType::Int:
            case QMetaType::LongLong: root[key] = value.toInt(); break;
            case QMetaType::Double: root[key] = value.toDouble(); break;
            case QMetaType::QStringList: {
                QJsonArray array;
                for (const QString& item : value.toStringList()) array.append(item);
                root[key] = array;
                break;
            }
            default: root[key] = value.toString(); break;
        }
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        showError(tr("No se pudo guardar la configuración."));
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    statusBar()->showMessage(tr("Configuración guardada: %1").arg(QFileInfo(path).fileName()), 5000);
}

/// @brief Importa preferencias desde JSON.
void MainWindow::loadConfiguration() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Abrir configuración"), lastDir_, tr("Configuración (*.json)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        showError(tr("No se pudo abrir la configuración."));
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return;
    QSettings settings;
    const QJsonObject root = doc.object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QJsonValue value = it.value();
        if (value.isBool()) settings.setValue(it.key(), value.toBool());
        else if (value.isDouble()) settings.setValue(it.key(), value.toDouble());
        else if (value.isArray()) {
            QStringList list;
            for (const QJsonValue& item : value.toArray()) list << item.toString();
            settings.setValue(it.key(), list);
        } else {
            const QString text = value.toString();
            if (it.key().endsWith("/bg") || it.key().endsWith("/copper") ||
                it.key().endsWith("/follow") || it.key().endsWith("/toolpath"))
                settings.setValue(it.key(), QColor(text));
            else
                settings.setValue(it.key(), text);
        }
    }
    loadPreferences();
    if (canvas_) canvas_->setColors(background_, copperColor_, followColor_, toolpathColor_);
    refreshProperties();
    statusBar()->showMessage(tr("Configuración cargada"), 5000);
}

/// @brief Marca el proyecto como modificado desde el último guardado.
void MainWindow::markProjectDirty() {
    projectDirty_ = true;
}

/// @brief Permite cerrar sin preguntar (capturas y automatización).
void MainWindow::allowCloseWithoutPrompt() {
    projectDirty_ = false;
}

/**
 * @brief Pregunta si se deben guardar los cambios antes de cerrar.
 *
 * @param event Evento de cierre; se ignora si el usuario cancela o si falla
 *        el guardado.
 */
void MainWindow::closeEvent(QCloseEvent* event) {
    if (!projectDirty_ || objectItems().empty()) {
        QMainWindow::closeEvent(event);
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Salir de FlatCAM C++"),
        tr("Hay archivos o cambios sin guardar.\n¿Querés guardar el proyecto antes de salir?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (answer == QMessageBox::Cancel) {
        event->ignore();
        return;
    }
    if (answer == QMessageBox::Save) {
        QString path = projectPath_;
        if (path.isEmpty()) {
            path = QFileDialog::getSaveFileName(
                this, tr("Guardar proyecto"),
                QFileInfo(lastDir_).absolutePath() + "/proyecto.FlatPrj",
                tr("Proyecto FlatCAM (*.FlatPrj);;Todos (*)"));
        }
        if (path.isEmpty() || !saveProjectToPath(path)) {
            event->ignore();
            return;
        }
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::saveProjectAs() {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Guardar proyecto"), QFileInfo(lastDir_).absolutePath() + "/proyecto.FlatPrj",
        tr("Proyecto FlatCAM (*.FlatPrj);;Todos (*)"));
    if (path.isEmpty()) return;
    if (saveProjectToPath(path))
        statusBar()->showMessage(tr("Proyecto guardado: %1").arg(QFileInfo(path).fileName()), 5000);
}

/// @brief Escribe el proyecto en una ruta concreta.
bool MainWindow::saveProjectToPath(const QString& path) {
    Project project;
    for (QTreeWidgetItem* item : objectItems()) {
        const int id = item->data(0, Qt::UserRole).toInt();
        ProjectObject object;
        object.name = item->text(1).toStdString();
        object.kind = item->text(2).toStdString();
        object.units = defaultUnits_;
        const auto source = sourcePaths_.find(id);
        if (source != sourcePaths_.end()) object.sourceFile = source->second.toStdString();
        object.solids = canvas_->layerSolids(id);
        object.follows = canvas_->layerFollows(id);
        const auto text = objectText_.find(id);
        if (text != objectText_.end()) object.text = text->second.toStdString();
        if (item->text(2).toLower() == "gerber") {
            objectProps_[id][QStringLiteral("__toolTables")] = toolTablesJson();
        }
        const auto props = objectProps_.find(id);
        if (props != objectProps_.end()) {
            QJsonObject json;
            for (const auto& [key, value] : props->second) json[key] = value;
            object.properties = QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString();
        }
        if (object.solids.empty() && object.follows.empty() && object.text.empty()) continue;
        project.objects.push_back(std::move(object));
    }
    project.options = toolTablesJson().toStdString();
    try {
        saveProject(path.toStdString(), project);
        projectPath_ = path;
        projectDirty_ = false;
        return true;
    } catch (const std::exception& e) {
        showError(QString::fromUtf8(e.what()));
        return false;
    }
}

/// @brief Traslada el objeto activo en X/Y.
void MainWindow::moveObject() {
    auto* item = objects_->currentItem();
    if (!item) return;
    const int id = item->data(0, Qt::UserRole).toInt();
    const auto solids = canvas_->layerSolids(id);
    const auto follows = canvas_->layerFollows(id);
    if (solids.empty() && follows.empty()) return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Mover objeto"));
    auto* form = new QFormLayout(&dialog);
    auto* dx = new UnitSpinBox(&dialog);
    dx->setRange(-1000, 1000);
    dx->setDecimals(3);
    dx->setSuffix(" mm");
    auto* dy = new UnitSpinBox(&dialog);
    dy->setRange(-1000, 1000);
    dy->setDecimals(3);
    dy->setSuffix(" mm");
    form->addRow(tr("ΔX:"), dx);
    form->addRow(tr("ΔY:"), dy);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(box);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    std::vector<Geom> afterSolids;
    for (const Geom& g : solids) afterSolids.push_back(g.translate(dx->value(), dy->value()));
    std::vector<Geom> afterFollows;
    for (const Geom& g : follows) afterFollows.push_back(g.translate(dx->value(), dy->value()));
    canvas_->setLayerGeometry(id, afterSolids, afterFollows);
    refreshProperties();
    undoStack_->push(new LambdaCommand(
        [this, id, solids, follows] { canvas_->setLayerGeometry(id, solids, follows); },
        [this, id, afterSolids, afterFollows] {
            canvas_->setLayerGeometry(id, afterSolids, afterFollows);
        },
        tr("Mover objeto")));
}

/// @brief Marca todos los objetos como seleccionados.
void MainWindow::selectAllObjects() {
    for (QTreeWidgetItem* item : objectItems()) item->setSelected(true);
}

/// @brief Limpia la selección.
void MainWindow::deselectAllObjects() {
    for (QTreeWidgetItem* item : objectItems()) item->setSelected(false);
}

/// @brief Serializa las tablas de herramientas a JSON.
QString MainWindow::toolTablesJson() const {
    QJsonObject root;
    auto addTable = [&root](const QString& key, QTableWidget* table) {
        if (!table) return;
        QJsonArray rows;
        for (int row = 0; row < table->rowCount(); ++row) {
            QJsonArray cells;
            for (int col = 0; col < table->columnCount(); ++col) {
                auto* item = table->item(row, col);
                cells.append(item ? item->text() : QString());
            }
            rows.append(cells);
        }
        root[key] = rows;
    };
    addTable("iso", isoTable_);
    addTable("ncc", nccTable_);
    addTable("cut", cutTable_);
    addTable("paint", paintTable_);
    addTable("drill", drillTable_);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

/// @brief Restaura las tablas de herramientas desde JSON.
void MainWindow::loadToolTablesJson(const QString& json) {
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) return;
    const QJsonObject root = doc.object();
    auto applyTable = [&root](const QString& key, QTableWidget* table) {
        if (!table || !root.contains(key) || !root[key].isArray()) return;
        const QJsonArray rows = root[key].toArray();
        table->setRowCount(0);
        for (const QJsonValue& rowValue : rows) {
            const QJsonArray cells = rowValue.toArray();
            const int row = table->rowCount();
            table->insertRow(row);
            for (int col = 0; col < cells.size(); ++col) {
                auto* item = new QTableWidgetItem(cells[col].toString());
                if (col == 0) item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                table->setItem(row, col, item);
            }
        }
    };
    applyTable("iso", isoTable_);
    applyTable("ncc", nccTable_);
    applyTable("cut", cutTable_);
    applyTable("paint", paintTable_);
    applyTable("drill", drillTable_);
}

/// @brief Redondea un valor al paso de rejilla.
double MainWindow::snapValue(double value) const {
    if (!snapGrid_) return value;
    double step = canvas_ ? canvas_->gridStep() : 0.0;
    if (step <= 0.0) step = 1.0;
    return std::round(value / step) * step;
}

/// @brief Formatea una longitud según la unidad activa.
QString MainWindow::formatLength(double mm, int decimals) const {
    if (displayInch_) {
        const int d = decimals >= 0 ? decimals : std::max(2, decimals_ + 1);
        return QString("%1 in").arg(mm / 25.4, 0, 'f', d);
    }
    const int d = decimals >= 0 ? decimals : decimals_;
    return QString("%1 mm").arg(mm, 0, 'f', d);
}

/// @brief Crea un documento o script de texto.
void MainWindow::createTextObject(const QString& kind) {
    bool ok = false;
    const QString title = kind == "script" ? tr("Nuevo script de proyecto")
                                            : tr("Nuevo documento");
    const QString text = QInputDialog::getMultiLineText(
        this, title, kind == "script" ? tr("Contenido del script:") : tr("Contenido:"),
        kind == "script" ? "// Script FlatCAM C++\n" : "", &ok);
    if (!ok) return;
    static int counter = 0;
    ++counter;
    const QString name = kind == "script" ? tr("Script %1").arg(counter)
                                          : tr("Documento %1").arg(counter);
    const int id = canvas_->appendGeometry({}, {}, true);
    canvas_->endGeometry();
    addObjectRow(name, kind, "-", id);
    objectText_[id] = text;
    objectProps_[id][tr("Tipo")] = kind == "script" ? tr("Script") : tr("Documento");
    objects_->setCurrentItem(findObjectItem(id));
    openTextObject(id);
}

/// @brief Abre el editor del objeto de texto.
void MainWindow::openTextObject(int layerId) {
    auto it = objectText_.find(layerId);
    if (it == objectText_.end()) {
        QTreeWidgetItem* item = findObjectItem(layerId);
        if (!item) return;
        it = objectText_.insert({layerId, QString()}).first;
    }
    QTreeWidgetItem* item = findObjectItem(layerId);
    QDialog dialog(this);
    dialog.resize(900, 650);
    dialog.setWindowTitle(item ? item->text(1) : tr("Texto"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* editor = new QPlainTextEdit(&dialog);
    editor->setPlainText(it->second);
    editor->setStyleSheet("QPlainTextEdit { font-family: Menlo; font-size: 12px; }");
    layout->addWidget(editor);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, &dialog);
    if (item && item->text(2).toLower() == "script") {
        auto* run = new QPushButton(tr("Ejecutar script"), &dialog);
        box->addButton(run, QDialogButtonBox::ActionRole);
        connect(run, &QPushButton::clicked, &dialog, [this, editor] {
            const QString temp = QDir::tempPath() + "/flatcam_project_script.js";
            QFile file(temp);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
            file.write(editor->toPlainText().toUtf8());
            file.close();
            runScriptFile(temp);
            QFile::remove(temp);
        });
    }
    layout->addWidget(box);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(box->button(QDialogButtonBox::Save), &QPushButton::clicked, &dialog,
            [this, layerId, editor] {
                objectText_[layerId] = editor->toPlainText();
                statusBar()->showMessage(tr("Texto guardado"), 3000);
            });
    dialog.exec();
}

/// @brief Agrega un CNCJob con su G-code y propiedades.
void MainWindow::addCncJobObject(const QString& name, const QString& code,
                                 const std::map<QString, QString>& props) {
    const int id = canvas_->appendGeometry({}, {}, true);
    canvas_->endGeometry();
    addObjectRow(tr("CNCJob: %1").arg(name), "cncjob", "-", id, true, gcodeGroup_);
    objectText_[id] = code;
    objectProps_[id][tr("Tipo")] = tr("CNCJob");
    objectProps_[id][tr("Líneas")] = QString::number(code.count('\n') + 1);
    objectProps_[id][tr("Caracteres")] = QString::number(code.size());
    for (const auto& [key, value] : props) objectProps_[id][key] = value;
    if (gcodeView_) gcodeView_->setPlainText(code);
}

/// @brief Define el origen de visualización.
void MainWindow::setDisplayOrigin() {
    bool ok = false;
    const QString unit = displayInch_ ? "in" : "mm";
    const int decimals = displayInch_ ? 5 : 3;
    const double x = lengthToMm(QInputDialog::getDouble(
        this, tr("Definir origen"), tr("X (%1):").arg(unit),
        displayLength(canvas_->displayOrigin().x), displayLength(-100000.0),
        displayLength(100000.0), decimals, &ok));
    if (!ok) return;
    const double y = lengthToMm(QInputDialog::getDouble(
        this, tr("Definir origen"), tr("Y (%1):").arg(unit),
        displayLength(canvas_->displayOrigin().y), displayLength(-100000.0),
        displayLength(100000.0), decimals, &ok));
    if (!ok) return;
    canvas_->setDisplayOrigin({x, y});
    statusBar()->showMessage(tr("Origen de visualización: %1, %2")
                                 .arg(lengthText(x), lengthText(y)), 4000);
}

/// @brief Mide distancias entre objetos seleccionados.
void MainWindow::distanceMeasure() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Medir distancia"));
    auto* form = new QFormLayout(&dialog);
    auto* x1 = new UnitSpinBox(&dialog);
    x1->setRange(-100000, 100000);
    x1->setDecimals(3);
    auto* y1 = new UnitSpinBox(&dialog);
    y1->setRange(-100000, 100000);
    y1->setDecimals(3);
    auto* x2 = new UnitSpinBox(&dialog);
    x2->setRange(-100000, 100000);
    x2->setDecimals(3);
    auto* y2 = new UnitSpinBox(&dialog);
    y2->setRange(-100000, 100000);
    y2->setDecimals(3);
    auto* result = new QLabel(tr("—"), &dialog);
    form->addRow(tr("X1:"), x1);
    form->addRow(tr("Y1:"), y1);
    form->addRow(tr("X2:"), x2);
    form->addRow(tr("Y2:"), y2);
    form->addRow(tr("Distancia:"), result);
    auto* draw = new QPushButton(tr("Dibujar"), &dialog);
    form->addRow(draw);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    form->addRow(box);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(draw, &QPushButton::clicked, this, [this, x1, y1, x2, y2, result] {
        const Pt a{snapValue(x1->value()), snapValue(y1->value())};
        const Pt b{snapValue(x2->value()), snapValue(y2->value())};
        const double d = std::hypot(b.x - a.x, b.y - a.y);
        result->setText(QString("%1 mm").arg(d, 0, 'f', 4));
        addGeneratedLayer(tr("Distancia"), {Geom::line({a, b})}, 0);
    });
    dialog.exec();
}

/// @brief Cierra una pestaña de herramienta.
void MainWindow::closeToolTab() {
    if (!leftTabs_) return;
    const int index = leftTabs_->currentIndex();
    if (index >= 3) leftTabs_->removeTab(index);
}

/// @brief Mueve el objeto al origen.
void MainWindow::moveToOrigin() {
    auto* item = objects_->currentItem();
    if (!item) return;
    const int id = item->data(0, Qt::UserRole).toInt();
    const BBox b = canvas_->layerBounds(id);
    if (!b.valid) return;
    const std::vector<Geom> solids = canvas_->layerSolids(id);
    const std::vector<Geom> follows = canvas_->layerFollows(id);
    const double dx = -b.xmin;
    const double dy = -b.ymin;
    std::vector<Geom> afterSolids;
    std::vector<Geom> afterFollows;
    for (const Geom& g : solids) afterSolids.push_back(g.translate(dx, dy));
    for (const Geom& g : follows) afterFollows.push_back(g.translate(dx, dy));
    canvas_->setLayerGeometry(id, afterSolids, afterFollows);
    const auto apply = [this, id](const std::vector<Geom>& s, const std::vector<Geom>& f) {
        canvas_->setLayerGeometry(id, s, f);
        refreshProperties();
    };
    undoStack_->push(new LambdaCommand([apply, solids, follows] { apply(solids, follows); },
                                       [apply, afterSolids, afterFollows] {
                                           apply(afterSolids, afterFollows);
                                       },
                                       tr("Mover a origen")));
    statusBar()->showMessage(tr("Objeto movido al origen"), 4000);
}

/// @brief Une varios objetos en una capa.
void MainWindow::joinObjects() {
    auto* item = objects_->currentItem();
    if (!item) return;
    const int idA = item->data(0, Qt::UserRole).toInt();
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Unir objetos"));
    auto* form = new QFormLayout(&dialog);
    auto* combo = new QComboBox(&dialog);
    std::vector<int> ids;
    for (QTreeWidgetItem* row : objectItems()) {
        const int id = row->data(0, Qt::UserRole).toInt();
        if (id == idA) continue;
        combo->addItem(row->text(1));
        ids.push_back(id);
    }
    if (ids.empty()) {
        QMessageBox::information(this, tr("Unir objetos"),
                                 tr("No hay otro objeto para unir."));
        return;
    }
    form->addRow(tr("Unir con:"), combo);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(box);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    const int idB = ids[static_cast<std::size_t>(combo->currentIndex())];
    std::vector<Geom> solids = canvas_->layerSolids(idA);
    std::vector<Geom> follows = canvas_->layerFollows(idA);
    const auto sb = canvas_->layerSolids(idB);
    const auto fb = canvas_->layerFollows(idB);
    solids.insert(solids.end(), sb.begin(), sb.end());
    follows.insert(follows.end(), fb.begin(), fb.end());
    const int id = canvas_->appendGeometry(solids, follows, true);
    canvas_->endGeometry();
    addObjectRow(tr("Unión de objetos"), "herramienta", "-", id, true, toolsGroup_);
    statusBar()->showMessage(tr("Objetos unidos"), 4000);
}

/// @brief Separa un objeto en sus componentes.
void MainWindow::explodeObject() {
    auto* item = objects_->currentItem();
    if (!item) return;
    const int id = item->data(0, Qt::UserRole).toInt();
    const std::vector<Geom> solids = canvas_->layerSolids(id);
    const std::vector<Geom> follows = canvas_->layerFollows(id);
    if (solids.size() + follows.size() < 2) {
        QMessageBox::information(this, tr("Explotar objeto"),
                                 tr("El objeto no tiene varias primitivas."));
        return;
    }
    int index = 1;
    for (const Geom& g : solids) {
        const int newId = canvas_->appendGeometry({g}, {}, true);
        addObjectRow(tr("%1 · %2").arg(item->text(1)).arg(index++), "herramienta", "-",
                     newId, true, toolsGroup_);
    }
    for (const Geom& g : follows) {
        const int newId = canvas_->appendGeometry({}, {g}, true);
        addObjectRow(tr("%1 · %2").arg(item->text(1)).arg(index++), "herramienta", "-",
                     newId, true, toolsGroup_);
    }
    canvas_->endGeometry();
    statusBar()->showMessage(tr("Objeto explotado en %1 capas").arg(index - 1), 4000);
}

/// @brief Centra la vista en coordenadas X/Y.
void MainWindow::jumpToLocation() {
    bool ok = false;
    const QString unit = displayInch_ ? "in" : "mm";
    const int decimals = displayInch_ ? 5 : 3;
    const double x = lengthToMm(QInputDialog::getDouble(
        this, tr("Ir a coordenadas"), tr("X (%1):").arg(unit), 0.0,
        displayLength(-100000.0), displayLength(100000.0), decimals, &ok));
    if (!ok) return;
    const double y = lengthToMm(QInputDialog::getDouble(
        this, tr("Ir a coordenadas"), tr("Y (%1):").arg(unit), 0.0,
        displayLength(-100000.0), displayLength(100000.0), decimals, &ok));
    if (!ok) return;
    const double sx = snapValue(x);
    const double sy = snapValue(y);
    canvas_->centerOn(sx, sy);
    statusBar()->showMessage(tr("Vista centrada en %1, %2").arg(sx).arg(sy), 3000);
}

/// @brief Alinea objetos contra el origen o entre sí.
void MainWindow::alignObjects() {
    auto* item = objects_->currentItem();
    if (!item) return;
    const int id = item->data(0, Qt::UserRole).toInt();
    const std::vector<Geom> solids = canvas_->layerSolids(id);
    const BBox source = canvas_->layerBounds(id);
    if (solids.empty() || !source.valid) {
        QMessageBox::information(this, tr("Alinear"), tr("El objeto no tiene geometría editable."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Alinear objeto"));
    auto* form = new QFormLayout(&dialog);
    auto* reference = new QComboBox(&dialog);
    reference->addItem(tr("Origen (0, 0)"), -1);
    std::vector<int> ids;
    for (const int other : canvas_->layerIds()) {
        if (other == id || canvas_->layerSolids(other).empty()) continue;
        for (QTreeWidgetItem* candidate : objectItems()) {
            if (candidate->data(0, Qt::UserRole).toInt() == other) {
                reference->addItem(candidate->text(1), other);
                ids.push_back(other);
                break;
            }
        }
    }
    auto* mode = new QComboBox(&dialog);
    mode->addItems({tr("Izquierda"), tr("Centro horizontal"), tr("Derecha"),
                    tr("Arriba"), tr("Centro vertical"), tr("Abajo")});
    form->addRow(tr("Referencia:"), reference);
    form->addRow(tr("Alineación:"), mode);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(box);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    BBox ref{0, 0, 0, 0};
    const int refId = reference->currentData().toInt();
    if (refId >= 0) {
        ref = canvas_->layerBounds(refId);
    }
    if (!ref.valid) return;

    double dx = 0.0;
    double dy = 0.0;
    switch (mode->currentIndex()) {
        case 0: dx = ref.xmin - source.xmin; break;
        case 1: dx = (ref.xmin + ref.xmax) / 2.0 - (source.xmin + source.xmax) / 2.0; break;
        case 2: dx = ref.xmax - source.xmax; break;
        case 3: dy = ref.ymax - source.ymax; break;
        case 4: dy = (ref.ymin + ref.ymax) / 2.0 - (source.ymin + source.ymax) / 2.0; break;
        default: dy = ref.ymin - source.ymin; break;
    }

    std::vector<Geom> after;
    after.reserve(solids.size());
    for (const Geom& g : solids) after.push_back(g.translate(dx, dy));
    const auto before = solids;
    canvas_->setLayerGeometry(id, after, canvas_->layerFollows(id));
    refreshProperties();
    undoStack_->push(new LambdaCommand(
        [this, id, before] { canvas_->setLayerGeometry(id, before, canvas_->layerFollows(id)); },
        [this, id, after] { canvas_->setLayerGeometry(id, after, canvas_->layerFollows(id)); },
        tr("Alinear objeto")));
}

/// @brief Crea una copia espejada para doble faz.
void MainWindow::doubleSided() {
    auto* item = objects_->currentItem();
    if (!item) return;
    const int id = item->data(0, Qt::UserRole).toInt();
    const std::vector<Geom> solids = canvas_->layerSolids(id);
    const BBox b = canvas_->layerBounds(id);
    if (solids.empty() || !b.valid) {
        QMessageBox::information(this, tr("Doble faz"),
                                 tr("El objeto no tiene geometría editable."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Doble faz (espejar)"));
    auto* form = new QFormLayout(&dialog);
    auto* axis = new QComboBox(&dialog);
    axis->addItems({tr("Espejar horizontal (X)"), tr("Espejar vertical (Y)")});
    auto* offsetX = new UnitSpinBox(&dialog);
    offsetX->setRange(-10000, 10000);
    offsetX->setDecimals(3);
    offsetX->setValue(b.width());
    offsetX->setSuffix(" mm");
    auto* offsetY = new UnitSpinBox(&dialog);
    offsetY->setRange(-10000, 10000);
    offsetY->setDecimals(3);
    offsetY->setValue(0.0);
    offsetY->setSuffix(" mm");
    form->addRow(tr("Eje:"), axis);
    form->addRow(tr("Desplazamiento X:"), offsetX);
    form->addRow(tr("Desplazamiento Y:"), offsetY);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(box);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    const double cx = (b.xmin + b.xmax) / 2.0;
    const double cy = (b.ymin + b.ymax) / 2.0;
    const bool mirrorX = axis->currentIndex() == 0;
    std::vector<Geom> out;
    out.reserve(solids.size());
    for (const Geom& g : solids) {
        Geom t = mirrorX ? g.scale(-1.0, 1.0, {cx, cy}) : g.scale(1.0, -1.0, {cx, cy});
        t = t.translate(offsetX->value(), offsetY->value());
        out.push_back(t);
    }
    const int newId = canvas_->appendGeometry(out, {}, true);
    canvas_->endGeometry();
    addObjectRow(tr("Doble faz (espejo)"), "gerber", "-", newId, true, toolsGroup_);
    statusBar()->showMessage(tr("Copia espejada creada"), 4000);
}

/// @brief Regenera el Gerber con nuevos tamaños de apertura.
bool MainWindow::applyApertureSizes(int layerId, const std::map<QString, double>& sizes) {
    const auto source = sourcePaths_.find(layerId);
    if (source == sourcePaths_.end()) {
        QMessageBox::information(
            this, tr("Aperturas"),
            tr("Este objeto no tiene un archivo Gerber de origen. Guardalo como Gerber "
               "y volvé a abrirlo para editar sus aperturas."));
        return false;
    }
    QFile file(source->second);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        showError(tr("No se pudo leer el Gerber de origen."));
        return false;
    }
    std::string text = file.readAll().toStdString();
    file.close();
    int changed = 0;
    for (const auto& [code, diameter] : sizes) {
        int count = 0;
        text = editApertureSize(text, code.toStdString(), diameter, 4, &count);
        changed += count;
    }
    if (changed == 0) {
        QMessageBox::information(this, tr("Aperturas"),
                                 tr("No se encontraron definiciones ADD de esos círculos."));
        return false;
    }

    try {
        GerberOptions opt;
        opt.appUnits = defaultUnits_;
        opt.stepsPerCircle = stepsPerCircle_;
        const GerberResult fresh = GerberParser(opt).parseString(text);
        const GerberResult before = gerberLayers_[layerId];
        const GerberResult after = fresh;

        auto apply = [this, layerId](const GerberResult& result) {
            gerberLayers_[layerId] = result;
            canvas_->setLayerGeometry(layerId, result.solids, result.follows);
            if (QTreeWidgetItem* item = findObjectItem(layerId))
                item->setText(3, QString::number(result.solid.area(), 'f', 2));
            refreshProperties();
        };
        apply(after);
        lastGerber_ = after;
        undoStack_->push(new LambdaCommand([apply, before] { apply(before); },
                                           [apply, after] { apply(after); },
                                           tr("Editar aperturas")));
        statusBar()->showMessage(tr("Aperturas actualizadas"), 5000);
        return true;
    } catch (const std::exception& e) {
        showError(QString::fromUtf8(e.what()));
        return false;
    }
}

/// @brief Regenera el Gerber con una macro editada.
bool MainWindow::applyMacroDefinition(int layerId, const QString& name, const QString& raw) {
    const auto source = sourcePaths_.find(layerId);
    if (source == sourcePaths_.end()) {
        QMessageBox::information(this, tr("Macros"),
                                 tr("Este objeto no tiene un Gerber de origen editable."));
        return false;
    }
    QFile file(source->second);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QString text = QString::fromUtf8(file.readAll());
    file.close();
    const QRegularExpression re("%AM" + QRegularExpression::escape(name) + "\\*([^%]*)%");
    if (!text.contains(re)) {
        QMessageBox::information(this, tr("Macros"),
                                 tr("No se encontró la definición de la macro en el archivo."));
        return false;
    }
    text.replace(re, "%AM" + name + "*" + raw + "%");
    try {
        GerberOptions opt;
        opt.appUnits = defaultUnits_;
        opt.stepsPerCircle = stepsPerCircle_;
        const GerberResult fresh = GerberParser(opt).parseString(text.toStdString());
        const GerberResult before = gerberLayers_[layerId];
        const GerberResult after = fresh;
        auto apply = [this, layerId](const GerberResult& result) {
            gerberLayers_[layerId] = result;
            canvas_->setLayerGeometry(layerId, result.solids, result.follows);
            if (QTreeWidgetItem* item = findObjectItem(layerId))
                item->setText(3, QString::number(result.solid.area(), 'f', 2));
            refreshProperties();
        };
        apply(after);
        lastGerber_ = after;
        undoStack_->push(new LambdaCommand([apply, before] { apply(before); },
                                           [apply, after] { apply(after); },
                                           tr("Editar macro")));
        statusBar()->showMessage(tr("Macro actualizada"), 5000);
        return true;
    } catch (const std::exception& e) {
        showError(QString::fromUtf8(e.what()));
        return false;
    }
}

/// @brief Abre el editor de primitivas Gerber.
void MainWindow::gerberEditor() {
    auto* item = objects_->currentItem();
    if (!item) {
        QMessageBox::information(this, tr("Editor Gerber"), tr("Seleccione un objeto."));
        return;
    }
    const int id = item->data(0, Qt::UserRole).toInt();
    std::vector<Geom> solids = canvas_->layerSolids(id);
    std::vector<Geom> follows = canvas_->layerFollows(id);
    if (solids.empty() && follows.empty()) {
        QMessageBox::information(this, tr("Editor Gerber"),
                                 tr("El objeto no tiene geometría editable."));
        return;
    }

    QDialog dialog(this);
    dialog.resize(900, 650);
    dialog.setWindowTitle(tr("Editor Gerber — %1").arg(item->text(0)));
    auto* layout = new QHBoxLayout(&dialog);

    auto* list = new QListWidget(&dialog);
    list->setMinimumWidth(280);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(list);

    auto* right = new QWidget(&dialog);
    auto* form = new QFormLayout(right);
    layout->addWidget(right);

    auto* info = new QLabel(tr("Seleccione una primitiva"), right);
    info->setWordWrap(true);
    form->addRow(info);

    auto* dx = new UnitSpinBox(right);
    dx->setRange(-1000, 1000);
    dx->setDecimals(3);
    dx->setSuffix(" mm");
    auto* dy = new UnitSpinBox(right);
    dy->setRange(-1000, 1000);
    dy->setDecimals(3);
    dy->setSuffix(" mm");
    form->addRow(tr("Mover X:"), dx);
    form->addRow(tr("Mover Y:"), dy);
    auto* move = new QPushButton(tr("Mover"), right);
    auto* remove = new QPushButton(tr("Eliminar"), right);
    auto* duplicate = new QPushButton(tr("Duplicar"), right);
    auto* unite = new QPushButton(tr("Unir seleccionadas"), right);
    auto* subtractSel = new QPushButton(tr("Restar seleccionadas"), right);
    auto* intersectSel = new QPushButton(tr("Intersección seleccionadas"), right);
    auto* undoEdit = new QPushButton(tr("Deshacer edición"), right);
    auto* redoEdit = new QPushButton(tr("Rehacer edición"), right);
    form->addRow(move);
    form->addRow(remove);
    form->addRow(duplicate);
    form->addRow(unite);
    form->addRow(subtractSel);
    form->addRow(intersectSel);
    form->addRow(undoEdit);
    form->addRow(redoEdit);
    auto* rotateAngle = new UnitSpinBox(right);
    rotateAngle->setRange(-360.0, 360.0);
    rotateAngle->setDecimals(2);
    rotateAngle->setSuffix(" °");
    auto* rotateButton = new QPushButton(tr("Rotar primitiva"), right);
    form->addRow(tr("Rotar:"), rotateAngle);
    form->addRow(rotateButton);
    auto* scaleFactor = new UnitSpinBox(right);
    scaleFactor->setRange(0.01, 100.0);
    scaleFactor->setDecimals(4);
    scaleFactor->setValue(1.0);
    auto* scaleButton = new QPushButton(tr("Escalar primitiva"), right);
    form->addRow(tr("Escala:"), scaleFactor);
    form->addRow(scaleButton);
    auto* simplifyTol = new UnitSpinBox(right);
    simplifyTol->setRange(0.0001, 10.0);
    simplifyTol->setDecimals(4);
    simplifyTol->setValue(0.01);
    auto* simplifyButton = new QPushButton(tr("Simplificar primitiva"), right);
    form->addRow(tr("Tolerancia:"), simplifyTol);
    form->addRow(simplifyButton);
    auto* bufferDist = new UnitSpinBox(right);
    bufferDist->setRange(-10.0, 10.0);
    bufferDist->setDecimals(3);
    bufferDist->setValue(0.0);
    auto* bufferButton = new QPushButton(tr("Buffer de primitiva"), right);
    form->addRow(tr("Buffer:"), bufferDist);
    form->addRow(bufferButton);
    auto* aperturesButton = new QPushButton(tr("Aperturas del Gerber..."), right);
    form->addRow(aperturesButton);

    auto* addGroup = new QGroupBox(tr("Agregar primitiva"), right);
    auto* addForm = new QFormLayout(addGroup);
    auto* kind = new QComboBox(addGroup);
    kind->addItems({tr("Círculo"), tr("Rectángulo"), tr("Línea"), tr("Polígono"), tr("Texto"),
                    tr("Arco")});
    auto* px = new UnitSpinBox(addGroup);
    px->setRange(-10000, 10000);
    px->setDecimals(3);
    auto* py = new UnitSpinBox(addGroup);
    py->setRange(-10000, 10000);
    py->setDecimals(3);
    auto* pw = new UnitSpinBox(addGroup);
    pw->setRange(0.01, 1000);
    pw->setDecimals(3);
    pw->setValue(1.0);
    auto* ph = new UnitSpinBox(addGroup);
    ph->setRange(0.01, 1000);
    ph->setDecimals(3);
    ph->setValue(1.0);
    auto* textEntry = new QLineEdit(tr("FlatCAM"), addGroup);
    auto* add = new QPushButton(tr("Agregar"), addGroup);
    addForm->addRow(tr("Tipo:"), kind);
    addForm->addRow(tr("Texto:"), textEntry);
    addForm->addRow(tr("X / X1:"), px);
    addForm->addRow(tr("Y / Y1:"), py);
    addForm->addRow(tr("Diámetro / ancho:"), pw);
    addForm->addRow(tr("Alto / X2 / lados:"), ph);
    addForm->addRow(add);
    form->addRow(addGroup);

    auto* saveGerber = new QPushButton(tr("Guardar como Gerber..."), right);
    form->addRow(saveGerber);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(box);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto refresh = [&] {
        const int current = list->currentRow();
        list->clear();
        for (std::size_t i = 0; i < solids.size(); ++i)
            list->addItem(tr("Sólido %1  (área %2)").arg(i).arg(solids[i].area(), 0, 'f', 3));
        for (std::size_t i = 0; i < follows.size(); ++i)
            list->addItem(tr("Recorrido %1  (largo %2)").arg(i).arg(follows[i].length(), 0, 'f', 3));
        if (current >= 0 && current < list->count()) list->setCurrentRow(current);
    };
    refresh();

    std::vector<std::pair<std::vector<Geom>, std::vector<Geom>>> history;
    int historyPos = -1;
    auto pushHistory = [&] {
        history.resize(static_cast<std::size_t>(historyPos + 1));
        history.emplace_back(solids, follows);
        historyPos = static_cast<int>(history.size()) - 1;
    };
    auto restoreHistory = [&](int pos) {
        if (pos < 0 || pos >= static_cast<int>(history.size())) return;
        historyPos = pos;
        solids = history[static_cast<std::size_t>(pos)].first;
        follows = history[static_cast<std::size_t>(pos)].second;
        refresh();
    };
    pushHistory();
    connect(undoEdit, &QPushButton::clicked, this, [&] {
        if (historyPos > 0) restoreHistory(historyPos - 1);
    });
    connect(redoEdit, &QPushButton::clicked, this, [&] {
        if (historyPos + 1 < static_cast<int>(history.size())) restoreHistory(historyPos + 1);
    });

    auto selected = [&]() -> int { return list->currentRow(); };
    auto isSolid = [&](int index) { return index >= 0 && index < static_cast<int>(solids.size()); };

    connect(list, &QListWidget::currentRowChanged, this, [&](int row) {
        if (row < 0) {
            info->setText(tr("Seleccione una primitiva"));
            return;
        }
        if (isSolid(row)) {
            const BBox b = solids[row].bounds();
            info->setText(tr("Sólido %1\nÁrea: %2\nBounds: %3, %4 .. %5, %6")
                              .arg(row).arg(solids[row].area(), 0, 'f', 3)
                              .arg(b.xmin, 0, 'f', 3).arg(b.ymin, 0, 'f', 3)
                              .arg(b.xmax, 0, 'f', 3).arg(b.ymax, 0, 'f', 3));
        } else {
            const int i = row - static_cast<int>(solids.size());
            if (i >= 0 && i < static_cast<int>(follows.size()))
                info->setText(tr("Recorrido %1\nLargo: %2").arg(i).arg(follows[i].length(), 0, 'f', 3));
        }
    });

    connect(move, &QPushButton::clicked, this, [&] {
        pushHistory();
        const int row = selected();
        if (row < 0) return;
        if (isSolid(row)) solids[row] = solids[row].translate(dx->value(), dy->value());
        else {
            const int i = row - static_cast<int>(solids.size());
            if (i >= 0 && i < static_cast<int>(follows.size()))
                follows[i] = follows[i].translate(dx->value(), dy->value());
        }
        refresh();
    });
    connect(remove, &QPushButton::clicked, this, [&] {
        pushHistory();
        auto rows = list->selectionModel()->selectedRows();
        std::vector<int> indices;
        for (const QModelIndex& index : rows) indices.push_back(index.row());
        std::sort(indices.begin(), indices.end(), std::greater<int>());
        for (int row : indices) {
            if (isSolid(row)) solids.erase(solids.begin() + row);
            else {
                const int i = row - static_cast<int>(solids.size());
                if (i >= 0 && i < static_cast<int>(follows.size()))
                    follows.erase(follows.begin() + i);
            }
        }
        refresh();
    });
    connect(duplicate, &QPushButton::clicked, this, [&] {
        pushHistory();
        auto rows = list->selectionModel()->selectedRows();
        std::sort(rows.begin(), rows.end(), [](const QModelIndex& a, const QModelIndex& b) {
            return a.row() < b.row();
        });
        std::vector<Geom> newSolids;
        std::vector<Geom> newFollows;
        for (const QModelIndex& index : rows) {
            const int row = index.row();
            if (isSolid(row)) newSolids.push_back(solids[row].translate(2.0, 2.0));
            else {
                const int i = row - static_cast<int>(solids.size());
                if (i >= 0 && i < static_cast<int>(follows.size()))
                    newFollows.push_back(follows[i].translate(2.0, 2.0));
            }
        }
        solids.insert(solids.end(), newSolids.begin(), newSolids.end());
        follows.insert(follows.end(), newFollows.begin(), newFollows.end());
        refresh();
    });
    connect(unite, &QPushButton::clicked, this, [&] {
        pushHistory();
        auto rows = list->selectionModel()->selectedRows();
        std::vector<int> indices;
        for (const QModelIndex& index : rows)
            if (isSolid(index.row())) indices.push_back(index.row());
        if (indices.size() < 2) {
            QMessageBox::information(&dialog, tr("Unir"),
                                     tr("Seleccioná dos o más sólidos."));
            return;
        }
        std::sort(indices.begin(), indices.end(), std::greater<int>());
        std::vector<Geom> selectedGeoms;
        for (int row : indices) selectedGeoms.push_back(solids[row]);
        const Geom united = Geom::unionOf(selectedGeoms);
        for (int row : indices) solids.erase(solids.begin() + row);
        solids.push_back(united);
        refresh();
    });
    connect(rotateButton, &QPushButton::clicked, this, [&] {
        pushHistory();
        const int row = selected();
        if (row < 0) return;
        if (isSolid(row)) {
            const BBox b = solids[row].bounds();
            solids[row] = solids[row].rotateDeg(rotateAngle->value(),
                                                {(b.xmin + b.xmax) / 2, (b.ymin + b.ymax) / 2});
        } else {
            const int i = row - static_cast<int>(solids.size());
            if (i >= 0 && i < static_cast<int>(follows.size())) {
                const BBox b = follows[i].bounds();
                follows[i] = follows[i].rotateDeg(rotateAngle->value(),
                                                  {(b.xmin + b.xmax) / 2, (b.ymin + b.ymax) / 2});
            }
        }
        refresh();
    });
    connect(scaleButton, &QPushButton::clicked, this, [&] {
        pushHistory();
        const int row = selected();
        if (row < 0) return;
        const double factor = scaleFactor->value();
        if (isSolid(row)) {
            const BBox b = solids[row].bounds();
            solids[row] = solids[row].scale(factor, factor,
                                            {(b.xmin + b.xmax) / 2, (b.ymin + b.ymax) / 2});
        } else {
            const int i = row - static_cast<int>(solids.size());
            if (i >= 0 && i < static_cast<int>(follows.size())) {
                const BBox b = follows[i].bounds();
                follows[i] = follows[i].scale(factor, factor,
                                              {(b.xmin + b.xmax) / 2, (b.ymin + b.ymax) / 2});
            }
        }
        refresh();
    });
    connect(subtractSel, &QPushButton::clicked, this, [&] {
        pushHistory();
        auto rows = list->selectionModel()->selectedRows();
        std::vector<int> indices;
        for (const QModelIndex& index : rows)
            if (isSolid(index.row())) indices.push_back(index.row());
        if (indices.size() < 2) {
            QMessageBox::information(&dialog, tr("Restar"), tr("Seleccioná dos o más sólidos."));
            return;
        }
        std::sort(indices.begin(), indices.end());
        Geom result = solids[indices.front()];
        for (std::size_t i = 1; i < indices.size(); ++i)
            result = result.difference(solids[indices[i]]);
        std::sort(indices.begin(), indices.end(), std::greater<int>());
        for (int row : indices) solids.erase(solids.begin() + row);
        solids.push_back(result);
        refresh();
    });
    connect(intersectSel, &QPushButton::clicked, this, [&] {
        pushHistory();
        auto rows = list->selectionModel()->selectedRows();
        std::vector<int> indices;
        for (const QModelIndex& index : rows)
            if (isSolid(index.row())) indices.push_back(index.row());
        if (indices.size() < 2) {
            QMessageBox::information(&dialog, tr("Intersección"),
                                     tr("Seleccioná dos o más sólidos."));
            return;
        }
        std::sort(indices.begin(), indices.end());
        Geom result = solids[indices.front()];
        for (std::size_t i = 1; i < indices.size(); ++i)
            result = result.intersection(solids[indices[i]]);
        std::sort(indices.begin(), indices.end(), std::greater<int>());
        for (int row : indices) solids.erase(solids.begin() + row);
        solids.push_back(result);
        refresh();
    });
    connect(simplifyButton, &QPushButton::clicked, this, [&] {
        pushHistory();
        const int row = selected();
        if (row < 0) return;
        if (isSolid(row)) solids[row] = solids[row].simplify(simplifyTol->value()).fixed();
        else {
            const int i = row - static_cast<int>(solids.size());
            if (i >= 0 && i < static_cast<int>(follows.size()))
                follows[i] = follows[i].simplify(simplifyTol->value());
        }
        refresh();
    });
    connect(bufferButton, &QPushButton::clicked, this, [&] {
        pushHistory();
        const int row = selected();
        if (row < 0) return;
        const double distance = bufferDist->value();
        if (std::fabs(distance) < 1e-9) return;
        if (isSolid(row)) solids[row] = solids[row].buffer(distance, 16);
        else {
            const int i = row - static_cast<int>(solids.size());
            if (i >= 0 && i < static_cast<int>(follows.size()))
                follows[i] = follows[i].buffer(distance, 16);
        }
        refresh();
    });
    connect(aperturesButton, &QPushButton::clicked, this, [this, id] {
        QDialog apertures(this);
        apertures.setWindowTitle(tr("Aperturas del Gerber"));
        auto* layout = new QVBoxLayout(&apertures);
        auto* note = new QLabel(tr("Los círculos, rectángulos, obrounds y polígonos se pueden "
                                   "editar: cambiá el tamaño y presioná «Aplicar cambios». "
                                   "Macros y regiones son informativos."),
                                &apertures);
        note->setWordWrap(true);
        layout->addWidget(note);
        auto* table = new QTableWidget(0, 4, &apertures);
        table->setHorizontalHeaderLabels({tr("Código"), tr("Tipo"), tr("Tamaño"), tr("Dibujos")});
        table->horizontalHeader()->setStretchLastSection(true);
        table->verticalHeader()->setVisible(false);
        const auto it = gerberLayers_.find(id);
        if (it != gerberLayers_.end()) {
            for (const auto& [code, aperture] : it->second.apertures) {
                const int row = table->rowCount();
                table->insertRow(row);
                QString kindText = "?";
                switch (aperture.kind) {
                    case GerberAperture::Kind::Circle: kindText = tr("Círculo"); break;
                    case GerberAperture::Kind::Rectangle: kindText = tr("Rectángulo"); break;
                    case GerberAperture::Kind::Obround: kindText = tr("Obround"); break;
                    case GerberAperture::Kind::Polygon: kindText = tr("Polígono"); break;
                    case GerberAperture::Kind::Macro: kindText = tr("Macro"); break;
                    case GerberAperture::Kind::Region: kindText = tr("Región"); break;
                }
                double size = aperture.size;
                if (size <= 0.0) size = std::max(aperture.width, aperture.height);
                auto* codeItem = new QTableWidgetItem(QString::fromStdString(code));
                codeItem->setData(Qt::UserRole, size);
                table->setItem(row, 0, codeItem);
                table->setItem(row, 1, new QTableWidgetItem(kindText));
                auto* sizeItem = new QTableWidgetItem(QString::number(size, 'f', 4));
                if (aperture.kind == GerberAperture::Kind::Macro ||
                    aperture.kind == GerberAperture::Kind::Region)
                    sizeItem->setFlags(sizeItem->flags() & ~Qt::ItemIsEditable);
                table->setItem(row, 2, sizeItem);
                table->setItem(row, 3, new QTableWidgetItem(
                    QString::number(aperture.geometry.size())));
            }
        }
        layout->addWidget(table);
        auto* apply = new QPushButton(tr("Aplicar cambios"), &apertures);
        layout->addWidget(apply);
        auto* macrosButton = new QPushButton(tr("Editar macros..."), &apertures);
        layout->addWidget(macrosButton);
        auto* box = new QDialogButtonBox(QDialogButtonBox::Close, &apertures);
        layout->addWidget(box);
        connect(box, &QDialogButtonBox::rejected, &apertures, &QDialog::reject);
        connect(macrosButton, &QPushButton::clicked, this, [this, id, &apertures] {
            const auto it = gerberLayers_.find(id);
            if (it == gerberLayers_.end() || it->second.macros.empty()) {
                QMessageBox::information(&apertures, tr("Macros"),
                                         tr("Este Gerber no tiene macros."));
                return;
            }
            QDialog macroDialog(&apertures);
            macroDialog.setWindowTitle(tr("Macros del Gerber"));
            auto* layout = new QVBoxLayout(&macroDialog);
            auto* combo = new QComboBox(&macroDialog);
            for (const auto& [name, macro] : it->second.macros) combo->addItem(QString::fromStdString(name));
            auto* editor = new QPlainTextEdit(&macroDialog);
            editor->setStyleSheet("QPlainTextEdit { font-family: Menlo; font-size: 12px; }");
            auto update = [&] {
                const QString name = combo->currentText();
                const auto macro = it->second.macros.find(name.toStdString());
                editor->setPlainText(macro != it->second.macros.end()
                                         ? QString::fromStdString(macro->second.raw)
                                         : QString());
            };
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), &macroDialog,
                    [&](int) { update(); });
            update();
            layout->addWidget(combo);
            layout->addWidget(editor);
            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                                 &macroDialog);
            layout->addWidget(buttons);
            connect(buttons, &QDialogButtonBox::accepted, &macroDialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &macroDialog, &QDialog::reject);
            macroDialog.resize(700, 500);
            if (macroDialog.exec() == QDialog::Accepted) {
                if (applyMacroDefinition(id, combo->currentText(), editor->toPlainText()))
                    apertures.accept();
            }
        });
        connect(apply, &QPushButton::clicked, this, [this, id, table, &apertures] {
            std::map<QString, double> sizes;
            for (int row = 0; row < table->rowCount(); ++row) {
                auto* codeItem = table->item(row, 0);
                auto* sizeItem = table->item(row, 2);
                if (!codeItem || !sizeItem) continue;
                if (!(sizeItem->flags() & Qt::ItemIsEditable)) continue;
                const double oldSize = codeItem->data(Qt::UserRole).toDouble();
                const double newSize = sizeItem->text().toDouble();
                if (newSize <= 0.0 || std::fabs(newSize - oldSize) < 1e-9) continue;
                sizes[codeItem->text()] = newSize;
            }
            if (sizes.empty()) {
                apertures.accept();
                return;
            }
            if (applyApertureSizes(id, sizes)) apertures.accept();
        });
        apertures.resize(680, 440);
        apertures.exec();
    });

    connect(add, &QPushButton::clicked, this, [&] {
        pushHistory();
        const int type = kind->currentIndex();
        if (type == 0) {
            solids.push_back(Geom::circle({px->value(), py->value()}, pw->value() / 2.0, 32));
        } else if (type == 1) {
            solids.push_back(Geom::box(px->value(), py->value(),
                                       px->value() + pw->value(), py->value() + ph->value()));
        } else if (type == 2) {
            solids.push_back(Geom::line({{px->value(), py->value()},
                                         {ph->value(), py->value()}})
                                 .buffer(pw->value() / 2.0, 8));
        } else if (type == 3) {
            const int sides = std::clamp(static_cast<int>(ph->value()), 3, 64);
            solids.push_back(Geom::regularPolygon({px->value(), py->value()},
                                                  pw->value(), sides, 0.0));
        } else if (type == 4) {
            QPainterPath path;
            QFont font("Helvetica");
            font.setPixelSize(100);
            path.addText(0, 0, font, textEntry->text().isEmpty() ? "FlatCAM" : textEntry->text());
            if (path.isEmpty()) {
                refresh();
                return;
            }
            const QRectF br = path.boundingRect();
            if (br.height() <= 0.0) return;
            const double scale = pw->value() / br.height();
            QTransform transform;
            transform.translate(px->value(), py->value() - br.height() * scale);
            transform.scale(scale, -scale);
            const QPainterPath mapped = transform.map(path);
            const QList<QPolygonF> polys = mapped.toSubpathPolygons();
            for (const QPolygonF& poly : polys) {
                if (poly.size() < 3) continue;
                Ring ring;
                ring.reserve(static_cast<std::size_t>(poly.size()));
                for (const QPointF& pt : poly) ring.push_back({pt.x(), pt.y()});
                solids.push_back(Geom::polygon(ring));
            }
        }
        if (type == 5) {
            const double radius = pw->value() / 2.0;
            const double cx = px->value();
            const double cy = py->value();
            Ring arc;
            for (int step = 0; step <= 36; ++step) {
                const double angle = M_PI * step / 36.0;
                arc.push_back({cx + radius * std::cos(angle), cy + radius * std::sin(angle)});
            }
            follows.push_back(Geom::line(arc));
        }
        refresh();
    });
    connect(saveGerber, &QPushButton::clicked, this, [&] {
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Guardar como Gerber"), QFileInfo(lastDir_).absolutePath() + "/edited.gbr",
            tr("Gerber (*.gbr *.gtl *.gbl);;Todos (*)"));
        if (path.isEmpty()) return;
        try {
            writeGerber(path.toStdString(), solids, follows, defaultUnits_);
            statusBar()->showMessage(tr("Gerber guardado: %1").arg(QFileInfo(path).fileName()), 5000);
        } catch (const std::exception& e) {
            showError(QString::fromUtf8(e.what()));
        }
    });

    if (dialog.exec() != QDialog::Accepted) return;

    const std::vector<Geom> beforeSolids = canvas_->layerSolids(id);
    const std::vector<Geom> beforeFollows = canvas_->layerFollows(id);

    GerberResult r;
    r.solids = solids;
    r.follows = follows;
    r.solid = Geom::unionOf(solids);
    r.bounds = r.solid.bounds();
    r.fileUnits = defaultUnits_;
    gerberLayers_[id] = r;
    lastGerber_ = r;
    canvas_->setLayerGeometry(id, solids, follows);
    item->setText(2, QString::number(r.solid.area(), 'f', 2));
    refreshProperties();

    auto apply = [this, id](const std::vector<Geom>& s, const std::vector<Geom>& f) {
        canvas_->setLayerGeometry(id, s, f);
        refreshProperties();
    };
    undoStack_->push(new LambdaCommand([apply, beforeSolids, beforeFollows] {
                                          apply(beforeSolids, beforeFollows);
                                      },
                                      [apply, solids, follows] { apply(solids, follows); },
                                      tr("Editar Gerber")));
    statusBar()->showMessage(tr("Gerber editado"), 4000);
}

/// @brief Edita el archivo Gerber/Excellon fuente.
void MainWindow::editSource() {
    auto* item = objects_->currentItem();
    if (!item) {
        QMessageBox::information(this, tr("Editor de fuente"), tr("Seleccione un objeto."));
        return;
    }
    const int id = item->data(0, Qt::UserRole).toInt();
    const auto it = sourcePaths_.find(id);
    if (it == sourcePaths_.end()) {
        QMessageBox::information(this, tr("Editor de fuente"),
                                 tr("Este objeto no tiene un archivo fuente asociado."));
        return;
    }
    const QString path = it->second;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        showError(tr("No se pudo leer el archivo"));
        return;
    }
    const QString original = QString::fromUtf8(file.readAll());
    file.close();

    QDialog dialog(this);
    dialog.resize(900, 700);
    dialog.setWindowTitle(tr("Editor de fuente — %1").arg(QFileInfo(path).fileName()));
    auto* layout = new QVBoxLayout(&dialog);
    auto* editor = new QPlainTextEdit(&dialog);
    editor->setPlainText(original);
    editor->setStyleSheet("QPlainTextEdit { font-family: Menlo; font-size: 12px; }");
    layout->addWidget(editor);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    box->button(QDialogButtonBox::Ok)->setText(tr("Guardar y reprocesar"));
    layout->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        showError(tr("No se pudo guardar el archivo"));
        return;
    }
    out.write(editor->toPlainText().toUtf8());
    out.close();

    try {
        if (gerberLayers_.count(id)) {
            GerberOptions opt;
            opt.appUnits = defaultUnits_;
            opt.stepsPerCircle = stepsPerCircle_;
            const GerberResult r = GerberParser(opt).parseFile(path.toStdString());
            gerberLayers_[id] = r;
            lastGerber_ = r;
            canvas_->setLayerGeometry(id, r.solids, r.follows);
            item->setText(2, QString::number(r.solid.area(), 'f', 2));
        } else if (excellonLayers_.count(id)) {
            ExcellonOptions opt;
            opt.appUnits = defaultUnits_;
            opt.stepsPerCircle = stepsPerCircle_;
            const ExcellonResult r = ExcellonParser(opt).parseFile(path.toStdString());
            excellonLayers_[id] = r;
            lastExcellon_ = r;
            lastDrillJob_ = drillJobFromExcellon(r);
            canvas_->setLayerGeometry(id, r.geometry, {});
            item->setText(2, QString::number(r.solid.area(), 'f', 2));
            refreshDrillTable();
        } else {
            return;
        }
        refreshProperties();
        statusBar()->showMessage(tr("Fuente guardada y reprocesada"), 5000);
    } catch (const std::exception& e) {
        showError(QString::fromUtf8(e.what()));
    }
}

/// @brief Construye los paneles de herramientas CAM.
void MainWindow::buildToolPanel(QTabWidget* tabs) {
    tabs->setToolTip(tr("Herramientas CAM: una pestaña por operación."));

    auto makeSpin = [this](double min, double max, double value, int decimals, const QString& suffix,
                           const QString& tip) {
        auto* spin = new UnitSpinBox(this);
        spin->setRange(min, max);
        spin->setDecimals(decimals);
        spin->setValue(value);
        if (!suffix.isEmpty()) spin->setSuffix(suffix);
        applyTip(spin, tip);
        return spin;
    };
    auto addRow = [](QFormLayout* form, const QString& label, QWidget* widget) {
        form->addRow(label, widget);
        applyTip(widget, widget->toolTip());
    };
    auto pageTitle = [this](QFormLayout* form, const QString& text, const QString& tip) {
        auto* label = new QLabel(text, this);
        label->setProperty("role", "pageTitle");
        label->setToolTip(tip);
        form->addRow(label);
        auto* line = new QFrame(this);
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Plain);
        line->setProperty("role", "rule");
        form->addRow(line);
    };
    auto section = [this](QFormLayout* form, const QString& text) {
        auto* label = new QLabel(text, this);
        label->setProperty("role", "section");
        form->addRow(label);
    };
    auto markAdvanced = [this](QFormLayout* form, QWidget* widget) {
        widget->setProperty("advanced", true);
        advancedWidgets_.push_back(widget);
        if (QWidget* label = form->labelForField(widget)) {
            label->setProperty("advanced", true);
            advancedWidgets_.push_back(label);
        }
    };
    auto wrapScroll = [this](QWidget* page) {
        if (auto* form = qobject_cast<QFormLayout*>(page->layout())) {
            form->setContentsMargins(14, 12, 14, 14);
            form->setVerticalSpacing(8);
            form->setHorizontalSpacing(12);
        }
        auto* area = new QScrollArea(this);
        area->setWidgetResizable(true);
        area->setFrameShape(QFrame::NoFrame);
        area->setWidget(page);
        return area;
    };

    struct ToolRow {
        double dia = 0.2;
        double fxy = 300.0;
        double fz = 60.0;
        double rpm = 10000.0;
        double laser = 300.0;
        double zcut = -1.5;
        double depth = 0.0;
    };

    auto makeTable = [this](const std::vector<ToolRow>& rows, bool editableDiameter) {
        auto* table = new QTableWidget(static_cast<int>(rows.size()), 8, this);
        table->setHorizontalHeaderLabels({tr("#"), tr("Ø"), tr("Av. XY"), tr("Av. Z"), tr("RPM"),
                                          tr("S"), tr("Z corte"), tr("Prof.")});
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        table->horizontalHeader()->setStretchLastSection(true);
        table->verticalHeader()->setVisible(false);
        table->setMinimumHeight(120);
        const QStringList headerTips = {
            tr("<b>#</b><br>Número de herramienta. Se usa como T1, T2... en el G-code "
               "y para los cambios de herramienta."),
            tr("<b>Ø (diámetro)</b><br>Diámetro de la fresa.<br>"
               "Define el ancho del corte y la separación entre el cobre y la primera "
               "pasada (Ø/2). En taladrado viene del archivo Excellon."),
            tr("<b>Av. XY</b><br>Velocidad de avance horizontal (mm/min) para esta "
               "herramienta.<br>Fresado PCB: 150–500 mm/min. Láser: 300–1500 mm/min."),
            tr("<b>Av. Z</b><br>Velocidad de bajada / penetración (mm/min) para esta "
               "herramienta.<br>Brocas finas: 5–20 mm/min. Fresas grandes: 30–80 mm/min."),
            tr("<b>RPM</b><br>Velocidad del husillo (revoluciones por minuto) para esta "
               "herramienta.<br>PCB: 10.000–20.000 RPM. No se usa en modo láser."),
            tr("<b>S (potencia láser)</b><br>Valor del comando <tt>M3 S...</tt> cuando el "
               "postprocesador es GRBL láser.<br>Se ignora cuando la herramienta es una "
               "fresa. Rango típico 0–1000."),
            tr("<b>Z corte</b><br>Profundidad final de esta herramienta (negativa).<br>"
               "Aislar: −0,05 a −0,15 mm. Cortar placa de 1,6 mm: −1,8 a −2,0 mm."),
            tr("<b>Prof. (profundidad por pasada)</b><br>Cuánto baja la herramienta en cada "
               "pasada.<br>0 = una sola pasada hasta Z corte. Si es mayor que 0, baja en "
               "escalones de ese valor.")};
        for (int c = 0; c < headerTips.size(); ++c) {
            if (auto* item = table->horizontalHeaderItem(c)) item->setToolTip(headerTips[c]);
        }
        table->setToolTip(tr("Una fila por herramienta. Cada fila tiene su propio diámetro, "
                             "avance XY, avance Z, husillo, potencia láser, Z de corte y "
                             "profundidad por pasada. Doble clic para editar."));
        for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
            const ToolRow& r = rows[static_cast<std::size_t>(i)];
            auto set = [&](int col, const QString& text, bool editable = true) {
                auto* item = new QTableWidgetItem(text);
                if (!editable) item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                table->setItem(i, col, item);
            };
            set(0, QString::number(i + 1), false);
            set(1, lengthText(r.dia, 3), editableDiameter);
            set(2, QString::number(r.fxy, 'f', 1));
            set(3, QString::number(r.fz, 'f', 1));
            set(4, QString::number(r.rpm, 'f', 0));
            set(5, QString::number(r.laser, 'f', 1));
            set(6, lengthText(r.zcut, 3));
            set(7, lengthText(r.depth, 3));
        }
        const int widths[8] = {26, 50, 62, 52, 60, 48, 60, 56};
        const int inchWidths[8] = {26, 82, 62, 52, 60, 48, 88, 88};
        const int* chosen = displayInch_ ? inchWidths : widths;
        for (int c = 0; c < 8; ++c) table->setColumnWidth(c, chosen[c]);
        connect(table, &QTableWidget::cellChanged, this,
                [this](int, int) { markProjectDirty(); });
        return table;
    };

    auto readTable = [this](QTableWidget* table) {
        std::vector<ToolRow> rows;
        for (int i = 0; i < table->rowCount(); ++i) {
            auto value = [table, i](int col, double fallback) {
                auto* item = table->item(i, col);
                if (!item) return fallback;
                bool ok = false;
                const double v = item->text().toDouble(&ok);
                return ok ? v : fallback;
            };
            ToolRow r;
            r.dia = lengthToMm(value(1, displayLength(0.2)));
            r.fxy = value(2, 300.0);
            r.fz = value(3, 60.0);
            r.rpm = value(4, 10000.0);
            r.laser = value(5, 300.0);
            r.zcut = lengthToMm(value(6, displayLength(-1.5)));
            r.depth = lengthToMm(value(7, 0.0));
            rows.push_back(r);
        }
        return rows;
    };

    auto addTableButtons = [this, &makeTable](QFormLayout* form, QTableWidget* table, bool editableDiameter) {
        auto* box = new QWidget(this);
        auto* layout = new QHBoxLayout(box);
        auto* add = new QPushButton(originalIcon("plus"), tr("Añadir"), box);
        auto* remove = new QPushButton(originalIcon("trash"), tr("Eliminar"), box);
        auto* applyAll = new QPushButton(tr("Aplicar a todas"), box);
        auto* fromDb = new QPushButton(tr("Desde DB"), box);
        applyTip(add, tr("Agrega una herramienta nueva con valores por defecto."));
        applyTip(remove, tr("Elimina la herramienta seleccionada."));
        applyTip(applyAll, tr("Copia los parámetros de la fila seleccionada a todas las demás."));
        applyTip(fromDb, tr("Agrega una herramienta desde la base de datos de herramientas."));
        connect(fromDb, &QPushButton::clicked, this, [this, table] { addToolFromDb(table); });
        layout->addWidget(add);
        layout->addWidget(remove);
        layout->addWidget(applyAll);
        layout->addWidget(fromDb);
        form->addRow(box);
        connect(add, &QPushButton::clicked, this, [this, table, editableDiameter] {
            const int row = table->rowCount();
            table->insertRow(row);
            auto set = [&](int col, const QString& text, bool editable = true) {
                auto* item = new QTableWidgetItem(text);
                if (!editable) item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                table->setItem(row, col, item);
            };
            set(0, QString::number(row + 1), false);
            set(1, lengthText(0.2, 3), editableDiameter);
            set(2, "300.0");
            set(3, "60.0");
            set(4, "10000");
            set(5, "300.0");
            set(6, lengthText(-1.5, 3));
            set(7, lengthText(0.0, 3));
        });
        connect(remove, &QPushButton::clicked, this, [table] {
            const int row = table->currentRow();
            if (row >= 0) table->removeRow(row);
        });
        connect(applyAll, &QPushButton::clicked, this, [table] {
            const int src = table->currentRow();
            if (src < 0) return;
            for (int i = 0; i < table->rowCount(); ++i) {
                if (i == src) continue;
                for (int c = 2; c < 8; ++c) {
                    if (auto* from = table->item(src, c)) {
                        if (auto* to = table->item(i, c)) to->setText(from->text());
                        else table->setItem(i, c, new QTableWidgetItem(from->text()));
                    }
                }
            }
        });
    };

    struct Common {
        UnitSpinBox* zMove = nullptr;
        UnitSpinBox* zTool = nullptr;
        UnitSpinBox* zEnd = nullptr;
        UnitSpinBox* dwell = nullptr;
        UnitSpinBox* endX = nullptr;
        UnitSpinBox* endY = nullptr;
        QComboBox* pre = nullptr;
        QCheckBox* noZ = nullptr;
        QPlainTextEdit* customTc = nullptr;
    };

    auto addCommon = [&](QFormLayout* form, QTableWidget* table) {
        Common c;
        c.zMove = makeSpin(0.0, 50.0, 2.0, 3, " mm",
            tr("<b>Altura Z para recorridos</b><br>Altura de traslado entre recorridos. "
               "Típico 2–5 mm.<br>No aplica en modo láser (se ignora y se oculta)."));
        c.zTool = makeSpin(0.0, 100.0, 19.0, 3, " mm",
            tr("<b>Z de cambio de herramienta</b><br>Altura segura para cambiar la fresa.<br>"
               "No aplica en modo láser."));
        c.zEnd = makeSpin(-50.0, 100.0, 19.0, 3, " mm",
            tr("<b>Fin del movimiento Z</b><br>Posición Z al terminar el programa.<br>"
               "No aplica en modo láser."));
        c.dwell = makeSpin(0.0, 100.0, 0.0, 2, " s",
            tr("<b>Dwell</b><br>Pausa tras bajar. 0 = sin pausa."));
        c.endX = makeSpin(-1000.0, 1000.0, 0.0, 3, " mm",
            tr("<b>X final</b><br>0,0 = sin movimiento final."));
        c.endY = makeSpin(-1000.0, 1000.0, 0.0, 3, " mm",
            tr("<b>Y final</b><br>0,0 = sin movimiento final."));
        c.pre = new QComboBox(this);
        c.pre->addItems(gcodeFlavorLabels());
        applyTip(c.pre, tr("<b>Postprocesador</b><br>Todos los perfiles de FlatCAM 2024.4: "
                           "MACH3, GRBL, Marlin, Repetier, Berta, ISEL, Roland, cambio de "
                           "herramienta y láser.<br>Al elegir un perfil láser se activa "
                           "«Ignorar Z» automáticamente."));

        c.noZ = new QCheckBox(tr("Ignorar Z (modo láser)"), this);
        applyTip(c.noZ, tr("<b>Ignorar Z</b><br>"
                           "Desactiva la altura Z para recorridos, el cambio de herramienta, "
                           "el fin de Z y las columnas Av. Z / RPM / Z corte / Prof.<br>"
                           "Se activa automáticamente con GRBL láser y también podés "
                           "usarlo para máquinas sin eje Z."));

        c.customTc = new QPlainTextEdit(this);
        c.customTc->setMaximumHeight(80);
        c.customTc->setPlaceholderText(
            tr("T{tool}\nM6\n(MSG, Cambiar a Ø {diameter})\nM0\n"
               "G00 Z{z}"));
        c.customTc->setPlainText(customToolchange_);
        c.customTc->setToolTip(tr("<b>Toolchange personalizado</b><br>Se usa solo con el "
                                  "postprocesador «Personalizado». Placeholders: {tool}, "
                                  "{diameter}, {z}, {x}, {y}."));
        connect(c.customTc, &QPlainTextEdit::textChanged, this, [this, c] {
            customToolchange_ = c.customTc->toPlainText();
            QSettings().setValue("preferences/customToolchange", customToolchange_);
        });
        addRow(form, tr("Toolchange:"), c.customTc);
        QWidget* customLabel = form->labelForField(c.customTc);
        const bool customInitially = c.pre->currentIndex() == 18;
        if (customLabel) customLabel->setVisible(customInitially);
        c.customTc->setVisible(customInitially);
        connect(c.pre, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [c, customLabel](int index) {
                    const bool custom = index == 18;
                    if (customLabel) customLabel->setVisible(custom);
                    c.customTc->setVisible(custom);
                });

        auto updateZ = [c, table, form] {
            const bool laser = isLaserFlavorIndex(c.pre->currentIndex());
            const bool ignore = c.noZ->isChecked();
            c.zMove->setEnabled(!ignore);
            c.zTool->setEnabled(!ignore);
            c.zEnd->setEnabled(!ignore);
            auto grayLabel = [form](QWidget* field, bool enabled) {
                if (QWidget* label = form->labelForField(field)) label->setEnabled(enabled);
            };
            grayLabel(c.zMove, !ignore);
            grayLabel(c.zTool, !ignore);
            grayLabel(c.zEnd, !ignore);
            if (table) {
                table->setColumnHidden(3, ignore);          // Av. Z
                table->setColumnHidden(4, ignore || laser); // RPM no aplica sin Z ni en láser
                table->setColumnHidden(5, !laser);          // S sólo en modo láser
                table->setColumnHidden(6, ignore);          // Z corte
                table->setColumnHidden(7, ignore);          // Prof.
                if (auto* header = table->horizontalHeaderItem(5))
                    header->setText(tr("Pot. láser"));
            }
        };
        connect(c.pre, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [c, updateZ](int index) {
                    if (isLaserFlavorIndex(index) && !c.noZ->isChecked()) {
                        const QSignalBlocker blocker(c.noZ);
                        c.noZ->setChecked(true);
                    }
                    updateZ();
                });
        connect(c.noZ, &QCheckBox::toggled, this, [updateZ](bool) { updateZ(); });

        addRow(form, tr("Altura Z p/recorr.:"), c.zMove);
        addRow(form, tr("Cambio herram. Z:"), c.zTool);
        addRow(form, tr("Fin del mov. Z:"), c.zEnd);
        addRow(form, tr("Dwell:"), c.dwell);
        addRow(form, tr("X final:"), c.endX);
        addRow(form, tr("Y final:"), c.endY);
        addRow(form, tr("Postprocesador:"), c.pre);
        form->addRow(QString(), c.noZ);
        updateZ();
        return c;
    };

    auto optionsFor = [](const ToolRow& row, const Common& c) {
        fc::GCodeOptions opt;
        opt.units = fc::Units::Mm;
        opt.feedXY = row.fxy;
        opt.feedZ = row.fz;
        opt.spindleSpeed = row.rpm;
        opt.laserPower = row.laser;
        const bool ignoreZ = c.noZ->isChecked();
        opt.zCut = ignoreZ ? 0.0 : row.zcut;
        opt.depthPerPass = ignoreZ ? 0.0 : row.depth;
        opt.zMove = ignoreZ ? 0.0 : c.zMove->value();
        opt.zToolchange = ignoreZ ? 0.0 : c.zTool->value();
        opt.zEnd = ignoreZ ? 0.0 : c.zEnd->value();
        opt.dwellTime = c.dwell->value();
        if (c.endX->value() != 0.0 || c.endY->value() != 0.0)
            opt.endXY = fc::Pt{c.endX->value(), c.endY->value()};
        opt.flavor = gcodeFlavorFromIndex(c.pre->currentIndex());
        opt.customToolchange = c.customTc ? c.customTc->toPlainText().toStdString() : std::string();
        return opt;
    };

    auto sourceBase = [](QComboBox* combo) {
        if (!combo) return QString();
        return QFileInfo(combo->currentText()).completeBaseName();
    };
    auto camName = [this, sourceBase](const QString& op, QComboBox* combo) {
        const QString base = sourceBase(combo);
        return base.isEmpty() ? op : tr("%1 · %2").arg(op, base);
    };

    // --- Isolation
    auto* isoPage = new QWidget(this);
    auto* isoForm = new QFormLayout(isoPage);
    isoSource_ = new QComboBox(this);
    isoSource_->setToolTip(tr("Gerber del que se genera el aislamiento."));
    pageTitle(isoForm, tr("Herramienta de aislamiento"), tr("Crea recorridos de aislamiento alrededor del cobre."));
    isoForm->addRow(tr("GERBER:"), isoSource_);
    section(isoForm, tr("Parámetros para: Herramienta"));
    auto* isoPasses = new QSpinBox(this);
    isoPasses->setRange(1, 20);
    isoPasses->setValue(1);
    applyTip(isoPasses, tr("<b>Pasadas</b><br>Cada pasada se aleja un diámetro."));
    auto* isoOverlap = makeSpin(0.0, 90.0, 0.0, 1, " %",
        tr("<b>Solape entre pasadas</b><br>0 % = un diámetro de separación."));
    auto* isoInvert = new QCheckBox(tr("Sentido invertido"), this);
    applyTip(isoInvert, tr("<b>Sentido de giro</b><br>Climb vs. convencional."));
    auto* isoOrder = new QComboBox(this);
    isoOrder->addItems({tr("Orden de la tabla"), tr("Menor a mayor"), tr("Mayor a menor")});
    isoOrder->setToolTip(tr("Orden en que se usan las herramientas del aislamiento."));
    auto* isoType = new QComboBox(this);
    isoType->addItems({tr("Circular"), tr("V-shape"), tr("Bola")});
    auto* isoAngle = makeSpin(1.0, 180.0, 60.0, 1, " °",
        tr("<b>Ángulo de punta</b><br>Solo para herramienta V-shape."));
    auto* isoDepth = makeSpin(0.001, 10.0, 0.1, 3, " mm",
        tr("<b>Profundidad de corte</b><br>Con el ángulo define el diámetro efectivo."));
    isoForm->addRow(tr("Pasadas:"), isoPasses);
    addRow(isoForm, tr("Solape:"), isoOverlap);
    addRow(isoForm, tr("Orden:"), isoOrder);
    markAdvanced(isoForm, isoOrder);
    addRow(isoForm, tr("Herramienta:"), isoType);
    markAdvanced(isoForm, isoType);
    addRow(isoForm, tr("Ángulo V:"), isoAngle);
    markAdvanced(isoForm, isoAngle);
    addRow(isoForm, tr("Profundidad V:"), isoDepth);
    markAdvanced(isoForm, isoDepth);
    auto* isoFollow = new QCheckBox(tr("Generar follow (seguir contorno)"), this);
    isoForm->addRow(QString(), isoFollow);
    markAdvanced(isoForm, isoFollow);
    auto* isoCombine = new QCheckBox(tr("Combinar recorridos cercanos"), this);
    isoCombine->setToolTip(tr("No retrae entre recorridos cuyos extremos estén a menos de 0,5 mm."));
    isoForm->addRow(QString(), isoCombine);
    markAdvanced(isoForm, isoCombine);
    auto* isoRest = new QCheckBox(tr("Rest machining (evitar solapes)"), this);
    isoRest->setToolTip(tr("Ordena de mayor a menor y omite recorridos ya cubiertos."));
    isoForm->addRow(QString(), isoRest);
    markAdvanced(isoForm, isoRest);
    isoForm->addRow(QString(), isoInvert);
    auto* isoTable = makeTable({{0.2, 300, 60, 10000, 300, -0.1, 0}}, true);
    isoTable_ = isoTable;
    section(isoForm, tr("Tabla de herramientas"));
    isoForm->addRow(isoTable);
    addTableButtons(isoForm, isoTable, true);
    section(isoForm, tr("Parámetros comunes"));
    Common isoCommon = addCommon(isoForm, isoTable);
    auto* isoPreview = new QPushButton(originalIcon("geometry"), tr("Generar geometría"), this);
    auto* isoCnc = new QPushButton(originalIcon("cnc"), tr("Generar CNCJob"), this);
    isoCnc->setProperty("primary", true);
    auto* isoExport = new QPushButton(originalIcon("save_as"), tr("Exportar G-code"), this);
    isoForm->addRow(isoPreview);
    isoForm->addRow(isoCnc);
    isoForm->addRow(isoExport);
    connect(isoPreview, &QPushButton::clicked, this,
            [this, camName, isoTable, isoPasses, isoOverlap, isoInvert, isoOrder, isoType, isoAngle,
             isoDepth, isoFollow, isoCombine, isoRest, isoCommon, optionsFor, readTable] {
                fc::GerberResult* src = sourceGerber(isoSource_);
                if (!src) return;
                auto rows = readTable(isoTable);
                if (rows.empty()) return;
                if (isoRest->isChecked()) {
                    std::sort(rows.begin(), rows.end(),
                              [](const ToolRow& a, const ToolRow& b) { return a.dia > b.dia; });
                } else if (isoOrder->currentIndex() == 1) {
                    std::sort(rows.begin(), rows.end(),
                              [](const ToolRow& a, const ToolRow& b) { return a.dia < b.dia; });
                } else if (isoOrder->currentIndex() == 2) {
                    std::sort(rows.begin(), rows.end(),
                              [](const ToolRow& a, const ToolRow& b) { return a.dia > b.dia; });
                }
                lastIsoMulti_.clear();
                std::vector<Geom> preview;
                Geom covered;
                for (std::size_t i = 0; i < rows.size(); ++i) {
                    IsolationOptions o;
                    double effective = rows[i].dia;
                    if (isoType->currentIndex() == 1) {
                        effective = 2.0 * isoDepth->value() *
                                    std::tan(isoAngle->value() * M_PI / 360.0);
                    }
                    if (effective <= 0.0) effective = rows[i].dia;
                    o.toolDiameter = effective;
                    o.passes = isoPasses->value();
                    o.overlap = isoOverlap->value() / 100.0;
                    o.invert = isoInvert->isChecked();
                    const IsolationResult res = isolateCopper(src->solid, o);
                    fc::PathToolJob job;
                    job.number = static_cast<int>(i) + 1;
                    job.diameter = effective;
                    job.options = optionsFor(rows[i], isoCommon);
                    job.options.toolDiameter = effective;
                    job.options.connectPaths = isoCombine->isChecked();
                    job.bounds = res.bounds;
                    for (const Geom& g : res.paths) {
                        if (isoRest->isChecked() && covered.valid() && !covered.empty()) {
                            const Geom probe = g.buffer(0.01, 8);
                            if (probe.valid() && probe.difference(covered).empty()) continue;
                        }
                        Ring ring = g.coords();
                        if (ring.size() >= 2) job.paths.emplace_back(ring.begin(), ring.end());
                        preview.push_back(g);
                        if (isoRest->isChecked()) {
                            const Geom area = g.buffer(effective / 2.0, 8);
                            covered = covered.valid() ? covered.unite(area) : area;
                        }
                    }
                    if (isoFollow->isChecked() && i == 0) {
                        for (const PolygonContours& pc : src->solid.polygons()) {
                            std::vector<Ring> rings;
                            rings.push_back(pc.outer);
                            for (const Ring& hole : pc.holes) rings.push_back(hole);
                            for (const Ring& ring : rings) {
                                if (ring.size() < 2) continue;
                                job.paths.push_back(ring);
                                preview.push_back(Geom::line(ring));
                            }
                        }
                    }
                    lastIsoMulti_.push_back(std::move(job));
                }
                lastIsoHeader_ = optionsFor(rows.front(), isoCommon);
                const int sourceId = isoSource_ ? isoSource_->currentData().toInt() : 0;
                addGeneratedLayer(camName(tr("Aislamiento"), isoSource_), preview, sourceId);
                statusBar()->showMessage(tr("Aislamiento: %1 herramientas").arg(lastIsoMulti_.size()), 4000);
            });
    connect(isoCnc, &QPushButton::clicked, this, [this, camName] {
        if (lastIsoMulti_.empty()) {
            QMessageBox::information(this, tr("Aislamiento"),
                                     tr("Genere primero la geometría de aislamiento."));
            return;
        }
        const std::string code = pathsGCodeMulti(lastIsoMulti_, lastIsoHeader_);
        addCncJobObject(camName(tr("Aislamiento"), isoSource_), QString::fromStdString(code),
                        cncProps(lastIsoHeader_, tr("Aislamiento")));
        statusBar()->showMessage(tr("CNCJob de aislamiento generado"), 4000);
    });
    connect(isoExport, &QPushButton::clicked, this, &MainWindow::exportIsolationGCode);
    toolTabs_[tr("Herramienta de aislamiento")] = wrapScroll(isoPage);

    // --- NCC
    auto* nccPage = new QWidget(this);
    auto* nccForm = new QFormLayout(nccPage);
    nccSource_ = new QComboBox(this);
    nccSource_->setToolTip(tr("Gerber del que se despeja el cobre."));
    pageTitle(nccForm, tr("Herramienta NCC"), tr("Despeja el cobre sobrante de la placa."));
    nccForm->addRow(tr("GERBER:"), nccSource_);
    section(nccForm, tr("Parámetros para: Herramienta"));
    auto* nccStep = makeSpin(0.0, 95.0, 80.0, 1, " %",
        tr("<b>Superposición</b><br>paso = Ø × (100 − superposición) %."));
    auto* nccClear = makeSpin(0.0, 10.0, 0.1, 3, " mm",
        tr("<b>Distancia al cobre</b><br>Margen de seguridad."));
    auto* nccAngle = makeSpin(-90.0, 90.0, 0.0, 1, " °",
        tr("<b>Ángulo del hatch</b><br>45° recomendado."));
    auto* nccMargin = makeSpin(0.0, 50.0, 1.0, 2, " mm",
        tr("<b>Margen del área</b><br>Expande el área a despejar."));
    auto* nccStrategy = new QComboBox(this);
    nccStrategy->addItems({tr("Hatch (líneas)"), tr("Paint (contornos)")});
    nccStrategy->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    nccStrategy->setToolTip(tr("Hatch: líneas paralelas. Paint: contornos concéntricos."));
    nccStrategy->setItemData(0, tr("Líneas paralelas para despejar áreas grandes."),
                             Qt::ToolTipRole);
    nccStrategy->setItemData(1, tr("Contornos paralelos para un acabado más uniforme."),
                             Qt::ToolTipRole);
    addRow(nccForm, tr("Superposición:"), nccStep);
    addRow(nccForm, tr("Distancia al cobre:"), nccClear);
    addRow(nccForm, tr("Ángulo:"), nccAngle);
    addRow(nccForm, tr("Margen del área:"), nccMargin);
    addRow(nccForm, tr("Estrategia:"), nccStrategy);
    auto* nccConnect = new QCheckBox(tr("Conectar recorridos cercanos"), this);
    nccConnect->setToolTip(tr("Une recorridos cuyos extremos estén a menos de 0,5 mm."));
    nccForm->addRow(QString(), nccConnect);
    markAdvanced(nccForm, nccConnect);
    auto* nccRest = new QCheckBox(tr("Rest machining (evitar solapes)"), this);
    nccRest->setToolTip(tr("Ordena de mayor a menor y omite recorridos ya cubiertos."));
    nccForm->addRow(QString(), nccRest);
    markAdvanced(nccForm, nccRest);
    auto* nccTable = makeTable({{0.5, 300, 60, 10000, 300, -1.5, 0}}, true);
    nccTable_ = nccTable;
    section(nccForm, tr("Tabla de herramientas"));
    nccForm->addRow(nccTable);
    addTableButtons(nccForm, nccTable, true);
    section(nccForm, tr("Parámetros comunes"));
    Common nccCommon = addCommon(nccForm, nccTable);
    auto* nccPreview = new QPushButton(originalIcon("geometry"), tr("Generar geometría"), this);
    auto* nccCnc = new QPushButton(originalIcon("cnc"), tr("Generar CNCJob"), this);
    nccCnc->setProperty("primary", true);
    auto* nccExport = new QPushButton(originalIcon("save_as"), tr("Exportar G-code"), this);
    nccForm->addRow(nccPreview);
    nccForm->addRow(nccCnc);
    nccForm->addRow(nccExport);
    connect(nccPreview, &QPushButton::clicked, this,
            [this, camName, nccTable, nccStep, nccClear, nccAngle, nccMargin, nccStrategy, nccConnect,
             nccRest, nccCommon, optionsFor, readTable] {
                fc::GerberResult* src = sourceGerber(nccSource_);
                if (!src) return;
                auto rows = readTable(nccTable);
                if (rows.empty()) return;
                if (nccRest->isChecked())
                    std::sort(rows.begin(), rows.end(),
                              [](const ToolRow& a, const ToolRow& b) { return a.dia > b.dia; });
                lastNccMulti_.clear();
                std::vector<Geom> preview;
                Geom covered;
                for (std::size_t i = 0; i < rows.size(); ++i) {
                    NccOptions o;
                    o.toolDiameter = rows[i].dia;
                    o.stepOver = (100.0 - nccStep->value()) / 100.0;
                    o.clearance = nccClear->value();
                    o.angleDeg = nccAngle->value();
                    o.margin = nccMargin->value();
                    o.strategy = nccStrategy->currentIndex() == 1 ? NccStrategy::Paint : NccStrategy::Hatch;
                    const BBox b = src->bounds.expanded(nccMargin->value());
                    const Geom area = b.valid ? Geom::box(b.xmin, b.ymin, b.xmax, b.ymax) : Geom();
                    const NccResult res = clearCopper(src->solid, area, o);
                    fc::PathToolJob job;
                    job.number = static_cast<int>(i) + 1;
                    job.diameter = rows[i].dia;
                    job.options = optionsFor(rows[i], nccCommon);
                    job.options.connectPaths = nccConnect->isChecked();
                    job.bounds = res.bounds;
                    for (const Geom& g : res.paths) {
                        if (nccRest->isChecked() && covered.valid() && !covered.empty()) {
                            const Geom probe = g.buffer(0.01, 8);
                            if (probe.valid() && probe.difference(covered).empty()) continue;
                        }
                        Ring ring = g.coords();
                        if (ring.size() >= 2) job.paths.emplace_back(ring.begin(), ring.end());
                        preview.push_back(g);
                        if (nccRest->isChecked()) {
                            const Geom area = g.buffer(rows[i].dia / 2.0, 8);
                            covered = covered.valid() ? covered.unite(area) : area;
                        }
                    }
                    lastNccMulti_.push_back(std::move(job));
                }
                lastNccHeader_ = optionsFor(rows.front(), nccCommon);
                const int sourceId = nccSource_ ? nccSource_->currentData().toInt() : 0;
                addGeneratedLayer(camName(tr("NCC"), nccSource_), preview, sourceId);
                statusBar()->showMessage(tr("NCC: %1 herramientas").arg(lastNccMulti_.size()), 4000);
            });
    connect(nccCnc, &QPushButton::clicked, this, [this, camName] {
        if (lastNccMulti_.empty()) {
            QMessageBox::information(this, tr("NCC"),
                                     tr("Genere primero la geometría NCC."));
            return;
        }
        const std::string code = pathsGCodeMulti(lastNccMulti_, lastNccHeader_);
        addCncJobObject(camName(tr("NCC"), nccSource_), QString::fromStdString(code),
                        cncProps(lastNccHeader_, tr("NCC")));
        statusBar()->showMessage(tr("CNCJob NCC generado"), 4000);
    });
    connect(nccExport, &QPushButton::clicked, this, &MainWindow::exportNccGCode);
    toolTabs_[tr("Herramienta NCC")] = wrapScroll(nccPage);

    // --- Cutout
    auto* cutPage = new QWidget(this);
    auto* cutForm = new QFormLayout(cutPage);
    cutSource_ = new QComboBox(this);
    cutSource_->setToolTip(tr("Gerber cuyo contorno se corta."));
    pageTitle(cutForm, tr("Herramienta de corte"), tr("Corta el contorno de la placa, con puentes opcionales."));
    cutForm->addRow(tr("GERBER:"), cutSource_);
    section(cutForm, tr("Parámetros para: Herramienta"));
    auto* cutMargin = makeSpin(-50.0, 50.0, 0.0, 3, " mm",
        tr("<b>Margen</b><br>Separación extra del contorno."));
    auto* cutTabs = new QSpinBox(this);
    cutTabs->setRange(0, 12);
    cutTabs->setValue(0);
    auto* cutTabW = makeSpin(0.0, 50.0, 1.8, 3, " mm",
        tr("<b>Ancho de puente</b><br>Debe superar el diámetro de la fresa."));
    auto* cutInternal = new QCheckBox(tr("Corte interno"), this);
    auto* cutGapType = new QComboBox(this);
    cutGapType->addItems({tr("Con puentes"), tr("Corte completo")});
    auto* cutSides = new QComboBox(this);
    cutSides->addItems({tr("Todos los lados"), tr("Arriba y abajo"),
                        tr("Izquierda y derecha")});
    cutSides->setToolTip(tr("Zona del contorno donde se permiten puentes."));
    auto* cutManual = new QLineEdit(this);
    cutManual->setPlaceholderText(tr("ej. 10, 30, 70 % (vacío = automático)"));
    cutManual->setToolTip(tr("Posiciones manuales de los puentes como porcentaje del "
                             "perímetro. Si se completa, reemplaza la cantidad y los lados."));
    auto* cutHoles = new QCheckBox(tr("Incluir agujeros internos"), this);
    addRow(cutForm, tr("Margen:"), cutMargin);
    cutForm->addRow(tr("Puentes:"), cutTabs);
    addRow(cutForm, tr("Ancho de puente:"), cutTabW);
    addRow(cutForm, tr("Lados de puente:"), cutSides);
    addRow(cutForm, tr("Puentes manuales:"), cutManual);
    addRow(cutForm, tr("Separación:"), cutGapType);
    markAdvanced(cutForm, cutGapType);
    auto updateCutTabs = [cutGapType, cutTabs, cutTabW, cutSides, cutManual] {
        const bool enabled = cutGapType->currentIndex() == 0;
        cutTabs->setEnabled(enabled);
        cutTabW->setEnabled(enabled);
        cutSides->setEnabled(enabled);
        cutManual->setEnabled(enabled);
    };
    connect(cutGapType, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [updateCutTabs](int) { updateCutTabs(); });
    updateCutTabs();
    cutForm->addRow(QString(), cutInternal);
    cutForm->addRow(QString(), cutHoles);
    markAdvanced(cutForm, cutHoles);
    auto* cutTable = makeTable({{1.2, 300, 60, 10000, 300, -1.8, 0.6}}, true);
    cutTable_ = cutTable;
    section(cutForm, tr("Tabla de herramientas"));
    cutForm->addRow(cutTable);
    addTableButtons(cutForm, cutTable, true);
    section(cutForm, tr("Parámetros comunes"));
    Common cutCommon = addCommon(cutForm, cutTable);
    auto* cutPreview = new QPushButton(originalIcon("geometry"), tr("Generar geometría"), this);
    auto* cutCnc = new QPushButton(originalIcon("cnc"), tr("Generar CNCJob"), this);
    cutCnc->setProperty("primary", true);
    auto* cutExport = new QPushButton(originalIcon("save_as"), tr("Exportar G-code"), this);
    cutForm->addRow(cutPreview);
    cutForm->addRow(cutCnc);
    cutForm->addRow(cutExport);
    connect(cutPreview, &QPushButton::clicked, this,
            [this, camName, cutTable, cutMargin, cutTabs, cutTabW, cutInternal, cutGapType,
             cutSides, cutManual, cutHoles, cutCommon, optionsFor, readTable] {
                fc::GerberResult* src = sourceGerber(cutSource_);
                if (!src) return;
                const auto rows = readTable(cutTable);
                if (rows.empty()) return;
                lastCutMulti_.clear();
                std::vector<Geom> preview;
                for (std::size_t i = 0; i < rows.size(); ++i) {
                    CutoutOptions o;
                    o.toolDiameter = rows[i].dia;
                    o.margin = cutMargin->value();
                    o.tabCount = cutGapType->currentIndex() == 1 ? 0 : cutTabs->value();
                    o.tabWidth = cutTabW->value();
                    o.tabSides = cutSides->currentIndex() == 1
                                     ? fc::TabSides::TopBottom
                                     : (cutSides->currentIndex() == 2
                                            ? fc::TabSides::LeftRight
                                            : fc::TabSides::All);
                    o.tabPositions.clear();
                    if (cutGapType->currentIndex() == 0) {
                        const QString manual = cutManual->text().trimmed();
                        for (const QString& part : manual.split(QRegularExpression("[,;\\s]+"),
                                                                Qt::SkipEmptyParts)) {
                            bool ok = false;
                            const double percent = part.toDouble(&ok);
                            if (ok) o.tabPositions.push_back(percent / 100.0);
                        }
                    }
                    o.external = !cutInternal->isChecked();
                    o.includeHoles = cutHoles->isChecked();
                    const CutoutResult res = cutoutGeometry(src->solid, o);
                    fc::PathToolJob job;
                    job.number = static_cast<int>(i) + 1;
                    job.diameter = rows[i].dia;
                    job.options = optionsFor(rows[i], cutCommon);
                    job.bounds = res.bounds;
                    for (const Geom& g : res.paths) {
                        Ring ring = g.coords();
                        if (ring.size() >= 2) job.paths.emplace_back(ring.begin(), ring.end());
                        preview.push_back(g);
                    }
                    lastCutMulti_.push_back(std::move(job));
                }
                lastCutHeader_ = optionsFor(rows.front(), cutCommon);
                const int sourceId = cutSource_ ? cutSource_->currentData().toInt() : 0;
                addGeneratedLayer(camName(tr("Corte"), cutSource_), preview, sourceId);
                statusBar()->showMessage(tr("Corte: %1 herramientas").arg(lastCutMulti_.size()), 4000);
            });
    connect(cutCnc, &QPushButton::clicked, this, [this, camName] {
        if (lastCutMulti_.empty()) {
            QMessageBox::information(this, tr("Corte"),
                                     tr("Genere primero la geometría de corte."));
            return;
        }
        const std::string code = pathsGCodeMulti(lastCutMulti_, lastCutHeader_);
        addCncJobObject(camName(tr("Corte"), cutSource_), QString::fromStdString(code),
                        cncProps(lastCutHeader_, tr("Corte")));
        statusBar()->showMessage(tr("CNCJob de corte generado"), 4000);
    });
    connect(cutExport, &QPushButton::clicked, this, &MainWindow::exportCutoutGCode);
    toolTabs_[tr("Herramienta de corte")] = wrapScroll(cutPage);

    // --- Paint
    auto* paintPage = new QWidget(this);
    auto* paintForm = new QFormLayout(paintPage);
    paintSource_ = new QComboBox(this);
    paintSource_->setToolTip(tr("Gerber del área a pintar."));
    pageTitle(paintForm, tr("Herramienta de pintura"), tr("Rellena un área con contornos concéntricos."));
    paintForm->addRow(tr("GERBER:"), paintSource_);
    section(paintForm, tr("Parámetros para: Herramienta"));
    auto* paintStep = makeSpin(0.0, 95.0, 80.0, 1, " %",
        tr("<b>Superposición</b><br>paso = Ø × (100 − superposición) %."));
    auto* paintMargin = makeSpin(-10.0, 10.0, 0.0, 3, " mm",
        tr("<b>Margen del área</b><br>Expande o contrae el área."));
    addRow(paintForm, tr("Superposición:"), paintStep);
    addRow(paintForm, tr("Margen:"), paintMargin);
    auto* paintTable = makeTable({{0.1, 300, 60, 10000, 300, -1.5, 0}}, true);
    paintTable_ = paintTable;
    section(paintForm, tr("Tabla de herramientas"));
    paintForm->addRow(paintTable);
    addTableButtons(paintForm, paintTable, true);
    section(paintForm, tr("Parámetros comunes"));
    Common paintCommon = addCommon(paintForm, paintTable);
    auto* paintPreview = new QPushButton(originalIcon("geometry"), tr("Generar geometría"), this);
    auto* paintCnc = new QPushButton(originalIcon("cnc"), tr("Generar CNCJob"), this);
    paintCnc->setProperty("primary", true);
    auto* paintExport = new QPushButton(originalIcon("save_as"), tr("Exportar G-code"), this);
    paintForm->addRow(paintPreview);
    paintForm->addRow(paintCnc);
    paintForm->addRow(paintExport);
    connect(paintPreview, &QPushButton::clicked, this,
            [this, camName, paintTable, paintStep, paintMargin, paintCommon, optionsFor, readTable] {
                fc::GerberResult* src = sourceGerber(paintSource_);
                if (!src) return;
                const auto rows = readTable(paintTable);
                if (rows.empty()) return;
                lastPaintMulti_.clear();
                std::vector<Geom> preview;
                for (std::size_t i = 0; i < rows.size(); ++i) {
                    PaintOptions o;
                    o.toolDiameter = rows[i].dia;
                    o.stepOver = (100.0 - paintStep->value()) / 100.0;
                    o.margin = paintMargin->value();
                    const PaintResult res = paintArea(src->solid, o);
                    fc::PathToolJob job;
                    job.number = static_cast<int>(i) + 1;
                    job.diameter = rows[i].dia;
                    job.options = optionsFor(rows[i], paintCommon);
                    job.bounds = res.bounds;
                    for (const Geom& g : res.paths) {
                        Ring ring = g.coords();
                        if (ring.size() >= 2) job.paths.emplace_back(ring.begin(), ring.end());
                        preview.push_back(g);
                    }
                    lastPaintMulti_.push_back(std::move(job));
                }
                lastPaintHeader_ = optionsFor(rows.front(), paintCommon);
                const int sourceId = paintSource_ ? paintSource_->currentData().toInt() : 0;
                addGeneratedLayer(camName(tr("Pintura"), paintSource_), preview, sourceId);
                statusBar()->showMessage(tr("Pintura: %1 herramientas").arg(lastPaintMulti_.size()), 4000);
            });
    connect(paintCnc, &QPushButton::clicked, this, [this, camName] {
        if (lastPaintMulti_.empty()) {
            QMessageBox::information(this, tr("Pintura"),
                                     tr("Genere primero la geometría de pintura."));
            return;
        }
        const std::string code = pathsGCodeMulti(lastPaintMulti_, lastPaintHeader_);
        addCncJobObject(camName(tr("Pintura"), paintSource_), QString::fromStdString(code),
                        cncProps(lastPaintHeader_, tr("Pintura")));
        statusBar()->showMessage(tr("CNCJob de pintura generado"), 4000);
    });
    connect(paintExport, &QPushButton::clicked, this, &MainWindow::exportPaintGCode);
    toolTabs_[tr("Herramienta de pintura")] = wrapScroll(paintPage);

    // --- Drill
    auto* drillPage = new QWidget(this);
    auto* drillForm = new QFormLayout(drillPage);
    drillSource_ = new QComboBox(this);
    drillSource_->setToolTip(tr("Excellon con los taladros a generar."));
    pageTitle(drillForm, tr("Herramienta de perforación"), tr("Genera el taladrado a partir de un archivo Excellon."));
    drillForm->addRow(tr("EXCELLON:"), drillSource_);
    auto* drillNote = new QLabel(tr("Los diámetros provienen del Excellon cargado. "
                                    "Cada fila tiene sus propios avances, husillo y Z."), this);
    drillNote->setWordWrap(true);
    drillForm->addRow(drillNote);
    drillTable_ = makeTable({}, false);
    drillTable_->setToolTip(tr("Una fila por herramienta del archivo Excellon. Editá avances, "
                               "husillo, Z de corte y profundidad por pasada."));
    section(drillForm, tr("Tabla de herramientas"));
    drillForm->addRow(drillTable_);
    section(drillForm, tr("Parámetros comunes"));
    Common drillCommon = addCommon(drillForm, drillTable_);
    auto* drillOffsetX = makeSpin(-10.0, 10.0, 0.0, 3, " mm",
        tr("<b>Offset X</b><br>Desplaza todos los taladros en X."));
    auto* drillOffsetY = makeSpin(-10.0, 10.0, 0.0, 3, " mm",
        tr("<b>Offset Y</b><br>Desplaza todos los taladros en Y."));
    addRow(drillForm, tr("Offset X:"), drillOffsetX);
    markAdvanced(drillForm, drillOffsetX);
    addRow(drillForm, tr("Offset Y:"), drillOffsetY);
    markAdvanced(drillForm, drillOffsetY);
    auto* drillOrder = new QCheckBox(tr("Optimizar orden de taladrado"), this);
    drillOrder->setChecked(true);
    drillForm->addRow(QString(), drillOrder);
    markAdvanced(drillForm, drillOrder);
    auto* drillPreview = new QPushButton(originalIcon("cnc"), tr("Generar CNCJob"), this);
    drillPreview->setProperty("primary", true);
    auto* drillExport = new QPushButton(originalIcon("save_as"), tr("Exportar G-code"), this);
    drillForm->addRow(drillPreview);
    drillForm->addRow(drillExport);
    connect(drillPreview, &QPushButton::clicked, this,
            [this, camName, drillCommon, drillOffsetX, drillOffsetY, drillOrder, optionsFor,
             readTable] {
                fc::ExcellonResult* src = sourceExcellon(drillSource_);
                if (!src) {
                    QMessageBox::information(this, tr("Taladrado"), tr("Cargá un Excellon primero."));
                    return;
                }
                const auto rows = readTable(drillTable_);
                fc::DrillJob job;
                std::vector<int> numbers;
                for (const auto& [number, tool] : src->tools) {
                    if (tool.drills.empty() && tool.slotSegments.empty()) continue;
                    numbers.push_back(number);
                }
                for (std::size_t i = 0; i < numbers.size() && i < rows.size(); ++i) {
                    const int number = numbers[i];
                    const auto& source = src->tools.at(number);
                    fc::DrillTool tool;
                    tool.number = number;
                    tool.diameter = source.diameter;
                    tool.holes = source.drills;
                    tool.slotSegments = source.slotSegments;
                    tool.zCut = rows[i].zcut;
                    tool.feedZ = rows[i].fz;
                    tool.feedXY = rows[i].fxy;
                    tool.spindleSpeed = rows[i].rpm;
                    tool.dwell = drillCommon.dwell->value();
                    tool.offsetX = drillOffsetX->value();
                    tool.offsetY = drillOffsetY->value();
                    job.tools.push_back(std::move(tool));
                }
                if (job.tools.empty()) return;
                lastDrillJob_ = job;
                lastDrillHeader_ = optionsFor(rows.front(), drillCommon);
                lastDrillGCode_ = lastDrillHeader_;
                if (drillOrder->isChecked()) optimizeDrillOrder(*lastDrillJob_);
                const std::string code = drillingGCode(*lastDrillJob_, lastDrillHeader_);
                addCncJobObject(camName(tr("Taladrado"), drillSource_),
                                QString::fromStdString(code),
                                cncProps(lastDrillHeader_, tr("Taladrado")));
                statusBar()->showMessage(
                    tr("Taladrado: %1 herramientas · CNCJob generado").arg(job.tools.size()), 4000);
            });
    connect(drillExport, &QPushButton::clicked, this, &MainWindow::exportDrillGCode);
    toolTabs_[tr("Herramienta de perforación")] = wrapScroll(drillPage);

    // --- Milling (follow paths)
    auto* millPage = new QWidget(this);
    auto* millForm = new QFormLayout(millPage);
    millSource_ = new QComboBox(this);
    millSource_->setToolTip(tr("Gerber cuyos recorridos se fresan."));
    pageTitle(millForm, tr("Herramienta de fresado"), tr("Sigue los recorridos del objeto con una fresa."));
    millForm->addRow(tr("GERBER:"), millSource_);
    section(millForm, tr("Parámetros para: Herramienta"));
    auto* millDia = makeSpin(0.01, 20.0, 0.2, 3, " mm",
        tr("<b>Herramienta de fresado</b><br>Sigue los recorridos del Gerber con esta fresa."));
    auto* millFeed = makeSpin(1.0, 20000.0, 300.0, 1, " mm/min",
        tr("<b>Avance XY</b><br>Velocidad a lo largo de los recorridos."));
    auto* millFeedZ = makeSpin(0.1, 5000.0, 60.0, 1, " mm/min",
        tr("<b>Avance Z</b><br>Velocidad de bajada."));
    auto* millZ = makeSpin(-50.0, 0.0, -1.5, 3, " mm",
        tr("<b>Corte Z</b><br>Profundidad de fresado."));
    auto* millZMove = makeSpin(0.0, 50.0, 2.0, 3, " mm",
        tr("<b>Altura Z p/recorr.</b><br>Altura entre recorridos."));
    auto* millDepth = makeSpin(0.0, 20.0, 0.0, 3, " mm",
        tr("<b>Profundidad por pasada</b><br>0 = una pasada."));
    auto* millSpindle = makeSpin(0.0, 60000.0, 10000.0, 0, " RPM",
        tr("<b>Husillo</b><br>RPM de la fresa."));
    addRow(millForm, tr("Herramienta:"), millDia);
    addRow(millForm, tr("Avance XY:"), millFeed);
    addRow(millForm, tr("Avance Z:"), millFeedZ);
    addRow(millForm, tr("Corte Z:"), millZ);
    addRow(millForm, tr("Altura Z p/recorr.:"), millZMove);
    addRow(millForm, tr("Prof. por pasada:"), millDepth);
    addRow(millForm, tr("Husillo:"), millSpindle);
    auto* millPreview = new QPushButton(originalIcon("geometry"), tr("Generar geometría"), this);
    auto* millCnc = new QPushButton(originalIcon("cnc"), tr("Generar CNCJob"), this);
    millCnc->setProperty("primary", true);
    auto* millExport = new QPushButton(originalIcon("save_as"), tr("Exportar G-code"), this);
    millForm->addRow(millPreview);
    millForm->addRow(millCnc);
    millForm->addRow(millExport);
    connect(millPreview, &QPushButton::clicked, this,
            [this, millDia, millFeed, millFeedZ, millZ, millZMove, millDepth, millSpindle] {
                fc::GerberResult* src = sourceGerber(millSource_);
                if (!src) return;
                std::vector<std::vector<Pt>> lines;
                for (const Geom& g : src->follows) {
                    if (!g.valid() || g.empty()) continue;
                    if (g.typeId() == GEOS_LINESTRING || g.typeId() == GEOS_LINEARRING) {
                        Ring ring = g.coords();
                        if (ring.size() >= 2) lines.emplace_back(ring.begin(), ring.end());
                    } else {
                        for (int i = 0; i < g.numGeometries(); ++i) {
                            const Geom part = g.geometryN(i);
                            if (part.typeId() == GEOS_LINESTRING || part.typeId() == GEOS_LINEARRING) {
                                Ring ring = part.coords();
                                if (ring.size() >= 2) lines.emplace_back(ring.begin(), ring.end());
                            }
                        }
                    }
                }
                lastMillJob_ = PathJob{};
                lastMillJob_->paths = lines;
                lastMillJob_->bounds = src->bounds;
                lastMillJob_->toolDiameter = millDia->value();
                lastMillOptions_ = GCodeOptions{};
                lastMillOptions_.units = Units::Mm;
                lastMillOptions_.feedXY = millFeed->value();
                lastMillOptions_.feedZ = millFeedZ->value();
                lastMillOptions_.zCut = millZ->value();
                lastMillOptions_.zMove = millZMove->value();
                lastMillOptions_.depthPerPass = millDepth->value();
                lastMillOptions_.spindleSpeed = millSpindle->value();
                std::vector<Geom> preview;
                for (const auto& l : lines) preview.push_back(Geom::line(l));
                const int sourceId = millSource_ ? millSource_->currentData().toInt() : 0;
                addGeneratedLayer(tr("Fresado"), preview, sourceId);
                statusBar()->showMessage(tr("Fresado: %1 recorridos").arg(lines.size()), 4000);
            });
    connect(millCnc, &QPushButton::clicked, this, [this, camName] {
        if (!lastMillJob_ || lastMillJob_->paths.empty()) {
            QMessageBox::information(this, tr("Fresado"),
                                     tr("Genere primero la geometría de fresado."));
            return;
        }
        const std::string code = pathsGCode(*lastMillJob_, lastMillOptions_);
        addCncJobObject(camName(tr("Fresado"), millSource_), QString::fromStdString(code),
                        cncProps(lastMillOptions_, tr("Fresado")));
        statusBar()->showMessage(tr("CNCJob de fresado generado"), 4000);
    });
    connect(millExport, &QPushButton::clicked, this, &MainWindow::exportMillGCode);
    toolTabs_[tr("Herramienta de fresado")] = wrapScroll(millPage);

    // --- Copper thieving
    auto* thiefPage = new QWidget(this);
    auto* thiefForm = new QFormLayout(thiefPage);
    thiefSource_ = new QComboBox(this);
    thiefSource_->setToolTip(tr("Gerber de referencia para los puntos."));
    pageTitle(thiefForm, tr("Copper Thieving"), tr("Agrega puntos de cobre para uniformizar el baño."));
    thiefForm->addRow(tr("GERBER:"), thiefSource_);
    auto* thiefDia = makeSpin(0.05, 10.0, 0.5, 3, " mm",
        tr("<b>Diámetro de punto</b><br>Tamaño de cada punto de cobre agregado."));
    auto* thiefSpacing = makeSpin(0.2, 50.0, 2.0, 3, " mm",
        tr("<b>Separación</b><br>Distancia entre puntos de la grilla."));
    auto* thiefClear = makeSpin(0.0, 10.0, 0.5, 3, " mm",
        tr("<b>Distancia al cobre</b><br>Margen para no tocar el cobre existente."));
    auto* thiefMargin = makeSpin(0.0, 50.0, 0.5, 3, " mm",
        tr("<b>Margen</b><br>Borde sin puntos alrededor de la placa."));
    addRow(thiefForm, tr("Punto:"), thiefDia);
    addRow(thiefForm, tr("Separación:"), thiefSpacing);
    addRow(thiefForm, tr("Distancia al cobre:"), thiefClear);
    addRow(thiefForm, tr("Margen:"), thiefMargin);
    auto* thiefRef = new QComboBox(this);
    thiefRef->addItem(tr("Área del Gerber"), 0);
    for (QTreeWidgetItem* row : objectItems())
        thiefRef->addItem(row->text(1), row->data(0, Qt::UserRole).toInt());
    auto* thiefPpm = new QSpinBox(this);
    thiefPpm->setRange(0, 1000000);
    thiefPpm->setValue(0);
    thiefPpm->setToolTip(tr("Densidad en puntos por millón de mm². 0 usa la separación."));
    addRow(thiefForm, tr("Referencia:"), thiefRef);
    markAdvanced(thiefForm, thiefRef);
    addRow(thiefForm, tr("Densidad PPM:"), thiefPpm);
    markAdvanced(thiefForm, thiefPpm);
    auto* thiefPattern = new QComboBox(this);
    thiefPattern->addItems({tr("Puntos"), tr("Cuadrados"), tr("Líneas")});
    addRow(thiefForm, tr("Patrón:"), thiefPattern);
    markAdvanced(thiefForm, thiefPattern);
    auto* thiefGenerate = new QPushButton(originalIcon("geometry"), tr("Generar capa de puntos"), this);
    thiefForm->addRow(thiefGenerate);
    connect(thiefGenerate, &QPushButton::clicked, this,
            [this, thiefDia, thiefSpacing, thiefClear, thiefMargin, thiefRef, thiefPpm,
             thiefPattern] {
                fc::GerberResult* src = sourceGerber(thiefSource_);
                if (!src) return;
                ThievingOptions opt;
                opt.dotDiameter = thiefDia->value();
                opt.spacing = thiefSpacing->value();
                if (thiefPpm->value() > 0)
                    opt.spacing = std::sqrt(1000000.0 / thiefPpm->value());
                opt.clearance = thiefClear->value();
                opt.margin = thiefMargin->value();
                BBox area = src->bounds;
                const int refId = thiefRef->currentData().toInt();
                if (refId > 0) {
                    const BBox rb = canvas_->layerBounds(refId);
                    if (rb.valid) area = rb;
                }
                const std::vector<Pt> dots = thievingPoints(src->solid, area, opt);
                std::vector<Geom> cells;
                for (const Pt& p : dots) {
                    if (thiefPattern->currentIndex() == 1) {
                        const double h = opt.dotDiameter / 2.0;
                        cells.push_back(Geom::box(p.x - h, p.y - h, p.x + h, p.y + h));
                    } else if (thiefPattern->currentIndex() == 2) {
                        const double h = opt.dotDiameter / 2.0;
                        cells.push_back(Geom::line({{p.x - h, p.y}, {p.x + h, p.y}})
                                            .buffer(opt.dotDiameter / 2.0, 8));
                    } else {
                        cells.push_back(Geom::circle(p, opt.dotDiameter / 2.0, 16));
                    }
                }
                const int newId = canvas_->appendGeometry(cells, {}, true);
                canvas_->endGeometry();
                addObjectRow(tr("Copper Thieving"), "gerber", "-", newId, true, toolsGroup_);
                statusBar()->showMessage(tr("Thieving: %1 puntos").arg(dots.size()), 4000);
            });
    toolTabs_[tr("Copper Thieving")] = wrapScroll(thiefPage);

    // --- Film
    auto* filmPage = new QWidget(this);
    auto* filmForm = new QFormLayout(filmPage);
    pageTitle(filmForm, tr("Film"), tr("Combina capas visibles y exporta un film para imprimir."));
    auto* filmCreate = new QPushButton(originalIcon("geometry"), tr("Combinar capas visibles"), this);
    auto* filmExport = new QPushButton(originalIcon("save_as"), tr("Exportar film SVG"), this);
    filmForm->addRow(filmCreate);
    filmForm->addRow(filmExport);
    connect(filmCreate, &QPushButton::clicked, this, &MainWindow::createFilm);
    connect(filmExport, &QPushButton::clicked, this, &MainWindow::exportFilm);
    toolTabs_[tr("Film")] = wrapScroll(filmPage);

    // --- Solder paste dispensing
    auto* pastePage = new QWidget(this);
    auto* pasteForm = new QFormLayout(pastePage);
    pageTitle(pasteForm, tr("Solder Paste"), tr("Genera G-code de dosificación de pasta desde un Gerber de máscara."));
    pasteSource_ = new QComboBox(this);
    pasteSource_->setToolTip(tr("Gerber de máscara de pasta: cada pad es un punto de dosificación."));
    pasteForm->addRow(tr("GERBER:"), pasteSource_);
    section(pasteForm, tr("Parámetros de dosificación"));
    auto* pasteNozzle = makeSpin(0.05, 5.0, 0.5, 3, " mm",
        tr("<b>Boquilla</b><br>Diámetro de la aguja dosificadora."));
    auto* pasteTravel = makeSpin(0.0, 50.0, 5.0, 3, " mm",
        tr("<b>Altura Z p/recorr.</b><br>Altura entre pads."));
    auto* pasteDispense = makeSpin(-10.0, 10.0, -0.2, 3, " mm",
        tr("<b>Z de dosificación</b><br>Altura donde se deposita la pasta."));
    auto* pasteRetract = makeSpin(-10.0, 20.0, 0.5, 3, " mm",
        tr("<b>Z de retracción</b><br>Altura tras dosificar."));
    auto* pasteFeedZ = makeSpin(0.1, 5000.0, 60.0, 1, " mm/min",
        tr("<b>Avance Z</b><br>Velocidad de bajada y subida."));
    auto* pasteDwell = makeSpin(0.0, 20.0, 0.3, 2, " s",
        tr("<b>Pausa</b><br>Tiempo de dosificación en cada pad."));
    addRow(pasteForm, tr("Boquilla:"), pasteNozzle);
    addRow(pasteForm, tr("Z viaje:"), pasteTravel);
    addRow(pasteForm, tr("Z dosificar:"), pasteDispense);
    addRow(pasteForm, tr("Z retraer:"), pasteRetract);
    addRow(pasteForm, tr("Avance Z:"), pasteFeedZ);
    addRow(pasteForm, tr("Pausa:"), pasteDwell);
    auto* nozzleTable = new QTableWidget(0, 2, this);
    nozzleTable->setHorizontalHeaderLabels({tr("Ø boquilla"), tr("Pausa (s)")});
    nozzleTable->horizontalHeader()->setStretchLastSection(true);
    nozzleTable->verticalHeader()->setVisible(false);
    const double defaultNozzles[][2] = {{0.5, 0.3}, {0.8, 0.4}, {1.2, 0.5}};
    for (const auto& row : defaultNozzles) {
        const int index = nozzleTable->rowCount();
        nozzleTable->insertRow(index);
        nozzleTable->setItem(index, 0, new QTableWidgetItem(QString::number(row[0], 'f', 3)));
        nozzleTable->setItem(index, 1, new QTableWidgetItem(QString::number(row[1], 'f', 2)));
    }
    pasteForm->addRow(tr("Boquillas:"), nozzleTable);
    markAdvanced(pasteForm, nozzleTable);
    auto* pasteDirection = new QComboBox(this);
    pasteDirection->addItems({tr("Normal"), tr("Inverso")});
    addRow(pasteForm, tr("Dirección:"), pasteDirection);
    markAdvanced(pasteForm, pasteDirection);
    auto* pasteGenerate = new QPushButton(originalIcon("geometry"), tr("Generar G-code"), this);
    auto* pasteExport = new QPushButton(originalIcon("save_as"), tr("Exportar G-code"), this);
    pasteForm->addRow(pasteGenerate);
    pasteForm->addRow(pasteExport);
    connect(pasteGenerate, &QPushButton::clicked, this,
            [this, pasteNozzle, pasteTravel, pasteDispense, pasteRetract, pasteFeedZ, pasteDwell,
             nozzleTable, pasteDirection] {
                GerberResult* src = sourceGerber(pasteSource_);
                if (!src) return;
                PasteJob job;
                job.reverse = pasteDirection->currentIndex() == 1;
                job.nozzleDiameter = pasteNozzle->value();
                std::vector<double> nozzleDias;
                std::vector<double> nozzleDwells;
                for (int row = 0; row < nozzleTable->rowCount(); ++row) {
                    auto* dia = nozzleTable->item(row, 0);
                    auto* dwell = nozzleTable->item(row, 1);
                    if (!dia) continue;
                    nozzleDias.push_back(dia->text().toDouble());
                    nozzleDwells.push_back(dwell ? dwell->text().toDouble() : pasteDwell->value());
                }
                if (nozzleDias.empty()) {
                    nozzleDias.push_back(pasteNozzle->value());
                    nozzleDwells.push_back(pasteDwell->value());
                }
                job.nozzleDiameters = nozzleDias;
                std::set<std::pair<long long, long long>> seen;
                for (const Geom& g : src->solids) {
                    if (!g.valid() || g.empty()) continue;
                    const BBox b = g.bounds();
                    if (!b.valid) continue;
                    const Pt c{(b.xmin + b.xmax) * 0.5, (b.ymin + b.ymax) * 0.5};
                    const double padSize = std::max(b.width(), b.height());
                    const auto key = std::make_pair(std::llround(c.x * 100.0),
                                                    std::llround(c.y * 100.0));
                    if (!seen.insert(key).second) continue;
                    int nozzle = 0;
                    for (std::size_t n = 0; n < nozzleDias.size(); ++n) {
                        if (nozzleDias[n] >= padSize) { nozzle = static_cast<int>(n); break; }
                        nozzle = static_cast<int>(n);
                    }
                    job.points.push_back(c);
                    job.padSizes.push_back(padSize);
                    job.nozzles.push_back(nozzle);
                    job.nozzleDwells.push_back(nozzleDwells[static_cast<std::size_t>(nozzle)]);
                }
                if (job.points.empty()) {
                    statusBar()->showMessage(tr("El Gerber no tiene pads"), 4000);
                    return;
                }
                BBox b;
                for (const Pt& p : job.points) {
                    if (!b.valid) b = BBox{p.x, p.y, p.x, p.y};
                    else {
                        b.xmin = std::min(b.xmin, p.x);
                        b.ymin = std::min(b.ymin, p.y);
                        b.xmax = std::max(b.xmax, p.x);
                        b.ymax = std::max(b.ymax, p.y);
                    }
                }
                job.bounds = b;
                lastPasteJob_ = job;
                lastPasteOptions_ = GCodeOptions{};
                lastPasteOptions_.units = defaultUnits_;
                lastPasteOptions_.zMove = pasteTravel->value();
                lastPasteOptions_.zStart = pasteTravel->value();
                lastPasteOptions_.zCut = pasteDispense->value();
                lastPasteOptions_.zEnd = pasteRetract->value();
                lastPasteOptions_.feedZ = pasteFeedZ->value();
                lastPasteOptions_.toolDiameter = job.nozzleDiameter;
                lastPasteOptions_.dwellTime = pasteDwell->value();
                statusBar()->showMessage(tr("Pasta: %1 puntos").arg(job.points.size()), 4000);
            });
    connect(pasteExport, &QPushButton::clicked, this, [this] {
        if (!lastPasteJob_) return;
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Exportar G-code de pasta"),
            QFileInfo(lastDir_).absolutePath() + "/solderpaste.nc", tr("G-code (*.nc *.gcode)"));
        if (path.isEmpty()) return;
        std::ofstream out(path.toStdString());
        const std::string pasteCode = pasteGCode(*lastPasteJob_, lastPasteOptions_);
        out << pasteCode;
        addCncJobObject(QFileInfo(path).fileName(), QString::fromStdString(pasteCode),
                    cncProps(lastPasteOptions_, tr("Solder Paste")));
        statusBar()->showMessage(tr("G-code de pasta guardado"), 5000);
    });
    toolTabs_[tr("Solder Paste")] = wrapScroll(pastePage);

    // --- QR Code
    auto* qrPage = new QWidget(this);
    auto* qrForm = new QFormLayout(qrPage);
    pageTitle(qrForm, tr("QR Code"), tr("Genera un código QR como cobre Gerber."));
    section(qrForm, tr("Contenido"));
    auto* qrText = new QPlainTextEdit(this);
    qrText->setPlainText("https://flatcam.org");
    qrText->setMaximumHeight(90);
    qrForm->addRow(qrText);
    section(qrForm, tr("Parámetros"));
    auto* qrModule = makeSpin(0.05, 10.0, 1.0, 3, " mm",
        tr("<b>Tamaño de módulo</b><br>Lado de cada cuadro del QR."));
    auto* qrBorder = new QSpinBox(this);
    qrBorder->setRange(0, 20);
    qrBorder->setValue(4);
    qrBorder->setToolTip(tr("Cantidad de módulos de margen alrededor del código."));
    auto* qrEcc = new QComboBox(this);
    qrEcc->addItem(tr("Baja (L)"), 'L');
    qrEcc->addItem(tr("Media (M)"), 'M');
    qrEcc->addItem(tr("Cuartil (Q)"), 'Q');
    qrEcc->addItem(tr("Alta (H)"), 'H');
    qrEcc->setCurrentIndex(1);
    auto* qrX = makeSpin(-10000.0, 10000.0, 0.0, 3, " mm", tr("<b>X</b><br>Origen del QR."));
    auto* qrY = makeSpin(-10000.0, 10000.0, 0.0, 3, " mm", tr("<b>Y</b><br>Origen del QR."));
    addRow(qrForm, tr("Módulo:"), qrModule);
    addRow(qrForm, tr("Borde:"), qrBorder);
    addRow(qrForm, tr("Corrección:"), qrEcc);
    addRow(qrForm, tr("Origen X:"), qrX);
    addRow(qrForm, tr("Origen Y:"), qrY);
    auto* qrGenerate = new QPushButton(originalIcon("qrcode"), tr("Generar QR"), this);
    auto* qrExport = new QPushButton(originalIcon("save_as"), tr("Exportar Gerber"), this);
    qrForm->addRow(qrGenerate);
    qrForm->addRow(qrExport);
    connect(qrGenerate, &QPushButton::clicked, this, [this, qrText, qrModule, qrBorder, qrEcc, qrX, qrY] {
        const QString text = qrText->toPlainText();
        if (text.isEmpty()) return;
        const auto geom = qrCodeGeometry(text.toStdString(), qrModule->value(), qrBorder->value(),
                                         qrX->value(), qrY->value(),
                                         qrEcc->currentData().toChar().toLatin1());
        if (geom.empty() || !geom.front().valid()) return;
        const int id = addGeneratedSolid(tr("QR Code"), geom, 0, "gerber");
        if (id > 0) {
            GerberResult r;
            r.solids = geom;
            r.solid = geom.front();
            r.bounds = geom.front().bounds();
            r.fileUnits = defaultUnits_;
            gerberLayers_[id] = r;
            lastGerber_ = r;
            refreshSourceCombos();
            canvas_->fitTo(r.bounds);
        }
        statusBar()->showMessage(tr("QR generado"), 4000);
    });
    connect(qrExport, &QPushButton::clicked, this, [this, qrText, qrModule, qrBorder, qrEcc, qrX, qrY] {
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Exportar QR"), QFileInfo(lastDir_).absolutePath() + "/qrcode.gbr",
            tr("Gerber (*.gbr);;Todos (*)"));
        if (path.isEmpty()) return;
        const auto geom = qrCodeGeometry(qrText->toPlainText().toStdString(), qrModule->value(),
                                         qrBorder->value(), qrX->value(), qrY->value(),
                                         qrEcc->currentData().toChar().toLatin1());
        try {
            writeGerber(path.toStdString(), geom, {}, defaultUnits_);
            statusBar()->showMessage(tr("QR exportado"), 4000);
        } catch (const std::exception& e) {
            showError(QString::fromUtf8(e.what()));
        }
    });
    toolTabs_[tr("QR Code")] = wrapScroll(qrPage);

    refreshSourceCombos();
    applyUiLevel();
}

/// @brief Aplica NCC (hatch o paint) al objeto activo.
bool MainWindow::applyNcc(double toolDiameter, double stepOverPercent, double clearance,
                          double angle, double margin, bool paintStrategy,
                          const fc::GCodeOptions& gcode) {
    if (!lastGerber_) return false;
    NccOptions opt;
    opt.toolDiameter = toolDiameter;
    opt.stepOver = stepOverPercent / 100.0;
    opt.clearance = clearance;
    opt.angleDeg = angle;
    opt.margin = margin;
    opt.strategy = paintStrategy ? NccStrategy::Paint : NccStrategy::Hatch;

    const BBox b = lastGerber_->bounds.expanded(margin);
    const Geom area = b.valid ? Geom::box(b.xmin, b.ymin, b.xmax, b.ymax) : Geom();
    lastNcc_ = clearCopper(lastGerber_->solid, area, opt);
    lastNccGCode_ = gcode;
    lastNccGCode_.toolDiameter = toolDiameter;
    lastNccHeader_ = gcode;
    lastNccMulti_.clear();
    {
        fc::PathToolJob job;
        job.number = 1;
        job.diameter = toolDiameter;
        job.options = gcode;
        job.bounds = lastNcc_->bounds;
        for (const Geom& g : lastNcc_->paths) {
            Ring ring = g.coords();
            if (ring.size() >= 2) job.paths.emplace_back(ring.begin(), ring.end());
        }
        lastNccMulti_.push_back(std::move(job));
    }
    int sourceId = 0;
    if (auto* cur = objects_->currentItem()) sourceId = cur->data(0, Qt::UserRole).toInt();
    addGeneratedLayer(toolLayerName(tr("NCC")), lastNcc_->paths, sourceId);
    statusBar()->showMessage(tr("NCC: %1 recorridos").arg(lastNcc_->paths.size()), 4000);
    return true;
}

/// Aplica pintura por contornos paralelos.
bool MainWindow::applyPaint(double toolDiameter, double stepOverPercent, double margin,
                            const fc::GCodeOptions& gcode) {
    if (!lastGerber_) return false;
    PaintOptions opt;
    opt.toolDiameter = toolDiameter;
    opt.stepOver = stepOverPercent / 100.0;
    opt.margin = margin;
    lastPaint_ = paintArea(lastGerber_->solid, opt);
    lastPaintGCode_ = gcode;
    lastPaintGCode_.toolDiameter = toolDiameter;
    lastPaintHeader_ = gcode;
    lastPaintMulti_.clear();
    {
        fc::PathToolJob job;
        job.number = 1;
        job.diameter = toolDiameter;
        job.options = gcode;
        job.bounds = lastPaint_->bounds;
        for (const Geom& g : lastPaint_->paths) {
            Ring ring = g.coords();
            if (ring.size() >= 2) job.paths.emplace_back(ring.begin(), ring.end());
        }
        lastPaintMulti_.push_back(std::move(job));
    }
    int sourceId = 0;
    if (auto* cur = objects_->currentItem()) sourceId = cur->data(0, Qt::UserRole).toInt();
    addGeneratedLayer(toolLayerName(tr("Pintura")), lastPaint_->paths, sourceId);
    statusBar()->showMessage(tr("Pintado: %1 recorridos").arg(lastPaint_->paths.size()), 4000);
    return true;
}

/// @brief Aplica taladrado al Excellon activo.
bool MainWindow::applyDrill(const fc::GCodeOptions& gcode) {
    if (!lastExcellon_) return false;
    lastDrillJob_ = drillJobFromExcellon(*lastExcellon_);
    lastDrillGCode_ = gcode;
    optimizeDrillOrder(*lastDrillJob_);
    statusBar()->showMessage(tr("Taladrado listo: %1 herramientas").arg(lastDrillJob_->tools.size()),
                             4000);
    return true;
}

/// @brief Exporta el G-code de fresado.
void MainWindow::exportMillGCode() {
    if (!lastMillJob_ || lastMillJob_->paths.empty()) {
        QMessageBox::information(this, tr("Exportar fresado"),
                                 tr("Genere primero el fresado desde el panel."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Guardar G-code de fresado"),
                                                      QFileInfo(lastDir_).absolutePath() + "/mill.nc",
                                                      tr("G-code (*.nc *.gcode);;Todos (*)"));
    if (path.isEmpty()) return;
    const std::string code = pathsGCode(*lastMillJob_, lastMillOptions_);
    std::ofstream out(path.toStdString(), std::ios::binary);
    if (!out) {
        showError(tr("No se pudo escribir el archivo"));
        return;
    }
    out << code;
    addCncJobObject(QFileInfo(path).fileName(), QString::fromStdString(code),
                    cncProps(lastMillOptions_, tr("Fresado")));
    statusBar()->showMessage(tr("G-code de fresado guardado"), 4000);
}

/// @brief Exporta el G-code de taladrado.
void MainWindow::exportDrillGCode() {
    if (!lastDrillJob_) {
        QMessageBox::information(this, tr("Exportar taladrado"),
                                 tr("Genere primero el taladrado desde el panel de herramientas."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Guardar G-code de taladrado"),
                                                      QFileInfo(lastDir_).absolutePath() + "/drill.nc",
                                                      tr("G-code (*.nc *.gcode);;Todos (*)"));
    if (path.isEmpty()) return;
    const std::string code = drillingGCode(*lastDrillJob_, lastDrillGCode_);
    std::ofstream out(path.toStdString(), std::ios::binary);
    if (!out) {
        showError(tr("No se pudo escribir el archivo"));
        return;
    }
    out << code;
    addCncJobObject(QFileInfo(path).fileName(), QString::fromStdString(code),
                    cncProps(lastDrillGCode_, tr("Taladrado")));
    statusBar()->showMessage(tr("G-code de taladrado guardado"), 4000);
}

/// @brief Carga un Gerber y crea su capa.
bool MainWindow::addGerber(const QString& path) {
    fc::GerberOptions opt;
    opt.appUnits = defaultUnits_;
    opt.stepsPerCircle = stepsPerCircle_;
    opt.simplification = gerberSimplify_;
    opt.simplifyTolerance = gerberTolerance_;
    const fc::GerberResult r = fc::GerberParser(opt).parseFile(path.toStdString());

    const bool first = canvas_->layerIds().empty();
    if (first) canvas_->setGeometryData(r.solids, r.follows);
    const int layerId = first ? canvas_->layerIds().front()
                              : canvas_->appendGeometry(r.solids, r.follows);
    gerberLayers_[layerId] = r;
    sourcePaths_[layerId] = path;
    lastGerber_ = r;
    lastExcellon_.reset();

    auto* item = addObjectRow(QFileInfo(path).fileName(), "gerber",
                              QString::number(r.solid.area(), 'f', 2), layerId);

    std::ostringstream os;
    os << "\n=== " << QFileInfo(path).fileName().toStdString() << " ===\n";
    os << "gerber · " << r.apertures.size() << " aperturas · área " << r.solid.area() << "\n";
    info_->setPlainText(info_->toPlainText() + QString::fromStdString(os.str()));
    return true;
}

/// @brief Devuelve el Gerber asociado a un selector.
fc::GerberResult* MainWindow::sourceGerber(QComboBox* combo) {
    if (combo) {
        const int id = combo->currentData().toInt();
        const auto it = gerberLayers_.find(id);
        if (it != gerberLayers_.end()) return &it->second;
    }
    return lastGerber_ ? &*lastGerber_ : nullptr;
}

/// @brief Devuelve el Excellon asociado a un selector.
fc::ExcellonResult* MainWindow::sourceExcellon(QComboBox* combo) {
    if (combo) {
        const int id = combo->currentData().toInt();
        const auto it = excellonLayers_.find(id);
        if (it != excellonLayers_.end()) return &it->second;
    }
    return lastExcellon_ ? &*lastExcellon_ : nullptr;
}

/// @brief Actualiza los selectores de objeto de cada herramienta.
void MainWindow::refreshSourceCombos() {
    auto fill = [this](QComboBox* combo, const QString& kind) {
        if (!combo) return;
        const int keep = combo->currentData().toInt();
        combo->blockSignals(true);
        combo->clear();
        for (QTreeWidgetItem* item : objectItems()) {
            if (item->text(2).compare(kind, Qt::CaseInsensitive) != 0) continue;
            const int id = item->data(0, Qt::UserRole).toInt();
            if (id <= 0) continue;
            combo->addItem(item->text(1), id);
        }
        const int index = combo->findData(keep);
        if (index >= 0) combo->setCurrentIndex(index);
        combo->blockSignals(false);
    };
    fill(isoSource_, "gerber");
    fill(nccSource_, "gerber");
    fill(cutSource_, "gerber");
    fill(paintSource_, "gerber");
    fill(drillSource_, "excellon");
    fill(millSource_, "gerber");
    fill(thiefSource_, "gerber");
    fill(pasteSource_, "gerber");
}

/// @brief Llena la tabla de taladrado desde el Excellon.
void MainWindow::refreshDrillTable() {
    if (!drillTable_) return;
    drillTable_->setRowCount(0);
    if (!lastExcellon_) return;
    for (const auto& [number, tool] : lastExcellon_->tools) {
        if (tool.drills.empty() && tool.slotSegments.empty()) continue;
        const int row = drillTable_->rowCount();
        drillTable_->insertRow(row);
        auto set = [&](int col, const QString& text, bool editable = true) {
            auto* item = new QTableWidgetItem(text);
            if (!editable) item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            drillTable_->setItem(row, col, item);
        };
        set(0, QString::number(number), false);
        set(1, lengthText(tool.diameter, 3), false);
        set(2, "100.0");
        set(3, "20.0");
        set(4, "10000");
        set(5, "0.0");
        set(6, lengthText(-2.4, 3));
        set(7, lengthText(0.0, 3));
    }
}

/// @brief Carga un Excellon y crea su capa.
bool MainWindow::addExcellon(const QString& path) {
    fc::ExcellonOptions opt;
    opt.appUnits = defaultUnits_;
    opt.stepsPerCircle = stepsPerCircle_;
    const fc::ExcellonResult r = fc::ExcellonParser(opt).parseFile(path.toStdString());

    const bool first = canvas_->layerIds().empty();
    if (first) canvas_->setGeometryData(r.geometry, {});
    const int layerId = first ? canvas_->layerIds().front()
                              : canvas_->appendGeometry(r.geometry, {});
    excellonLayers_[layerId] = r;
    sourcePaths_[layerId] = path;
    lastExcellon_ = r;
    lastGerber_.reset();
    lastDrillJob_ = fc::drillJobFromExcellon(r);
    refreshDrillTable();

    auto* item = addObjectRow(QFileInfo(path).fileName(), "excellon",
                              QString::number(r.solid.area(), 'f', 2), layerId);

    std::ostringstream os;
    os << "\n=== " << QFileInfo(path).fileName().toStdString() << " ===\n";
    os << "excellon · " << r.drillCount << " taladros · " << r.tools.size() << " herramientas\n";
    info_->setPlainText(info_->toPlainText() + QString::fromStdString(os.str()));
    return true;
}

/// @brief Agrega un Gerber al canvas y al árbol.
void MainWindow::showGerber(const QString& path) {
    fc::GerberOptions opt;
    opt.appUnits = fc::Units::Mm;
    const fc::GerberResult r = fc::GerberParser(opt).parseFile(path.toStdString());
    lastExcellon_.reset();
    lastGerber_ = r;
    lastIsolation_.reset();
    lastCutout_.reset();
    lastNcc_.reset();
    lastPaint_.reset();
    resetObjectGroups();
    canvas_->setGeometryData(r.solids, r.follows);
    auto* item = addObjectRow(QFileInfo(path).fileName(), "gerber",
                              QString::number(r.solid.area(), 'f', 2),
                              canvas_->layerIds().front());
    canvas_->setToolpaths({});

    std::ostringstream os;
    os << "GERBER\n";
    os << "archivo     : " << QFileInfo(path).fileName().toStdString() << "\n";
    os << "unidades    : " << fc::toString(r.fileUnits) << " (visualización "
       << fc::toString(opt.appUnits) << ")\n";
    os << "aperturas   : " << r.apertures.size() << "\n";
    os << "flashes     : " << r.flashes << "\n";
    os << "segmentos   : " << r.segments << "\n";
    os << "regiones    : " << r.regions << "\n";
    os << "área cobre  : " << r.solid.area() << "\n";
    if (r.bounds.valid) {
        os << "límites     : " << r.bounds.xmin << ", " << r.bounds.ymin << "  ..  " << r.bounds.xmax
           << ", " << r.bounds.ymax << "\n";
    }
    os << "aperturas:\n";
    for (const auto& [id, ap] : r.apertures) {
        os << "  D" << id << "  ";
        switch (ap.kind) {
            case fc::GerberAperture::Kind::Circle:
                os << "círculo Ø" << ap.size;
                break;
            case fc::GerberAperture::Kind::Rectangle:
                os << "rectángulo " << ap.width << " x " << ap.height;
                break;
            case fc::GerberAperture::Kind::Obround:
                os << "obround " << ap.width << " x " << ap.height;
                break;
            case fc::GerberAperture::Kind::Polygon:
                os << "polígono Ø" << ap.diameter << " (" << ap.vertices << " lados)";
                break;
            case fc::GerberAperture::Kind::Macro:
                os << "macro " << ap.macroName;
                break;
            default:
                os << "región";
                break;
        }
        os << "\n";
    }
    os << "colores     : cobre (ámbar), recorridos (azul), rejilla (gris)\n";
    os << "advertencias: " << r.warnings.size() << "\n";
    for (std::size_t i = 0; i < r.warnings.size() && i < 15; ++i) os << "  - " << r.warnings[i] << "\n";
    setInfo(QString::fromStdString(os.str()));

    statusLabel_->setText(tr("%1 · %2 · %3 objetos")
                              .arg(QFileInfo(path).fileName())
                              .arg(fc::toString(r.fileUnits).c_str())
                              .arg(r.apertures.size() + r.flashes));
    statusBar()->showMessage(tr("Gerber cargado"), 3000);
}

/// @brief Agrega un Excellon al canvas y al árbol.
void MainWindow::showExcellon(const QString& path) {
    fc::ExcellonOptions opt;
    opt.appUnits = fc::Units::Mm;
    const fc::ExcellonResult r = fc::ExcellonParser(opt).parseFile(path.toStdString());
    lastExcellon_ = r;
    lastGerber_.reset();
    lastIsolation_.reset();
    lastCutout_.reset();
    lastNcc_.reset();
    lastPaint_.reset();
    resetObjectGroups();
    canvas_->setGeometryData(r.geometry, {});
    auto* item = addObjectRow(QFileInfo(path).fileName(), "excellon",
                              QString::number(r.solid.area(), 'f', 2),
                              canvas_->layerIds().front());
    canvas_->setToolpaths({});

    std::ostringstream os;
    os << "EXCELLON\n";
    os << "archivo     : " << QFileInfo(path).fileName().toStdString() << "\n";
    os << "unidades    : " << fc::toString(r.fileUnits) << " (visualización "
       << fc::toString(opt.appUnits) << ")\n";
    os << "herramientas: " << r.tools.size() << "\n";
    os << "perforaciones: " << r.drillCount << "\n";
    os << "ranuras     : " << r.slotCount << "\n";
    os << "área         : " << r.solid.area() << "\n";
    if (r.bounds.valid) {
        os << "límites     : " << r.bounds.xmin << ", " << r.bounds.ymin << "  ..  " << r.bounds.xmax
           << ", " << r.bounds.ymax << "\n";
    }
    for (const auto& [num, tool] : r.tools) {
        os << "  T" << num << " Ø" << tool.diameter << "  (" << tool.drills.size() << " taladros, "
           << tool.slotSegments.size() << " ranuras)\n";
    }
    for (const std::string& w : r.warnings) os << "  aviso: " << w << "\n";
    setInfo(QString::fromStdString(os.str()));

    statusLabel_->setText(tr("%1 · %2 · %3 taladros")
                              .arg(QFileInfo(path).fileName())
                              .arg(fc::toString(r.fileUnits).c_str())
                              .arg(r.drillCount));
    statusBar()->showMessage(tr("Excellon cargado"), 3000);
}

/// @brief Escribe texto en el panel de informe.
void MainWindow::setInfo(const QString& text) { info_->setPlainText(text); }

/// @brief Muestra un error en un cuadro de diálogo.
void MainWindow::showError(const QString& message) {
    QMessageBox::critical(this, tr("Error"), message);
    statusBar()->showMessage(tr("Error al abrir el archivo"), 5000);
}

/// @brief Ajusta el canvas a todo el contenido.
void MainWindow::fitView() { canvas_->fit(); }

/// @brief Abre la herramienta de aislamiento.
void MainWindow::isolateDialog() {
    if (!lastGerber_) {
        QMessageBox::information(this, tr("Aislar cobre"),
                                 tr("Abra primero un archivo Gerber para poder aislar el cobre."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Aislar cobre"));
    auto* form = new QFormLayout(&dialog);

    auto* diameter = new UnitSpinBox(&dialog);
    diameter->setRange(0.01, 20.0);
    diameter->setDecimals(3);
    diameter->setValue(0.2);
    diameter->setSuffix(" mm");
    applyTip(diameter, tr("Diámetro de la fresa de aislamiento."));
    form->addRow(tr("Diámetro de herramienta:"), diameter);

    auto* passes = new QSpinBox(&dialog);
    applyTip(passes, tr("Cantidad de pasadas alrededor del cobre."));
    passes->setRange(1, 20);
    passes->setValue(1);
    form->addRow(tr("Pasadas:"), passes);

    auto* overlap = new UnitSpinBox(&dialog);
    overlap->setRange(0.0, 90.0);
    overlap->setDecimals(1);
    overlap->setValue(0.0);
    overlap->setSuffix(" %");
    applyTip(overlap, tr("Superposición entre pasadas para evitar rebabas."));
    form->addRow(tr("Solape entre pasadas:"), overlap);

    auto* invert = new QCheckBox(tr("Invertir sentido de corte"), &dialog);
    applyTip(invert, tr("Cambia el sentido de giro (convencional / climb)."));
    form->addRow(QString(), invert);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) return;

    applyIsolation(diameter->value(), passes->value(), overlap->value(), invert->isChecked());
}

// ============================================================================
// Herramientas CAM
// ============================================================================

/**
 * @brief Ejecuta el aislamiento sobre el Gerber activo.
 * @param toolDiameter Diámetro de fresa.
 * @param passes Pasadas.
 * @param overlapPercent Solape en porcentaje.
 * @param invert Invierte el sentido.
 * @param gcode Parámetros de G-code opcionales.
 * @return `true` si se generaron recorridos.
 */
bool MainWindow::applyIsolation(double toolDiameter, int passes, double overlapPercent, bool invert,
                                const fc::GCodeOptions& gcode) {
    if (!lastGerber_) return false;

    fc::IsolationOptions opt;
    opt.toolDiameter = toolDiameter;
    opt.passes = passes;
    opt.overlap = overlapPercent / 100.0;
    opt.invert = invert;

    lastIsoToolDiameter_ = opt.toolDiameter;
    lastIsolation_ = fc::isolateCopper(lastGerber_->solid, opt);
    lastCutout_.reset();
    lastIsoHeader_ = gcode;
    lastIsoMulti_.clear();
    {
        fc::PathToolJob job;
        job.number = 1;
        job.diameter = opt.toolDiameter;
        job.options = gcode;
        job.bounds = lastIsolation_->bounds;
        for (const Geom& g : lastIsolation_->paths) {
            Ring ring = g.coords();
            if (ring.size() >= 2) job.paths.emplace_back(ring.begin(), ring.end());
        }
        lastIsoMulti_.push_back(std::move(job));
    }
    int sourceId = 0;
    if (auto* cur = objects_->currentItem()) sourceId = cur->data(0, Qt::UserRole).toInt();
    addGeneratedLayer(toolLayerName(tr("Aislamiento")), lastIsolation_->paths, sourceId);

    std::ostringstream os;
    os << "\n--- AISLAMIENTO ---\n";
    os << "herramienta : Ø" << opt.toolDiameter << " mm\n";
    os << "pasadas     : " << opt.passes << "\n";
    os << "solape      : " << overlapPercent << " %\n";
    os << "recorridos  : " << lastIsolation_->paths.size() << "\n";
    os << "longitud    : " << lastIsolation_->totalLength << " mm\n";
    info_->setPlainText(info_->toPlainText() + QString::fromStdString(os.str()));

    statusBar()->showMessage(tr("Aislamiento generado: %1 recorridos")
                                 .arg(lastIsolation_->paths.size()),
                             5000);
    return true;
}

/// @brief Abre la herramienta de corte.
void MainWindow::cutoutDialog() {
    if (!lastGerber_) {
        QMessageBox::information(this, tr("Cortar contorno"),
                                 tr("Abra primero un archivo Gerber (por ejemplo la capa de "
                                    "contorno de la placa)."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Cortar contorno"));
    auto* form = new QFormLayout(&dialog);

    auto* diameter = new UnitSpinBox(&dialog);
    diameter->setRange(0.01, 20.0);
    diameter->setDecimals(3);
    diameter->setValue(1.0);
    diameter->setSuffix(" mm");
    applyTip(diameter, tr("Diámetro de la fresa de corte."));
    form->addRow(tr("Diámetro de herramienta:"), diameter);

    auto* margin = new UnitSpinBox(&dialog);
    margin->setRange(-50.0, 50.0);
    margin->setDecimals(3);
    margin->setValue(0.0);
    margin->setSuffix(" mm");
    applyTip(margin, tr("Separación extra entre el contorno y el corte."));
    form->addRow(tr("Margen extra:"), margin);

    auto* internal = new QCheckBox(tr("Corte interno (pocket)"), &dialog);
    applyTip(internal, tr("Corta por dentro del contorno en vez de por fuera."));
    form->addRow(QString(), internal);

    auto* zcut = new UnitSpinBox(&dialog);
    zcut->setRange(-50.0, 0.0);
    zcut->setDecimals(3);
    zcut->setValue(-1.8);
    zcut->setSuffix(" mm");
    applyTip(zcut, tr("Profundidad final de corte (negativa)."));
    form->addRow(tr("Profundidad de corte:"), zcut);

    auto* depth = new UnitSpinBox(&dialog);
    depth->setRange(0.0, 20.0);
    depth->setDecimals(3);
    depth->setValue(0.6);
    depth->setSuffix(" mm");
    applyTip(depth, tr("Cuánto baja la fresa en cada pasada."));
    form->addRow(tr("Prof. por pasada:"), depth);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) return;

    fc::GCodeOptions opt;
    opt.zCut = zcut->value();
    opt.depthPerPass = depth->value();
    applyCutout(diameter->value(), margin->value(), !internal->isChecked(), opt);
}

/// Aplica corte de contorno con margen y lado externo/interno.
bool MainWindow::applyCutout(double toolDiameter, double margin, bool external,
                             const fc::GCodeOptions& gcode) {
    if (!lastGerber_) return false;

    fc::CutoutOptions opt;
    opt.toolDiameter = toolDiameter;
    opt.margin = margin;
    opt.external = external;

    lastCutoutOptions_ = opt;
    lastCutoutGCode_ = gcode;

    lastCutout_ = fc::cutoutGeometry(lastGerber_->solid, opt);
    lastCutHeader_ = gcode;
    lastCutMulti_.clear();
    {
        fc::PathToolJob job;
        job.number = 1;
        job.diameter = opt.toolDiameter;
        job.options = gcode;
        job.bounds = lastCutout_->bounds;
        for (const Geom& g : lastCutout_->paths) {
            Ring ring = g.coords();
            if (ring.size() >= 2) job.paths.emplace_back(ring.begin(), ring.end());
        }
        lastCutMulti_.push_back(std::move(job));
    }
    int sourceId = 0;
    if (auto* cur = objects_->currentItem()) sourceId = cur->data(0, Qt::UserRole).toInt();
    addGeneratedLayer(toolLayerName(tr("Corte")), lastCutout_->paths, sourceId);

    std::ostringstream os;
    os << "\n--- CORTE DE CONTORNO ---\n";
    os << "herramienta : Ø" << toolDiameter << " mm\n";
    os << "margen      : " << margin << " mm\n";
    os << "tipo        : " << (external ? "externo" : "interno") << "\n";
    os << "corte Z     : " << gcode.zCut << " mm (por pasada " << gcode.depthPerPass << " mm)\n";
    os << "recorridos  : " << lastCutout_->paths.size() << "\n";
    os << "longitud    : " << lastCutout_->totalLength << " mm\n";
    info_->setPlainText(info_->toPlainText() + QString::fromStdString(os.str()));

    statusBar()->showMessage(tr("Corte generado: %1 recorridos").arg(lastCutout_->paths.size()),
                             5000);
    return true;
}

/// @brief Exporta el último corte a G-code.
void MainWindow::exportCutoutGCode() {
    if (lastCutMulti_.empty()) {
        QMessageBox::information(this, tr("Exportar G-code de corte"),
                                 tr("Genere primero un corte desde Herramientas → Cortar contorno."));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Guardar G-code de corte"),
        QFileInfo(lastDir_).absolutePath() + "/cutout.nc",
        tr("G-code (*.nc *.gcode *.tap);;Todos (*)"));
    if (path.isEmpty()) return;

    fc::PathJob job;
    job.toolDiameter = lastCutoutOptions_.toolDiameter;
    job.bounds = lastCutout_->bounds;
    for (const fc::Geom& geom : lastCutout_->paths) {
        fc::Ring ring = geom.coords();
        if (ring.size() < 2) continue;
        job.paths.emplace_back(ring.begin(), ring.end());
    }

    const fc::GCodeOptions opt = cutOptions_ ? cutOptions_() : lastCutoutGCode_;
    const std::string code = fc::pathsGCode(job, opt);
    std::ofstream out(path.toStdString(), std::ios::binary);
    if (!out) {
        showError(tr("No se pudo escribir el archivo"));
        return;
    }
    out << code;
    addCncJobObject(QFileInfo(path).fileName(), QString::fromStdString(code),
                    cncProps(opt, tr("Corte")));
    statusBar()->showMessage(tr("G-code de corte guardado: %1").arg(QFileInfo(path).fileName()),
                             5000);
}

/// @brief Abre la herramienta NCC.
void MainWindow::nccDialog() {
    if (!lastGerber_) {
        QMessageBox::information(this, tr("NCC"),
                                 tr("Abra primero una capa Gerber para despejar el cobre."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Despejar cobre (NCC)"));
    auto* form = new QFormLayout(&dialog);

    auto* diameter = new UnitSpinBox(&dialog);
    diameter->setRange(0.01, 20.0);
    diameter->setDecimals(3);
    diameter->setValue(0.5);
    diameter->setSuffix(" mm");
    applyTip(diameter, tr("Diámetro de la herramienta de despeje."));
    form->addRow(tr("Diámetro de herramienta:"), diameter);

    auto* step = new UnitSpinBox(&dialog);
    step->setRange(5.0, 95.0);
    step->setDecimals(1);
    step->setValue(80.0);
    step->setSuffix(" %");
    applyTip(step, tr("Superposición entre pasadas. El paso real es diámetro × (100 − "
                      "superposición) %. Si en FlatCAM usabas 80 %, dejá 80 acá."));
    form->addRow(tr("Superposición:"), step);

    auto* clearance = new UnitSpinBox(&dialog);
    clearance->setRange(0.0, 10.0);
    clearance->setDecimals(3);
    clearance->setValue(0.1);
    clearance->setSuffix(" mm");
    applyTip(clearance, tr("Margen de seguridad respecto al cobre a conservar."));
    form->addRow(tr("Distancia al cobre:"), clearance);

    auto* angle = new UnitSpinBox(&dialog);
    angle->setRange(-90.0, 90.0);
    angle->setDecimals(1);
    angle->setValue(0.0);
    angle->setSuffix(" °");
    applyTip(angle, tr("Dirección del hatch; 45° suele dar mejor acabado."));
    form->addRow(tr("Ángulo del hatch:"), angle);

    auto* margin = new UnitSpinBox(&dialog);
    margin->setRange(0.0, 50.0);
    margin->setDecimals(2);
    margin->setValue(1.0);
    margin->setSuffix(" mm");
    applyTip(margin,
        tr("<b>Margen del área</b><br>"
           "Expande el área a despejar más allá del rectángulo que contiene el cobre.<br><br>"
           "• 0 mm: se despeja sólo dentro de los límites del cobre.<br>"
           "• 1 mm: agrega 1 mm alrededor (útil para limpiar hasta el borde de "
           "la placa o dejar lugar al corte).<br>"
           "• Ejemplo: cobre de −35 a +35 mm con margen 1 → NCC de −36 a +36 mm.<br><br>"
           "No cambia la distancia de seguridad al cobre; para eso está "
           "«Distancia al cobre»."));
    form->addRow(tr("Margen del área:"), margin);

    auto* strategy = new QComboBox(&dialog);
    strategy->addItems({tr("Hatch (líneas)"), tr("Paint (contornos)")});
    applyTip(strategy, tr("Hatch: líneas paralelas. Paint: contornos concéntricos."));
    form->addRow(tr("Estrategia:"), strategy);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    fc::NccOptions opt;
    opt.toolDiameter = diameter->value();
    opt.stepOver = (100.0 - step->value()) / 100.0;
    opt.clearance = clearance->value();
    opt.angleDeg = angle->value();
    opt.margin = margin->value();
    opt.strategy = strategy->currentIndex() == 1 ? fc::NccStrategy::Paint : fc::NccStrategy::Hatch;

    const fc::BBox b = lastGerber_->bounds.expanded(margin->value());
    const fc::Geom area = b.valid ? fc::Geom::box(b.xmin, b.ymin, b.xmax, b.ymax) : fc::Geom();

    lastNcc_ = fc::clearCopper(lastGerber_->solid, area, opt);
    lastNccGCode_ = fc::GCodeOptions{};
    lastNccGCode_.units = fc::Units::Mm;
    lastNccGCode_.toolDiameter = opt.toolDiameter;
    int sourceId = 0;
    if (auto* cur = objects_->currentItem()) sourceId = cur->data(0, Qt::UserRole).toInt();
    addGeneratedLayer(toolLayerName(tr("NCC")), lastNcc_->paths, sourceId);

    std::ostringstream os;
    os << "\n--- NCC ---\n";
    os << "herramienta : Ø" << opt.toolDiameter << " mm\n";
    os << "estrategia  : " << (opt.strategy == fc::NccStrategy::Paint ? "paint" : "hatch") << "\n";
    os << "paso        : " << step->value() << " %\n";
    os << "recorridos  : " << lastNcc_->paths.size() << "\n";
    os << "longitud    : " << lastNcc_->totalLength << " mm\n";
    info_->setPlainText(info_->toPlainText() + QString::fromStdString(os.str()));
    statusBar()->showMessage(tr("NCC generado: %1 recorridos").arg(lastNcc_->paths.size()), 5000);
}

/// @brief Exporta el último NCC a G-code.
void MainWindow::exportNccGCode() {
    if (lastNccMulti_.empty()) {
        QMessageBox::information(this, tr("Exportar NCC"),
                                 tr("Genere primero un NCC desde Herramientas → Despejar cobre."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Guardar G-code NCC"),
                                                      QFileInfo(lastDir_).absolutePath() + "/ncc.nc",
                                                      tr("G-code (*.nc *.gcode);;Todos (*)"));
    if (path.isEmpty()) return;

    const std::string code = fc::pathsGCodeMulti(lastNccMulti_, lastNccHeader_);
    std::ofstream out(path.toStdString(), std::ios::binary);
    if (!out) {
        showError(tr("No se pudo escribir el archivo"));
        return;
    }
    out << code;
    addCncJobObject(QFileInfo(path).fileName(), QString::fromStdString(code),
                    cncProps(lastNccHeader_, tr("NCC")));
    statusBar()->showMessage(tr("G-code NCC guardado: %1").arg(QFileInfo(path).fileName()), 5000);
}

/// @brief Abre la herramienta de pintura.
void MainWindow::paintDialog() {
    if (!lastGerber_) {
        QMessageBox::information(this, tr("Pintar"),
                                 tr("Abra primero una capa Gerber para pintar su área."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Pintar área"));
    auto* form = new QFormLayout(&dialog);

    auto* diameter = new UnitSpinBox(&dialog);
    diameter->setRange(0.01, 20.0);
    diameter->setDecimals(3);
    diameter->setValue(0.1);
    diameter->setSuffix(" mm");
    applyTip(diameter, tr("Diámetro de la herramienta de pintado."));
    form->addRow(tr("Diámetro de herramienta:"), diameter);

    auto* step = new UnitSpinBox(&dialog);
    step->setRange(5.0, 95.0);
    step->setDecimals(1);
    step->setValue(80.0);
    step->setSuffix(" %");
    applyTip(step, tr("Superposición entre pasadas. El paso real es diámetro × (100 − "
                      "superposición) %. Si en FlatCAM usabas 80 %, dejá 80 acá."));
    form->addRow(tr("Superposición:"), step);

    auto* margin = new UnitSpinBox(&dialog);
    margin->setRange(-10.0, 10.0);
    margin->setDecimals(3);
    margin->setValue(0.0);
    margin->setSuffix(" mm");
    applyTip(margin, tr("Expande o contrae el área a pintar."));
    form->addRow(tr("Margen:"), margin);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    fc::PaintOptions opt;
    opt.toolDiameter = diameter->value();
    opt.stepOver = (100.0 - step->value()) / 100.0;
    opt.margin = margin->value();

    lastPaint_ = fc::paintArea(lastGerber_->solid, opt);
    lastPaintGCode_ = fc::GCodeOptions{};
    lastPaintGCode_.units = fc::Units::Mm;
    lastPaintGCode_.toolDiameter = opt.toolDiameter;
    int sourceId = 0;
    if (auto* cur = objects_->currentItem()) sourceId = cur->data(0, Qt::UserRole).toInt();
    addGeneratedLayer(toolLayerName(tr("Pintura")), lastPaint_->paths, sourceId);

    std::ostringstream os;
    os << "\n--- PAINT ---\n";
    os << "herramienta : Ø" << opt.toolDiameter << " mm\n";
    os << "paso        : " << step->value() << " %\n";
    os << "recorridos  : " << lastPaint_->paths.size() << "\n";
    os << "longitud    : " << lastPaint_->totalLength << " mm\n";
    info_->setPlainText(info_->toPlainText() + QString::fromStdString(os.str()));
    statusBar()->showMessage(tr("Pintado generado: %1 recorridos").arg(lastPaint_->paths.size()), 5000);
}

/// @brief Exporta la última pintura a G-code.
void MainWindow::exportPaintGCode() {
    if (lastPaintMulti_.empty()) {
        QMessageBox::information(this, tr("Exportar pintado"),
                                 tr("Genere primero un pintado desde Herramientas → Pintar área."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Guardar G-code de pintado"),
                                                      QFileInfo(lastDir_).absolutePath() + "/paint.nc",
                                                      tr("G-code (*.nc *.gcode);;Todos (*)"));
    if (path.isEmpty()) return;

    const std::string code = fc::pathsGCodeMulti(lastPaintMulti_, lastPaintHeader_);
    std::ofstream out(path.toStdString(), std::ios::binary);
    if (!out) {
        showError(tr("No se pudo escribir el archivo"));
        return;
    }
    out << code;
    addCncJobObject(QFileInfo(path).fileName(), QString::fromStdString(code),
                    cncProps(lastPaintHeader_, tr("Pintura")));
    statusBar()->showMessage(tr("G-code de pintado guardado: %1").arg(QFileInfo(path).fileName()), 5000);
}

/// @brief Exporta el último aislamiento a G-code.
void MainWindow::exportIsolationGCode() {
    if (lastIsoMulti_.empty()) {
        QMessageBox::information(this, tr("Exportar G-code de aislamiento"),
                                 tr("Genere primero un aislamiento desde Herramientas → Aislar cobre."));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Guardar G-code de aislamiento"),
        QFileInfo(lastDir_).absolutePath() + "/isolation.nc",
        tr("G-code (*.nc *.gcode *.tap);;Todos (*)"));
    if (path.isEmpty()) return;

    fc::PathJob job;
    job.toolDiameter = lastIsoToolDiameter_;
    if (lastGerber_) job.bounds = lastGerber_->bounds;
    for (const fc::Geom& geom : lastIsolation_->paths) {
        fc::Ring ring = geom.coords();
        if (ring.size() < 2) continue;
        job.paths.emplace_back(ring.begin(), ring.end());
    }

    const fc::GCodeOptions opt = isoOptions_ ? isoOptions_() : fc::GCodeOptions{};
    const std::string code = fc::pathsGCode(job, opt);
    std::ofstream out(path.toStdString(), std::ios::binary);
    if (!out) {
        showError(tr("No se pudo escribir el archivo"));
        return;
    }
    out << code;
    addCncJobObject(QFileInfo(path).fileName(), QString::fromStdString(code),
                    cncProps(lastIsoHeader_, tr("Aislamiento")));
    statusBar()->showMessage(tr("G-code de aislamiento guardado: %1")
                                 .arg(QFileInfo(path).fileName()),
                             5000);
}

/// @brief Exporta el CNCJob activo a G-code.
void MainWindow::exportGCode() {
    if (!lastExcellon_) {
        QMessageBox::information(this, tr("Exportar G-code"),
                                 tr("Abra primero un archivo Excellon (.drl/.xln) para exportar "
                                    "el taladrado.\n\nLa exportación de aislamiento y corte "
                                    "llegará con el hito M4."));
        return;
    }

    const QString suggested = QFileInfo(lastDir_).absolutePath() + "/drill.nc";
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Guardar G-code"), suggested,
        tr("G-code (*.nc *.gcode *.tap);;Todos (*)"));
    if (path.isEmpty()) return;

    try {
        const fc::DrillJob job = fc::drillJobFromExcellon(*lastExcellon_);
        fc::GCodeOptions opt;
        opt.units = fc::Units::Mm;
        const std::string code = fc::drillingGCode(job, opt);
        std::ofstream out(path.toStdString(), std::ios::binary);
        if (!out) throw std::runtime_error("no se pudo escribir el archivo");
        out << code;
        statusBar()->showMessage(tr("G-code guardado: %1").arg(QFileInfo(path).fileName()), 5000);
    } catch (const std::exception& e) {
        showError(QString::fromUtf8(e.what()));
    }
}

/// @brief Acepta arrastrar archivos sobre la ventana.
void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

/// @brief Abre los archivos arrastrados.
void MainWindow::dropEvent(QDropEvent* event) {
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty()) return;
    QStringList paths;
    for (const QUrl& url : urls) {
        if (url.isLocalFile()) paths << url.toLocalFile();
    }
    if (!paths.isEmpty()) openFiles(paths);
    event->acceptProposedAction();
}
