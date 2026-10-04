/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/themes/window_themes_embedded.h"

#include "lang/lang_keys.h"
#include "storage/serialize_common.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "ui/style/style_palette_colorizer.h"
#include "ui/effects/animations.h"
#include "window/themes/window_theme.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QPalette>

#include <cmath>
#include <numbers>

#ifdef Q_OS_WIN
#include "platform/win/integration_win.h"
#endif

// AyuGram includes
#include "extras/features/message_shot/message_shot.h"


namespace Window {
namespace Theme {
namespace {

constexpr auto kMaxAccentColors = 3;
constexpr auto kDayBaseFile = ":/gui/day-custom-base.tdesktop-theme"_cs;
constexpr auto kNightBaseFile = ":/gui/night-custom-base.tdesktop-theme"_cs;

const auto kColorizeIgnoredKeys = base::flat_set<QLatin1String>{ {
	qstr("boxTextFgGood"),
	qstr("boxTextFgError"),
	qstr("callIconFg"),
	qstr("historyPeer1NameFg"),
	qstr("historyPeer1NameFgSelected"),
	qstr("historyPeer1UserpicBg"),
	qstr("historyPeer2NameFg"),
	qstr("historyPeer2NameFgSelected"),
	qstr("historyPeer2UserpicBg"),
	qstr("historyPeer3NameFg"),
	qstr("historyPeer3NameFgSelected"),
	qstr("historyPeer3UserpicBg"),
	qstr("historyPeer4NameFg"),
	qstr("historyPeer4NameFgSelected"),
	qstr("historyPeer4UserpicBg"),
	qstr("historyPeer5NameFg"),
	qstr("historyPeer5NameFgSelected"),
	qstr("historyPeer5UserpicBg"),
	qstr("historyPeer6NameFg"),
	qstr("historyPeer6NameFgSelected"),
	qstr("historyPeer6UserpicBg"),
	qstr("historyPeer7NameFg"),
	qstr("historyPeer7NameFgSelected"),
	qstr("historyPeer7UserpicBg"),
	qstr("historyPeer8NameFg"),
	qstr("historyPeer8NameFgSelected"),
	qstr("historyPeer8UserpicBg"),
	qstr("historyPeer1UserpicBg2"),
	qstr("historyPeer2UserpicBg2"),
	qstr("historyPeer3UserpicBg2"),
	qstr("historyPeer4UserpicBg2"),
	qstr("historyPeer5UserpicBg2"),
	qstr("historyPeer6UserpicBg2"),
	qstr("historyPeer7UserpicBg2"),
	qstr("historyPeer8UserpicBg2"),
	qstr("msgFile1Bg"),
	qstr("msgFile1BgDark"),
	qstr("msgFile1BgOver"),
	qstr("msgFile1BgSelected"),
	qstr("msgFile2Bg"),
	qstr("msgFile2BgDark"),
	qstr("msgFile2BgOver"),
	qstr("msgFile2BgSelected"),
	qstr("msgFile3Bg"),
	qstr("msgFile3BgDark"),
	qstr("msgFile3BgOver"),
	qstr("msgFile3BgSelected"),
	qstr("msgFile4Bg"),
	qstr("msgFile4BgDark"),
	qstr("msgFile4BgOver"),
	qstr("msgFile4BgSelected"),
	qstr("mediaviewFileRedCornerFg"),
	qstr("mediaviewFileYellowCornerFg"),
	qstr("mediaviewFileGreenCornerFg"),
	qstr("mediaviewFileBlueCornerFg"),
	qstr("settingsIconBg1"),
	qstr("settingsIconBg2"),
	qstr("settingsIconBg3"),
	qstr("settingsIconBg4"),
	qstr("settingsIconBg5"),
	qstr("settingsIconBg6"),
	qstr("settingsIconBg8"),
	qstr("settingsIconBgArchive"),
	qstr("premiumButtonBg1"),
	qstr("premiumButtonBg2"),
	qstr("premiumButtonBg3"),
	qstr("premiumIconBg1"),
	qstr("premiumIconBg2"),
} };

constexpr auto kNightAccentColor = std::string_view("5288c1");

style::colorizer::Color cColor(std::string_view hex) {
	const auto q = style::ColorFromHex(hex);
	auto hue = int();
	auto saturation = int();
	auto value = int();
	q.getHsv(&hue, &saturation, &value);
	return style::colorizer::Color{ hue, saturation, value };
}

// 调色器只记录原强调色，以夜间主题的原强调色识别夜间调色器。
[[nodiscard]] bool IsNightColorizer(const style::colorizer &colorizer) {
	static const auto night = cColor(kNightAccentColor);
	return (colorizer.was.hue == night.hue)
		&& (colorizer.was.saturation == night.saturation)
		&& (colorizer.was.value == night.value);
}

// 夜间背景固定为中性灰：比强调色更灰的颜色只换色相，亮度保持原值，避免暗强调色压低层次。
[[nodiscard]] QColor KeepNeutralValue(
		const QColor &original,
		const QColor &colorized,
		const style::colorizer &colorizer) {
	if (colorized.rgba() == original.rgba()
		|| !IsNightColorizer(colorizer)
		|| original.hsvSaturation() >= colorizer.was.saturation) {
		return colorized;
	}
	auto hue = 0;
	auto saturation = 0;
	auto value = 0;
	colorized.getHsv(&hue, &saturation, &value);
	return QColor::fromHsv(hue, saturation, original.value(), colorized.alpha());
}

struct Oklch {
	double lightness = 0.;
	double chroma = 0.;
	double hue = 0.;
};

using LinearRgb = std::array<double, 3>;

[[nodiscard]] double ToLinear(double value) {
	return (value <= 0.04045)
		? (value / 12.92)
		: std::pow((value + 0.055) / 1.055, 2.4);
}

[[nodiscard]] double FromLinear(double value) {
	value = std::clamp(value, 0., 1.);
	return (value <= 0.0031308)
		? (12.92 * value)
		: (1.055 * std::pow(value, 1. / 2.4) - 0.055);
}

[[nodiscard]] Oklch ToOklch(const QColor &color) {
	const auto r = ToLinear(color.redF());
	const auto g = ToLinear(color.greenF());
	const auto b = ToLinear(color.blueF());
	const auto l = std::cbrt(
		0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
	const auto m = std::cbrt(
		0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
	const auto s = std::cbrt(
		0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
	const auto x = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
	const auto y = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
	const auto chroma = std::sqrt(x * x + y * y);
	const auto hue = (chroma < 0.000001)
		? 0.
		: (std::atan2(y, x) * 180. / std::numbers::pi);
	return {
		.lightness = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
		.chroma = chroma,
		.hue = (hue < 0.) ? (hue + 360.) : hue,
	};
}

[[nodiscard]] LinearRgb ToLinearRgb(double lightness, double x, double y) {
	auto l = lightness + 0.3963377774 * x + 0.2158037573 * y;
	auto m = lightness - 0.1055613458 * x - 0.0638541728 * y;
	auto s = lightness - 0.0894841775 * x - 1.2914855480 * y;
	l *= l * l;
	m *= m * m;
	s *= s * s;
	return {
		4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
		-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
		-0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s,
	};
}

[[nodiscard]] QColor ToColor(const LinearRgb &rgb) {
	const auto channel = [](double value) {
		return int(std::lround(FromLinear(value) * 255.));
	};
	return QColor(channel(rgb[0]), channel(rgb[1]), channel(rgb[2]));
}

// 超出 sRGB 色域时逐步降低彩度，最终退化为同明度的灰色。
[[nodiscard]] QColor FromOklch(double lightness, double chroma, double hue) {
	const auto radians = hue * std::numbers::pi / 180.;
	for (auto i = 0; i != 32; ++i) {
		const auto rgb = ToLinearRgb(
			lightness,
			chroma * std::cos(radians),
			chroma * std::sin(radians));
		if (ranges::all_of(rgb, [](double v) { return v >= 0. && v <= 1.; })) {
			return ToColor(rgb);
		}
		chroma *= 0.9;
	}
	return ToColor(ToLinearRgb(lightness, 0., 0.));
}

[[nodiscard]] double ContrastRatio(const QColor &a, const QColor &b) {
	const auto luminance = [](const QColor &color) {
		return 0.2126 * ToLinear(color.redF())
			+ 0.7152 * ToLinear(color.greenF())
			+ 0.0722 * ToLinear(color.blueF());
	};
	const auto first = luminance(a);
	const auto second = luminance(b);
	return (std::max(first, second) + 0.05) / (std::min(first, second) + 0.05);
}

// 与 stelliberty 一致：只取系统色相，彩度限制在 0.025..0.18，明度按主题固定，与背景对比度至少 3:1。
[[nodiscard]] QColor NormalizeSystemAccent(
		const QColor &source,
		const QColor &surface,
		bool dark) {
	const auto oklch = ToOklch(source);
	const auto chroma = (oklch.chroma < 0.015)
		? 0.
		: std::clamp(oklch.chroma, 0.025, 0.18);
	auto result = FromOklch(dark ? 0.78 : 0.54, chroma, oklch.hue);
	if (ContrastRatio(result, surface) >= 3.) {
		return result;
	}
	auto tone = ToOklch(result).lightness;
	for (auto i = 0; i != 24; ++i) {
		tone = std::clamp(tone + (dark ? 0.02 : -0.02), 0.30, 0.88);
		result = FromOklch(tone, chroma, oklch.hue);
		if (ContrastRatio(result, surface) >= 3.) {
			return result;
		}
	}
	return result;
}

} // namespace

style::colorizer ColorizerFrom(
		const EmbeddedScheme &scheme,
		const QColor &color) {
	using Color = style::colorizer::Color;
	using Pair = std::pair<Color, Color>;

	auto result = style::colorizer();
	result.ignoreKeys = kColorizeIgnoredKeys;
	result.hueThreshold = 15;
	scheme.accentColor.getHsv(
		&result.was.hue,
		&result.was.saturation,
		&result.was.value);
	color.getHsv(
		&result.now.hue,
		&result.now.saturation,
		&result.now.value);
	switch (scheme.type) {
	case EmbeddedType::Default:
		result.lightnessMax = 160;
		break;
	case EmbeddedType::Night:
		result.keepContrast = base::flat_map<QLatin1String, Pair>{ {
			//{ qstr("windowFgActive"), Pair{ cColor("5288c1"), cColor("17212b") } }, // windowBgActive
			{ qstr("activeButtonFg"), Pair{ cColor("2f6ea5"), cColor("17212b") } }, // activeButtonBg
			{ qstr("profileVerifiedCheckFg"), Pair{ cColor("5288c1"), cColor("17212b") } }, // profileVerifiedCheckBg
			{ qstr("overviewCheckFgActive"), Pair{ cColor("5288c1"), cColor("17212b") } }, // overviewCheckBgActive
			{ qstr("historyFileInIconFg"), Pair{ cColor("3f96d0"), cColor("182533") } }, // msgFileInBg, msgInBg
			{ qstr("historyFileInIconFgSelected"), Pair{ cColor("6ab4f4"), cColor("2e70a5") } }, // msgFileInBgSelected, msgInBgSelected
			{ qstr("historyFileInRadialFg"), Pair{ cColor("3f96d0"), cColor("182533") } }, // msgFileInBg, msgInBg
			{ qstr("historyFileInRadialFgSelected"), Pair{ cColor("6ab4f4"), cColor("2e70a5") } }, // msgFileInBgSelected, msgInBgSelected
			{ qstr("historyFileOutIconFg"), Pair{ cColor("4c9ce2"), cColor("2b5278") } }, // msgFileOutBg, msgOutBg
			{ qstr("historyFileOutIconFgSelected"), Pair{ cColor("58abf3"), cColor("2e70a5") } }, // msgFileOutBgSelected, msgOutBgSelected
			{ qstr("historyFileOutRadialFg"), Pair{ cColor("4c9ce2"), cColor("2b5278") } }, // msgFileOutBg, msgOutBg
			{ qstr("historyFileOutRadialFgSelected"), Pair{ cColor("58abf3"), cColor("2e70a5") } }, // msgFileOutBgSelected, msgOutBgSelected
		} };
		result.lightnessMin = 64;
		break;
	}
	const auto nowLightness = color.lightness();
	const auto limitedLightness = std::clamp(
		nowLightness,
		result.lightnessMin,
		result.lightnessMax);
	if (limitedLightness != nowLightness) {
		QColor::fromHsl(
			color.hslHue(),
			color.hslSaturation(),
			limitedLightness).getHsv(
				&result.now.hue,
				&result.now.saturation,
				&result.now.value);
	}
	return result;
}

std::optional<QColor> SystemAccentColor() {
#ifdef Q_OS_WIN
	return Platform::WindowsIntegration::Instance().systemAccentColor();
#else
	const auto accent = QPalette().color(QPalette::Highlight);
	return accent.isValid() ? std::make_optional(accent) : std::nullopt;
#endif
}

std::optional<QColor> SystemAccentColor(const EmbeddedScheme &scheme) {
	const auto accent = SystemAccentColor();
	if (!accent) {
		return std::nullopt;
	}
	return NormalizeSystemAccent(
		*accent,
		scheme.background,
		scheme.type == EmbeddedType::Night);
}

style::colorizer ColorizerForTheme(const QString &absolutePath) {
	if (!IsEmbeddedTheme(absolutePath)) {
		return {};
	}
	const auto schemes = EmbeddedThemes();
	const auto i = ranges::find(
		schemes,
		absolutePath,
		&EmbeddedScheme::path);
	if (i == end(schemes)) {
		return {};
	}
	const auto &settings = Core::App().settings();
	if (settings.systemAccentColorEnabled()) {
		if (const auto accent = SystemAccentColor(*i)) {
			return ColorizerFrom(*i, *accent);
		}
	}
	const auto &colors = settings.themesAccentColors();
	if (const auto accent = ExtrasFeatures::MessageShot::isChoosingTheme() ? ExtrasFeatures::MessageShot::getSelectedColorFromDefault() : colors.get(i->type)) {
		return ColorizerFrom(*i, *accent);
	}
	return {};
}

void ColorizeSchemeValue(
		QLatin1String name,
		uchar &r,
		uchar &g,
		uchar &b,
		const style::colorizer &colorizer) {
	const auto original = QColor(int(r), int(g), int(b));
	style::colorize(name, r, g, b, colorizer);
	// 对比度配对的键已由调色器选定前景，不再调整亮度。
	if (colorizer.keepContrast.contains(name)) {
		return;
	}
	const auto result = KeepNeutralValue(
		original,
		QColor(int(r), int(g), int(b)),
		colorizer).toRgb();
	r = uchar(result.red());
	g = uchar(result.green());
	b = uchar(result.blue());
}

void Colorize(EmbeddedScheme &scheme, const style::colorizer &colorizer) {
	const auto colors = {
		&EmbeddedScheme::background,
		&EmbeddedScheme::sent,
		&EmbeddedScheme::received,
		&EmbeddedScheme::radiobuttonActive,
		&EmbeddedScheme::radiobuttonInactive
	};
	for (const auto color : colors) {
		if (const auto changed = style::colorize(scheme.*color, colorizer)) {
			scheme.*color = KeepNeutralValue(
				scheme.*color,
				*changed,
				colorizer).toRgb();
		}
	}
}

void ApplyAccentPalette(style::palette &palette) {
	// 选中行用强调色实底配白字，强调色偏浅时压暗到白字对比度至少 4.5:1。
	// 所有主题统一覆盖，导入主题自带的这些配色不生效。
	const auto white = QColor(255, 255, 255);
	const auto accent = palette.windowBgActive()->c;
	auto bg = accent;
	for (auto i = 1; i <= 100 && ContrastRatio(bg, white) < 4.5; ++i) {
		bg = anim::color(accent, QColor(0, 0, 0), i / 100.);
	}
	const auto tint = [&](float64 ratio) {
		return anim::color(bg, white, ratio);
	};
	const auto tick = palette.windowActiveTextFg()->c;
	const std::pair<const char*, QColor> colors[] = {
		// 已读勾号跟随强调色，不用固定绿色。
		{ "dialogsSentIconFg", tick },
		{ "dialogsSentIconFgOver", tick },
		{ "dialogsBgActive", bg },
		{ "dialogsNameFgActive", white },
		{ "dialogsChatIconFgActive", white },
		{ "dialogsDateFgActive", tint(0.82) },
		{ "dialogsTextFgActive", tint(0.9) },
		{ "dialogsTextFgServiceActive", white },
		{ "dialogsDraftFgActive", white },
		{ "dialogsScamFgActive", white },
		{ "dialogsVerifiedIconBgActive", white },
		{ "dialogsVerifiedIconFgActive", bg },
		{ "dialogsSentIconFgActive", white },
		{ "dialogsUnreadBgActive", white },
		{ "dialogsUnreadFgActive", bg },
		{ "dialogsUnreadBgMutedActive", tint(0.7) },
		{ "dialogsOnlineBadgeFgActive", white },
		{ "dialogsRippleBgActive", tint(0.12) },
	};
	for (const auto &[name, color] : colors) {
		palette.setColor(QLatin1String(name), color);
	}
}

std::vector<EmbeddedScheme> EmbeddedThemes() {
	const auto qColor = [](auto hex) {
		return style::ColorFromHex(hex);
	};
	const auto name = [](auto key) {
		return rpl::deferred([=] { return key(); });
	};
	return {
		EmbeddedScheme{
			EmbeddedType::Default,
			qColor("ffffff"),
			qColor("d4edff"),
			qColor("ffffff"),
			qColor("d4edff"),
			qColor("ffffff"),
			name(tr::extras_ThemeSimpleWhite),
			QString(),
			qColor("238fe8")
		},
		EmbeddedScheme{
			EmbeddedType::Night,
			qColor("212121"),
			qColor("5ca7d4"),
			qColor("6b808d"),
			qColor("6b808d"),
			qColor("5ca7d4"),
			name(tr::extras_ThemeSimpleBlack),
			":/gui/night.tdesktop-theme",
			qColor(kNightAccentColor)
		},
	};
}

std::vector<QColor> DefaultAccentColors(EmbeddedType type) {
	const auto qColor = [](auto hex) {
		return style::ColorFromHex(hex);
	};
	switch (type) {
	case EmbeddedType::Default:
		return {
			qColor("45bce7"),
			qColor("52b440"),
			qColor("d46c99"),
			qColor("df8a49"),
			qColor("9978c8"),
			qColor("c55245"),
			qColor("687b98"),
			qColor("dea922"),
		};
	case EmbeddedType::Night:
		return {
			qColor("58bfe8"),
			qColor("466f42"),
			qColor("aa6084"),
			qColor("a46d3c"),
			qColor("917bbd"),
			qColor("ab5149"),
			qColor("697b97"),
			qColor("9b834b"),
		};
	}
	Unexpected("Type in Window::Theme::AccentColors.");
}

Fn<void(style::palette&)> PreparePaletteCallback(
		bool dark,
		std::optional<QColor> accent) {
	return [=](style::palette &palette) {
		using namespace Theme;
		const auto &embedded = EmbeddedThemes();
		const auto i = ranges::find(
			embedded,
			dark ? EmbeddedType::Night : EmbeddedType::Default,
			&EmbeddedScheme::type);
		Assert(i != end(embedded));
		const auto colorizer = accent
			? ColorizerFrom(*i, *accent)
			: style::colorizer();

		auto instance = Instance();
		const auto loaded = LoadFromFile(
			(dark ? kNightBaseFile : kDayBaseFile).utf16(),
			&instance,
			nullptr,
			nullptr,
			colorizer);
		Assert(loaded);
		palette.finalize();
		palette = instance.palette;
	};
}

Fn<void(style::palette&)> PrepareCurrentPaletteCallback() {
	return [=, data = style::main_palette::save()](style::palette &palette) {
		palette.load(data);
	};
}

QByteArray AccentColors::serialize() const {
	auto result = QByteArray();
	if (_data.empty() && !_legacyDayColor) {
		return result;
	}

	const auto count = _data.size() + (_legacyDayColor ? 1 : 0);
	auto size = sizeof(qint32) * (count + 1)
		+ Serialize::colorSize() * count;
	result.reserve(size);

	auto stream = QDataStream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << qint32(count);
	if (_legacyDayColor) {
		stream << qint32(0);
		Serialize::writeColor(stream, *_legacyDayColor);
	}
	for (const auto &[type, color] : _data) {
		stream << static_cast<qint32>(type);
		Serialize::writeColor(stream, color);
	}
	stream.device()->close();

	return result;
}

bool AccentColors::setFromSerialized(const QByteArray &serialized) {
	if (serialized.isEmpty()) {
		_data.clear();
		_legacyDayColor = std::nullopt;
		return true;
	}
	auto copy = QByteArray(serialized);
	auto stream = QDataStream(&copy, QIODevice::ReadOnly);
	stream.setVersion(QDataStream::Qt_5_1);

	auto count = qint32();
	stream >> count;
	if (stream.status() != QDataStream::Ok) {
		return false;
	}
	if (count <= 0 || count > kMaxAccentColors) {
		return false;
	}
	auto data = base::flat_map<EmbeddedType, QColor>();
	auto legacyDayColor = std::optional<QColor>();
	for (auto i = 0; i != count; ++i) {
		auto type = qint32();
		stream >> type;
		const auto color = Serialize::readColor(stream);
		if (type == 0) {
			legacyDayColor = color;
			continue;
		}
		const auto uncheckedType = static_cast<EmbeddedType>(type);
		switch (uncheckedType) {
		case EmbeddedType::Default:
		case EmbeddedType::Night:
			data.emplace(uncheckedType, color);
			break;
		default:
			return false;
		}
	}
	if (stream.status() != QDataStream::Ok) {
		return false;
	}
	_data = std::move(data);
	_legacyDayColor = legacyDayColor;
	return true;
}

void AccentColors::migrateLegacyDayTheme() {
	if (!_legacyDayColor) {
		clear(EmbeddedType::Default);
		return;
	}
	// 旧主题路径可能在取消预览后再次读取，保留旧色以便重复迁移。
	set(EmbeddedType::Default, *_legacyDayColor);
}

void AccentColors::set(EmbeddedType type, const QColor &value) {
	_data.emplace_or_assign(type, value);
}

void AccentColors::clear(EmbeddedType type) {
	_data.remove(type);
}

std::optional<QColor> AccentColors::get(EmbeddedType type) const {
	const auto i = _data.find(type);
	return (i != end(_data)) ? std::make_optional(i->second) : std::nullopt;
}

} // namespace Theme
} // namespace Window
