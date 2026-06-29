#include "ui/theme.hpp"
#include <QApplication>
#include <QStyleFactory>
#include <QPalette>
#include <QProcess>
#include <cstdlib>

namespace AgoraTheme {

static const char* dark_css = R"(
/* === Agora Dark Theme (Midnight) === */

QWidget {
    background-color: #1a1a2e;
    color: #e0e0e0;
    font-size: 13px;
}

QMainWindow {
    background-color: #1a1a2e;
}

QToolBar {
    background-color: #16213e;
    border-bottom: 1px solid #2a2a4e;
    padding: 4px;
    spacing: 4px;
}

QToolBar QToolButton {
    background-color: transparent;
    color: #b0b8e0;
    border: 1px solid transparent;
    border-radius: 6px;
    padding: 4px 10px;
}

QToolBar QToolButton:hover {
    background-color: #2a2a4e;
    color: #c4c8f0;
}

QLineEdit {
    background-color: #16213e;
    color: #e0e0e0;
    border: 1px solid #2a2a4e;
    border-radius: 8px;
    padding: 6px 10px;
    selection-background-color: #3a4a8e;
}

QLineEdit:focus {
    border: 1px solid #5a6abe;
}

QTextEdit, QPlainTextEdit {
    background-color: #16213e;
    color: #e0e0e0;
    border: 1px solid #2a2a4e;
    border-radius: 6px;
    padding: 6px;
    selection-background-color: #3a4a8e;
}

QListWidget {
    background-color: #1a1a2e;
    color: #e0e0e0;
    border: none;
    outline: none;
}

QListWidget::item {
    background-color: transparent;
    border: none;
    padding: 2px;
}

QListWidget::item:hover {
    background-color: #222244;
}

QListWidget::item:selected {
    background-color: #2a2a4e;
}

QScrollBar:vertical {
    background-color: #16213e;
    width: 8px;
    margin: 0;
}

QScrollBar::handle:vertical {
    background-color: #3a4a6e;
    border-radius: 4px;
    min-height: 20px;
}

QScrollBar::handle:vertical:hover {
    background-color: #4a5a8e;
}

QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0;
}

QPushButton {
    background-color: #2a2a4e;
    color: #b0b8e0;
    border: 1px solid #3a3a5e;
    border-radius: 6px;
    padding: 5px 14px;
}

QPushButton:hover {
    background-color: #3a3a6e;
    color: #c4c8f0;
}

QPushButton:pressed {
    background-color: #1a1a3e;
}

QPushButton:disabled {
    background-color: #1a1a2e;
    color: #5a5a7e;
}

QCheckBox {
    color: #e0e0e0;
    spacing: 6px;
}

QCheckBox::indicator {
    width: 16px;
    height: 16px;
    border: 1px solid #3a4a6e;
    border-radius: 3px;
    background-color: #16213e;
}

QCheckBox::indicator:checked {
    background-color: #5a6abe;
    border-color: #7a8ade;
}

QRadioButton {
    color: #e0e0e0;
    spacing: 6px;
}

QRadioButton::indicator {
    width: 16px;
    height: 16px;
    border: 2px solid #3a4a6e;
    border-radius: 8px;
    background-color: #16213e;
}

QRadioButton::indicator:checked {
    background-color: #5a6abe;
    border-color: #7a8ade;
}

QComboBox {
    background-color: #16213e;
    color: #e0e0e0;
    border: 1px solid #2a2a4e;
    border-radius: 6px;
    padding: 4px 8px;
}

QComboBox::drop-down {
    border: none;
}

QComboBox QAbstractItemView {
    background-color: #16213e;
    color: #e0e0e0;
    selection-background-color: #2a2a4e;
    border: 1px solid #2a2a4e;
}

QTabWidget::pane {
    background-color: #1a1a2e;
    border: 1px solid #2a2a4e;
    border-radius: 6px;
}

QTabBar::tab {
    background-color: #16213e;
    color: #8a8aae;
    border: 1px solid #2a2a4e;
    border-bottom: none;
    border-top-left-radius: 6px;
    border-top-right-radius: 6px;
    padding: 6px 16px;
    margin-right: 2px;
}

QTabBar::tab:selected {
    background-color: #1a1a2e;
    color: #c4c8f0;
    border-bottom: 2px solid #5a6abe;
}

QTabBar::tab:hover {
    background-color: #222244;
}

QLabel {
    color: #e0e0e0;
    background: transparent;
}

QDialog {
    background-color: #1a1a2e;
}

QDialog QLabel {
    color: #e0e0e0;
}

QMenu {
    background-color: #16213e;
    color: #e0e0e0;
    border: 1px solid #2a2a4e;
    padding: 4px;
}

QMenu::item {
    padding: 6px 24px;
    border-radius: 4px;
}

QMenu::item:selected {
    background-color: #2a2a4e;
}

QMenu::separator {
    height: 1px;
    background-color: #2a2a4e;
    margin: 4px 8px;
}

QSplitter::handle {
    background-color: #2a2a4e;
    width: 2px;
}

QScrollArea {
    background-color: transparent;
    border: none;
}

QFrame[HLine="true"] {
    color: #2a2a4e;
}

QFrame[VLine="true"] {
    color: #2a2a4e;
}
)";

QString dark_stylesheet() {
    return QString(dark_css);
}

QString light_stylesheet() {
    return ""; // system default
}

QString stylesheet_for(const QString& mode) {
    if (mode == "dark") return dark_stylesheet();
    if (mode == "system") {
        // Detect system dark mode preference via env or DBus
        // For now, check common env vars
        const char* gtk_theme = getenv("GTK_THEME");
        if (gtk_theme && QString(gtk_theme).contains("dark", Qt::CaseInsensitive))
            return dark_stylesheet();
        // Check if running GNOME/KDE dark mode
        QProcess proc;
        proc.start("gsettings", {"get", "org.gnome.desktop.interface", "color-scheme"});
        proc.waitForFinished(2000);
        QString output = proc.readAllStandardOutput().trimmed();
        if (output.contains("dark") || output.contains("prefer-dark"))
            return dark_stylesheet();
        return light_stylesheet();
    }
    return light_stylesheet();
}

void apply(QWidget* app, const QString& mode) {
    QString ss = stylesheet_for(mode);
    app->setStyleSheet(ss);

    if (mode == "dark" || (mode == "system" && !ss.isEmpty())) {
        QPalette pal;
        pal.setColor(QPalette::Window, QColor("#1a1a2e"));
        pal.setColor(QPalette::WindowText, QColor("#e0e0e0"));
        pal.setColor(QPalette::Base, QColor("#16213e"));
        pal.setColor(QPalette::AlternateBase, QColor("#1a1a2e"));
        pal.setColor(QPalette::Text, QColor("#e0e0e0"));
        pal.setColor(QPalette::Button, QColor("#2a2a4e"));
        pal.setColor(QPalette::ButtonText, QColor("#b0b8e0"));
        pal.setColor(QPalette::Highlight, QColor("#3a4a8e"));
        pal.setColor(QPalette::HighlightedText, QColor("#ffffff"));
        app->setPalette(pal);
    }
}

} // namespace AgoraTheme
