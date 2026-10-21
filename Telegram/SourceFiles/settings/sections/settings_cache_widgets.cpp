#include "settings/sections/settings_cache_widgets.h"

#include "settings.h"
#include "lang/lang_keys.h"
#include "statistics/view/stack_linear_chart_common.h"
#include "ui/text/format_values.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/text_utilities.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"
#include "styles/style_background_preview_box.h"
#include "styles/style_boxes.h"
#include "styles/style_settings_local_storage.h"

#include <QtCore/QStorageInfo>

namespace Settings {

QColor CachePartColor(int index) {
	switch (index) {
	case 0: return st::statisticsChartLineLightblue->c;
	case 1: return st::statisticsChartLineOrange->c;
	case 2: return st::statisticsChartLineLightgreen->c;
	case 3: return st::statisticsChartLinePurple->c;
	case 4: return st::statisticsChartLineGreen->c;
	case 5: return st::statisticsChartLineBlue->c;
	}
	Unexpected("Invalid cache category.");
}

QString CachePartName(int index) {
	switch (Extras::MediaCacheCategory(index)) {
	case Extras::MediaCacheCategory::Images: return tr::extras_CacheImages(tr::now);
	case Extras::MediaCacheCategory::Stickers: return tr::extras_CacheStickers(tr::now);
	case Extras::MediaCacheCategory::Documents: return tr::extras_CacheDocuments(tr::now);
	case Extras::MediaCacheCategory::Avatars: return tr::extras_CacheAvatars(tr::now);
	case Extras::MediaCacheCategory::Other: return tr::extras_CacheOther(tr::now);
	case Extras::MediaCacheCategory::Messages: return tr::extras_CacheMessages(tr::now);
	case Extras::MediaCacheCategory::Count: break;
	}
	Unexpected("Invalid cache category.");
}

QString CacheCategoryId(int index) {
	const auto names = std::array{
		u"images"_q, u"stickers"_q, u"documents"_q,
		u"avatars"_q, u"other"_q, u"messages"_q,
	};
	return names[index];
}

namespace {

constexpr auto kMegabyte = int64(1024 * 1024);
constexpr auto kCompleteParticlesCount = 25;

constexpr auto kChartSeparator = 2.;
constexpr auto kChartMinFraction = 0.02;
constexpr auto kChartMorphDuration = crl::time(650);
constexpr auto kChartCompleteDuration = crl::time(650);
constexpr auto kChartAppearDuration = crl::time(750);
constexpr auto kChartSelectDuration = crl::time(200);
constexpr auto kChartPercentFadeDuration = crl::time(200);

const style::icon &ParticleIcon(int index) {
	switch (index) {
	case 0: return st::localStorageParticlePhotos;
	case 1: return st::localStorageParticleStickers;
	case 2: return st::localStorageParticleDocuments;
	case 3: return st::localStorageParticlePhotos;
	case 4: return st::localStorageParticleVideos;
	case 5: return st::localStorageParticleDocuments;
	}
	Unexpected("Invalid cache particle index.");
}

const QImage &ParticleImage(int index) {
	static auto images = std::array<QImage, kChartPartsCount>();
	auto &image = images[index];
	if (image.isNull()) {
		image = ParticleIcon(index).instance(Qt::white);
	}
	return image;
}

[[nodiscard]] QString FormatStoragePercent(int64 part, int64 whole) {
	if (whole <= 0) {
		return u"0%"_q;
	}
	const auto ratio = part / float64(whole);
	if (ratio < 0.001) {
		return u"<0.1%"_q;
	}
	const auto percent = int(base::SafeRound(ratio * 100.));
	return (percent <= 0)
		? u"<1%"_q
		: (QString::number(percent) + '%');
}

[[nodiscard]] QString FormatStorageSize(int64 size) {
	constexpr auto kGigabyte = int64(1024) * 1024 * 1024;
	if (size < kGigabyte) {
		return Ui::FormatSizeText(size);
	}
	const auto tenthGb = size * 10 / kGigabyte;
	return QString::number(tenthGb / 10)
		+ '.'
		+ QString::number(tenthGb % 10)
		+ u" GB"_q;
}

} // namespace

CompleteParticles::CompleteParticles()
: _particles([] {
	const int sizes[] = {
		st::localStorageCompleteStar1,
		st::localStorageCompleteStar2,
		st::localStorageCompleteStar3,
	};
	auto sprites = std::vector<Ui::DriftingSprite>();
	sprites.reserve(std::size(sizes));
	for (const auto size : sizes) {
		sprites.push_back({
			.image = Ui::FourPointStarImage({ .size = size }),
		});
	}
	return Ui::DriftingParticles::Config{
		.sprites = std::move(sprites),
		.count = kCompleteParticlesCount,
		.speed = float64(st::localStorageCompleteSpeed),
		.excludeRadius = float64(st::localStorageCompleteExclude),
		.orbit = true,
		.checkBounds = true,
	};
}()) {
}

void CompleteParticles::paint(
		QPainter &p,
		QRect rect,
		QPointF center,
		float64 alpha,
		bool paused) {
	if (alpha <= 0.) {
		return;
	}
	const auto field = float64(st::localStorageCompleteField);
	_particles.setGeometry(
		Rect(center.x() - field / 2., center.y() - field / 2., Size(field)),
		rect,
		QRectF());
	_particles.setPaused(paused);

	const auto ratio = style::DevicePixelRatio();
	const auto pixelSize = rect.size() * ratio;
	if (_layer.size() != pixelSize) {
		_layer = QImage(pixelSize, QImage::Format_ARGB32_Premultiplied);
		_layer.setDevicePixelRatio(ratio);
	}
	_layer.fill(Qt::transparent);

	auto lp = QPainter(&_layer);
	lp.translate(-rect.x(), -rect.y());
	_particles.paint(lp);
	lp.resetTransform();
	lp.setOpacity(1.);
	lp.setCompositionMode(QPainter::CompositionMode_SourceIn);
	auto gradient = QLinearGradient(
		QPointF(0, 0),
		QPointF(0, rect.height()));
	gradient.setColorAt(0., QColor(0x6e, 0xd5, 0x56, 0));
	gradient.setColorAt(0.07, QColor(0x6e, 0xd5, 0x56));
	gradient.setColorAt(0.93, QColor(0x41, 0xba, 0x71));
	gradient.setColorAt(1., QColor(0x41, 0xba, 0x71, 0));
	lp.fillRect(Rect(rect.size()), gradient);
	lp.end();

	p.setOpacity(alpha);
	p.drawImage(rect.topLeft(), _layer);
	p.setOpacity(1.);
}

CacheChart::CacheChart(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller) {
	setMouseTracking(true);

	_particlesStart = crl::now();
	_particles.init([=](crl::time) {
		update();
		return true;
	});
	const auto refresh = [=] {
		if (_shown && !paused()) {
			if (_particlesPausedAt) {
				_particlesStart += crl::now() - _particlesPausedAt;
				_particlesPausedAt = 0;
			}
			if (!_particles.animating()) {
				_particles.start();
			}
		} else {
			if (!_particlesPausedAt) {
				_particlesPausedAt = crl::now();
			}
			_particles.stop();
		}
	};
	shownValue() | rpl::on_next([=](bool shown) {
		_shown = shown;
		refresh();
	}, lifetime());
	_controller->gifPauseLevelChanged(
	) | rpl::on_next(refresh, lifetime());
}

bool CacheChart::paused() const {
	return anim::Disabled()
		|| _controller->isGifPausedAtLeastFor(Window::GifPauseReason::Layer);
}

void CacheChart::drawParticles(
		QPainter &p,
		QPointF center,
		int index,
		float64 from,
		float64 to,
		float64 innerRadius,
		float64 outerRadius,
		float64 alpha) {
	if (alpha <= 0. || anim::Disabled()) {
		return;
	}
	const auto &image = ParticleImage(index);
	const auto size = float64(st::localStorageChartParticle);
	const auto sqrt2 = std::sqrt(2.);
	const auto reference = _particlesPausedAt ? _particlesPausedAt : crl::now();
	const auto time = (reference - _particlesStart) / 10000.;
	const auto step = 7.;
	const auto fromStep = int(std::floor(from / step));
	const auto toStep = int(std::ceil(to / step));
	const auto rFrom = innerRadius - size * sqrt2;
	const auto rTo = outerRadius + size * sqrt2;
	for (auto k = fromStep; k <= toStep; ++k) {
		const auto angle = k * step;
		const auto t = std::fmod(
			(time + 100.) * (1. + (std::sin(angle * 2000.) + 1.) * 0.25),
			1.);
		const auto radius = rFrom + (rTo - rFrom) * t;
		const auto point = radialPoint(center, radius, angle);
		const auto distance = std::hypot(
			point.x() - center.x(),
			point.y() - center.y());
		const auto centerFade = std::min(distance / 64., 1.);
		auto particleAlpha = 0.45
			* alpha
			* (-1.75 * std::abs(t - 0.5) + 1.)
			* (0.25 * (std::sin(t * M_PI) - 1.) + 1.)
			* centerFade;
		particleAlpha = std::clamp(particleAlpha, 0., 1.);
		if (particleAlpha <= 0.) {
			continue;
		}
		const auto scale = 0.75
			* (0.25 * (std::sin(t * M_PI) - 1.) + 1.)
			* (0.8 + (std::sin(angle) + 1.) * 0.25);
		const auto side = size * scale;
		p.setOpacity(particleAlpha);
		p.drawImage(
			Rect(point.x() - side / 2., point.y() - side / 2., Size(side)),
			image);
	}
}

void CacheChart::setLoaded() {
	_loaded = true;
}

void CacheChart::setParts(
		std::array<int64, kChartPartsCount> sizes,
		int64 total) {
	const auto complete = _loaded && (total <= 0);
	if (_complete != complete) {
		_complete = complete;
		if (_appeared) {
			_completeAnimation.start(
				[=] { update(); },
				complete ? 0. : 1.,
				complete ? 1. : 0.,
				kChartCompleteDuration,
				anim::easeOutQuint);
		} else {
			_completeAnimation.stop();
		}
	}
	if (_sizes == sizes && _total == total) {
		update();
		return;
	}
	hideTooltip();
	_from = shown();
	_sizes = sizes;
	_total = total;
	computeTargets();

	for (auto i = 0; i != kChartPartsCount; ++i) {
		const auto target = _showPercent[i] ? 1. : 0.;
		if (_percentTarget[i] != target) {
			const auto from = _percentAlpha[i].value(_percentTarget[i]);
			_percentTarget[i] = target;
			_percentAlpha[i].start(
				[=] { update(); },
				from,
				target,
				kChartPercentFadeDuration);
		}
	}

	_morph.stop();
	_morph.start(
		[=] { update(); },
		0.,
		1.,
		kChartMorphDuration,
		anim::easeOutQuint);
	if (!_appeared && total > 0) {
		_appeared = true;
		_appear.start(
			[=] { update(); },
			0.,
			1.,
			kChartAppearDuration,
			anim::easeOutQuint);
	}
	update();
}

void CacheChart::computeTargets() {
	auto sum = int64();
	auto count = 0;
	for (auto i = 0; i != kChartPartsCount; ++i) {
		if (_sizes[i] > 0) {
			sum += _sizes[i];
			++count;
		}
	}
	_to = std::array<Geometry, kChartPartsCount>();
	_percent = { { 0 } };
	_showPercent = { { false } };
	if (sum <= 0) {
		for (auto i = 0; i != kChartPartsCount; ++i) {
			_to[i].center = _from[i].center;
		}
		return;
	}

	auto under = 0;
	auto minus = 0.;
	for (auto i = 0; i != kChartPartsCount; ++i) {
		const auto fraction = _sizes[i] / float64(sum);
		if (fraction > 0. && fraction < kChartMinFraction) {
			++under;
			minus += fraction;
		}
	}

	auto values = std::vector<float64>(kChartPartsCount);
	for (auto i = 0; i != kChartPartsCount; ++i) {
		values[i] = float64(_sizes[i]);
	}
	const auto percentage = Statistic::PiePartsPercentage(
		values,
		float64(sum),
		true);

	const auto separators = (count >= 2) ? count : 0;
	const auto sweep = 360. - kChartSeparator * separators;
	auto start = 0.;
	auto drawn = 0;
	for (auto i = 0; i != kChartPartsCount; ++i) {
		const auto from = start + drawn * kChartSeparator;
		const auto fraction = _sizes[i] / float64(sum);
		if (fraction <= 0.) {
			_to[i].center = from;
			_to[i].half = 0.;
			continue;
		}
		_percent[i] = int(base::SafeRound(
			percentage.parts[i].roundedPercentage * 100.));
		_showPercent[i] = (fraction > 0.05) && (fraction < 1.);
		auto adjusted = fraction;
		if (adjusted < kChartMinFraction) {
			adjusted = kChartMinFraction;
		} else {
			adjusted *= 1. - (kChartMinFraction * under - minus);
		}
		const auto to = from + adjusted * sweep;
		_to[i].center = (from + to) / 2.;
		_to[i].half = (to - from) / 2.;
		start += adjusted * sweep;
		++drawn;
	}
}

auto CacheChart::shown() const
-> std::array<Geometry, kChartPartsCount> {
	const auto progress = _morph.value(1.);
	auto result = std::array<Geometry, kChartPartsCount>();
	for (auto i = 0; i != kChartPartsCount; ++i) {
		result[i].center = _from[i].center
			+ (_to[i].center - _from[i].center) * progress;
		result[i].half = _from[i].half
			+ (_to[i].half - _from[i].half) * progress;
	}
	return result;
}

QPointF CacheChart::radialPoint(
		QPointF center,
		float64 radius,
		float64 angle) const {
	const auto radians = angle * M_PI / 180.;
	return QPointF(
		center.x() + radius * std::cos(radians),
		center.y() + radius * std::sin(radians));
}

void CacheChart::setSelected(int index) {
	if (_selected == index) {
		return;
	}
	const auto previous = _selected;
	_selected = index;
	const auto animate = [&](int i, float64 to) {
		_selectAnimations[i].start(
			[=] { update(); },
			1. - to,
			to,
			kChartSelectDuration,
			anim::easeOutQuint);
	};
	if (previous >= 0) {
		animate(previous, 0.);
	}
	if (index >= 0) {
		animate(index, 1.);
	}
	setCursor(index >= 0 ? style::cur_pointer : style::cur_default);
	update();
}

int CacheChart::resizeGetHeight(int newWidth) {
	return st::localStorageChartHeight;
}

int CacheChart::segmentAt(QPointF pos) const {
	const auto center = QPointF(width() / 2., height() / 2.);
	const auto outer = st::localStorageChartDiameter / 2.;
	const auto thickness = st::localStorageChartThickness;
	const auto inner = outer - thickness;
	const auto dx = pos.x() - center.x();
	const auto dy = pos.y() - center.y();
	const auto distance = std::hypot(dx, dy);
	if (distance < inner - st::localStorageChartSelectGrow
		|| distance > outer + st::localStorageChartSelectGrow) {
		return -1;
	}
	auto angle = std::atan2(dy, dx) * 180. / M_PI;
	if (angle < 0.) {
		angle += 360.;
	}
	const auto geometry = shown();
	for (auto i = 0; i != kChartPartsCount; ++i) {
		if (geometry[i].half <= 0.) {
			continue;
		}
		auto delta = std::fmod(std::abs(angle - geometry[i].center), 360.);
		if (delta > 180.) {
			delta = 360. - delta;
		}
		if (delta <= geometry[i].half) {
			return i;
		}
	}
	return -1;
}

void CacheChart::mouseMoveEvent(QMouseEvent *e) {
	setSelected(segmentAt(e->pos()));
}

void CacheChart::mousePressEvent(QMouseEvent *e) {
	const auto index = segmentAt(e->pos());
	if (index < 0 || index == _tooltipPart) {
		hideTooltip();
	} else {
		showTooltip(index);
	}
}

QPointF CacheChart::partCenter(int index) const {
	const auto center = QPointF(width() / 2., height() / 2.);
	const auto outer = st::localStorageChartDiameter / 2.;
	const auto thickness = float64(st::localStorageChartThickness);
	const auto midRadius = outer - thickness / 2.;
	const auto geometry = shown();
	return radialPoint(center, midRadius, geometry[index].center);
}

void CacheChart::setPartInfo(Fn<TextWithEntities(int)> info) {
	_partInfo = std::move(info);
}

void CacheChart::showTooltip(int index) {
	hideTooltip();
	const auto parent = window();
	if (!_partInfo || index < 0) {
		return;
	}
	_tooltipPart = index;
	_tooltip = std::make_unique<Ui::ImportantTooltip>(
		parent,
		Ui::MakeNiceTooltipLabel(
			parent,
			rpl::single(_partInfo(index)),
			st::localStorageUsageTooltipMaxWidth,
			st::defaultImportantTooltipLabel),
		st::defaultImportantTooltip);
	const auto tooltip = _tooltip.get();
	const auto weak = base::make_weak(tooltip);
	tooltip->setAttribute(Qt::WA_TransparentForMouseEvents);
	tooltip->setHiddenCallback([=] { delete weak.get(); });
	const auto local = partCenter(index);
	const auto upper = (local.y() < height() / 2.);
	auto area = QRect(local.toPoint(), QSize());
	area.translate(0, upper
		? -st::localStorageUsageTooltipSkip
		: st::localStorageUsageTooltipSkip);
	area = Ui::MapFrom(parent, this, area);
	tooltip->pointAt(area, upper ? RectPart::Top : RectPart::Bottom);
	tooltip->toggleAnimated(true);
}

void CacheChart::hideTooltip() {
	_tooltipPart = -1;
	if (const auto tooltip = _tooltip.release()) {
		tooltip->toggleAnimated(false);
	}
}

void CacheChart::leaveEventHook(QEvent *e) {
	setSelected(-1);
}

void CacheChart::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);

	const auto appear = (_total <= 0)
		? 1.
		: (_appeared ? _appear.value(1.) : 0.);
	const auto complete = _completeAnimation.value(_complete ? 1. : 0.);
	const auto segmentAppear = appear * (1. - complete);
	const auto center = QPointF(width() / 2., height() / 2.);
	const auto outer = st::localStorageChartDiameter / 2.;
	const auto thin = float64(st::localStorageChartThicknessThin);
	const auto full = float64(st::localStorageChartThickness);
	const auto thickness = thin + (full - thin) * segmentAppear;
	const auto rotation = (1. - appear) * -120.;
	const auto midRadius = outer - thickness / 2.;

	p.setOpacity(appear);

	auto background = QPen(st::windowBgOver->c);
	background.setWidthF(thickness);
	background.setCapStyle(Qt::FlatCap);
	p.setPen(background);
	p.setBrush(Qt::NoBrush);
	p.drawEllipse(center, midRadius, midRadius);

	p.setOpacity(segmentAppear);
	const auto geometry = shown();
	for (auto i = 0; i != kChartPartsCount; ++i) {
		if (geometry[i].half <= 0.1) {
			continue;
		}
		const auto selected = _selectAnimations[i].value(
			(_selected == i) ? 1. : 0.);
		const auto grow = selected * st::localStorageChartSelectGrow;
		const auto segmentThickness = thickness + grow;
		const auto segmentMid = midRadius + grow / 2.;
		const auto segmentOuter = outer + grow;
		const auto base = CachePartColor(i);

		auto gradient = QRadialGradient(center, segmentOuter);
		gradient.setColorAt(
			0.3,
			anim::color(base, QColor(255, 255, 255), 0.30));
		gradient.setColorAt(1., base);

		auto pen = QPen(QBrush(gradient), segmentThickness);
		pen.setCapStyle(Qt::FlatCap);
		p.setPen(pen);

		const auto from = geometry[i].center - geometry[i].half + rotation;
		const auto to = geometry[i].center + geometry[i].half + rotation;
		const auto rect = Rect(
			center.x() - segmentMid,
			center.y() - segmentMid,
			Size(segmentMid * 2.));
		const auto startAngle = int(base::SafeRound(-from * 16.));
		const auto spanAngle = -int(base::SafeRound((to - from) * 16.));
		p.drawArc(rect, startAngle, spanAngle);

		const auto innerRadius = segmentOuter - segmentThickness;
		const auto outerRect = Rect(
			center.x() - segmentOuter,
			center.y() - segmentOuter,
			Size(segmentOuter * 2.));
		const auto innerRect = Rect(
			center.x() - innerRadius,
			center.y() - innerRadius,
			Size(innerRadius * 2.));
		auto sector = QPainterPath();
		sector.arcMoveTo(outerRect, startAngle / 16.);
		sector.arcTo(outerRect, startAngle / 16., spanAngle / 16.);
		sector.arcTo(
			innerRect,
			(startAngle + spanAngle) / 16.,
			-spanAngle / 16.);
		sector.closeSubpath();
		p.save();
		p.setClipPath(sector);
		drawParticles(
			p,
			center,
			i,
			from,
			to,
			innerRadius,
			segmentOuter,
			segmentAppear);
		p.restore();
		p.setOpacity(segmentAppear);
	}

	p.setFont(st::localStorageChartPercentFont);
	const auto percentFont = st::localStorageChartPercentFont;
	for (auto i = 0; i != kChartPartsCount; ++i) {
		const auto labelAlpha = _percentAlpha[i].value(_percentTarget[i]);
		if (labelAlpha <= 0. || geometry[i].half <= 0.1) {
			continue;
		}
		const auto selected = _selectAnimations[i].value(
			(_selected == i) ? 1. : 0.);
		const auto point = radialPoint(
			center,
			midRadius + selected * st::localStorageChartSelectGrow / 2.,
			geometry[i].center + rotation);
		const auto text = QString::number(_percent[i]) + '%';
		const auto twidth = percentFont->width(text);
		p.setOpacity(appear * labelAlpha);
		p.setPen(QColor(255, 255, 255));
		p.save();
		p.translate(point);
		const auto scale = 1. + selected * 0.1;
		p.scale(scale, scale);
		p.drawText(
			QRectF(
				-twidth,
				-percentFont->height,
				twidth * 2.,
				percentFont->height * 2.),
			text,
			QTextOption(Qt::AlignCenter));
		p.restore();
	}
	p.setOpacity(1.);

	const auto gigabytes = (_total >= 1024 * kMegabyte);
	const auto amount = gigabytes
		? QString::number(_total / float64(1024 * kMegabyte), 'f', 1)
		: QString::number((_total + kMegabyte - 1) / kMegabyte);
	const auto unit = gigabytes ? u"GB"_q : u"MB"_q;
	const auto sizeFont = st::localStorageChartSizeFont;
	const auto unitFont = st::localStorageChartUnitFont;
	const auto blockHeight = sizeFont->height + unitFont->height;
	const auto top = (st::localStorageChartHeight - blockHeight) / 2;

	p.setOpacity(segmentAppear);
	p.save();
	const auto scale = 0.6 + 0.4 * appear;
	p.translate(center);
	p.scale(scale, scale);
	p.translate(-center);
	p.setFont(sizeFont);
	p.setPen(st::windowBoldFg);
	p.drawText(
		QRect(0, top, width(), sizeFont->height),
		amount,
		style::al_top);
	p.setFont(unitFont);
	p.setPen(st::windowSubTextFg);
	p.drawText(
		QRect(0, top + sizeFont->height, width(), unitFont->height),
		unit,
		style::al_top);
	p.restore();

	if (complete > 0.) {
		_completeParticles.paint(p, rect(), center, complete, paused());

		auto gradient = QLinearGradient(
			QPointF(center.x(), center.y() - outer),
			QPointF(center.x(), center.y() + outer));
		gradient.setColorAt(0., QColor(0x6e, 0xd5, 0x56));
		gradient.setColorAt(1., QColor(0x41, 0xba, 0x71));
		const auto brush = QBrush(gradient);
		p.setOpacity(complete);
		p.setBrush(Qt::NoBrush);

		auto ring = QPen(brush, thickness);
		ring.setCapStyle(Qt::FlatCap);
		p.setPen(ring);
		p.drawEllipse(center, midRadius, midRadius);

		const auto boxLeft = center.x() - outer;
		const auto boxTop = center.y() - outer;
		const auto diameter = outer * 2.;
		auto check = QPainterPath();
		check.moveTo(boxLeft + diameter * 0.348, boxTop + diameter * 0.538);
		check.lineTo(boxLeft + diameter * 0.447, boxTop + diameter * 0.636);
		check.lineTo(boxLeft + diameter * 0.678, boxTop + diameter * 0.402);
		auto checkPen = QPen(brush, thin);
		checkPen.setCapStyle(Qt::RoundCap);
		checkPen.setJoinStyle(Qt::RoundJoin);
		p.setPen(checkPen);
		p.drawPath(check);
	}
	p.setOpacity(1.);
}

CacheDeviceBar::CacheDeviceBar(QWidget *parent)
: RpWidget(parent)
, _subtitle(this, QString(), st::localStorageUsageSubtitle) {
	_subtitle->setAttribute(Qt::WA_TransparentForMouseEvents);
	setMouseTracking(true);
	refresh();
}

void CacheDeviceBar::refresh() {
	const auto weak = base::make_weak(this);
	const auto dir = cWorkingDir();
	crl::async([=] {
		const auto info = QStorageInfo(dir);
		const auto total = info.isValid() ? info.bytesTotal() : int64();
		const auto free = info.isValid() ? info.bytesAvailable() : int64();
		crl::on_main(weak, [=] {
			_total = total;
			_free = (free > total) ? total : free;
			apply();
		});
	});
}

void CacheDeviceBar::apply() {
	const auto used = std::max(_total - _free, int64());
	_cache = std::min(_reported, used);
	_subtitle->setText(tr::lng_local_storage_device_usage(
		tr::now,
		lt_percent,
		FormatStoragePercent(_cache, _total)));
	resizeToWidth(width());
	update();
}

void CacheDeviceBar::setCache(int64 cache) {
	_reported = cache;
	apply();
	refresh();
}

QRect CacheDeviceBar::barRect() const {
	const auto width = std::min(st::localStorageUsageBarWidth, QWidget::width());
	const auto top = st::localStorageUsageTopSkip
		+ _subtitle->height()
		+ st::localStorageUsageBarSkip;
	return QRect(
		(QWidget::width() - width) / 2,
		top,
		width,
		st::localStorageUsageBarHeight);
}

int CacheDeviceBar::resizeGetHeight(int newWidth) {
	_subtitle->resizeToWidth(newWidth);
	_subtitle->moveToLeft(0, st::localStorageUsageTopSkip, newWidth);
	return st::localStorageUsageTopSkip
		+ _subtitle->height()
		+ st::localStorageUsageBarSkip
		+ st::localStorageUsageBarHeight
		+ st::localStorageUsageBottomSkip;
}

void CacheDeviceBar::paintEvent(QPaintEvent *e) {
	if (_total <= 0) {
		return;
	}
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);

	const auto bar = QRectF(barRect());
	const auto radius = bar.height() / 2.;
	const auto used = _total - _free;
	const auto cacheColor = st::activeButtonBg->c;
	const auto usedColor = st::scrollBg->c;
	const auto freeColor = st::windowBgRipple->c;

	auto path = QPainterPath();
	path.addRoundedRect(bar, radius, radius);
	p.setClipPath(path);
	p.fillRect(bar, freeColor);

	const auto fill = [&](int64 from, int64 size, QColor color) {
		if (size <= 0) {
			return;
		}
		const auto x1 = bar.x() + bar.width() * (from / float64(_total));
		const auto x2 = bar.x()
			+ bar.width() * ((from + size) / float64(_total));
		p.fillRect(QRectF(x1, bar.y(), x2 - x1, bar.height()), color);
	};
	fill(0, used, usedColor);
	fill(0, _cache, cacheColor);
}

void CacheDeviceBar::mouseMoveEvent(QMouseEvent *e) {
	if (_total > 0 && barRect().marginsAdded(st::localStorageUsageBarMargin)
			.contains(e->pos())) {
		showTooltip();
	} else {
		hideTooltip();
	}
}

void CacheDeviceBar::leaveEventHook(QEvent *e) {
	hideTooltip();
}

TextWithEntities CacheDeviceBar::tooltipText() const {
	const auto used = _total - _free;
	const auto other = std::max(used - _cache, int64());
	auto result = TextWithEntities();
	const auto line = [&](const QString &label, int64 size) {
		if (!result.text.isEmpty()) {
			result.append('\n');
		}
		result.append(label).append(u": "_q).append(
			Ui::Text::Bold(FormatStorageSize(size)));
	};
	line(tr::lng_local_storage_device_telegram(tr::now), _cache);
	line(tr::lng_local_storage_device_other(tr::now), other);
	line(tr::lng_local_storage_device_free(tr::now), _free);
	line(tr::lng_local_storage_device_total(tr::now), _total);
	return result;
}

void CacheDeviceBar::showTooltip() {
	if (_tooltip || _total <= 0) {
		return;
	}
	const auto parent = window();
	_tooltip = std::make_unique<Ui::ImportantTooltip>(
		parent,
		Ui::MakeNiceTooltipLabel(
			parent,
			rpl::single(tooltipText()),
			st::localStorageUsageTooltipMaxWidth,
			st::defaultImportantTooltipLabel),
		st::defaultImportantTooltip);
	const auto tooltip = _tooltip.get();
	const auto weak = base::make_weak(tooltip);
	tooltip->setAttribute(Qt::WA_TransparentForMouseEvents);
	tooltip->setHiddenCallback([=] { delete weak.get(); });
	auto area = Ui::MapFrom(parent, this, barRect());
	area.translate(0, -st::localStorageUsageTooltipSkip);
	tooltip->pointAt(area, RectPart::Top);
	tooltip->toggleAnimated(true);
}

void CacheDeviceBar::hideTooltip() {
	if (const auto tooltip = _tooltip.release()) {
		tooltip->toggleAnimated(false);
	}
}

CacheClearButton::CacheClearButton(QWidget *parent)
: RippleButton(parent, st::defaultRippleAnimation)
, _label(st::localStorageClearFont, [=] { update(); }, {
	.splitByWords = true,
	.preserveIndex = true,
	.startFromEnd = true,
	.moveAmplitude = 0.25,
	.duration = crl::time(300),
})
, _amount(st::localStorageClearFont, [=] { update(); }, {
	.splitByWords = true,
	.preserveIndex = true,
	.startFromEnd = true,
	.moveAmplitude = 0.25,
	.duration = crl::time(300),
}) {
}

void CacheClearButton::setText(
		const QString &label,
		const QString &amount) {
	_label.setText(label);
	_amount.setText(amount);
}

int CacheClearButton::resizeGetHeight(int newWidth) {
	return st::localStorageClearHeight;
}

void CacheClearButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);

	const auto disabled = isDisabled();
	const auto radius = height() / 2.;
	p.setPen(Qt::NoPen);
	p.setBrush(disabled ? st::windowBgRipple : st::activeButtonBg);
	p.drawRoundedRect(rect(), radius, radius);

	if (!disabled) {
		const auto ripple = st::activeButtonBgRipple->c;
		paintRipple(p, 0, 0, &ripple);
	}

	const auto font = st::localStorageClearFont;
	const auto labelWidth = _label.currentWidth();
	const auto amountWidth = _amount.currentWidth();
	const auto skip = (amountWidth > 0.) ? st::localStorageClearSkip : 0;
	const auto left = (width() - labelWidth - skip - amountWidth) / 2.;
	const auto top = (height() - font->height) / 2;
	const auto fg = disabled ? st::windowSubTextFg->c : st::activeButtonFg->c;
	_label.draw(p, int(base::SafeRound(left)), top, fg);
	_amount.draw(p, int(base::SafeRound(left + labelWidth + skip)), top, fg, 0.6);
}

QImage CacheClearButton::prepareRippleMask() const {
	return Ui::RippleAnimation::RoundRectMask(size(), height() / 2);
}

QPoint CacheClearButton::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

CacheCategoryRow::CacheCategoryRow(
	QWidget *parent,
	const QString &title,
	int chartIndex,
	bool checked,
	const Extras::MediaCacheUsage &data)
: RippleButton(parent, st::defaultRippleAnimation)
, _checkColor([=] { return CachePartColor(chartIndex); })
, _checkStyle(st::defaultCheck)
, _check(_checkStyle, checked, [=] { Ui::RpWidget::update(); })
, _percent(this, QString(), st::localStorageCategoryPercent)
, _name(this, title, st::localStorageCategoryName)
, _size(this, sizeText(data), st::localStorageCategorySize) {
	_checkStyle.toggledFg = _checkColor.color();
	_percent->setAttribute(Qt::WA_TransparentForMouseEvents);
	_name->setAttribute(Qt::WA_TransparentForMouseEvents);
	_size->setAttribute(Qt::WA_TransparentForMouseEvents);
	setClickedCallback([=] { toggle(); });
}

void CacheCategoryRow::toggle() {
	const auto value = !_check.checked();
	_check.setChecked(value, anim::type::normal);
	_checkedChanges.fire_copy(value);
}

rpl::producer<bool> CacheCategoryRow::checkedChanges() const {
	return _checkedChanges.events();
}

void CacheCategoryRow::setUsage(const Extras::MediaCacheUsage &data) {
	_size->setText(sizeText(data));
	resizeToWidth(width());
}

void CacheCategoryRow::setPercent(int percent) {
	_hasPercent = (percent > 0);
	_percent->setText(_hasPercent
		? (QString::number(percent) + '%')
		: QString());
	resizeToWidth(width());
}

int CacheCategoryRow::resizeGetHeight(int newWidth) {
	const auto height = st::localStorageCategoryHeight;
	const auto pad = st::localStorageCategoryPadding;
	const auto checkSize = _check.getSize();
	const auto left = pad.left()
		+ checkSize.width()
		+ st::localStorageCategoryCheckSkip;

	_size->resizeToWidth(std::min(_size->textMaxWidth(), newWidth));
	_size->moveToRight(pad.right(), (height - _size->height()) / 2, newWidth);

	_percent->resizeToWidth(_percent->textMaxWidth());
	_percent->setVisible(_hasPercent);
	const auto percentWidth = _hasPercent
		? (st::localStorageCategoryPercentSkip + _percent->width())
		: 0;
	const auto nameAvailable = std::max(
		_size->x() - st::localStorageCategoryPercentSkip - percentWidth - left,
		0);
	_name->resizeToWidth(std::min(_name->textMaxWidth(), nameAvailable));
	_name->moveToLeft(left, (height - _name->height()) / 2, newWidth);
	if (_hasPercent) {
		_percent->moveToLeft(
			left + _name->width() + st::localStorageCategoryPercentSkip,
			(height - _percent->height()) / 2,
			newWidth);
	}
	return height;
}

void CacheCategoryRow::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	paintRipple(p, 0, 0);
	if (isDisabled()) {
		p.setOpacity(st::defaultCheckbox.disabledOpacity);
	}
	const auto pad = st::localStorageCategoryPadding;
	const auto checkSize = _check.getSize();
	_check.paint(
		p,
		pad.left(),
		(height() - checkSize.height()) / 2,
		width());
}

void CacheCategoryRow::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	_check.setOver(
		isOver() && !isDisabled(),
		isDisabled() ? anim::type::instant : anim::type::normal);
}

QImage CacheCategoryRow::prepareRippleMask() const {
	return Ui::RippleAnimation::RectMask(size());
}

QPoint CacheCategoryRow::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

QString CacheCategoryRow::sizeText(
		const Extras::MediaCacheUsage &data) const {
	return data.bytes
		? Ui::FormatSizeText(data.bytes)
		: tr::lng_local_storage_empty(tr::now);
}

} // namespace Settings
