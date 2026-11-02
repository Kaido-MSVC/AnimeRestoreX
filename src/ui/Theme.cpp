#include "ui/Theme.h"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QHash>
#include <QStyleFactory>
#include <QStyleHints>

namespace theme {

namespace {

using Tokens = QHash<QString, QString>;

constexpr double kFontPointSize = 9.5;  // Windows default is 9 pt; 10 pt breaks the 620 px minimum width (ja)

// Shape tokens shared by both modes.
const Tokens &shape()
{
    static const Tokens t{
        {QStringLiteral("r"), QStringLiteral("12")},         // cards, list
        {QStringLiteral("r_small"), QStringLiteral("8")},    // buttons, fields
        {QStringLiteral("r_bar"), QStringLiteral("4")},
        {QStringLiteral("bar_h"), QStringLiteral("8")},
        {QStringLiteral("row_pad"), QStringLiteral("5")},
        {QStringLiteral("btn_pad"), QStringLiteral("4px 8px")},
        {QStringLiteral("btn_pad_main"), QStringLiteral("5px 24px")},
        {QStringLiteral("group_pad"), QStringLiteral("10px 6px 8px 8px")},
    };
    return t;
}

const Tokens &tokens(Mode mode)
{
    static const Tokens light{
        {QStringLiteral("mode"), QStringLiteral("light")},
        {QStringLiteral("window"), QStringLiteral("#F6F1EA")},
        {QStringLiteral("surface"), QStringLiteral("#FFFDF9")},
        {QStringLiteral("field"), QStringLiteral("#FFFFFF")},
        {QStringLiteral("field_line"), QStringLiteral("#D8CBB9")},
        {QStringLiteral("border"), QStringLiteral("#E9DFD2")},
        {QStringLiteral("hover"), QStringLiteral("#F4ECE2")},
        {QStringLiteral("pressed"), QStringLiteral("#EBE0D2")},
        {QStringLiteral("text"), QStringLiteral("#2A2420")},
        {QStringLiteral("text_muted"), QStringLiteral("#7A6C60")},
        {QStringLiteral("text_dis"), QStringLiteral("#B3A79B")},
        {QStringLiteral("surface_dis"), QStringLiteral("#F8F3EC")},
        {QStringLiteral("border_dis"), QStringLiteral("#EEE6DB")},
        {QStringLiteral("group_title"), QStringLiteral("#B83A2C")},
        {QStringLiteral("accent"), QStringLiteral("#D9483B")},
        {QStringLiteral("accent_soft"), QStringLiteral("#EE9A8C")},
        {QStringLiteral("accent_hover"), QStringLiteral("#E25B4E")},
        {QStringLiteral("accent_pressed"), QStringLiteral("#C23E32")},
        {QStringLiteral("on_accent"), QStringLiteral("#FFFFFF")},
        {QStringLiteral("accent_dis"), QStringLiteral("#EDB7AF")},
        {QStringLiteral("on_accent_dis"), QStringLiteral("#FFF6F4")},
        {QStringLiteral("selection"), QStringLiteral("#F9E1DB")},
        {QStringLiteral("track"), QStringLiteral("#EDE3D7")},
        {QStringLiteral("scroll"), QStringLiteral("#D6C8B6")},
        {QStringLiteral("danger"), QStringLiteral("#B3261E")},
    };
    static const Tokens dark{
        {QStringLiteral("mode"), QStringLiteral("dark")},
        {QStringLiteral("window"), QStringLiteral("#1C181D")},
        {QStringLiteral("surface"), QStringLiteral("#27212A")},
        {QStringLiteral("field"), QStringLiteral("#2E2731")},
        {QStringLiteral("field_line"), QStringLiteral("#6E5F6A")},
        {QStringLiteral("border"), QStringLiteral("#3A313C")},
        {QStringLiteral("hover"), QStringLiteral("#332B35")},
        {QStringLiteral("pressed"), QStringLiteral("#3D343F")},
        {QStringLiteral("text"), QStringLiteral("#F3ECE6")},
        {QStringLiteral("text_muted"), QStringLiteral("#B9A9A4")},
        {QStringLiteral("text_dis"), QStringLiteral("#776B70")},
        {QStringLiteral("surface_dis"), QStringLiteral("#231E25")},
        {QStringLiteral("border_dis"), QStringLiteral("#322B34")},
        {QStringLiteral("group_title"), QStringLiteral("#FF8B78")},
        {QStringLiteral("accent"), QStringLiteral("#FF7A66")},
        {QStringLiteral("accent_soft"), QStringLiteral("#B0574A")},
        {QStringLiteral("accent_hover"), QStringLiteral("#FF8F7E")},
        {QStringLiteral("accent_pressed"), QStringLiteral("#E86A57")},
        {QStringLiteral("on_accent"), QStringLiteral("#2A0F0B")},
        {QStringLiteral("accent_dis"), QStringLiteral("#5A3A36")},
        {QStringLiteral("on_accent_dis"), QStringLiteral("#A88A85")},
        {QStringLiteral("selection"), QStringLiteral("#4A2F2F")},
        {QStringLiteral("track"), QStringLiteral("#3A313C")},
        {QStringLiteral("scroll"), QStringLiteral("#5A4E5B")},
        {QStringLiteral("danger"), QStringLiteral("#FF8A80")},
    };
    return mode == Mode::Dark ? dark : light;
}

// Placeholders {name} are replaced by tokens. Fonts come from the
// application font (install), not from the style sheet.
const char *const kTemplate = R"QSS(
QMainWindow, QDialog, QWidget#centralWidget { background: {window}; }
QLabel { color: {text}; background: transparent; }
ElidedLabel { color: {text}; }
ElidedLabel#itemProgressLabel, ElidedLabel#batchProgressLabel, QStatusBar ElidedLabel { color: {text_muted}; }
QMenuBar { background: {window}; color: {text}; padding: 2px 4px; }
QMenuBar::item { padding: 4px 10px; border-radius: {r_small}px; background: transparent; }
QMenuBar::item:selected { background: {hover}; }
QMenu { background: {surface}; color: {text}; border: 1px solid {border}; border-radius: {r_small}px; padding: 4px; }
QMenu::item { padding: 5px 24px 5px 12px; border-radius: 6px; }
QMenu::item:selected { background: {hover}; }
QMenu::item:disabled { color: {text_dis}; }
QMenu::separator { height: 1px; background: {border}; margin: 4px 8px; }
QPushButton { background: {surface}; color: {text}; border: 1px solid {border}; border-radius: {r_small}px; padding: {btn_pad}; }
QPushButton:hover { background: {hover}; }
QPushButton:pressed { background: {pressed}; }
QPushButton:focus { border-color: {accent_soft}; }
QPushButton:disabled { color: {text_dis}; background: {surface_dis}; border-color: {border_dis}; }
QPushButton:default, QPushButton#startButton { background: {accent}; color: {on_accent}; border: 1px solid {accent}; font-weight: 600; }
QPushButton#startButton { padding: {btn_pad_main}; }
QPushButton:default:hover, QPushButton#startButton:hover { background: {accent_hover}; border-color: {accent_hover}; }
QPushButton:default:pressed, QPushButton#startButton:pressed { background: {accent_pressed}; }
QPushButton:default:disabled, QPushButton#startButton:disabled { background: {accent_dis}; color: {on_accent_dis}; border-color: {accent_dis}; }
QTreeWidget { background: {surface}; alternate-background-color: {surface}; color: {text}; border: 1px solid {border}; border-radius: {r}px; padding: 4px; outline: 0; }
QTreeWidget::item { padding: {row_pad}px 4px; border: none; }
QTreeWidget::item:selected { background: {selection}; color: {text}; }
QTreeWidget::item:hover:!selected { background: {hover}; }
QHeaderView { background: transparent; }
QHeaderView::section { background: {surface}; color: {text_muted}; border: none; border-bottom: 1px solid {border}; padding: 5px 6px; font-weight: 600; }
QGroupBox { background: {surface}; border: 1px solid {border}; border-radius: {r}px; margin-top: 0; padding: {group_pad}; padding-top: 2em; color: {text}; }
QGroupBox::title { subcontrol-origin: padding; subcontrol-position: top left; left: 10px; top: 0.5em; padding: 0 2px; color: {group_title}; font-weight: 600; }
QGroupBox::title:disabled { color: {group_title}; }
QComboBox, QLineEdit, QSpinBox { background: {field}; color: {text}; border: 1px solid {border}; border-bottom-color: {field_line}; border-radius: {r_small}px; padding: 3px 7px; selection-background-color: {accent}; selection-color: {on_accent}; }
QComboBox { padding-right: 22px; }
QComboBox:hover, QLineEdit:hover, QSpinBox:hover { border-color: {field_line}; }
QComboBox:focus, QLineEdit:focus, QSpinBox:focus { border-color: {accent}; }
QComboBox:disabled, QLineEdit:disabled, QSpinBox:disabled, QLineEdit[readOnly="true"] { color: {text_muted}; background: {surface_dis}; border-color: {border_dis}; }
QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; border: none; width: 20px; }
QComboBox::down-arrow { image: url(:/theme/chevron-down-{mode}.png); width: 12px; height: 12px; }
QComboBox QAbstractItemView { background: {surface}; color: {text}; border: 1px solid {border}; padding: 2px; outline: 0; selection-background-color: {selection}; selection-color: {text}; }
QSpinBox { padding-right: 22px; }
QSpinBox::up-button, QSpinBox::down-button { subcontrol-origin: border; width: 20px; border: none; background: transparent; }
QSpinBox::up-button { subcontrol-position: top right; }
QSpinBox::down-button { subcontrol-position: bottom right; }
QSpinBox::up-button:hover, QSpinBox::down-button:hover { background: {hover}; }
QSpinBox::up-arrow { image: url(:/theme/chevron-up-{mode}.png); width: 10px; height: 10px; }
QSpinBox::down-arrow { image: url(:/theme/chevron-down-{mode}.png); width: 10px; height: 10px; }
QCheckBox, QRadioButton { color: {text}; }
QProgressBar { background: {track}; border: none; border-radius: {r_bar}px; min-height: {bar_h}px; max-height: {bar_h}px; }
QProgressBar::chunk { background: {accent}; border-radius: {r_bar}px; }
QProgressBar#itemProgressBar::chunk { background: {accent_soft}; }
QScrollArea#previewArea { background: {surface}; border: 1px solid {border}; border-radius: {r}px; padding: 5px; }
QScrollArea#previewArea > QWidget#qt_scrollarea_viewport { background: transparent; }
QStatusBar { background: {window}; color: {text_muted}; }
QStatusBar QLabel { color: {text_muted}; }
QStatusBar::item { border: none; }
QSplitter::handle { background: transparent; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: {scroll}; border-radius: 3px; min-height: 24px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: {scroll}; border-radius: 3px; min-width: 24px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QTextBrowser, QTextEdit, QPlainTextEdit { background: {surface}; color: {text}; border: 1px solid {border}; border-radius: {r_small}px; }
QToolTip { background: {surface}; color: {text}; border: 1px solid {border}; padding: 4px; }
)QSS";

Mode g_mode = Mode::Light;

QColor tokenColor(Mode mode, const char *name)
{
    return QColor(tokens(mode).value(QString::fromLatin1(name)));
}

} // namespace

Mode systemMode()
{
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark ? Mode::Dark : Mode::Light;
}

Mode currentMode()
{
    return g_mode;
}

QColor color(const char *token)
{
    return tokenColor(g_mode, token);
}

QString styleSheet(Mode mode)
{
    QString qss = QString::fromUtf8(kTemplate);
    for (const Tokens *t : {&tokens(mode), &shape()})
        for (auto it = t->cbegin(); it != t->cend(); ++it)
            qss.replace(QLatin1Char('{') + it.key() + QLatin1Char('}'), it.value());
    return qss;
}

QPalette palette(Mode mode)
{
    auto c = [mode](const char *name) { return tokenColor(mode, name); };
    QPalette p;
    p.setColor(QPalette::Window, c("window"));
    p.setColor(QPalette::WindowText, c("text"));
    p.setColor(QPalette::Base, c("surface"));
    p.setColor(QPalette::AlternateBase, c("surface"));
    p.setColor(QPalette::Text, c("text"));
    p.setColor(QPalette::Button, c("surface"));
    p.setColor(QPalette::ButtonText, c("text"));
    p.setColor(QPalette::BrightText, c("danger"));
    p.setColor(QPalette::Highlight, c("accent"));
    p.setColor(QPalette::HighlightedText, c("on_accent"));
    p.setColor(QPalette::Link, c("accent"));
    p.setColor(QPalette::LinkVisited, c("accent_pressed"));
    p.setColor(QPalette::PlaceholderText, c("text_muted"));
    p.setColor(QPalette::ToolTipBase, c("surface"));
    p.setColor(QPalette::ToolTipText, c("text"));
    p.setColor(QPalette::Light, c("surface"));
    p.setColor(QPalette::Midlight, c("border"));
    p.setColor(QPalette::Mid, c("border"));
    p.setColor(QPalette::Dark, c("field_line"));
    p.setColor(QPalette::Shadow, c("field_line"));
    for (auto role : {QPalette::Text, QPalette::ButtonText, QPalette::WindowText})
        p.setColor(QPalette::Disabled, role, c("text_dis"));
    p.setColor(QPalette::Disabled, QPalette::Highlight, c("accent_dis"));
    return p;
}

void apply(Mode mode)
{
    g_mode = mode;
    QApplication::setPalette(palette(mode));
    qApp->setStyleSheet(styleSheet(mode));
}

void install(QApplication &app)
{
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    // Segoe UI Variable (Windows 11) when present, otherwise Segoe UI; CJK
    // text falls back to the Windows fonts for the script as before.
    QFont font = QApplication::font();
    QStringList families;
    for (const QString &f : {QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI")})
        if (QFontDatabase::hasFamily(f))
            families << f;
    if (!families.isEmpty())
        font.setFamilies(families);
    font.setPointSizeF(kFontPointSize);
    QApplication::setFont(font);

    apply(systemMode());
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, &app,
                     [](Qt::ColorScheme scheme) {
                         apply(scheme == Qt::ColorScheme::Dark ? Mode::Dark : Mode::Light);
                     });
}

} // namespace theme
