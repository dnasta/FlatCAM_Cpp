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

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    bool openPath(const QString& path);
    void show3D();
    void excellonEditor();
    double displayLength(double mm) const;
    double lengthToMm(double value) const;
    QString lengthText(double mm, int decimals = 3) const;
    void convertToolTableUnits(bool toInch);
    bool openFiles(const QStringList& paths);
    bool openProject(const QString& path);
    bool runScriptFile(const QString& path);
    bool tclOpen(const QString& path, bool excellon);
    int tclIsolate(double dia, int passes, double overlap);
    int tclCutout(double dia, double margin, int tabs, double width);
    int tclNcc(double dia, double overlapPercent);
    int tclPaint(double dia, double overlapPercent);
    bool tclExportGcode(const QString& path);
    bool tclExportGerber(const QString& path);
    int tclExtractDrills(double maxDiameter);
    int tclPunch(double margin);
    int tclPanelize(int rows, int cols, double gapX, double gapY);
    int tclQr(const QString& text, double module, int border);
    int tclCorners(double length, double margin);
    double tclOptimal(double maxDiameter);
    QString tclInfo() const;
    QString tclVersion() const;
    QString tclHelp() const;
    QString tclGetNames(const QString& kind = QString()) const;
    QString tclBBox(const QString& name) const;
    bool tclSetActive(const QString& name);
    bool tclOffset(const QString& name, double dx, double dy);
    bool tclScale(const QString& name, double factor);
    bool tclMirror(const QString& name, const QString& axis);
    bool tclDelete(const QString& name);
    bool tclSaveProject(const QString& path);
    bool tclExportDxf(const QString& path);
    bool tclExportSvg(const QString& path);
    void tclPlotAll();
    void tclSetOrigin(double x, double y);
    bool tclNewObject(const QString& name, const QString& kind);
    bool tclAddCircle(const QString& name, double cx, double cy, double dia);
    bool tclAddRectangle(const QString& name, double x1, double y1, double x2, double y2);
    bool tclAddPolygon(const QString& name, const std::vector<double>& coords);
    bool tclAddPolyline(const QString& name, const std::vector<double>& coords);
    bool tclNew(const QString& name);
    int tclRings(const QString& name, bool holes);
    bool tclJoin(const QString& a, const QString& b);
    bool tclGeoUnion(const QString& a, const QString& b);
    bool tclExportExcellon(const QString& path);
    QString tclGetPath() const;
    void tclSetPath(const QString& path);
    QString tclGetSys(const QString& key) const;
    void tclSetSys(const QString& key, const QString& value);
    QString tclListSys() const;
    void tclSaveSys();
    QString tclOptions() const;
    int tclNRegions(const QString& name);
    bool tclSubtractShape(const QString& name, const std::vector<double>& coords, bool rectangle);
    bool tclFollow(const QString& name, double diameter);
    bool tclAlignDrill(const QString& name, const QString& reference);
    bool tclAlignDrillGrid(const QString& name, double grid);
    bool tclCreateCncJob(const QString& name, bool drillJob);
    bool tclMillDrills(const QString& name, double diameter);
    bool tclMillSlots(const QString& name, double diameter);
    void tclPlotObjects(const QStringList& names);
    bool applyIsolation(double toolDiameter, int passes, double overlapPercent, bool invert,
                        const fc::GCodeOptions& gcode = {});
    bool applyCutout(double toolDiameter, double margin, bool external,
                     const fc::GCodeOptions& gcode = {});
    bool applyNcc(double toolDiameter, double stepOverPercent, double clearance, double angle,
                  double margin, bool paintStrategy, const fc::GCodeOptions& gcode = {});
    bool applyPaint(double toolDiameter, double stepOverPercent, double margin,
                    const fc::GCodeOptions& gcode = {});
    bool applyDrill(const fc::GCodeOptions& gcode = {});
    void openToolTab(const QString& title);
    void showObjectTool();
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
    void buildMenus();
    void buildLayout();
    void buildToolPanel(QTabWidget* tabs);
    QWidget* buildCalculatorPage();
    void showCalculators();
    void showPreferences();
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
