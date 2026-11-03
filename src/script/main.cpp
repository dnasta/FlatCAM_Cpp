#include <QCoreApplication>
#include <QFile>
#include <QJSEngine>
#include <QJSValue>
#include <QTextStream>

#include <iostream>

#include "ScriptApi.h"

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
