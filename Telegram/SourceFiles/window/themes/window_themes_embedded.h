/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace style {
struct colorizer;
} // namespace style

namespace Window {
namespace Theme {

enum class EmbeddedType {
	Default = 1,
	Night = 2,
};

struct EmbeddedScheme {
	EmbeddedType type = EmbeddedType::Default;
	QColor background;
	QColor sent;
	QColor received;
	QColor radiobuttonInactive;
	QColor radiobuttonActive;
	rpl::producer<QString> name;
	QString path;
	QColor accentColor;
};

class AccentColors final {
public:
	[[nodiscard]] QByteArray serialize() const;
	bool setFromSerialized(const QByteArray &serialized);
	void migrateLegacyDayTheme();

	void set(EmbeddedType type, const QColor &value);
	void clear(EmbeddedType type);
	[[nodiscard]] std::optional<QColor> get(EmbeddedType type) const;

private:
	base::flat_map<EmbeddedType, QColor> _data;
	std::optional<QColor> _legacyDayColor;

};

[[nodiscard]] style::colorizer ColorizerFrom(
	const EmbeddedScheme &scheme,
	const QColor &color);
[[nodiscard]] std::optional<QColor> SystemAccentColor();
[[nodiscard]] style::colorizer ColorizerForTheme(const QString &absolutePath);
void ApplyAccentPalette(style::palette &palette);

void Colorize(
	EmbeddedScheme &scheme,
	const style::colorizer &colorizer);

[[nodiscard]] std::vector<EmbeddedScheme> EmbeddedThemes();
[[nodiscard]] std::vector<QColor> DefaultAccentColors(EmbeddedType type);

[[nodiscard]] Fn<void(style::palette&)> PreparePaletteCallback(
	bool dark,
	std::optional<QColor> accent);
[[nodiscard]] Fn<void(style::palette&)> PrepareCurrentPaletteCallback();

} // namespace Theme
} // namespace Window
