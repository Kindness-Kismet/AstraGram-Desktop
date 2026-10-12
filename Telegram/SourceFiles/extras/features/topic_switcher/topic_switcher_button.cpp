#include "extras/features/topic_switcher/topic_switcher_button.h"

#include "ui/painter.h"
#include "ui/unread_badge_paint.h"
#include "ui/unread_counter_format.h"
#include "styles/style_extras_styles.h"
#include "styles/style_window.h"

namespace ExtrasFeatures::TopicSwitcher {

Ui::UnreadBadgeStyle UnreadBadge(bool muted) {
	auto result = Ui::UnreadBadgeStyle();
	result.font = st::mainMenuBadgeFont;
	result.size = st::mainMenuBadgeSize;
	result.sizeId = Ui::UnreadBadgeSize::MainMenu;
	result.align = style::al_left;
	result.muted = muted;
	return result;
}

Button::Button(QWidget *parent, Fn<bool()> paused)
: AbstractButton(parent)
, _paused(std::move(paused)) {
	resize(width(), st::topicSwitcherHeight);
	setObjectName(u"topicSwitcher"_q);
}

void Button::setContent(ButtonContent &&content) {
	auto context = std::move(content.context);
	context.repaint = [=] { update(); };
	_text.setMarkedText(
		st::topicSwitcherTextStyle,
		content.label,
		kDefaultTextOptions,
		context);
	_unread = (content.unread > 0)
		? FormatUnreadCounter(content.unread, false)
		: QString();
	_unreadMuted = content.unreadMuted;
	setAccessibleName(content.label.text);
	refreshNaturalWidth();
	update();
}

void Button::setOpened(bool opened) {
	if (_opened == opened) {
		return;
	}
	_opened = opened;
	_openedAnimation.start(
		[=] { update(); },
		opened ? 0. : 1.,
		opened ? 1. : 0.,
		st::slideWrapDuration);
}

int Button::badgeWidth() const {
	return _unread.isEmpty()
		? 0
		: Ui::CountUnreadBadgeSize(_unread, UnreadBadge(_unreadMuted)).width();
}

void Button::refreshNaturalWidth() {
	const auto &padding = st::topicSwitcherPadding;
	const auto badge = badgeWidth();
	setNaturalWidth(padding.left()
		+ _text.maxWidth()
		+ st::topicSwitcherSkip
		+ st::topicSwitcherChevron.width()
		+ (badge ? (st::topicSwitcherSkip + badge) : 0)
		+ padding.right());
}

void Button::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto opened = _openedAnimation.value(_opened ? 1. : 0.);

	const auto &padding = st::topicSwitcherPadding;
	const auto badge = badgeWidth();
	const auto chevron = st::topicSwitcherChevron.width();
	const auto tail = st::topicSwitcherSkip
		+ chevron
		+ (badge ? (st::topicSwitcherSkip + badge) : 0);
	const auto textWidth = std::max(
		width() - padding.left() - padding.right() - tail,
		0);
	const auto textHeight = st::topicSwitcherTextStyle.font->height;
	p.setPen(st::windowActiveTextFg);
	_text.draw(p, {
		.position = { padding.left(), (height() - textHeight) / 2 },
		.outerWidth = width(),
		.availableWidth = textWidth,
		.now = crl::now(),
		.paused = _paused && _paused(),
		.elisionLines = 1,
	});

	auto left = padding.left()
		+ std::min(textWidth, _text.maxWidth())
		+ st::topicSwitcherSkip;
	paintChevron(p, left, opened);
	if (badge) {
		left += chevron + st::topicSwitcherSkip;
		const auto style = UnreadBadge(_unreadMuted);
		Ui::PaintUnreadBadge(
			p,
			_unread,
			left,
			(height() - style.size) / 2,
			style);
	}
}

// 向下的折线箭头，展开时翻转向上。
void Button::paintChevron(QPainter &p, int left, float64 opened) const {
	const auto size = st::topicSwitcherChevron;
	const auto top = (height() - size.height()) / 2.;
	const auto half = size.height() * (1. - 2. * opened) / 2.;
	const auto middle = top + size.height() / 2.;
	auto hq = PainterHighQualityEnabler(p);
	auto pen = QPen(st::windowActiveTextFg->c);
	pen.setWidthF(style::ConvertScaleExact(1.5));
	pen.setCapStyle(Qt::RoundCap);
	pen.setJoinStyle(Qt::RoundJoin);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	const auto points = std::array<QPointF, 3>{ {
		{ left + 0., middle - half },
		{ left + size.width() / 2., middle + half },
		{ left + size.width() + 0., middle - half },
	} };
	p.drawPolyline(points.data(), int(points.size()));
}

} // namespace ExtrasFeatures::TopicSwitcher
