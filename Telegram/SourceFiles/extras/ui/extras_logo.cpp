#include "extras/ui/extras_logo.h"

#include "extras/extras_settings.h"
#include "styles/style_extras_styles.h"
#include "ui/rect.h"

#include <QSvgRenderer>
#include <QtCore/QCryptographicHash>
#include <QtCore/QHash>
#include <QtCore/QResource>
#include <QtCore/QSaveFile>

static QString LAST_LOADED_NAME;
static QImage LAST_LOADED;
static QImage LAST_LOADED_PAD;

namespace ExtrasAssets {

const QVector<QString> &appIcons() {
	static const auto result = QVector<QString>{
		DEFAULT_ICON,
		ALT_ICON,
		DISCORD_ICON,
		SPOTIFY_ICON,
		EXTERA_ICON,
		NOTHING_ICON,
		BARD_ICON,
		YAPLUS_ICON,
	};
	return result;
}

QString appIcoPath() {
	const auto iconName = ExtrasSettings::getInstance().appIcon();
	static auto fileNames = QHash<QString, QString>();
	auto i = fileNames.constFind(iconName);
	if (i == fileNames.cend()) {
		const auto resource = QResource(
			u":/gui/art/extras/%1/app_icon.ico"_q.arg(iconName));
		const auto digest = QCryptographicHash::hash(
			resource.uncompressedData(),
			QCryptographicHash::Sha256).toHex().left(16);
		// 资源变化时更换缓存路径，避免系统复用旧位图；同一资源保持路径稳定。
		i = fileNames.insert(iconName, u"extras-%1-%2.ico"_q.arg(
			iconName,
			QString::fromLatin1(digest)));
	}
	return cWorkingDir() + u"tdata/"_q + *i;
}

AppIcoStatus loadAppIco() {
	const auto &settings = ExtrasSettings::getInstance();
	const auto iconPath = appIcoPath();
	auto source = QFile(u":/gui/art/extras/%1/app_icon.ico"_q.arg(
		settings.appIcon()));
	if (!source.open(QIODevice::ReadOnly)) {
		LOG(("Icon Error: Could not open icon resource: %1").arg(source.errorString()));
		return AppIcoStatus::Failed;
	}
	const auto bytes = source.readAll();
	auto existing = QFile(iconPath);
	if (existing.exists()) {
		if (!existing.open(QIODevice::ReadOnly)) {
			LOG(("Icon Error: Could not read %1: %2")
				.arg(iconPath).arg(existing.errorString()));
			return AppIcoStatus::Failed;
		}
		const auto same = (existing.readAll() == bytes);
		if (existing.error() != QFileDevice::NoError) {
			LOG(("Icon Error: Could not read %1: %2")
				.arg(iconPath).arg(existing.errorString()));
			return AppIcoStatus::Failed;
		}
		existing.close();
		if (same) {
			return AppIcoStatus::Unchanged;
		}
	}

	// 原子替换，避免短暂移除系统正在引用的图标。
	auto output = QSaveFile(iconPath);
	if (!output.open(QIODevice::WriteOnly)
		|| output.write(bytes) != bytes.size()
		|| !output.commit()) {
		LOG(("Icon Error: Could not save %1: %2")
			.arg(iconPath).arg(output.errorString()));
		return AppIcoStatus::Failed;
	}
	DEBUG_LOG(("Icon: Updated file %1").arg(iconPath));
	return AppIcoStatus::Updated;
}

QImage CreateImage(const QString &name, const QSize resultImageSize, const int padding = 0) {
	const auto iconSize = resultImageSize.shrunkBy(QMargins(padding, padding, padding, padding));

	const auto pngPath = qsl(":/gui/art/extras/%1/app.png").arg(name);
	if (QFile::exists(pngPath)) {
		const auto loaded = QImage(pngPath).scaled(iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
		auto res = QImage(
			resultImageSize * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		res.setDevicePixelRatio(style::DevicePixelRatio());
		res.fill(Qt::transparent);
		{
			auto p = QPainter(&res);
			p.drawImage(QRect(padding, padding, iconSize.width(), iconSize.height()), loaded);
		}
		return res;
	}

	const auto svgPath = qsl(":/gui/art/extras/%1/app.svg").arg(name);
	if (!QFile::exists(svgPath)) {
		return {};
	}

	auto svg = QSvgRenderer(svgPath);
	auto image = QImage(
		resultImageSize * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(style::DevicePixelRatio());
	image.fill(Qt::transparent);
	{
		auto p = QPainter(&image);

		QPainterPath path;
		path.addRoundedRect(
			QRect(padding, padding, iconSize.width(), iconSize.height()),
			iconSize.width() / 2.0f,
			iconSize.height() / 2.0f
		);

		p.save();

		p.setRenderHint(QPainter::Antialiasing, true);
		p.setClipPath(path);
		p.setRenderHint(QPainter::Antialiasing, false);

		svg.render(&p, QRect(padding, padding, iconSize.width(), iconSize.height()));

		p.restore();
	}
	return image;
}

void loadIcons() {
	const auto &settings = ExtrasSettings::getInstance();
	if (LAST_LOADED_NAME != settings.appIcon()) {
		LAST_LOADED_NAME = settings.appIcon();
		LAST_LOADED = CreateImage(settings.appIcon(), Size(256));
		LAST_LOADED_PAD = CreateImage(settings.appIcon(), Size(256), 12);
	}
}

QImage loadPreview(const QString &name) {
	return CreateImage(name, Size(st::iconPickerIconSize), st::iconPickerImagePadding);
}

QString currentAppLogoName() {
	return LAST_LOADED_NAME;
}

QImage currentAppLogo() {
	loadIcons();
	return LAST_LOADED;
}

QImage currentAppLogoPad() {
	loadIcons();
	return LAST_LOADED_PAD;
}

}
