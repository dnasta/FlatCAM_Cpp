/**
 * @file GCodeEditor.cpp
 * @brief Editor modal de G-code con resaltado de sintaxis y guardado en sitio.
 *
 * Implementa un `QSyntaxHighlighter` propio con reglas para G, M, T, ejes
 * (X/Y/Z/I/J/K), avances/velocidad (F/S) y números, más los comentarios entre
 * paréntesis, y un diálogo que carga el archivo completo en un
 * `QPlainTextEdit` oscuro y permite guardarlo sobrescribiendo el original.
 */
#include "GCodeEditor.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVBoxLayout>

#include <memory>

namespace {

/**
 * @brief Resaltador de sintaxis para programas G-code.
 *
 * Mantiene formatos precalculados y una lista de reglas (expresión regular +
 * formato) que se aplican a cada línea; después pinta los comentarios entre
 * paréntesis para que el texto comentado siempre quede legible. Es interno al
 * archivo: el resto de la aplicación sólo usa openGCodeEditor().
 */
class GCodeHighlighter : public QSyntaxHighlighter {
public:
    /**
     * @brief Prepara los formatos y las reglas de resaltado.
     *
     * Paleta tipo "VS Code oscuro": comentarios en verde itálica, G en azul
     * negrita, M en violeta, T en naranja, coordenadas en gris claro, F/S en
     * rojo y números en verde claro.
     *
     * @param document Documento de texto al que se asocia el resaltador; queda
     *        como padre y lo libera al destruirse.
     */
    explicit GCodeHighlighter(QTextDocument* document) : QSyntaxHighlighter(document) {
        comment_.setForeground(QColor(106, 153, 85));
        comment_.setFontItalic(true);
        gcode_.setForeground(QColor(86, 156, 214));
        gcode_.setFontWeight(QFont::Bold);
        mcode_.setForeground(QColor(197, 134, 192));
        mcode_.setFontWeight(QFont::Bold);
        tool_.setForeground(QColor(220, 160, 80));
        coord_.setForeground(QColor(220, 220, 220));
        feed_.setForeground(QColor(255, 120, 120));
        number_.setForeground(QColor(181, 206, 168));
        // El número genérico va primero: las reglas posteriores (G/M/T,
        // coordenadas y F/S) sobrescriben el color de sus dígitos.
        rules_ = {
            {QRegularExpression(R"(\b\d+(\.\d+)?\b)"), number_},
            {QRegularExpression(R"(\bG\d+(\.\d+)?\b)"), gcode_},
            {QRegularExpression(R"(\bM\d+\b)"), mcode_},
            {QRegularExpression(R"(\bT\d+\b)"), tool_},
            {QRegularExpression(R"([XYZIJK][-+]?\d+(\.\d+)?)"), coord_},
            {QRegularExpression(R"([FS][-+]?\d+(\.\d+)?)"), feed_},
        };
    }

protected:
    /**
     * @brief Aplica los formatos a una línea de G-code.
     *
     * Ejecuta todas las reglas sobre la línea, cada una con coincidencia global,
     * y por último resalta los comentarios `( ... )`, que se pintan encima de
     * cualquier token para que el texto comentado quede siempre en verde.
     *
     * @param text Contenido de la línea sin el salto.
     * @note Las reglas se aplican en orden y la última gana en los solapamientos;
     *       como la regla numérica es la última, los dígitos de `G01`, `M06` o
     *       `F300` terminan con el color de número y pierden el del token
     *       (GCodeEditor.cpp:48-55).
     */
    void highlightBlock(const QString& text) override {
        for (const auto& rule : rules_) {
            auto it = rule.pattern.globalMatch(text);
            while (it.hasNext()) {
                const auto match = it.next();
                setFormat(match.capturedStart(), match.capturedLength(), rule.format);
            }
        }

        // Parenthesised comments.
        const QRegularExpression comment(R"(\([^)]*\))");
        auto it = comment.globalMatch(text);
        while (it.hasNext()) {
            const auto match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), comment_);
        }
    }

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    std::vector<Rule> rules_;
    QTextCharFormat comment_;
    QTextCharFormat gcode_;
    QTextCharFormat mcode_;
    QTextCharFormat tool_;
    QTextCharFormat coord_;
    QTextCharFormat feed_;
    QTextCharFormat number_;
};

}  // namespace

/**
 * @brief Abre el editor de G-code para un archivo.
 *
 * Lee el archivo como UTF-8 y crea un `QDialog` no modal con `WA_DeleteOnClose`
 * (se libera al cerrarse), un `QPlainTextEdit` con fuente monoespaciada y fondo
 * oscuro, el resaltador asociado y una caja con Guardar/Cerrar. El botón Guardar
 * **sobrescribe el archivo original** con el contenido del editor, sin copia de
 * seguridad, y marca el título con el sufijo "(guardado)".
 *
 * @param path Archivo a editar.
 * @param parent Widget padre del diálogo.
 * @note Si el archivo no se puede leer se muestra un aviso y no se abre nada.
 *       El texto se carga completo en memoria: archivos de cientos de MB
 *       pueden consumir mucha RAM.
 * @warning No se pide confirmación al cerrar con cambios sin guardar y el
 *          botón Guardar no cierra el diálogo ni controla el estado
 *          modificado (GCodeEditor.cpp:150-162).
 */
void openGCodeEditor(const QString& path, QWidget* parent) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(parent, QObject::tr("Editor G-code"),
                             QObject::tr("No se pudo abrir %1").arg(path));
        return;
    }
    const QString content = QString::fromUtf8(file.readAll());
    file.close();

    auto* dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->resize(900, 700);
    dialog->setWindowTitle(QObject::tr("G-code — %1").arg(QFileInfo(path).fileName()));

    auto* layout = new QVBoxLayout(dialog);
    auto* editor = new QPlainTextEdit(dialog);
    editor->setPlainText(content);
    editor->setStyleSheet("QPlainTextEdit { font-family: Menlo; font-size: 12px; "
                          "background:#1e1e1e; color:#d4d4d4; }");
    new GCodeHighlighter(editor->document());
    layout->addWidget(editor);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, dialog);
    layout->addWidget(box);

    auto savedContent = std::make_shared<QString>(content);
    auto markModified = [dialog, editor, path, savedContent] {
        const bool dirty = editor->toPlainText() != *savedContent;
        dialog->setWindowTitle(
            QObject::tr("G-code — %1%2")
                .arg(QFileInfo(path).fileName(), dirty ? QStringLiteral(" *") : QString()));
    };
    QObject::connect(editor, &QPlainTextEdit::textChanged, dialog, markModified);

    auto save = [dialog, editor, path, savedContent]() -> bool {
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(dialog, QObject::tr("Editor G-code"),
                                 QObject::tr("No se pudo guardar %1.").arg(path));
            return false;
        }
        const QByteArray data = editor->toPlainText().toUtf8();
        if (out.write(data) != data.size() || !out.flush()) {
            QMessageBox::warning(dialog, QObject::tr("Editor G-code"),
                                 QObject::tr("No se pudo escribir todo el archivo."));
            return false;
        }
        out.close();
        *savedContent = editor->toPlainText();
        dialog->setWindowTitle(QObject::tr("G-code — %1 (guardado)")
                                   .arg(QFileInfo(path).fileName()));
        return true;
    };
    QObject::connect(box->button(QDialogButtonBox::Save), &QPushButton::clicked, dialog, save);
    QObject::connect(box, &QDialogButtonBox::rejected, dialog, [dialog, editor, path, savedContent] {
        if (editor->toPlainText() == *savedContent) {
            dialog->reject();
            return;
        }
        const auto answer = QMessageBox::question(
            dialog, QObject::tr("Editor G-code"),
            QObject::tr("Hay cambios sin guardar en %1.\n¿Guardarlos antes de cerrar?")
                .arg(QFileInfo(path).fileName()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (answer == QMessageBox::Cancel) return;
        if (answer == QMessageBox::Save) {
            QFile out(path);
            const QByteArray data = editor->toPlainText().toUtf8();
            if (!out.open(QIODevice::WriteOnly | QIODevice::Text) ||
                out.write(data) != data.size() || !out.flush()) {
                QMessageBox::warning(dialog, QObject::tr("Editor G-code"),
                                     QObject::tr("No se pudo guardar %1.").arg(path));
                return;
            }
            out.close();
        }
        dialog->reject();
    });
    dialog->show();
}
