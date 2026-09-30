#pragma once

#define ICON(name, value) const auto name##_ICON = QStringLiteral(value)

namespace ExtrasAssets {

ICON(DEFAULT, "default");
ICON(ALT, "alt");
ICON(DISCORD, "discord");
ICON(SPOTIFY, "spotify");
ICON(EXTERA, "extera");
ICON(NOTHING, "nothing");
ICON(BARD, "bard");
ICON(YAPLUS, "yaplus");

// 图标选择器提供的全部预设，顺序即界面顺序。
[[nodiscard]] const QVector<QString> &appIcons();

void loadAppIco();
QString appIcoPath();

QImage loadPreview(const QString& name);

QString currentAppLogoName();
QImage currentAppLogo();
QImage currentAppLogoPad();

}
