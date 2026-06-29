#pragma once

#include <QWidget>
#include <QString>

namespace AgoraTheme {

QString dark_stylesheet();
QString light_stylesheet();
QString stylesheet_for(const QString& mode);
void apply(QWidget* app, const QString& mode);

} // namespace AgoraTheme
