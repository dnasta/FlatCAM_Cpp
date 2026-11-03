/**
 * @file MainWindow.h
 * @brief Ventana principal de FlatCAM C++.
 *
 * `MainWindow` concentra la interfaz Qt y la lógica de aplicación:
 * - carga/guardado de Gerber, Excellon, proyectos y scripts,
 * - árbol de objetos y panel de propiedades,
 * - paneles de herramientas CAM y tablas de mecanizado,
 * - editores Gerber/Excellon/Geometría/G-code,
 * - vista 3D, preferencias, consola Tcl/JS y exportaciones,
 * - API de comandos Tcl (`tcl*`).
 *
 * La geometría y los algoritmos viven en `fc` (src/core, src/cam); esta clase
 * los orquesta y mantiene el estado de la sesión.
 */
#pragma once

#include <QMainWindow>
#include <QString>
#include <functional>
#include <map>
#include <optional>
#include <vector>

#include "flatcam/excellon.hpp"
#include "flatcam/gcode.hpp"
#include "flatcam/gerber.hpp"
#include "flatcam/cutout.hpp"
#include "flatcam/isolation.hpp"
#include "flatcam/ncc.hpp"
#include "flatcam/paint.hpp"
#include "flatcam/project.hpp"
#include "ScriptApi.h"

class Canvas;
class QLabel;
class QImage;
class QMenu;
class QPlainTextEdit;
class QComboBox;
class QDoubleSpinBox;
class QJSEngine;
class QLineEdit;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QUndoStack;

/**
 * @brief Ventana principal y controlador de la aplicación.
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    /// @brief Construye la ventana, los paneles y las preferencias.
    explicit MainWindow(QWidget* parent = nullptr);

    /// @brief Abre un archivo individual detectando su tipo.
    bool openPath(const QString& path);
    /// @brief Abre la vista 3D del cobre visible.
    void show3D();
    /// @brief Abre el editor gráfico de Excellon.
    void excellonEditor();
    /// @brief Convierte mm a la unidad de visualización.
    double displayLength(double mm) const;
    /// @brief Convierte un valor mostrado a mm.
    double lengthToMm(double value) const;
    /// @brief Formatea una longitud en la unidad activa.
    QString lengthText(double mm, int decimals = 3) const;
    /// @brief Convierte las tablas de herramientas a mm/in.
    void convertToolTableUnits(bool toInch);
    /// @brief Abre varios archivos como capas independientes.
    bool openFiles(const QStringList& paths);
    /// @brief Abre un proyecto `.FlatPrj`.
    bool openProject(const QString& path);
    /// @brief Ejecuta un script JavaScript.
    bool runScriptFile(const QString& path);
    /// @brief Comando Tcl: abre Gerber o Excellon.
    bool tclOpen(const QString& path, bool excellon);
    /// @brief Comando Tcl: aislamiento.
    int tclIsolate(double dia, int passes, double overlap);
    /// @brief Comando Tcl: corte.
    int tclCutout(double dia, double margin, int tabs, double width);
    /// @brief Comando Tcl: NCC.
    int tclNcc(double dia, double overlapPercent);
    /// @brief Comando Tcl: pintura.
    int tclPaint(double dia, double overlapPercent);
    /// @brief Comando Tcl: exporta G-code.
    bool tclExportGcode(const QString& path);
    /// @brief Comando Tcl: exporta Gerber.
    bool tclExportGerber(const QString& path);
    /// @brief Comando Tcl: extrae taladros.
    int tclExtractDrills(double maxDiameter);
    /// @brief Comando Tcl: punch de taladros.
    int tclPunch(double margin);
    /// @brief Comando Tcl: paneliza.
    int tclPanelize(int rows, int cols, double gapX, double gapY);
    /// @brief Comando Tcl: genera QR.
    int tclQr(const QString& text, double module, int border);
    /// @brief Comando Tcl: marcas de esquina.
    int tclCorners(double length, double margin);
    /// @brief Comando Tcl: herramienta óptima.
    double tclOptimal(double maxDiameter);
    /// @brief Comando Tcl: resumen del estado.
    QString tclInfo() const;
    /// @brief Comando Tcl: versión.
    QString tclVersion() const;
    /// @brief Comando Tcl: ayuda.
    QString tclHelp() const;
    /// @brief Comando Tcl: nombres de objetos.
    QString tclGetNames(const QString& kind = QString()) const;
    /// @brief Comando Tcl: límites de un objeto.
    QString tclBBox(const QString& name) const;
    /// @brief Comando Tcl: activa un objeto.
    bool tclSetActive(const QString& name);
    /// @brief Comando Tcl: traslada un objeto.
    bool tclOffset(const QString& name, double dx, double dy);
    /// @brief Comando Tcl: escala un objeto.
    bool tclScale(const QString& name, double factor);
    /// @brief Comando Tcl: espeja un objeto.
    bool tclMirror(const QString& name, const QString& axis);
    /// @brief Comando Tcl: elimina un objeto.
    bool tclDelete(const QString& name);
    /// @brief Comando Tcl: guarda el proyecto.
    bool tclSaveProject(const QString& path);
    /// @brief Comando Tcl: exporta DXF.
    bool tclExportDxf(const QString& path);
    /// @brief Comando Tcl: exporta SVG.
    bool tclExportSvg(const QString& path);
    /// @brief Comando Tcl: redibuja todo.
    void tclPlotAll();
    /// @brief Comando Tcl: define el origen.
    void tclSetOrigin(double x, double y);
    /// @brief Comando Tcl: crea una capa vacía.
    bool tclNewObject(const QString& name, const QString& kind);
    /// @brief Comando Tcl: agrega un círculo.
    bool tclAddCircle(const QString& name, double cx, double cy, double dia);
    /// @brief Comando Tcl: agrega un rectángulo.
    bool tclAddRectangle(const QString& name, double x1, double y1, double x2, double y2);
    /// @brief Comando Tcl: agrega un polígono.
    bool tclAddPolygon(const QString& name, const std::vector<double>& coords);
    /// @brief Comando Tcl: agrega una polilínea.
    bool tclAddPolyline(const QString& name, const std::vector<double>& coords);
    /// @brief Comando Tcl: nuevo objeto.
    bool tclNew(const QString& name);
    /// @brief Comando Tcl: cuenta contornos o agujeros.
    int tclRings(const QString& name, bool holes);
    /// @brief Comando Tcl: une objetos.
    bool tclJoin(const QString& a, const QString& b);
    /// @brief Comando Tcl: unión geométrica.
    bool tclGeoUnion(const QString& a, const QString& b);
    /// @brief Comando Tcl: exporta Excellon.
    bool tclExportExcellon(const QString& path);
    /// @brief Comando Tcl: ruta actual.
    QString tclGetPath() const;
    /// @brief Comando Tcl: define la ruta actual.
    void tclSetPath(const QString& path);
    /// @brief Comando Tcl: lee una preferencia.
    QString tclGetSys(const QString& key) const;
    /// @brief Comando Tcl: escribe una preferencia.
    void tclSetSys(const QString& key, const QString& value);
    /// @brief Comando Tcl: lista preferencias.
    QString tclListSys() const;
    /// @brief Comando Tcl: guarda preferencias.
    void tclSaveSys();
    /// @brief Comando Tcl: opciones activas.
    QString tclOptions() const;
    /// @brief Comando Tcl: cuenta regiones.
    int tclNRegions(const QString& name);
    /// @brief Comando Tcl: resta una forma.
    bool tclSubtractShape(const QString& name, const std::vector<double>& coords, bool rectangle);
    /// @brief Comando Tcl: genera follow.
    bool tclFollow(const QString& name, double diameter);
    /// @brief Comando Tcl: alinea taladros.
    bool tclAlignDrill(const QString& name, const QString& reference);
    /// @brief Comando Tcl: alinea a grilla.
    bool tclAlignDrillGrid(const QString& name, double grid);
    /// @brief Comando Tcl: crea un CNCJob.
    bool tclCreateCncJob(const QString& name, bool drillJob);
    /// @brief Comando Tcl: fresa taladros.
    bool tclMillDrills(const QString& name, double diameter);
    /// @brief Comando Tcl: fresa ranuras.
    bool tclMillSlots(const QString& name, double diameter);
    /// @brief Comando Tcl: redibuja objetos.
    void tclPlotObjects(const QStringList& names);
    /// @brief Aplica aislamiento y actualiza capas/preview.
    bool applyIsolation(double toolDiameter, int passes, double overlapPercent, bool invert,
                        const fc::GCodeOptions& gcode = {});
    /// @brief Aplica corte de contorno.
    bool applyCutout(double toolDiameter, double margin, bool external,
                     const fc::GCodeOptions& gcode = {});
    /// @brief Aplica NCC (hatch o paint).
    bool applyNcc(double toolDiameter, double stepOverPercent, double clearance, double angle,
                  double margin, bool paintStrategy, const fc::GCodeOptions& gcode = {});
    /// @brief Aplica pintura.
    bool applyPaint(double toolDiameter, double stepOverPercent, double margin,
                    const fc::GCodeOptions& gcode = {});
    /// @brief Aplica taladrado al Excellon activo.
    bool applyDrill(const fc::GCodeOptions& gcode = {});
    /// @brief Abre una pestaña de herramienta.
    void openToolTab(const QString& title);
    /// @brief Muestra la pestaña Herramienta.
    void showObjectTool();
    /// @brief Ejecuta un comando Tcl y devuelve el resultado.
    QString runTclCommand(const QString& command);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void chooseFile();
    void chooseProject();
    void fitView();
    void exportGCode();
    void isolateDialog();
    void exportIsolationGCode();
    void cutoutDialog();
    void exportCutoutGCode();
    void nccDialog();
    void exportNccGCode();
    void paintDialog();
    void exportPaintGCode();
    void exportDrillGCode();
    void exportMillGCode();
    void chooseProjectObject();
    void projectObjectSelected();
    void objectItemChanged(QTreeWidgetItem* item, int column);
    void refreshProperties();
    void deleteSelectedObject();
    void duplicateSelectedObject();
    void showToolTab(const QString& title);
    void refreshObjectToolPage();
    QString formatLength(double mm, int decimals = -1) const;
    double snapValue(double value) const;
    QString toolTablesJson() const;
    void loadToolTablesJson(const QString& json);
    void createTextObject(const QString& kind);
    void openTextObject(int layerId);
    void addCncJobObject(const QString& name, const QString& code,
                         const std::map<QString, QString>& props = {});
    void runScript();
    void newScript();
    void editScript();
    void transformSelectedObject();
    void editGeometry();
    void geometryOperations();
    void booleanDialog();
    void createFilm();
    void exportFilm();

private:
    /// @name Construcción de la interfaz
    /// @{
    void buildMenus();
    void buildLayout();
    void buildToolPanel(QTabWidget* tabs);
    QWidget* buildCalculatorPage();
    void showCalculators();
    void showPreferences();
    /// @}
    /// @name Editores y utilidades
    /// @{
    void editSource();
    void scriptEditor(const QString& path);
    void gerberEditor();
    bool applyApertureSizes(int layerId, const std::map<QString, double>& sizes);
    bool applyMacroDefinition(int layerId, const QString& name, const QString& raw);
    void etchCompensation();
    void invertGerber();
    void distanceTool();
    void optimalTool();
    void addCornerMarks();
    void punchGerberAction();
    void extractDrillsAction();
    void exportPdf();
    void importImage();
    void importVector(bool svg);
    bool importVectorPath(const QString& path, bool svg);
    void importHpglFile();
    void importPdfFile();
    bool importPdfPath(const QString& path);
    void addVectorGeometry(const QString& name, const std::vector<fc::Geom>& geoms);
    void addImageGeometry(const QImage& image, const QString& name, double widthMm,
                          int threshold, bool invert, bool colorMode = false,
                          const QColor& target = Qt::black, int tolerance = 60,
                          bool maskEnabled = false, const QColor& maskColor = Qt::white,
                          int maskTolerance = 30);
    void exportPng();
    void exportDxfFile();
    void exportHpglFile();
    void printView();
    void loadInfFile();
    void pcbWizardImport();
    /// @}
    void openGCodeFile();
    bool addGCodeLayer(const QString& path);
    void rulesCheck();
    void alignObjects();
    void doubleSided();
    void newProject();
    void recordRecent(const QString& path);
    void rebuildRecentMenu();
    void addBookmark();
    void rebuildBookmarksMenu();
    void saveProjectAs();
    void saveConfiguration();
    void loadConfiguration();
    void moveObject();
    void moveToOrigin();
    void joinObjects();
    void explodeObject();
    void jumpToLocation();
    void setDisplayOrigin();
    void distanceMeasure();
    void closeToolTab();
    void selectAllObjects();
    void deselectAllObjects();
    void panelize();
    void addFiducials();
    void calibrate();
    void advancedFilm();
    void buildConsole(QTabWidget* tabs);
    QTreeWidgetItem* addObjectRow(const QString& name, const QString& kind, const QString& area,
                                  int layerId, bool visible = true,
                                  QTreeWidgetItem* parent = nullptr);
    int addGeneratedLayer(const QString& name, const std::vector<fc::Geom>& paths, int sourceId);
    int addGeneratedSolid(const QString& name, const std::vector<fc::Geom>& solids,
                          int sourceId, const QString& kind = "herramienta");
    std::vector<fc::Geom> layerGeometries(int layerId) const;
    void addGCodeRow(const QString& path);
    std::vector<QTreeWidgetItem*> objectItems() const;
    QTreeWidgetItem* findObjectItem(int layerId) const;
    QTreeWidgetItem* findObjectByName(const QString& name) const;
    bool saveProjectToPath(const QString& path);
    void resetObjectGroups();
    void setObjectVisible(QTreeWidgetItem* item, bool visible);
    void buildTclConsole(QTabWidget* tabs);
    void loadPreferences();
    void applyTheme(bool dark);
    void applyUiLevel();
    void showToolDb();
    void addToolFromDb(class QTableWidget* table);
    class QJsonArray toolDbEntries() const;
    void setToolDbEntries(const class QJsonArray& entries) const;
    void savePreferences();
    std::function<fc::GCodeOptions()> isoOptions_;
    std::function<fc::GCodeOptions()> nccOptions_;
    std::function<fc::GCodeOptions()> cutOptions_;
    std::function<fc::GCodeOptions()> paintOptions_;
    std::function<fc::GCodeOptions()> drillOptions_;
    bool addGerber(const QString& path);
    bool addExcellon(const QString& path);
    void showGerber(const QString& path);
    void showExcellon(const QString& path);
    void setInfo(const QString& text);
    void showError(const QString& message);

    Canvas* canvas_ = nullptr;
    QPlainTextEdit* info_ = nullptr;
    QPlainTextEdit* gcodeView_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* coordsLabel_ = nullptr;
    QLabel* unitsLabel_ = nullptr;
    QString lastDir_;
    std::optional<fc::ExcellonResult> lastExcellon_;
    std::optional<fc::GerberResult> lastGerber_;
    std::optional<fc::IsolationResult> lastIsolation_;
    double lastIsoToolDiameter_ = 0.2;
    std::optional<fc::CutoutResult> lastCutout_;
    fc::CutoutOptions lastCutoutOptions_;
    fc::GCodeOptions lastCutoutGCode_;
    std::optional<fc::NccResult> lastNcc_;
    fc::GCodeOptions lastNccGCode_;
    std::optional<fc::PaintResult> lastPaint_;
    fc::GCodeOptions lastPaintGCode_;
    std::optional<fc::DrillJob> lastDrillJob_;
    std::optional<fc::PathJob> lastMillJob_;
    std::optional<fc::PasteJob> lastPasteJob_;
    fc::GCodeOptions lastPasteOptions_;
    fc::GCodeOptions lastMillOptions_;
    std::vector<fc::PathToolJob> lastIsoMulti_;
    std::vector<fc::PathToolJob> lastNccMulti_;
    std::vector<fc::PathToolJob> lastCutMulti_;
    std::vector<fc::PathToolJob> lastPaintMulti_;
    fc::GCodeOptions lastIsoHeader_;
    fc::GCodeOptions lastNccHeader_;
    fc::GCodeOptions lastCutHeader_;
    fc::GCodeOptions lastPaintHeader_;
    fc::GCodeOptions lastDrillHeader_;
    class QTableWidget* drillTable_ = nullptr;
    class QTableWidget* isoTable_ = nullptr;
    class QTableWidget* nccTable_ = nullptr;
    class QTableWidget* cutTable_ = nullptr;
    class QTableWidget* paintTable_ = nullptr;
    class QComboBox* isoSource_ = nullptr;
    class QComboBox* nccSource_ = nullptr;
    class QComboBox* cutSource_ = nullptr;
    class QComboBox* paintSource_ = nullptr;
    class QComboBox* drillSource_ = nullptr;
    class QComboBox* millSource_ = nullptr;
    class QComboBox* thiefSource_ = nullptr;
    class QComboBox* pasteSource_ = nullptr;
    void refreshDrillTable();
    void refreshSourceCombos();
    fc::GerberResult* sourceGerber(QComboBox* combo);
    fc::ExcellonResult* sourceExcellon(QComboBox* combo);

    std::map<int, fc::GerberResult> gerberLayers_;
    std::map<int, fc::ExcellonResult> excellonLayers_;
    fc::GCodeOptions lastDrillGCode_;
    QTabWidget* leftTabs_ = nullptr;
    QWidget* objectToolPage_ = nullptr;
    class QVBoxLayout* objectToolLayout_ = nullptr;
    QTreeWidget* objects_ = nullptr;
    QTreeWidget* properties_ = nullptr;
    QString projectPath_;
    bool updatingTree_ = false;
    bool displayInch_ = false;
    bool snapGrid_ = false;
    bool advancedLevel_ = true;
    std::vector<QWidget*> advancedWidgets_;
    QString customToolchange_;
    bool darkTheme_ = false;
    int decimals_ = 3;
    int stepsPerCircle_ = 64;
    bool gerberSimplify_ = false;
    bool geometrySimplify_ = false;
    double geometryTolerance_ = 0.01;
    double excellonExtractMaxDia_ = 3.0;
    double cncDefaultFeedXY_ = 300.0;
    double cncDefaultFeedZ_ = 60.0;
    double cncDefaultRPM_ = 10000.0;
    double cncDefaultZCut_ = -1.5;
    double gerberTolerance_ = 0.005;
    fc::Units defaultUnits_ = fc::Units::Mm;
    std::map<int, QString> sourcePaths_;
    std::map<int, QString> objectText_;
    std::map<int, std::map<QString, QString>> objectProps_;
    std::map<QString, QWidget*> toolTabs_;
    QTreeWidgetItem* filesGroup_ = nullptr;
    QTreeWidgetItem* toolsGroup_ = nullptr;
    QTreeWidgetItem* gcodeGroup_ = nullptr;
    std::map<int, std::vector<int>> generatedFrom_;
    QColor background_{16, 20, 24};
    QColor copperColor_{217, 164, 65};
    QColor followColor_{111, 168, 220};
    QColor toolpathColor_{102, 214, 132};
    QTabWidget* bottomTabs_ = nullptr;
    QPlainTextEdit* consoleOutput_ = nullptr;
    QLineEdit* consoleInput_ = nullptr;
    QJSEngine* consoleEngine_ = nullptr;
    fc::ScriptApi* consoleApi_ = nullptr;
    QPlainTextEdit* tclOutput_ = nullptr;
    QLineEdit* tclInput_ = nullptr;
    void* tclInterp_ = nullptr;
    QUndoStack* undoStack_ = nullptr;
    QMenu* recentMenu_ = nullptr;
    QMenu* bookmarksMenu_ = nullptr;
};
