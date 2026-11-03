/**
 * @file main.cpp
 * @brief Punto de entrada del ejecutable de scripting `flatcam-js`.
 *
 * Ejecuta un archivo JavaScript con `QJSEngine` (Qt Core/Qml), sin GUI, tras
 * instalar la API `fc::ScriptApi` como objeto global `flatcam` y la función
 * `print()`. Es el equivalente de consola del scripting nativo del proyecto.
 */

#include <QCoreApplication>
#include <QFile>
#include <QJSEngine>
#include <QJSValue>
#include <QTextStream>

#include <iostream>

#include "ScriptApi.h"

/**
 * @brief Carga y evalúa el script indicado en la línea de comandos.
 *
 * Flujo: crea `QCoreApplication`, valida que haya un argumento, lee el archivo
 * completo como UTF-8, crea el motor, instala la API y evalúa el código usando
 * la ruta como nombre de archivo (para que los errores informen la ubicación).
 *
 * @param argc Debe ser al menos 2 (programa + script).
 * @param argv Ruta del script en `argv[1]`.
 * @return 0 si el script se evalúa sin error; 1 si el archivo no se puede abrir
 *         o la evaluación devuelve un error; 2 si faltan argumentos.
 *
 * @note No llama a `engine.installExtensions(ConsoleExtension)`: `console.log`
 *       no está disponible, pero `print()` sí, porque lo define
 *       `installScriptApi`.
 * @note Los errores de script se escriben por `stderr` y detienen el proceso;
 *       no se ejecuta ningún `catch` de JavaScript que el script no haya
 *       definido.
 * @warning `api` se construye después de `engine`, de modo que se destruye
 *          antes. Como `newQObject` aplica `JavaScriptOwnership`, el motor
 *          podría intentar borrarlo en su destructor; ver la advertencia de
 *          `installScriptApi`.
 */
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        std::cerr << "usage: flatcam-js <script.js>\n";
        return 2;
    }

    QFile file(QString::fromLocal8Bit(argv[1]));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::cerr << "cannot open script\n";
        return 1;
    }
    const QString code = QString::fromUtf8(file.readAll());

    QJSEngine engine;
    fc::ScriptApi api;
    fc::installScriptApi(engine, &api);

    const QJSValue result = engine.evaluate(code, QString::fromLocal8Bit(argv[1]));
    if (result.isError()) {
        std::cerr << "script error: " << result.toString().toStdString() << "\n";
        return 1;
    }
    return 0;
}
