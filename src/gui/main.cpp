#include <QApplication>
#include <QDebug>
#include <QSettings>
#include <QStyleFactory>
#include <QPalette>
#include <QFileInfo>
#include <QHelpEvent>
#include <QLabel>
#include <QTimer>
#include <QToolTip>

#include "MainWindow.h"
#include "flatcam/gcode.hpp"

namespace {

// Every form label shows the tooltip of its buddy (the field it describes),
// including labels created by QFormLayout without an explicit tooltip.
class LabelTooltipFilter : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() == QEvent::ToolTip) {
            auto* label = qobject_cast<QLabel*>(obj);
            if (label && label->toolTip().isEmpty() && label->buddy()) {
                const QString tip = label->buddy()->toolTip();
                if (!tip.isEmpty()) {
                    auto* help = static_cast<QHelpEvent*>(event);
                    QToolTip::showText(help->globalPos(), tip, label);
                    return true;
                }
            }
        }
        return QObject::eventFilter(obj, event);
    }
};

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("FlatCAM C++");
    app.installEventFilter(new LabelTooltipFilter(&app));
    QApplication::setOrganizationName("FlatCAM C++");
    QApplication::setWindowIcon(QIcon(":/icons/appicon.png"));
    const bool darkTheme = QSettings().value("preferences/darkTheme", false).toBool();
    if (!darkTheme) {
        app.setStyleSheet(R"(
            QWidget { background-color: #f1f1f1; color: #1e1e1e; }
            QMenuBar, QToolBar { background-color: #f1f1f1; }
            QTabBar::tab { background: #e4e4e4; border: 1px solid #b8b8b8;
                           padding: 4px 10px; }
            QTabBar::tab:selected { background: #fafafa; }
            QTabWidget::pane { border: 1px solid #c0c0c0; }
            QPushButton { background: #e9e9e9; border: 1px solid #adadad;
                          border-radius: 3px; padding: 4px 8px; }
            QPushButton:hover { background: #f3f3f3; }
            QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QPlainTextEdit,
            QTableWidget, QTreeWidget { background: #ffffff; border: 1px solid #b8b8b8; }
            QHeaderView::section { background: #e6e6e6; border: 0px;
                                   border-right: 1px solid #c0c0c0;
                                   border-bottom: 1px solid #c0c0c0; padding: 3px; }
        )");
    }
    if (darkTheme) {
        QApplication::setStyle(QStyleFactory::create("Fusion"));
        QPalette palette;
        palette.setColor(QPalette::Window, QColor(45, 45, 48));
        palette.setColor(QPalette::WindowText, QColor(230, 230, 230));
        palette.setColor(QPalette::Base, QColor(30, 30, 32));
        palette.setColor(QPalette::AlternateBase, QColor(45, 45, 48));
        palette.setColor(QPalette::Text, QColor(230, 230, 230));
        palette.setColor(QPalette::Button, QColor(53, 53, 56));
        palette.setColor(QPalette::ButtonText, QColor(230, 230, 230));
        palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
        palette.setColor(QPalette::HighlightedText, Qt::white);
        palette.setColor(QPalette::ToolTipBase, QColor(60, 60, 64));
        palette.setColor(QPalette::ToolTipText, QColor(230, 230, 230));
        app.setPalette(palette);
    }

    MainWindow window;
    window.resize(1400, 900);
    window.show();

    QStringList files;
    QString screenshot;
    double isolate = 0.0;
    double cutout = 0.0;
    QString script;
    QString tool;
    bool objectPanel = false;
    bool view3d = false;
    bool excellonEditor = false;
    QString tclCommand;
    const QStringList args = QApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == "--screenshot" && i + 1 < args.size()) {
            screenshot = args[++i];
        } else if (args[i] == "--isolate" && i + 1 < args.size()) {
            isolate = args[++i].toDouble();
        } else if (args[i] == "--cutout" && i + 1 < args.size()) {
            cutout = args[++i].toDouble();
        } else if (args[i] == "--script" && i + 1 < args.size()) {
            script = args[++i];
        } else if (args[i] == "--tool" && i + 1 < args.size()) {
            tool = args[++i];
        } else if (args[i] == "--object-panel") {
            objectPanel = true;
        } else if (args[i] == "--3d") {
            view3d = true;
        } else if (args[i] == "--excellon-editor") {
            excellonEditor = true;
        } else if (args[i] == "--tcl" && i + 1 < args.size()) {
            tclCommand = args[++i];
        } else if (!args[i].startsWith("--")) {
            files << args[i];
        }
    }

    if (!files.isEmpty()) window.openFiles(files);
    if (isolate > 0.0) window.applyIsolation(isolate, 1, 0.0, false);
    if (cutout > 0.0) {
        fc::GCodeOptions opt;
        opt.zCut = -1.8;
        opt.depthPerPass = 0.6;
        window.applyCutout(cutout, 0.5, true, opt);
    }
    if (!tool.isEmpty()) window.openToolTab(tool);
    if (objectPanel) window.showObjectTool();
    if (view3d) window.show3D();
    if (excellonEditor) QTimer::singleShot(200, &window, [&window] { window.excellonEditor(); });
    if (!tclCommand.isEmpty()) qInfo().noquote() << window.runTclCommand(tclCommand);
    if (!script.isEmpty()) window.runScriptFile(script);
    if (!screenshot.isEmpty()) {
        QTimer::singleShot(900, &app, [&window, screenshot, view3d, excellonEditor] {
            QWidget* target = (view3d || excellonEditor) ? QApplication::activeWindow() : &window;
            if (!target) target = &window;
            target->grab().save(screenshot);
            QApplication::quit();
        });
    }
    return app.exec();
}
