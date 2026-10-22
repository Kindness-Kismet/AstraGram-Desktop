#include "extras/ui/extras_logo.h"

#include "ui/rect.h"

#include <QSvgRenderer>
#include <QtCore/QCryptographicHash>
#include <QtCore/QResource>
#include <QtCore/QSaveFile>

namespace ExtrasAssets {
namespace {

const auto kIcoResource = u":/gui/art/extras/default/app_icon.ico"_q;
const auto kSvgResource = u":/gui/art/extras/default/app.svg"_q;

} // namespace

QString appIcoPath() {
	static const auto fileName = [] {
		const auto resource = QResource(kIcoResource);
		const auto digest = QCryptographicHash::hash(
			resource.uncompressedData(),
			QCryptographicHash::Sha256).toHex().left(16);
		// 资源变化时更换缓存路径，避免系统复用旧位图；同一资源保持路径稳定。
		return u"extras-default-%1.ico"_q.arg(QString::fromLatin1(digest));
	}();
	return cWorkingDir() + u"tdata/"_q + fileName;
}

AppIcoStatus loadAppIco() {
	const auto iconPath = appIcoPath();
	auto source = QFile(kIcoResource);
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

QImage currentAppLogo() {
	static const auto result = [] {
		const auto size = Size(256);
		auto image = QImage(
			size * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(style::DevicePixelRatio());
		image.fill(Qt::transparent);
		auto p = QPainter(&image);
		auto path = QPainterPath();
		path.addEllipse(QRectF(QPointF(), QSizeF(size)));
		p.setRenderHint(QPainter::Antialiasing);
		p.setClipPath(path);
		QSvgRenderer(kSvgResource).render(&p, QRectF(QPointF(), QSizeF(size)));
		return image;
	}();
	return result;
}

} // namespace ExtrasAssets
