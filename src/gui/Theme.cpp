/**
 * @file Theme.cpp
 * @brief Implementación del tema visual moderno (paleta + hoja de estilo).
 *
 * El tema se construye en dos capas: una `QPalette` que cubre los colores
 * base de Qt y una hoja QSS que define bordes redondeados, espaciado y
 * estados de hover/foco. Se aplica con el estilo Fusion para que el resultado
 * sea idéntico en macOS y en otras plataformas.
 */
#include "Theme.h"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>

#include <utility>

namespace fc::theme {
namespace {

/// Estado global consultado por los iconos para adaptarse al tema.
bool g_dark = false;

/// @brief Colores de un tema concreto.
struct Colors {
    QString window;
    QString base;
    QString alternate;
    QString text;
    QString muted;
    QString disabled;
    QString border;
    QString borderStrong;
    QString button;
    QString buttonDisabled;
    QString hover;
    QString pressed;
    QString header;
    QString accent;
    QString accentHover;
    QString accentText;
    QString accentSoft;
    QString scroll;
    QString scrollHover;
    QString tooltipBg;
    QString tooltipText;
    QString checkIcon;
    QString chevronIcon;
};

/// @brief Devuelve la paleta de colores del tema solicitado.
Colors colorsFor(bool dark) {
    if (dark) {
        return Colors{
            /*window*/ "#1b1f24",
            /*base*/ "#15181c",
            /*alternate*/ "#1f242a",
            /*text*/ "#e7e9ec",
            /*muted*/ "#9aa3ad",
            /*disabled*/ "#6f7882",
            /*border*/ "#2f363e",
            /*borderStrong*/ "#414a54",
            /*button*/ "#242a31",
            /*buttonDisabled*/ "#20252b",
            /*hover*/ "#2e353d",
            /*pressed*/ "#39424c",
            /*header*/ "#20252b",
            /*accent*/ "#e0a458",
            /*accentHover*/ "#eeb96f",
            /*accentText*/ "#1b1f24",
            /*accentSoft*/ "#3a3022",
            /*scroll*/ "#3a424b",
            /*scrollHover*/ "#4a545f",
            /*tooltipBg*/ "#0f1216",
            /*tooltipText*/ "#e7e9ec",
            /*checkIcon*/ ":/icons/check_dark.svg",
            /*chevronIcon*/ ":/icons/chevron_dark.svg",
        };
    }
    return Colors{
        /*window*/ "#f7f5f2",
        /*base*/ "#ffffff",
        /*alternate*/ "#faf8f5",
        /*text*/ "#1f2328",
        /*muted*/ "#6b7280",
        /*disabled*/ "#a8a29a",
        /*border*/ "#ded8cf",
        /*borderStrong*/ "#c6bfb4",
        /*button*/ "#ffffff",
        /*buttonDisabled*/ "#f1eee9",
        /*hover*/ "#f0ece5",
        /*pressed*/ "#e5dfd5",
        /*header*/ "#f1ede6",
        /*accent*/ "#a8641a",
        /*accentHover*/ "#8f5412",
        /*accentText*/ "#ffffff",
        /*accentSoft*/ "#f8ecd8",
        /*scroll*/ "#cfc8bd",
        /*scrollHover*/ "#b3aa9d",
        /*tooltipBg*/ "#2b2f36",
        /*tooltipText*/ "#f4f4f5",
        /*checkIcon*/ ":/icons/check_light.svg",
        /*chevronIcon*/ ":/icons/chevron_light.svg",
    };
}

/// @brief Sustituye los marcadores `@color` de la plantilla QSS.
QString expand(QString sheet, const Colors& c) {
    const std::pair<const char*, QString> tokens[] = {
        {"@window", c.window},         {"@base", c.base},
        {"@alternate", c.alternate},   {"@text", c.text},
        {"@muted", c.muted},           {"@disabled", c.disabled},
        {"@borderStrong", c.borderStrong},
        {"@border", c.border},         {"@buttonDisabled", c.buttonDisabled},
        {"@button", c.button},         {"@hover", c.hover},
        {"@pressed", c.pressed},       {"@header", c.header},
        {"@accentHover", c.accentHover},
        {"@accentSoft", c.accentSoft}, {"@accentText", c.accentText},
        {"@accent", c.accent},         {"@scrollHover", c.scrollHover},
        {"@scroll", c.scroll},         {"@tooltipBg", c.tooltipBg},
        {"@tooltipText", c.tooltipText},
        {"@checkIcon", c.checkIcon},
        {"@chevronIcon", c.chevronIcon},
    };
    for (const auto& [token, value] : tokens)
        sheet.replace(QString::fromLatin1(token), value);
    return sheet;
}

/// @brief Plantilla QSS común a los dos temas.
const char* kTemplate = R"QSS(
/* ---- Ventanas y separadores ------------------------------------------- */
QMainWindow, QDialog { background: @window; }
QMainWindow::separator { background: @border; width: 1px; height: 1px; }
QMainWindow::separator:hover { background: @accent; }

/* ---- Barra de menús y menús ------------------------------------------- */
QMenuBar { background: @window; border-bottom: 1px solid @border; padding: 3px 6px; }
QMenuBar::item { background: transparent; padding: 5px 10px; border-radius: 6px; }
QMenuBar::item:selected { background: @hover; }
QMenuBar::item:pressed { background: @pressed; }

QMenu { background: @base; border: 1px solid @border; border-radius: 9px; padding: 6px; }
QMenu::item { padding: 6px 26px 6px 28px; border-radius: 6px; }
QMenu::item:selected { background: @accentSoft; color: @text; }
QMenu::item:disabled { color: @disabled; }
QMenu::separator { height: 1px; background: @border; margin: 5px 8px; }

/* ---- Barras de herramientas ------------------------------------------- */
QToolBar { background: @window; border: 0; border-bottom: 1px solid @border;
           spacing: 3px; padding: 5px 6px; }
QToolBar::separator { background: @border; width: 1px; margin: 4px 5px; }
QToolButton { background: transparent; border: 1px solid transparent;
              border-radius: 7px; padding: 4px; }
QToolButton:hover { background: @hover; border-color: @border; }
QToolButton:pressed, QToolButton:checked { background: @accentSoft; border-color: @accent; }

/* ---- Pestañas ---------------------------------------------------------- */
QTabWidget::pane { border: 1px solid @border; border-radius: 9px;
                   background: @base; top: -1px; }
QTabWidget[flat="true"]::pane { border: 0; background: transparent; }
QTabBar { background: transparent; }
QTabBar::tab { background: transparent; color: @muted; border: 0;
               border-bottom: 2px solid transparent; padding: 7px 14px;
               margin-right: 2px; }
QTabBar::tab:hover:!selected { color: @text; background: @hover;
                               border-top-left-radius: 7px;
                               border-top-right-radius: 7px; }
QTabBar::tab:selected { color: @text; border-bottom: 2px solid @accent;
                        font-weight: 600; }
QTabBar::tab:disabled { color: @disabled; }
QTabBar QToolButton { background: @button; border: 1px solid @border;
                      border-radius: 6px; }

/* ---- Docks y paneles --------------------------------------------------- */
QDockWidget { background: @window; color: @text; }
QDockWidget::title { background: @window; padding: 6px; border: 0; }
QScrollArea { background: transparent; border: 0; }

/* ---- Botones ----------------------------------------------------------- */
QPushButton { background: @button; color: @text; border: 1px solid @border;
              border-radius: 7px; padding: 5px 12px; min-height: 16px; }
QPushButton:hover { background: @hover; border-color: @borderStrong; }
QPushButton:pressed { background: @pressed; }
QPushButton:checked { background: @accentSoft; border-color: @accent; }
QPushButton:disabled { background: @buttonDisabled; color: @disabled;
                       border-color: @border; }
QPushButton:default, QPushButton[primary="true"] {
    background: @accent; color: @accentText; border: 1px solid @accent;
    font-weight: 600;
}
QPushButton:default:hover, QPushButton[primary="true"]:hover {
    background: @accentHover; border-color: @accentHover;
}
QPushButton:default:pressed, QPushButton[primary="true"]:pressed {
    background: @accent; border-color: @accent;
}
QPushButton[primary="true"]:disabled { background: @buttonDisabled;
                                       color: @disabled; border-color: @border; }
QDialogButtonBox QPushButton { min-width: 88px; }

/* ---- Entradas de texto y números -------------------------------------- */
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QPlainTextEdit, QTextEdit,
QAbstractSpinBox {
    background: @base; color: @text; border: 1px solid @border;
    border-radius: 7px; padding: 4px 7px;
    selection-background-color: @accent; selection-color: @accentText;
}
QLineEdit:hover, QComboBox:hover, QSpinBox:hover, QDoubleSpinBox:hover,
QPlainTextEdit:hover, QTextEdit:hover { border-color: @borderStrong; }
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus,
QPlainTextEdit:focus, QTextEdit:focus { border-color: @accent; }
QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled,
QDoubleSpinBox:disabled, QPlainTextEdit:disabled, QTextEdit:disabled {
    background: @buttonDisabled; color: @disabled; border-color: @border;
}
QComboBox::drop-down { border: 0; width: 20px; background: transparent; }
QComboBox::down-arrow { image: url(@chevronIcon); width: 12px; height: 12px;
                        margin-right: 4px; }
QComboBox QAbstractItemView { background: @base; border: 1px solid @border;
                              border-radius: 7px; padding: 4px; outline: 0;
                              selection-background-color: @accentSoft;
                              selection-color: @text; }
QPlainTextEdit, QTextEdit { padding: 6px; }

/* ---- Casillas y botones de opción ------------------------------------- */
QCheckBox, QRadioButton { spacing: 7px; background: transparent; }
QCheckBox:disabled, QRadioButton:disabled { color: @disabled; }
QCheckBox::indicator, QRadioButton::indicator {
    width: 15px; height: 15px; background: @base;
    border: 1px solid @borderStrong;
}
QCheckBox::indicator { border-radius: 4px; }
QRadioButton::indicator { border-radius: 8px; }
QCheckBox::indicator:hover, QRadioButton::indicator:hover {
    border-color: @accent;
}
QCheckBox::indicator:checked { background: @accent; border-color: @accent;
                               image: url(@checkIcon); }
QCheckBox::indicator:checked:disabled { background: @disabled;
                                        border-color: @disabled; }
QRadioButton::indicator:checked { background: @accent; border-color: @accent; }
QCheckBox::indicator:indeterminate { background: @accent; border-color: @accent; }

/* ---- Tablas, árboles y listas ----------------------------------------- */
QTableWidget, QTableView, QTreeWidget, QTreeView, QListWidget, QListView {
    background: @base; alternate-background-color: @alternate;
    border: 1px solid @border; border-radius: 9px;
    gridline-color: @border; outline: 0;
    selection-background-color: @accentSoft; selection-color: @text;
}
QTableView::item, QTreeView::item, QListView::item { padding: 4px 3px; }
QTableView::item:selected, QTreeView::item:selected, QListView::item:selected {
    background: @accentSoft; color: @text;
}
QTableView::item:hover, QTreeView::item:hover, QListView::item:hover {
    background: @hover;
}
QHeaderView { background: transparent; }
QHeaderView::section {
    background: @header; color: @muted; border: 0;
    border-right: 1px solid @border; border-bottom: 1px solid @border;
    padding: 5px 6px; font-weight: 600;
}
QHeaderView::section:hover { color: @text; }
QTableCornerButton::section { background: @header; border: 0; }
QTreeView::indicator { width: 15px; height: 15px; background: @base;
                       border: 1px solid @borderStrong; border-radius: 4px; }
QTreeView::indicator:checked { background: @accent; border-color: @accent;
                               image: url(@checkIcon); }
QTreeView::branch { background: transparent; }

/* ---- Barras de desplazamiento ----------------------------------------- */
QScrollBar:vertical { background: transparent; width: 12px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px; }
QScrollBar::handle:vertical { background: @scroll; min-height: 28px;
                              border-radius: 4px; }
QScrollBar::handle:horizontal { background: @scroll; min-width: 28px;
                                border-radius: 4px; }
QScrollBar::handle:hover { background: @scrollHover; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; border: 0;
                                             background: transparent; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

/* ---- Cajas de grupo, barra de estado y ayudas -------------------------- */
QGroupBox { background: @base; border: 1px solid @border; border-radius: 9px;
            margin-top: 14px; padding: 12px 8px 8px 8px; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left;
                   left: 12px; padding: 0 5px; color: @muted;
                   font-weight: 600; }
QStatusBar { background: @window; border-top: 1px solid @border; color: @muted; }
QStatusBar::item { border: 0; }
QStatusBar QLabel { color: @muted; padding: 0 6px; }
QToolTip { background: @tooltipBg; color: @tooltipText; border: 1px solid @border;
           border-radius: 6px; padding: 6px 8px; }

/* ---- Etiquetas con función -------------------------------------------- */
QLabel:disabled { color: @disabled; }
QLabel[role="pageTitle"] { font-size: 17px; font-weight: 700; color: @text;
                           padding: 2px 0 4px 0; }
QLabel[role="section"] { font-size: 11px; font-weight: 700; color: @accent;
                         letter-spacing: 1px; text-transform: uppercase;
                         padding-top: 10px; padding-bottom: 2px; }
QFrame[role="rule"] { background: @border; border: 0; max-height: 1px;
                      min-height: 1px; }
QLabel[role="badge"] { background: @accentSoft; color: @accent;
                       border-radius: 9px; padding: 2px 9px;
                       font-size: 10px; font-weight: 700;
                       letter-spacing: 1px; }

/* ---- Progreso ---------------------------------------------------------- */
QProgressBar { background: @buttonDisabled; border: 1px solid @border;
               border-radius: 7px; text-align: center; color: @text;
               min-height: 14px; }
QProgressBar::chunk { background: @accent; border-radius: 6px; margin: 1px; }
)QSS";

}  // namespace

QString styleSheet(bool dark) {
    return expand(QString::fromUtf8(kTemplate), colorsFor(dark));
}

bool isDark() {
    return g_dark;
}

void apply(QApplication& app, bool dark) {
    g_dark = dark;
    const Colors c = colorsFor(dark);
    const auto color = [](const QString& value) { return QColor(value); };

    if (QStyle* style = QStyleFactory::create("Fusion")) app.setStyle(style);

    QPalette palette;
    palette.setColor(QPalette::Window, color(c.window));
    palette.setColor(QPalette::WindowText, color(c.text));
    palette.setColor(QPalette::Base, color(c.base));
    palette.setColor(QPalette::AlternateBase, color(c.alternate));
    palette.setColor(QPalette::Text, color(c.text));
    palette.setColor(QPalette::Button, color(c.button));
    palette.setColor(QPalette::ButtonText, color(c.text));
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::Highlight, color(c.accent));
    palette.setColor(QPalette::HighlightedText, color(c.accentText));
    palette.setColor(QPalette::ToolTipBase, color(c.tooltipBg));
    palette.setColor(QPalette::ToolTipText, color(c.tooltipText));
    palette.setColor(QPalette::PlaceholderText, color(c.muted));
    palette.setColor(QPalette::Link, color(c.accent));
    palette.setColor(QPalette::LinkVisited, color(c.accentHover));
    palette.setColor(QPalette::Light, color(c.borderStrong));
    palette.setColor(QPalette::Midlight, color(c.border));
    palette.setColor(QPalette::Mid, color(c.borderStrong));
    palette.setColor(QPalette::Dark, color(c.disabled));
    palette.setColor(QPalette::Shadow, color(c.text));

    palette.setColor(QPalette::Disabled, QPalette::WindowText, color(c.disabled));
    palette.setColor(QPalette::Disabled, QPalette::Text, color(c.disabled));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, color(c.disabled));
    palette.setColor(QPalette::Disabled, QPalette::Base, color(c.buttonDisabled));
    palette.setColor(QPalette::Disabled, QPalette::Button, color(c.buttonDisabled));
    palette.setColor(QPalette::Disabled, QPalette::Highlight, color(c.borderStrong));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, color(c.disabled));

    app.setPalette(palette);
    app.setStyleSheet(styleSheet(dark));
}

}  // namespace fc::theme
