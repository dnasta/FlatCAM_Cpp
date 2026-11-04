/**
 * @file main.cpp
 * @brief Punto de entrada de la GUI: tema, filtro de tooltips y opciones CLI.
 *
 * Configura la aplicación Qt (nombre, icono, tema claro u oscuro persistido en
 * `QSettings`), instala un filtro global que hace que las etiquetas de
 * formulario muestren el tooltip de su campo asociado, crea y muestra
 * `MainWindow`, procesa los argumentos de línea de comandos usados para
 * automatizar capturas y pruebas y entra en el bucle de eventos. Si se pide una
 * captura, la guarda tras un retardo fijo y cierra la aplicación.
 */
#include <QApplication>
#include <QDebug>
#include <QSettings>
#include <QFileInfo>
#include <QHelpEvent>
#include <QLabel>
#include <QTimer>
#include <QToolTip>

#include "MainWindow.h"
#include "Theme.h"
#include "flatcam/gcode.hpp"

namespace {

// Every form label shows the tooltip of its buddy (the field it describes),
// including labels created by QFormLayout without an explicit tooltip.
/**
 * @brief Filtro global que propaga el tooltip del campo a su etiqueta.
 *
 * Instalado sobre la `QApplication`, revisa los eventos de tooltip de todas las
 * etiquetas del programa. Si la etiqueta no tiene tooltip propio pero su
 * *buddy* sí, muestra el del campo: así los textos de ayuda definidos una sola
 * vez (en el control) aparecen también sobre el rótulo, incluso en los
 * formularios generados por `QFormLayout`.
 */
class LabelTooltipFilter : public QObject {
public:
    using QObject::QObject;

protected:
    /**
     * @brief Muestra el tooltip del campo asociado a una etiqueta.
     *
     * Intercepta `QEvent::ToolTip` sobre `QLabel`: si la etiqueta no tiene
     * tooltip propio pero sí un *buddy* (el campo que describe) con tooltip, lo
     * muestra en la posición del cursor y consume el evento. Así los formularios
     * creados con `QFormLayout` heredan la ayuda del campo sin duplicar textos.
     *
     * @param obj Objeto que recibe el evento.
     * @param event Evento a filtrar.
     * @return `true` si el evento quedó manejado; en cualquier otro caso delega en
     *         `QObject::eventFilter`.
     */
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

/**
 * @brief Arranca la aplicación de escritorio.
 *
 * Secuencia: crea `QApplication`, fija nombre/organización/icono, instala el
 * filtro de tooltips, aplica el tema (hoja de estilo clara o paleta Fusion
 * oscura según `preferences/darkTheme`), construye y muestra `MainWindow` con
 * tamaño 1400x900 y luego interpreta los argumentos:
 *
 * - `--screenshot <png>`: captura la ventana (o el diálogo activo) y sale.
 * - `--isolate <Ø>` / `--cutout <Ø>`: aplican aislamiento o corte de demostración.
 * - `--script <js>`: ejecuta un script de la API.
 * - `--tool <nombre>`: abre la pestaña de esa herramienta.
 * - `--object-panel`, `--3d`, `--excellon-editor`: abren paneles/diálogos.
 * - `--tcl <comando>`: ejecuta un comando Tcl e imprime el resultado.
 * - Cualquier otro argumento sin `--` se trata como archivo a abrir.
 *
 * @param argc Número de argumentos.
 * @param argv Vector de argumentos del sistema.
 * @return Código de salida del bucle de eventos de Qt.
 * @note Los valores numéricos inválidos se convierten a 0 y la acción
 *       correspondiente se omite; las opciones CAM de demostración usan valores
 *       fijos (1 pasada, margen 0.5, Z −1.8 y 0.6 mm por pasada).
 * @warning La captura se toma con un retardo fijo de 900 ms
 *          (main.cpp:188-195): con archivos grandes o el diálogo 3D recién
 *          abierto puede fotografiar antes de que el contenido esté listo. Las
 *          opciones desconocidas con valor dejan ese valor como "archivo".
 */
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("FlatCAM C++");
    app.installEventFilter(new LabelTooltipFilter(&app));
    QApplication::setOrganizationName("FlatCAM C++");
    QApplication::setWindowIcon(QIcon(":/icons/appicon.png"));
    const bool darkTheme = QSettings().value("preferences/darkTheme", false).toBool();
    fc::theme::apply(app, darkTheme);

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
            bool ok = false;
            isolate = args[++i].toDouble(&ok);
            if (!ok) {
                qWarning() << "Valor inválido para --isolate:" << args[i];
                return 2;
            }
        } else if (args[i] == "--cutout" && i + 1 < args.size()) {
            bool ok = false;
            cutout = args[++i].toDouble(&ok);
            if (!ok) {
                qWarning() << "Valor inválido para --cutout:" << args[i];
                return 2;
            }
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
        } else if (args[i].startsWith("--")) {
            qWarning() << "Opción desconocida:" << args[i];
        } else {
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
            if (!target->grab().save(screenshot))
                qWarning() << "No se pudo guardar la captura en" << screenshot;
            window.allowCloseWithoutPrompt();
            QApplication::quit();
        });
    }
    return app.exec();
}
