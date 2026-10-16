/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/discrete_sliders.h"

#include "extras/ui/components/horizontal_tabs.h"

#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "styles/style_widgets.h"

namespace Ui {
namespace {

// 分段底板上下留白，滑块与底板的间隙；圆角按间隙同心。
constexpr auto kSegmentedTrackSkip = 6;
constexpr auto kSegmentedPillInset = 3;
constexpr auto kSegmentedTrackRadius = 9;

} // namespace

DiscreteSlider::DiscreteSlider(QWidget *parent, bool snapToLabel)
: RpWidget(parent)
, _snapToLabel(snapToLabel) {
	setCursor(style::cur_pointer);
}

DiscreteSlider::~DiscreteSlider() = default;

void DiscreteSlider::setActiveSection(int index) {
	_activeIndex = index;
	activateCallback();
	setSelectedSection(index);
}

void DiscreteSlider::activateCallback() {
	if (_timerId >= 0) {
		killTimer(_timerId);
		_timerId = -1;
	}
	auto ms = crl::now();
	if (ms >= _callbackAfterMs) {
		_sectionActivated.fire_copy(_activeIndex);
	} else {
		_timerId = startTimer(_callbackAfterMs - ms, Qt::PreciseTimer);
	}
}

void DiscreteSlider::timerEvent(QTimerEvent *e) {
	activateCallback();
}

void DiscreteSlider::setActiveSectionFast(int index) {
	setActiveSection(index);
	finishAnimating();
}

void DiscreteSlider::finishAnimating() {
	stopAnimation();
	update();
	_callbackAfterMs = 0;
	if (_timerId >= 0) {
		activateCallback();
	}
}

void DiscreteSlider::setCrossFadeEnabled(bool enabled) {
	Expects(!enabled || !_snapToLabel);
	finishAnimating();
	_crossFade = enabled;
}

float64 DiscreteSlider::sectionActiveWeight(int index) const {
	const auto progress = _a_fade.value(1.);
	return _sections[index].fadeFrom * (1. - progress)
		+ (index == _selected ? progress : 0.);
}

void DiscreteSlider::selectSection(int index) {
	if (index < 0 || index >= _sections.size()) {
		for (auto &other : _sections) {
			if (other.ripple) {
				other.ripple->lastStop();
			}
		}
		return;
	}
	const auto &section = _sections[index];
	if (section.ripple && !section.ripple->empty()) {
		return;
	}
	for (auto &other : _sections) {
		if (other.ripple) {
			other.ripple->lastStop();
		}
	}
	startRipple(index);
}

void DiscreteSlider::setAdditionalContentWidthToSection(int index, int w) {
	if (index < 0 || index >= _sections.size()) {
		return;
	}
	auto &section = _sections[index];
	const auto contentWidth = section.label.maxWidth() + w;
	if (section.contentWidth == contentWidth) {
		return;
	}
	section.contentWidth = contentWidth;
	refresh();
}

int DiscreteSlider::sectionsCount() const {
	return int(_sections.size());
}

int DiscreteSlider::lookupSectionLeft(int index) const {
	Expects(index >= 0 && index < _sections.size());

	return _sections[index].left;
}

void DiscreteSlider::setSelectOnPress(bool selectOnPress) {
	_selectOnPress = selectOnPress;
}

bool DiscreteSlider::paused() const {
	return _paused && _paused();
}

std::vector<DiscreteSlider::Section> &DiscreteSlider::sectionsRef() {
	return _sections;
}

void DiscreteSlider::addSection(const QString &label) {
	_sections.push_back(Section(label, getLabelStyle()));
	resizeToWidth(width());
}

void DiscreteSlider::addSection(
		const TextWithEntities &label,
		Text::MarkedContext context) {
	context.repaint = [this] { update(); };
	_sections.push_back(Section(label, getLabelStyle(), context));
	resizeToWidth(width());
}

void DiscreteSlider::setSections(const std::vector<QString> &labels) {
	Assert(!labels.empty());

	_sections.clear();
	for (const auto &label : labels) {
		_sections.push_back(Section(label, getLabelStyle()));
	}
	refresh();
}

void DiscreteSlider::setSections(
		const std::vector<TextWithEntities> &labels,
		Text::MarkedContext context,
		Fn<bool()> paused) {
	Assert(!labels.empty());

	context.repaint = [this] { update(); };

	_sections.clear();
	for (const auto &label : labels) {
		_sections.push_back(Section(label, getLabelStyle(), context));
	}
	_paused = std::move(paused);
	refresh();
}

void DiscreteSlider::refresh() {
	stopAnimation();
	if (_activeIndex >= _sections.size()) {
		_activeIndex = 0;
	}
	if (_selected >= _sections.size()) {
		_selected = 0;
	}
	resizeToWidth(width());
	update();
}

DiscreteSlider::Range DiscreteSlider::getFinalActiveRange() const {
	const auto raw = (_sections.empty() || _selected < 0)
		? nullptr
		: &_sections[_selected];
	if (!raw) {
		return { 0, 0 };
	}
	const auto width = _snapToLabel
		? std::min(raw->width, raw->contentWidth)
		: raw->width;
	return { raw->left + ((raw->width - width) / 2), width };
}

DiscreteSlider::Range DiscreteSlider::getCurrentActiveRange() const {
	const auto to = getFinalActiveRange();
	return {
		int(base::SafeRound(_a_left.value(to.left))),
		int(base::SafeRound(_a_width.value(to.width))),
	};
}

void DiscreteSlider::enumerateSections(Fn<bool(Section&)> callback) {
	for (auto &section : _sections) {
		if (!callback(section)) {
			return;
		}
	}
}

void DiscreteSlider::enumerateSections(
		Fn<bool(const Section&)> callback) const {
	for (const auto &section : _sections) {
		if (!callback(section)) {
			return;
		}
	}
}

void DiscreteSlider::mousePressEvent(QMouseEvent *e) {
	const auto index = getIndexFromPosition(e->pos());
	if (_selectOnPress) {
		setSelectedSection(index);
	}
	startRipple(index);
	_pressed = index;
}

void DiscreteSlider::mouseMoveEvent(QMouseEvent *e) {
	if (_pressed < 0) {
		return;
	}
	if (_selectOnPress) {
		setSelectedSection(getIndexFromPosition(e->pos()));
	}
}

void DiscreteSlider::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = std::exchange(_pressed, -1);
	if (pressed < 0) {
		return;
	}

	const auto index = getIndexFromPosition(e->pos());
	if (pressed < _sections.size()) {
		if (_sections[pressed].ripple) {
			_sections[pressed].ripple->lastStop();
		}
	}
	if (_selectOnPress || index == pressed) {
		setActiveSection(index);
	}
}

void DiscreteSlider::setSelectedSection(int index) {
	if (index >= int(_sections.size())) {
		return;
	}

	if (_selected != index) {
		const auto from = getFinalActiveRange();
		if (_crossFade) {
			// 连续切换从当前透明度接续，避免中途跳回完整选中态。
			for (auto i = 0; i != int(_sections.size()); ++i) {
				_sections[i].fadeFrom = sectionActiveWeight(i);
			}
		}
		_selected = index;
		const auto to = getFinalActiveRange();
		const auto duration = getAnimationDuration();
		const auto updater = [this] { update(); };
		if (_crossFade) {
			_a_fade.start(updater, 0., 1., duration);
		} else {
			_a_left.start(updater, from.left, to.left, duration);
			_a_width.start(updater, from.width, to.width, duration);
		}
		_callbackAfterMs = crl::now() + duration;
	}
}

int DiscreteSlider::getIndexFromPosition(QPoint pos) {
	const auto count = _sections.size();
	const auto x = style::RightToLeft() ? width() - pos.x() - 1 : pos.x();
	for (auto i = 0; i != count; ++i) {
		if (_sections[i].left + _sections[i].width > x) {
			return i;
		}
	}
	return count - 1;
}

DiscreteSlider::Section::Section(
	const QString &label,
	const style::TextStyle &st)
: label(st, label)
, contentWidth(Section::label.maxWidth()) {
}

DiscreteSlider::Section::Section(
		const TextWithEntities &label,
		const style::TextStyle &st,
		const Text::MarkedContext &context) {
	this->label.setMarkedText(st, label, kMarkupTextOptions, context);
	contentWidth = Section::label.maxWidth();
}

SettingsSlider::SettingsSlider(
	QWidget *parent,
	const style::SettingsSlider &st)
: DiscreteSlider(parent, st.barSnapToLabel)
, _st(st) {
	setMouseTracking(true);
	if (_st.barRadius > 0) {
		_bar.emplace(_st.barRadius, _st.barFg);
		_barActive.emplace(_st.barRadius, _st.barFgActive);
	}
	setSelectOnPress(_st.ripple.showDuration == 0);
	ExtrasUi::HorizontalTabs::watch(this, [=] {
		if (_segmentedStyle) {
			update();
			return;
		}
		enumerateSections([](Section &section) {
			section.ripple = nullptr;
			return true;
		});
		if (_fitWidthToSections) {
			fitWidthToSections();
		} else {
			resizeSections(width());
		}
	});
}

void SettingsSlider::setSegmentedStyle(bool enabled) {
	if (_segmentedStyle == enabled) {
		return;
	}
	_segmentedStyle = enabled;
	enumerateSections([](Section &section) {
		section.ripple = nullptr;
		return true;
	});
	resizeSections(width());
	update();
}

bool SettingsSlider::usesCustomTabs() const {
	return !_segmentedStyle && ExtrasUi::HorizontalTabs::appliesTo(_st);
}

const style::SettingsSlider &SettingsSlider::st() const {
	return _st;
}

int SettingsSlider::centerOfSection(int section) const {
	auto result = 0;
	auto index = 0;
	enumerateSections([&](const Section &data) {
		if (index++ == section) {
			result = data.left + (data.width / 2);
			return false;
		}
		return true;
	});
	return result;
}

QRect SettingsSlider::sectionContentRect(int section) const {
	Expects(section >= 0 && section < _sectionContentRects.size());

	return _sectionContentRects[section];
}

rpl::producer<> SettingsSlider::sectionsGeometryChanges() const {
	return _sectionsGeometryChanges.events();
}

void SettingsSlider::fitWidthToSections() {
	_fitWidthToSections = true;
	const auto widths = countSectionsWidths(0);
	resizeToWidth(ranges::accumulate(widths, .0) + sectionsPadding() * 2);
}

void SettingsSlider::setRippleTopRoundRadius(int radius) {
	_rippleTopRoundRadius = radius;
}

const style::TextStyle &SettingsSlider::getLabelStyle() const {
	return _st.labelStyle;
}

int SettingsSlider::getAnimationDuration() const {
	if (_segmentedStyle) {
		return 180;
	}
	return _st.duration;
}

int SettingsSlider::sectionsPadding() const {
	if (_segmentedStyle) {
		return style::ConvertScale(3);
	}
	return usesCustomTabs()
		? std::max(_st.padding, style::ConvertScale(12))
		: _st.padding;
}

void SettingsSlider::resizeSections(int newWidth) {
	const auto count = getSectionsCount();
	if (!count) {
		return;
	}

	const auto sectionWidths = countSectionsWidths(newWidth);

	auto skip = 0;
	const auto custom = usesCustomTabs();
	const auto padding = sectionsPadding();
	const auto usedWidth = ranges::accumulate(sectionWidths, .0);
	auto x = custom && !_fitWidthToSections
		? std::max(double(padding), (newWidth - usedWidth) / 2.)
		: padding * 1.;
	const auto labelHeight = _st.labelStyle.font->height;
	const auto labelTop = (custom || _segmentedStyle)
		? (_st.height - labelHeight) / 2
		: _st.labelTop;
	auto contentRects = std::vector<QRect>();
	contentRects.reserve(count);
	auto changed = false;
	auto sectionWidth = sectionWidths.begin();
	enumerateSections([&](Section &section) {
		Expects(sectionWidth != sectionWidths.end());

		const auto left = int(std::floor(x)) + skip;
		x += *sectionWidth;
		const auto width = int(base::SafeRound(x)) - (left - skip);
		changed = changed || section.left != left || section.width != width;
		if (section.width != width) {
			section.ripple = nullptr;
		}
		section.left = left;
		section.width = width;
		const auto contentLeft = left + (width - section.contentWidth) / 2;
		contentRects.push_back(style::rtlrect(
			contentLeft,
			labelTop,
			section.contentWidth,
			labelHeight,
			newWidth));
		skip += custom ? 0 : _st.barSkip;
		++sectionWidth;
		return true;
	});
	stopAnimation();
	if (changed || _sectionContentRects != contentRects) {
		_sectionContentRects = std::move(contentRects);
		_sectionsGeometryChanges.fire({});
	}
}

std::vector<float64> SettingsSlider::countSectionsWidths(int newWidth) const {
	const auto count = getSectionsCount();
	if (!count) {
		return {};
	}
	if (_segmentedStyle) {
		return std::vector<float64>(count,
			std::max(0, newWidth - 2 * sectionsPadding()) / double(count));
	}
	if (usesCustomTabs()) {
		auto result = std::vector<float64>();
		result.reserve(count);
		auto labelsWidth = 0;
		enumerateSections([&](const Section &section) {
			labelsWidth += section.contentWidth;
			return true;
		});
		auto padding = double(style::ConvertScale(36));
		if (newWidth && !_fitWidthToSections) {
			const auto available = (newWidth - 2 * sectionsPadding() - labelsWidth)
				/ double(count);
			padding = std::clamp(available,
				double(style::ConvertScale(12)),
				double(style::ConvertScale(36)));
		}
		enumerateSections([&](const Section &section) {
			result.push_back(section.contentWidth + padding);
			return true;
		});
		return result;
	}
	const auto sectionsWidth = newWidth
		- 2 * _st.padding
		- (count - 1) * _st.barSkip;
	const auto sectionWidth = sectionsWidth / float64(count);

	auto result = std::vector<float64>(count, sectionWidth);
	auto labelsWidth = 0;
	auto commonWidth = true;
	enumerateSections([&](const Section &section) {
		labelsWidth += section.contentWidth;
		if (section.contentWidth >= sectionWidth) {
			commonWidth = false;
		}
		return true;
	});
	// If labelsWidth > sectionsWidth we're screwed anyway.
	if (_st.strictSkip || (!commonWidth && labelsWidth <= sectionsWidth)) {
		auto padding = _st.strictSkip
			? (_st.strictSkip / 2.)
			: (sectionsWidth - labelsWidth) / (2. * count);
		auto currentWidth = result.begin();
		enumerateSections([&](const Section &section) {
			Expects(currentWidth != result.end());

			*currentWidth = padding + section.contentWidth + padding;
			++currentWidth;
			return true;
		});
	}
	return result;
}

int SettingsSlider::resizeGetHeight(int newWidth) {
	resizeSections(newWidth);
	return _st.height;
}

void SettingsSlider::startRipple(int sectionIndex) {
	if (!_st.ripple.showDuration) {
		return;
	}
	auto index = 0;
	enumerateSections([this, &index, sectionIndex](Section &section) {
		if (index++ == sectionIndex) {
			if (!section.ripple) {
				auto mask = prepareRippleMask(sectionIndex, section);
				section.ripple = std::make_unique<RippleAnimation>(
					_st.ripple,
					std::move(mask),
					[this] { update(); });
			}
			const auto point = mapFromGlobal(QCursor::pos());
			const auto bounds = myrtlrect(section.left, 0, section.width, height());
			section.ripple->add(point - bounds.topLeft());
			return false;
		}
		return true;
	});
}

QImage SettingsSlider::prepareRippleMask(
		int sectionIndex,
		const Section &section) {
	if (_segmentedStyle) {
		const auto skip = style::ConvertScale(kSegmentedTrackSkip);
		const auto radius = style::ConvertScale(
			kSegmentedTrackRadius - kSegmentedPillInset);
		return RippleAnimation::RoundRectMask(
			QSize(section.width, height() - 2 * skip), radius);
	}
	if (usesCustomTabs()) {
		return ExtrasUi::HorizontalTabs::rippleMask(QSize(section.width, height()));
	}
	const auto size = QSize(section.width, height() - _st.rippleBottomSkip);
	if (!_rippleTopRoundRadius
		|| (sectionIndex > 0 && sectionIndex + 1 < getSectionsCount())) {
		return RippleAnimation::RectMask(size);
	}
	return RippleAnimation::MaskByDrawer(size, false, [&](QPainter &p) {
		const auto plusRadius = _rippleTopRoundRadius + 1;
		p.drawRoundedRect(
			0,
			0,
			section.width,
			height() + plusRadius,
			_rippleTopRoundRadius,
			_rippleTopRoundRadius);
		if (sectionIndex > 0) {
			p.fillRect(0, 0, plusRadius, plusRadius, p.brush());
		}
		if (sectionIndex + 1 < getSectionsCount()) {
			p.fillRect(
				section.width - plusRadius,
				0,
				plusRadius,
				plusRadius, p.brush());
		}
	});
}

bool SettingsSlider::eventHook(QEvent *e) {
	if (usesCustomTabs()
		&& (e->type() == QEvent::Enter || e->type() == QEvent::Leave
			|| e->type() == QEvent::MouseMove)) {
		update();
	}
	return DiscreteSlider::eventHook(e);
}

// 分段底板加一块等宽滑块，滑块中心跟随选中区间的动画位置。
void SettingsSlider::paintSegmentedBackground(QPainter &p, Range range) {
	const auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::controlBg);
	const auto skip = style::ConvertScale(kSegmentedTrackSkip);
	const auto track = rect().marginsRemoved({ 0, skip, 0, skip });
	const auto outer = style::ConvertScale(kSegmentedTrackRadius);
	p.drawRoundedRect(track, outer, outer);

	const auto count = sectionsCount();
	if (!count) {
		return;
	}
	const auto padding = sectionsPadding();
	const auto pill = std::max(0, width() - 2 * padding) / double(count);
	const auto center = range.left + range.width / 2.;
	const auto inset = style::ConvertScale(kSegmentedPillInset);
	const auto bounds = QRectF(
		center - pill / 2.,
		track.y() + inset,
		pill,
		track.height() - 2 * inset);
	auto fill = st::windowActiveTextFg->c;
	fill.setAlphaF(st::windowBg->c.lightnessF() < .5 ? .26 : .16);
	p.setBrush(fill);
	const auto inner = outer - inset;
	p.drawRoundedRect(style::rtlrect(bounds, width()), inner, inner);
}

void SettingsSlider::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto clip = e->rect();
	const auto range = DiscreteSlider::getCurrentActiveRange();
	if (_segmentedStyle) {
		paintSegmentedBackground(p, range);
	}
	const auto custom = usesCustomTabs();
	const auto mouse = mapFromGlobal(QCursor::pos());

	const auto drawRect = [&](QRect rect, bool active = false) {
		const auto &bar = active ? _barActive : _bar;
		if (bar) {
			bar->paint(p, rect);
		} else {
			p.fillRect(rect, active ? _st.barFgActive : _st.barFg);
		}
	};
	auto sectionIndex = 0;
	enumerateSections([&](Section &section) {
		const auto contentRect = sectionContentRect(sectionIndex);
		const auto activeWidth = _st.barSnapToLabel
			? section.contentWidth
			: section.width;
		const auto activeLeft = section.left
			+ (section.width - activeWidth) / 2;
		const auto divider = std::max(std::min(activeWidth, range.width), 1);
		const auto active = crossFadeEnabled()
			? sectionActiveWeight(sectionIndex)
			: 1.
			- std::clamp(
				std::abs(range.left - activeLeft) / float64(divider),
				0.,
				1.);
		++sectionIndex;
		if (custom) {
			const auto bounds = myrtlrect(section.left, 0, section.width, height());
			ExtrasUi::HorizontalTabs::paint(p, bounds, active,
				underMouse() && bounds.contains(mouse), true, section.contentWidth);
		}
		if (section.ripple) {
			const auto color = custom ? ExtrasUi::HorizontalTabs::ripple(active) : anim::color(
				_st.rippleBg,
				_st.rippleBgActive,
				active);
			const auto top = _segmentedStyle
				? style::ConvertScale(kSegmentedTrackSkip)
				: 0;
			section.ripple->paint(p, section.left, top, width(), &color);
			if (section.ripple->empty()) {
				section.ripple.reset();
			}
		}
		if (!custom && !_segmentedStyle && crossFadeEnabled()) {
			const auto rect = myrtlrect(
				activeLeft, _st.barTop, activeWidth, _st.barStroke);
			drawRect(rect);
			p.setOpacity(active);
			drawRect(rect, true);
			p.setOpacity(1.);
		} else if (!custom && !_segmentedStyle && !_st.barSnapToLabel) {
			auto from = activeLeft;
			auto tofill = activeWidth;
			if (range.left > from) {
				const auto fill = std::min(tofill, range.left - from);
				drawRect(myrtlrect(from, _st.barTop, fill, _st.barStroke));
				from += fill;
				tofill -= fill;
			}
			if (range.left + activeWidth > from) {
				const auto fill = std::min(
					tofill,
					range.left + activeWidth - from);
				if (fill) {
					drawRect(
						myrtlrect(from, _st.barTop, fill, _st.barStroke),
						true);
					from += fill;
					tofill -= fill;
				}
			}
			if (tofill) {
				drawRect(myrtlrect(from, _st.barTop, tofill, _st.barStroke));
			}
		}
		if (contentRect.intersects(clip)) {
			const auto labelLeft = style::RightToLeft()
				? width() - contentRect.x() - contentRect.width()
				: contentRect.x();
			p.setPen((custom || _segmentedStyle)
				? ExtrasUi::HorizontalTabs::foreground(_st.labelFg->c, active)
				: anim::color(_st.labelFg, _st.labelFgActive, active));
			section.label.draw(p, {
				.position = QPoint(labelLeft, contentRect.y()),
				.outerWidth = width(),
				.availableWidth = section.label.maxWidth(),
				.paused = paused(),
			});
		}
		return true;
	});
	if (!custom && !_segmentedStyle && _st.barSnapToLabel) {
		const auto add = _st.barStroke / 2;
		const auto from = std::max(range.left - add, 0);
		const auto till = std::min(range.left + range.width + add, width());
		if (from < till) {
			drawRect(
				myrtlrect(from, _st.barTop, till - from, _st.barStroke),
				true);
		}
	}
}

} // namespace Ui
