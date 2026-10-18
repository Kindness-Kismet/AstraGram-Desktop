#include "chat_helpers/tabbed_selector_tabs.h"

#include "extras/ui/components/horizontal_tabs.h"
#include "lang/lang_keys.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"
#include "styles/style_chat_helpers.h"

#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>

namespace ChatHelpers {

TabbedSelectorTabs::TabbedSelectorTabs(
	QWidget *parent,
	const style::SettingsSlider &st)
: DiscreteSlider(parent, false)
, _st(st)
, _settings(Ui::CreateChild<Ui::IconButton>(this, st::emojiPanelSettings))
, _close(Ui::CreateChild<Ui::IconButton>(this, st::emojiPanelClose)) {
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);
	setCrossFadeEnabled(true);
	_settings->setObjectName(u"compose.emoji.settings"_q);
	_close->setObjectName(u"compose.emoji.close"_q);
	tr::lng_menu_settings() | rpl::on_next([=](const QString &text) {
		_settings->setAccessibleName(text);
	}, _settings->lifetime());
	tr::lng_close() | rpl::on_next([=](const QString &text) {
		_close->setAccessibleName(text);
	}, _close->lifetime());
	const auto refreshColors = [=] {
		for (const auto button : { _settings, _close }) {
			button->setIconColorOverride(_st.labelFg->c);
			button->setIconOverColorOverride(_st.labelFgActive->c);
			button->setRippleColorOverride(&_st.rippleBg);
		}
	};
	refreshColors();
	style::PaletteChanged() | rpl::on_next(refreshColors, lifetime());
}

void TabbedSelectorTabs::setActions(Fn<void()> settings, Fn<void()> close) {
	_settingsAvailable = bool(settings);
	_closeEnabled = bool(close);
	_close->setVisible(_closeEnabled);
	_settings->setClickedCallback(std::move(settings));
	_close->setClickedCallback(std::move(close));
	setSettingsVisible(_settingsAvailable);
}

void TabbedSelectorTabs::setRightReserved(int width) {
	_rightReserved = width;
	resizeToWidth(this->width());
}

void TabbedSelectorTabs::setAdditionalContentWidthToSection(int index, int width) {
	Expects(index >= 0 && index < sectionsCount());
	_additionalWidths.resize(sectionsCount());
	_additionalWidths[index] = width;
	resizeToWidth(this->width());
}

QRect TabbedSelectorTabs::sectionContentRect(int index) const {
	Expects(index >= 0 && index < _contentRects.size());
	return _contentRects[index];
}

rpl::producer<> TabbedSelectorTabs::sectionsGeometryChanges() const {
	return _geometryChanges.events();
}

void TabbedSelectorTabs::setSettingsVisible(bool visible) {
	_settingsEnabled = visible && _settingsAvailable;
	_settings->setVisible(_settingsEnabled);
	resizeToWidth(width());
}

void TabbedSelectorTabs::setIcons(std::vector<const style::icon*> icons) {
	_icons = std::move(icons);
	resizeToWidth(width());
}

const style::TextStyle &TabbedSelectorTabs::getLabelStyle() const {
	return _st.labelStyle;
}

int TabbedSelectorTabs::getAnimationDuration() const {
	return 160;
}

int TabbedSelectorTabs::resizeGetHeight(int newWidth) {
	const auto buttonTop = (_st.height - _close->height()) / 2;
	_close->moveToRight(0, buttonTop, newWidth);
	_settings->moveToRight(_close->width(), buttonTop, newWidth);
	const auto reserved = (_closeEnabled ? _close->width() : 0)
		+ (_settingsEnabled ? _settings->width() : 0)
		+ _rightReserved
		+ style::ConvertScale(4);
	const auto available = std::max(0, newWidth - reserved);
	const auto gap = style::ConvertScale(4);
	const auto count = sectionsCount();
	auto labelsWidth = 0;
	_additionalWidths.resize(count);
	auto index = 0;
	for (const auto &section : sectionsRef()) {
		labelsWidth += section.label.maxWidth() + _additionalWidths[index++];
	}
	const auto minPadding = style::ConvertScale(4);
	// 窄侧栏优先显示完整译文，空间不足时收起装饰图标。
	_showIcons = !_icons.empty() && labelsWidth
		+ count * (st::emojiTabSmile.width() + gap + minPadding) <= available;
	const auto iconSkip = _showIcons ? (st::emojiTabSmile.width() + gap) : 0;
	const auto content = labelsWidth + count * iconSkip;
	const auto padding = std::clamp(
		(available - content) / std::max(count, 1),
		minPadding,
		style::ConvertScale(16));
	const auto desired = content + count * padding;
	const auto factor = desired ? std::min(1., available / double(desired)) : 1.;
	auto x = 0.;
	index = 0;
	auto rects = std::vector<QRect>();
	for (auto &section : sectionsRef()) {
		section.left = int(base::SafeRound(x));
		const auto natural = iconSkip + section.label.maxWidth() + _additionalWidths[index++];
		x += (natural + padding) * factor;
		section.width = int(base::SafeRound(x)) - section.left;
		section.contentWidth = std::min(natural, std::max(0, section.width - padding));
		rects.push_back(style::rtlrect(
			section.left + (section.width - section.contentWidth) / 2,
			(_st.height - _st.labelStyle.font->height) / 2,
			section.contentWidth,
			_st.labelStyle.font->height,
			newWidth));
	}
	stopAnimation();
	if (_contentRects != rects) {
		_contentRects = std::move(rects);
		_geometryChanges.fire({});
	}
	return _st.height;
}

void TabbedSelectorTabs::paintEvent(QPaintEvent *e) {
	Painter p(this);
	const auto hq = PainterHighQualityEnabler(p);
	const auto gap = style::ConvertScale(4);
	auto index = 0;
	for (const auto &section : sectionsRef()) {
		const auto active = sectionActiveWeight(index);
		const auto bounds = myrtlrect(section.left, 0, section.width, height());
		ExtrasUi::HorizontalTabs::paint(p, bounds, active,
			index == _hovered, true, section.contentWidth);
		const auto color = ExtrasUi::HorizontalTabs::foreground(_st.labelFg->c, active);
		const auto left = section.left + (section.width - section.contentWidth) / 2;
		if (_showIcons && index < _icons.size()) {
			_icons[index]->paint(p, left, (height() - _icons[index]->height()) / 2, width(), color);
		}
		const auto iconSkip = _showIcons ? (st::emojiTabSmile.width() + gap) : 0;
		const auto labelWidth = std::max(0,
			section.contentWidth - iconSkip - _additionalWidths[index]);
		p.setPen(color);
		section.label.draw(p, {
			.position = QPoint(left + iconSkip, (height() - _st.labelStyle.font->height) / 2),
			.outerWidth = width(),
			.availableWidth = labelWidth,
			.elisionLines = 1,
		});
		++index;
	}
}

void TabbedSelectorTabs::keyPressEvent(QKeyEvent *e) {
	auto next = activeSection();
	if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right) {
		const auto forward = (e->key() == Qt::Key_Right) != style::RightToLeft();
		next += forward ? 1 : -1;
	} else if (e->key() == Qt::Key_Home) {
		next = 0;
	} else if (e->key() == Qt::Key_End) {
		next = sectionsCount() - 1;
	} else {
		DiscreteSlider::keyPressEvent(e);
		return;
	}
	setActiveSection(std::clamp(next, 0, sectionsCount() - 1));
	e->accept();
}

int TabbedSelectorTabs::sectionAt(QPoint position) const {
	auto result = -1;
	auto index = 0;
	enumerateSections([&](const Section &section) {
		if (myrtlrect(section.left, 0, section.width, height()).contains(position)) {
			result = index;
			return false;
		}
		++index;
		return true;
	});
	return result;
}

void TabbedSelectorTabs::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		_pressed = sectionAt(e->pos());
	}
}

void TabbedSelectorTabs::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	const auto pressed = std::exchange(_pressed, -1);
	if (pressed >= 0 && pressed == sectionAt(e->pos())) {
		setActiveSection(pressed);
	}
}

void TabbedSelectorTabs::mouseMoveEvent(QMouseEvent *e) {
	const auto hovered = sectionAt(e->pos());
	if (_hovered != hovered) {
		_hovered = hovered;
		setCursor(hovered >= 0 ? style::cur_pointer : style::cur_default);
		update();
	}
}

void TabbedSelectorTabs::leaveEventHook(QEvent *e) {
	DiscreteSlider::leaveEventHook(e);
	_hovered = -1;
	update();
}

}
