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
ICON(WIN95, "win95");
ICON(CHIBI, "chibi");
ICON(CHIBI2, "chibi2");
ICON(EXTERA2, "extera2");

void loadAppIco();
QString appIcoPath();

QImage loadPreview(const QString& name);

QString currentAppLogoName();
QImage currentAppLogo();
QImage currentAppLogoPad();

}
