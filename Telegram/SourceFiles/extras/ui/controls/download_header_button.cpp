#include "extras/ui/controls/download_header_button.h"

#include "ui/painter.h"
#include "ui/controls/media_button.h"
#include "styles/style_extras_styles.h"
#include "styles/style_widgets.h"

#include <QtGui/QPainterPath>

namespace ExtrasUi {
namespace {

constexpr auto kDropPeriod = crl::time(1150);
constexpr auto kCheckDuration = crl::time(350);
constexpr auto kDoneDuration = crl::time(1600);

// 以下坐标都在 24 x 24 的图标网格内，与顶部下载图标的圆和箭头对齐。
constexpr auto kSize = 24.;
constexpr auto kRingRadius = 9.;
constexpr auto kInnerRadius = 8.;
constexpr auto kDropDistance = 11.;

// 一个周期内箭头的位移与透明度：从圆顶落到中间，再落出圆底。
[[nodiscard]] std::pair<float64, float64> dropState(float64 t) {
	const auto offset = (t < .55)
		? -kDropDistance * (1. - anim::easeOutCubic(1., t / .55))
		: kDropDistance * anim::easeInCubic(1., (t - .55) / .45);
	const auto opacity = (t < .22)
		? (t / .22)
		: (t > .78)
		? ((1. - t) / .22)
		: 1.;
	return { offset, opacity };
}

} // namespace

DownloadHeaderButton::DownloadHeaderButton(
	QWidget *parent,
	const style::IconButton &st)
: IconButton(parent, st)
, _loop([=] { update(); })
, _doneTimer([=] {
	_done = false;
	_active = false;
	update();
}) {
}

void DownloadHeaderButton::setLoading(int loading, bool finished) {
	if (_loading == loading) {
		return;
	}
	finished = finished && (_loading > 0) && !loading;
	_loading = loading;
	if (_loading) {
		_done = false;
		_doneTimer.cancel();
		_check.stop();
		if (!anim::Disabled() && !_loop.animating()) {
			_loopStarted = crl::now();
			_loop.start();
		}
	} else {
		_loop.stop();
		if (finished) {
			_done = true;
			_check.start([=] { update(); }, 0., 1., kCheckDuration);
			_doneTimer.callOnce(kDoneDuration);
		}
	}
	_active = (_loading > 0) || _done;
	update();
}

bool DownloadHeaderButton::active() const {
	return _active.current();
}

rpl::producer<bool> DownloadHeaderButton::activeValue() const {
	return _active.value();
}

void DownloadHeaderButton::paintEvent(QPaintEvent *e) {
	if (!_loading && !_done) {
		IconButton::paintEvent(e);
		return;
	}
	auto p = QPainter(this);
	paintIcon(p, crl::now());
}

void DownloadHeaderButton::paintIcon(QPainter &p, crl::time now) {
	auto hq = PainterHighQualityEnabler(p);
	const auto size = st().icon.width();
	const auto iconRect = QRect(
		(width() - size) / 2,
		(height() - size) / 2,
		size,
		size);
	const auto scale = size / kSize;
	const auto color = st::windowActiveTextFg->c;
	auto pen = QPen(color, 1.7);
	pen.setCapStyle(Qt::RoundCap);
	pen.setJoinStyle(Qt::RoundJoin);

	p.save();
	p.translate(iconRect.topLeft());
	p.scale(scale, scale);
	const auto center = QPointF(kSize / 2., kSize / 2.);
	Ui::PaintMediaButtonSurface(p, QRectF(
		center.x() - kRingRadius,
		center.y() - kRingRadius,
		2 * kRingRadius,
		2 * kRingRadius), Ui::MediaButtonPalette(color, st::windowBg->c));
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	if (_done) {
		// 对勾两段按总长度依次画出。
		const auto a = QPointF(8.2, 12.3);
		const auto b = QPointF(10.8, 14.9);
		const auto c = QPointF(15.8, 9.7);
		const auto first = QLineF(a, b).length();
		const auto second = QLineF(b, c).length();
		const auto drawn = _check.value(1.) * (first + second);
		p.drawLine(a, a + (b - a) * std::min(drawn / first, 1.));
		if (drawn > first) {
			p.drawLine(b, b + (c - b) * ((drawn - first) / second));
		}
	} else {
		const auto [offset, opacity] = _loop.animating()
			? dropState(((now - _loopStarted) % kDropPeriod)
				/ float64(kDropPeriod))
			: std::pair{ 0., 1. };
		auto clip = QPainterPath();
		clip.addEllipse(center, kInnerRadius, kInnerRadius);
		p.setClipPath(clip);
		p.setOpacity(opacity);
		p.translate(0., offset);
		p.drawLine(QPointF(12., 7.), QPointF(12., 16.2));
		const QPointF chevron[] = {
			{ 8.5, 12.7 },
			{ 12., 16.2 },
			{ 15.5, 12.7 },
		};
		p.drawPolyline(chevron, 3);
	}
	p.restore();

	if (_loading < 2) {
		return;
	}
	// 同时下载多个文件时在右上角显示数量。
	const auto &font = st::extrasDownloadBadgeFont;
	const auto text = QString::number(_loading);
	const auto height = st::extrasDownloadBadgeHeight;
	const auto badgeWidth = std::max(
		height,
		font->width(text) + 2 * st::extrasDownloadBadgePadding);
	const auto badge = QRect(
		std::min(
			iconRect.x() + iconRect.width() + st::extrasDownloadBadgeShift.x(),
			width()) - badgeWidth,
		iconRect.y() + st::extrasDownloadBadgeShift.y(),
		badgeWidth,
		height);
	p.setPen(Qt::NoPen);
	p.setBrush(st::activeButtonBg);
	p.drawRoundedRect(badge, height / 2., height / 2.);
	p.setPen(st::activeButtonFg);
	p.setFont(font);
	p.drawText(badge, Qt::AlignCenter, text);
}

} // namespace ExtrasUi
