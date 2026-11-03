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

namespace {

class GCodeHighlighter : public QSyntaxHighlighter {
public:
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
        rules_ = {
            {QRegularExpression(R"(\bG\d+(\.\d+)?\b)"), gcode_},
            {QRegularExpression(R"(\bM\d+\b)"), mcode_},
            {QRegularExpression(R"(\bT\d+\b)"), tool_},
            {QRegularExpression(R"([XYZIJK][-+]?\d+(\.\d+)?)"), coord_},
            {QRegularExpression(R"([FS][-+]?\d+(\.\d+)?)"), feed_},
            {QRegularExpression(R"(\b\d+(\.\d+)?\b)"), number_},
        };
    }

protected:
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
    QObject::connect(box, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(box->button(QDialogButtonBox::Save), &QPushButton::clicked, dialog,
                     [dialog, editor, path] {
                         QFile out(path);
                         if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
                             QMessageBox::warning(dialog, QObject::tr("Editor G-code"),
                                                  QObject::tr("No se pudo guardar."));
                             return;
                         }
                         out.write(editor->toPlainText().toUtf8());
                         dialog->setWindowTitle(QObject::tr("G-code — %1 (guardado)")
                                                    .arg(QFileInfo(path).fileName()));
                     });
    dialog->show();
}
