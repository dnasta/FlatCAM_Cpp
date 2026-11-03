/**
 * @file ScriptApi.h
 * @brief API JavaScript nativa expuesta a `QJSEngine`.
 *
 * Envuelve las operaciones principales de FlatCAM (abrir, aislar, cortar,
 * exportar) para que puedan usarse desde scripts y desde la consola JS.
 */
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

/**
 * @brief Fachada de operaciones FlatCAM para JavaScript.
 */
class ScriptApi : public QObject {
    Q_OBJECT

public:
    /// @brief Crea la API con estado vacío.
    explicit ScriptApi(QObject* parent = nullptr);

    /// @brief Abre un Gerber y lo deja como objeto activo.
    Q_INVOKABLE bool openGerber(const QString& path);
    /// @brief Abre un Excellon y lo deja como objeto activo.
    Q_INVOKABLE bool openExcellon(const QString& path);
    /// @brief Genera el aislamiento y agrega la capa.
    Q_INVOKABLE int isolate(double toolDiameter, int passes = 1, double overlapPercent = 0.0);
    /// @brief Genera el corte de contorno.
    Q_INVOKABLE int cutout(double toolDiameter, double margin = 0.0, int tabs = 0,
                           double tabWidth = 0.0);
    /// @brief Genera NCC con la superposición indicada.
    Q_INVOKABLE int ncc(double toolDiameter, double stepOverPercent = 50.0);
    /// @brief Genera una capa de pintura.
    Q_INVOKABLE int paint(double toolDiameter, double stepOverPercent = 50.0);
    /// @brief Genera fresado siguiendo los recorridos.
    Q_INVOKABLE int mill(double toolDiameter);
    /// @brief Extrae taladros circulares de un Gerber.
    Q_INVOKABLE int extractDrills(double maxDiameter = 3.0);
    /// @brief Resta taladros al cobre.
    Q_INVOKABLE int punch(double margin = 0.0);
    /// @brief Paneliza el objeto activo.
    Q_INVOKABLE int panelize(int rows, int cols, double gapX = 2.0, double gapY = 2.0,
                             double angle = 0.0);
    /// @brief Genera un código QR.
    Q_INVOKABLE int qrCode(const QString& text, double module = 1.0, int border = 4);
    /// @brief Agrega marcas de esquina.
    Q_INVOKABLE int cornerMarks(double length = 3.0, double margin = 0.0);
    /// @brief Calcula la fresa máxima de aislamiento.
    Q_INVOKABLE double optimalToolDiameter(double maxDiameter = 10.0);
    /// @brief Comprueba reglas de diseño.
    Q_INVOKABLE int rulesCheck(double clearance = 0.2);
    /// @brief Exporta el último CNCJob a G-code.
    Q_INVOKABLE bool exportGcode(const QString& path);
    /// @brief Devuelve un resumen del estado.
    Q_INVOKABLE QString info() const;
    /// @brief Devuelve el último error.
    Q_INVOKABLE QString lastError() const { return error_; }
    /// @brief Escribe un mensaje en el log.
    Q_INVOKABLE void log(const QString& text);

    /// Callback para entregar capas generadas a la GUI.
    using LayerSink = std::function<void(const QString&, const QString&,
                                        const std::vector<Geom>&, const std::vector<Geom>&)>;

    /// @brief Registra el receptor de capas generadas.
    void setLayerSink(LayerSink sink) { sink_ = std::move(sink); }

private:
    GerberResult gerber_; ///< Último Gerber abierto.
    bool hasGerber_ = false;      ///< Hay Gerber cargado.
    ExcellonResult excellon_;     ///< Último Excellon abierto.
    bool hasExcellon_ = false;    ///< Hay Excellon cargado.
    PathJob paths_;               ///< Último trabajo de recorridos.
    DrillJob drills_;             ///< Último trabajo de taladrado.
    bool hasPathJob_ = false;     ///< Hay trabajo de recorridos.
    bool hasDrillJob_ = false;    ///< Hay trabajo de taladrado.
    QString error_;               ///< Último error.
    LayerSink sink_;              ///< Receptor de capas.
};

/**
 * @brief Instala `api` como objeto global `flatcam` en un motor JS.
 * @param engine Motor QJSEngine.
 * @param api Instancia de la API (no toma propiedad).
 */
void installScriptApi(QJSEngine& engine, ScriptApi* api);

}  // namespace fc
