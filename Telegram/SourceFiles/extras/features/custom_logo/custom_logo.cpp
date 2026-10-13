#include "extras/features/custom_logo/custom_logo.h"

#include "extras/extras_settings.h"
#include "ui/image/image_prepare.h"
#include "ui/rp_widget.h"
#include "styles/style_basic.h"

#include <QtCore/QBuffer>
#include <QtCore/QCryptographicHash>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QRegularExpression>
#include <QtGui/QImageReader>

namespace ExtrasFeatures::CustomLogo {
namespace {

constexpr auto kMaxFileSize = 10 * 1024 * 1024;
constexpr auto kReadSide = 1024;
constexpr auto kStoredSide = 256;

[[nodiscard]] QString storedPath(const QString &name) {
	return cWorkingDir() + u"tdata/"_q + name;
}

// 大图解码时直接缩到边长 kReadSide 以内，裁边后仍有足够分辨率。
[[nodiscard]] QImage readImage(const QString &path) {
	auto reader = QImageReader(path);
	reader.setAutoTransform(true);
	const auto size = reader.size();
	if (qint64(size.width()) * size.height() > Images::kReadMaxArea) {
		return QImage();
	}
	if (size.width() > kReadSide || size.height() > kReadSide) {
		reader.setScaledSize(
			size.scaled(kReadSide, kReadSide, Qt::KeepAspectRatio));
	}
	return reader.read();
}

// 不完全透明的像素所占的矩形；image 须为 ARGB32_Premultiplied。
[[nodiscard]] QRect opaqueBounds(const QImage &image) {
	auto result = QRect();
	for (auto y = 0; y != image.height(); ++y) {
		const auto line = reinterpret_cast<const QRgb*>(
			image.constScanLine(y));
		for (auto x = 0; x != image.width(); ++x) {
			if (qAlpha(line[x])) {
				result |= QRect(x, y, 1, 1);
			}
		}
	}
	return result;
}

// 裁掉四周完全透明的行列，留白不同的图片显示大小一致；全透明时为空。
[[nodiscard]] QImage trimTransparent(QImage image) {
	image = std::move(image).convertToFormat(
		QImage::Format_ARGB32_Premultiplied);
	const auto bounds = opaqueBounds(image);
	return bounds.isEmpty() ? QImage() : image.copy(bounds);
}

// 读取当前设置对应的图片，文件名不变时复用；文件缺失时为空，显示原图标。
[[nodiscard]] const QImage &current() {
	static auto name = QString();
	static auto image = QImage();
	const auto &wanted = ExtrasSettings::getInstance().customLogo();
	if (name != wanted) {
		name = wanted;
		image = name.isEmpty() ? QImage() : QImage(storedPath(name));
	}
	return image;
}

} // namespace

ImportError importFile(const QString &path) {
	if (QFileInfo(path).size() > kMaxFileSize) {
		return ImportError::TooLarge;
	}
	auto image = trimTransparent(readImage(path));
	if (image.isNull()) {
		return ImportError::Unreadable;
	}
	if (image.width() > kStoredSide || image.height() > kStoredSide) {
		image = image.scaled(
			kStoredSide,
			kStoredSide,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
	}
	auto bytes = QByteArray();
	auto buffer = QBuffer(&bytes);
	image.save(&buffer, "PNG");

	// 文件名取内容摘要，替换图片时设置值一定变化，界面随之刷新。
	const auto hash = QCryptographicHash::hash(
		bytes,
		QCryptographicHash::Sha1).toHex().left(16);
	const auto name = u"extras-logo-"_q + QString::fromLatin1(hash) + u".png"_q;
	auto file = QFile(storedPath(name));
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
		return ImportError::WriteFailed;
	}
	file.close();

	auto &settings = ExtrasSettings::getInstance();
	const auto previous = settings.customLogo();
	settings.setCustomLogo(name);
	if (!previous.isEmpty() && previous != name) {
		QFile::remove(storedPath(previous));
	}
	return ImportError::None;
}

void reset() {
	auto &settings = ExtrasSettings::getInstance();
	const auto previous = settings.customLogo();
	if (previous.isEmpty()) {
		return;
	}
	settings.setCustomLogo(QString());
	QFile::remove(storedPath(previous));
}

bool isStoredName(const QString &name) {
	static const auto pattern = QRegularExpression(
		u"^extras-logo-[0-9a-f]{16}\\.png$"_q);
	return pattern.match(name).hasMatch();
}

QSize displaySize(const style::icon &original) {
	const auto image = original.instance(Qt::white);
	const auto bounds = opaqueBounds(image);
	const auto side = int(std::ceil(
		std::max(bounds.width(), bounds.height())
			/ image.devicePixelRatio()));
	return QSize(side, side);
}

QImage prepare(QSize box) {
	const auto &source = current();
	if (source.isNull()) {
		return QImage();
	}
	const auto ratio = style::DevicePixelRatio();
	const auto scaled = source.scaled(
		box * ratio,
		Qt::KeepAspectRatio,
		Qt::SmoothTransformation);
	auto result = QImage(box * ratio, QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	{
		auto p = QPainter(&result);
		p.drawImage(
			(result.width() - scaled.width()) / 2,
			(result.height() - scaled.height()) / 2,
			scaled);
		if (ExtrasSettings::getInstance().customLogoTint()) {
			// 只保留透明度，按主题强调色重新上色。
			p.setCompositionMode(QPainter::CompositionMode_SourceIn);
			p.fillRect(result.rect(), st::windowBgActive);
		}
	}
	result.setDevicePixelRatio(ratio);
	return result;
}

void attach(
		not_null<Ui::RpWidget*> button,
		const style::icon &original,
		Fn<void(bool custom)> customChanged) {
	const auto box = displaySize(original);
	const auto overlay = Ui::CreateChild<Ui::RpWidget>(button.get());
	overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
	overlay->resize(box);
	button->sizeValue() | rpl::on_next([=](QSize size) {
		overlay->move(
			(size.width() - box.width()) / 2,
			(size.height() - box.height()) / 2);
	}, overlay->lifetime());

	const auto cache = overlay->lifetime().make_state<QImage>();
	const auto &settings = ExtrasSettings::getInstance();
	rpl::combine(
		settings.customLogoValue(),
		settings.customLogoTintValue(),
		rpl::single(rpl::empty) | rpl::then(style::PaletteChanged())
	) | rpl::to_empty | rpl::on_next([=] {
		*cache = prepare(box);
		const auto custom = !cache->isNull();
		customChanged(custom);
		overlay->setVisible(custom);
		overlay->update();
	}, overlay->lifetime());

	overlay->paintRequest() | rpl::on_next([=] {
		QPainter(overlay).drawImage(0, 0, *cache);
	}, overlay->lifetime());
}

} // namespace ExtrasFeatures::CustomLogo
