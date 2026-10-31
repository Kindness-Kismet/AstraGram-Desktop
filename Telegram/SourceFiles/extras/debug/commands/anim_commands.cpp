#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "ui/effects/animation_value.h"
#include "ui/controls/media_button.h"
#include "extras/ui/components/image_transition.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QTimer>
#include <QtGui/QPainter>
#include <QtWidgets/QWidget>

namespace ExtrasDebug::Commands {
namespace {

using json = nlohmann::json;

[[nodiscard]] QColor transitionPixel(
		ExtrasUi::ImageTransition &transition,
		const QImage &image) {
	auto result = QImage(image.size(), QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	{
		QPainter painter(&result);
		transition.paint(painter, result.rect(), image, image.cacheKey(), [] {});
	}
	return result.pixelColor(0, 0);
}

// 在真实动画驱动下验证同一图片的中途续帧，不改消息、设置或系统焦点。
[[nodiscard]] Result checkImageTransition(const QStringList &args) {
	if (!args.empty()) {
		return Result::Err(u"usage: anim.check-image-transition"_q);
	}
	if (anim::Disabled() || anim::DurationMultiplier() != 1.) {
		return Result::Err(u"normal-speed enabled animations are required"_q);
	}
	const auto window = controlWindow();
	if (!window) {
		return Result::Err(u"no selected window"_q);
	}
	const auto id = beginJob("image-transition-check");
	QObject::connect(window, &QObject::destroyed, [=] {
		finishJob(id, false, "window closed");
	});
	const auto previous = std::make_shared<ExtrasUi::ImageTransition>();
	const auto rebuilt = std::make_shared<ExtrasUi::ImageTransition>();
	auto red = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
	auto blue = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
	red.fill(Qt::red);
	blue.fill(Qt::blue);
	(void)transitionPixel(*previous, red);
	(void)transitionPixel(*previous, blue);
	QTimer::singleShot(100, window, [=] {
		const auto before = transitionPixel(*previous, blue);
		auto intermediate = ExtrasUi::ImageTransition();
		intermediate.continueFrom(*previous);
		rebuilt->continueFrom(intermediate);
		previous->reset();
		const auto continued = transitionPixel(*rebuilt, blue);
		const auto smooth = before.red() > 0 && before.blue() > 0
			&& std::abs(before.red() - continued.red()) <= 2
			&& std::abs(before.blue() - continued.blue()) <= 2
			&& continued.alpha() == 255;
		QTimer::singleShot(350, window, [=] {
			const auto final = transitionPixel(*rebuilt, blue);
			const auto rgba = [](QColor color) {
				return json::array({ color.red(), color.green(), color.blue(), color.alpha() });
			};
			finishJob(id, smooth && final == QColor(Qt::blue), json{
				{ "rebuilds", 2 },
				{ "before", rgba(before) },
				{ "continued", rgba(continued) },
				{ "final", rgba(final) },
			});
		});
	});
	return jobStarted(id);
}

// 直接采样正式进度控件的瞬间完成，不改账号下载或文件缓存。
[[nodiscard]] Result sampleMediaProgress(const QStringList &args) {
	if (args.size() != 1) {
		return Result::Err(u"usage: anim.sample-media-progress <newPngPath>"_q);
	}
	const auto path = QFileInfo(args.front());
	if (!path.isAbsolute() || path.exists() || !path.dir().exists()) {
		return Result::Err(u"destination must be a new absolute path in an existing directory"_q);
	}
	if (anim::Disabled() || anim::DurationMultiplier() != 1.) {
		return Result::Err(u"normal-speed enabled animations are required"_q);
	}
	const auto window = controlWindow();
	if (!window) {
		return Result::Err(u"no selected window"_q);
	}
	struct Probe final : QObject {
		Probe(QWidget *parent, QString destination)
		: QObject(parent)
		, path(std::move(destination))
		, animation([=](crl::time now) {
			animation.update(1., true, now, true);
		}) {
			sheet.fill(Qt::white);
		}
		~Probe() {
			finishJob(id, false, "sampling ended before completion");
		}
		void capture(int index) {
			auto painter = QPainter(&sheet);
			const auto x = index * 100;
			animation.paint(painter, QRectF(x + 20, 10, 60, 60),
				Ui::MediaButtonPalette(QColor(55, 125, 75), Qt::white), false);
			painter.setPen(Qt::black);
			painter.drawText(QRect(x, 80, 100, 30), Qt::AlignCenter,
				QString::number(crl::now() - started) + u" ms"_q);
		}
		const uint64 id = beginJob("media-progress-sample");
		const crl::time started = crl::now();
		const QString path;
		QImage sheet = QImage(900, 115, QImage::Format_ARGB32_Premultiplied);
		Ui::MediaProgressAnimation animation;
		int captured = 1;
	};
	const auto probe = new Probe(window, path.absoluteFilePath());
	probe->animation.start(0.);
	probe->capture(0);
	probe->animation.update(1., true, crl::now(), true);
	const auto times = std::array{ 80, 160, 240, 400, 520, 720, 1000, 1400 };
	for (auto i = 0; i != times.size(); ++i) {
		QTimer::singleShot(times[i], Qt::PreciseTimer, probe, [=] {
			probe->capture(i + 1);
			if (++probe->captured != times.size() + 1) {
				return;
			}
			const auto saved = probe->sheet.save(probe->path, "PNG");
			finishJob(probe->id, saved, json{
				{ "path", probe->path.toStdString() },
				{ "frames", 9 },
				{ "elapsedMs", crl::now() - probe->started },
			});
			probe->deleteLater();
		});
	}
	return jobStarted(probe->id);
}

// 下限避免界面长时间停在动画中途，上限避免过渡短到只剩一两帧。
constexpr auto kMinSpeed = 0.01;
constexpr auto kMaxSpeed = 10.;

[[nodiscard]] json animState() {
	const auto multiplier = anim::DurationMultiplier();
	return json{
		{ "speed", 1. / multiplier },
		{ "durationMultiplier", multiplier },
		{ "disabled", anim::Disabled() },
	};
}

// 速度是时长倍率的倒数，只作用于之后启动的过渡动画；只存内存，重启恢复 1。
[[nodiscard]] Result animSpeed(const QStringList &args) {
	if (args.size() > 1) {
		return Result::Err(u"usage: anim.speed [speed]"_q);
	}
	if (args.isEmpty()) {
		return Result::Ok(Compact(animState()));
	}
	auto ok = false;
	const auto speed = args.front().toDouble(&ok);
	// 写成区间内判断，NaN 也会被拒绝。
	if (!ok || !(speed >= kMinSpeed && speed <= kMaxSpeed)) {
		return Result::Err(u"speed must be a number from %1 to %2"_q
			.arg(kMinSpeed)
			.arg(kMaxSpeed));
	}
	anim::SetDurationMultiplier(1. / speed);
	return Result::Ok(Compact(animState()));
}

} // namespace

const HandlerMap &animHandlers() {
	static const auto result = HandlerMap{
		{ u"anim.check-image-transition"_q, &checkImageTransition },
		{ u"anim.sample-media-progress"_q, &sampleMediaProgress },
		{ u"anim.speed"_q, &animSpeed },
	};
	return result;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
