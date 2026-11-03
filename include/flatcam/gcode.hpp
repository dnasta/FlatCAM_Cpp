/**
 * @file gcode.hpp
 * @brief Generación de G-code, optimización de recorridos y preprocesadores.
 *
 * El módulo convierte recorridos y taladros en programas G-code para distintos
 * firmwares (MACH3, GRBL, Marlin, Repetier, etc.). También ofrece:
 *
 * - optimización de orden de taladros y recorridos,
 * - G-code de geometría mono y multiherramienta,
 * - G-code de taladrado y de dosificación de pasta,
 * - construcción de trabajos a partir de un `ExcellonResult`.
 */
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "flatcam/geometry.hpp"
#include "flatcam/units.hpp"

namespace fc {

/**
 * @brief Perfil de postprocesador (firmware/controlador).
 *
 * Cambia cabecera, comandos de husillo/láser y si se emite `M6`.
 */
enum class GCodeFlavor {
    Default,          // MACH3
    DefaultNoM6,      // MACH3 sin M6
    Grbl,             // GRBL 1.1
    GrblNoM6,         // GRBL 1.1 sin M6
    GrblLaser,        // GRBL láser (M03/M05)
    Marlin,           // Marlin
    MarlinLaserFan,   // Marlin láser en pin ventilador (M106/M107)
    MarlinLaserSpindle, // Marlin láser en pin spindle (M3/M5)
    Repetier,         // Repetier
    Berta,            // Berta CNC
    Isel,             // ISEL CNC
    IselIcp,          // ISEL ICP CNC
    Roland,           // Roland MDX-20
    ToolchangeManual, // MACH3 cambio manual
    ToolchangeProbe,  // MACH3 con sonda
    ZLaser,          // láser controlado por Z
    Hpgl,            // plotter HPGL
    LineXyz,         // G-code con Z en cada movimiento lineal
    ToolchangeCustom // toolchange definido por el usuario
};

/**
 * @brief Parámetros de generación de un programa G-code.
 */
struct GCodeOptions {
    Units units = Units::Mm;      ///< Unidades del programa (`G20`/`G21`).
    int decimals = 4;             ///< Decimales de coordenadas.
    int feedDecimals = 2;         ///< Decimales de avances.

    double toolDiameter = 0.0;    ///< Diámetro informativo para comentarios/exportación.
    double feedXY = 200.0;        ///< Avance horizontal en mm/min.
    double feedZ = 60.0;          ///< Avance de penetración en mm/min.
    double zMove = 2.0;           ///< Altura de viaje.
    double zCut = -1.5;           ///< Profundidad final de corte.
    double zStart = 5.0;          ///< Z inicial del programa.
    double zEnd = 5.0;            ///< Z final del programa.
    double depthPerPass = 0.0;    ///< Profundidad por pasada; 0 = una sola bajada.

    double spindleSpeed = 1000.0; ///< Velocidad del husillo (RPM).
    bool spindleCw = true;        ///< `true` para `M03`, `false` para `M04`.

    bool includeToolChange = true; ///< Emite cambios de herramienta.
    double zToolchange = 10.0;     ///< Z segura para cambiar herramienta.
    std::optional<Pt> toolchangeXY; ///< Posición XY opcional para el cambio.
    std::optional<Pt> endXY;        ///< Posición XY final opcional.

    bool connectPaths = false;       ///< Une recorridos cercanos sin retraer.
    double connectTolerance = 0.5;   ///< Distancia máxima para conectar (mm).
    double dwellTime = 0.0;          ///< Pausa tras bajar (segundos).
    double laserPower = 300.0;       ///< Potencia láser (`S`).
    double zFocus = 0.0;             ///< Z de foco en modo láser.
    GCodeFlavor flavor = GCodeFlavor::Default; ///< Postprocesador.
    /// Plantilla para `ToolchangeCustom`; admite `{tool}`, `{diameter}`,
    /// `{z}`, `{x}` e `{y}`.
    std::string customToolchange;
};

/**
 * @brief Una herramienta de taladrado con sus agujeros y overrides.
 */
struct DrillTool {
    int number = 1;                         ///< Número de herramienta.
    double diameter = 0.8;                  ///< Diámetro de la broca.
    std::vector<Pt> holes;                  ///< Centros de taladros.
    std::vector<std::pair<Pt, Pt>> slotSegments; ///< Ranuras.
    std::optional<double> zCut;             ///< Override de Z de corte.
    std::optional<double> feedZ;            ///< Override de avance Z.
    std::optional<double> feedXY;           ///< Override de avance XY (ranuras).
    std::optional<double> spindleSpeed;     ///< Override de RPM.
    std::optional<double> zMove;            ///< Override de Z de viaje.
    std::optional<double> dwell;            ///< Override de pausa.
    double offsetX = 0.0;                   ///< Desplazamiento X de todos los taladros.
    double offsetY = 0.0;                   ///< Desplazamiento Y de todos los taladros.
};

/**
 * @brief Conjunto de herramientas de taladrado.
 */
struct DrillJob {
    std::vector<DrillTool> tools; ///< Herramientas en orden de ejecución.
    BBox bounds;                  ///< Límites de todos los taladros.
};

/// Estrategia de ordenamiento de taladros.
enum class DrillOrder {
    AsIs,           ///< Respeta el orden del archivo.
    NearestNeighbor ///< Vecino más cercano + 2-opt/Or-opt.
};

/**
 * @brief Calcula el recorrido XY total del trabajo.
 * @param job Trabajo a medir.
 * @param start Punto de partida (origen de la máquina).
 * @return Distancia en mm, incluyendo los viajes entre herramientas.
 */
double drillTravelDistance(const DrillJob& job, const Pt& start = {0.0, 0.0});

/**
 * @brief Reordena taladros y ranuras para reducir los rápidos.
 * @param job Trabajo a modificar.
 * @param order Estrategia de orden.
 * @param start Punto de partida.
 */
void optimizeDrillOrder(DrillJob& job, DrillOrder order = DrillOrder::NearestNeighbor,
                        const Pt& start = {0.0, 0.0});


/**
 * @brief Trabajo de dosificación de pasta de soldar.
 */
struct PasteJob {
    std::vector<Pt> points;             ///< Centros de pads.
    std::vector<double> padSizes;       ///< Tamaño de cada pad (opcional, paralelo).
    std::vector<int> nozzles;           ///< Índice de boquilla por punto (opcional).
    std::vector<double> nozzleDiameters;///< Diámetros de boquillas disponibles.
    std::vector<double> nozzleDwells;   ///< Pausas por boquilla.
    double nozzleDiameter = 0.5;        ///< Boquilla por defecto.
    BBox bounds;                        ///< Límites del trabajo.
    bool reverse = false;               ///< Invierte el orden de recorrido.
};

/**
 * @brief Genera G-code de dosificación de pasta.
 * @param job Puntos, boquillas y pausas.
 * @param options Parámetros de máquina.
 * @return Programa G-code (viaje, bajada, pausa, retracción).
 */
std::string pasteGCode(const PasteJob& job, const GCodeOptions& options);

/**
 * @brief Trabajo de recorridos para una sola herramienta.
 */
struct PathJob {
    std::vector<std::vector<Pt>> paths; ///< Recorridos.
    BBox bounds;                        ///< Límites.
    double toolDiameter = 0.0;          ///< Diámetro informativo.
};

/**
 * @brief Reordena recorridos por punto de inicio más cercano.
 * @param job Trabajo a modificar.
 * @param start Punto de partida.
 */
void optimizePathOrder(PathJob& job, const Pt& start = {0.0, 0.0});

/**
 * @brief Genera un programa de taladrado.
 * @param job Herramientas, taladros y ranuras.
 * @param options Cabecera, avances y postprocesador.
 * @return G-code compatible con el esquema de FlatCAM.
 */
std::string drillingGCode(const DrillJob& job, const GCodeOptions& options);

/**
 * @brief Genera G-code de geometría.
 * @param job Recorridos.
 * @param options Parámetros de máquina.
 * @return Programa con cabecera, cambios de herramienta y recorridos.
 */
std::string pathsGCode(const PathJob& job, const GCodeOptions& options);

/**
 * @brief Herramienta y recorridos para un programa multiherramienta.
 */
struct PathToolJob {
    int number = 1;                 ///< Número de herramienta.
    double diameter = 0.0;          ///< Diámetro.
    GCodeOptions options;           ///< Parámetros por herramienta.
    std::vector<std::vector<Pt>> paths; ///< Recorridos de esta herramienta.
    BBox bounds;                    ///< Límites.
};

/**
 * @brief G-code de geometría con varias herramientas.
 * @param tools Herramientas en orden.
 * @param header Opciones comunes y postprocesador.
 * @return Programa con cambios de herramienta.
 */
std::string pathsGCodeMulti(const std::vector<PathToolJob>& tools, const GCodeOptions& header);

/**
 * @brief Construye un trabajo de taladrado desde un Excellon parseado.
 * @param result Resultado del parser (unidades de la app).
 * @return Trabajo listo para `drillingGCode`.
 */
DrillJob drillJobFromExcellon(const struct ExcellonResult& result);

}  // namespace fc
