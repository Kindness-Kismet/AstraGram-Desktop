/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/requests_bar.h"

#include "ui/chat/floating_bar.h"
#include "ui/chat/group_call_userpics.h"
#include "ui/widgets/shadow.h"
#include "ui/text/text_options.h"
#include "ui/painter.h"
#include "lang/lang_keys.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h" // st::topBarArrowPadding, like TopBarWidget.
#include "styles/style_window.h" // st::columnMinimalWidthLeft
#include "styles/palette.h"

#include <QtGui/QtEvents>

namespace Ui {

RequestsBar::RequestsBar(
	not_null<QWidget*> parent,
	rpl::producer<RequestsBarContent> content,
	ChatBarShape shape)
: _shape(shape)
, _wrap(parent, object_ptr<RpWidget>(parent))
, _inner(_wrap.entity())
, _shadow(std::make_unique<PlainShadow>(_wrap.parentWidget()))
, _userpics(std::make_unique<GroupCallUserpics>(
		st::historyRequestsUserpics,
		rpl::single(false),
		[=] { _inner->update(); })) {
	if (_shape == ChatBarShape::Card) {
		ApplyChatControlSurface(_wrap.entity(), st::windowCardRadius);
	}
	_wrap.entity()->setObjectName(u"chatBar.requests"_q);
	_wrap.hide(anim::type::instant);
	_shadow->hide();

	auto copy = std::move(
		content
	) | rpl::start_spawning(_wrap.lifetime());

	rpl::duplicate(
		copy
	) | rpl::on_next([=](RequestsBarContent &&content) {
		_content = content;
		if (_content.count > 0) {
			if (_content.count == 1 && !_content.nameFull.isEmpty()) {
				_textFull.setText(
					st::defaultMessageBar.title,
					tr::lng_group_requests_pending_user(
						tr::now,
						lt_user,
						_content.nameFull),
					Ui::NameTextOptions());
				_textShort.setText(
					st::defaultMessageBar.title,
					tr::lng_group_requests_pending_user(
						tr::now,
						lt_user,
						_content.nameShort),
					Ui::NameTextOptions());
			} else {
				_textShort.setText(
					st::defaultMessageBar.title,
					tr::lng_group_requests_pending(
						tr::now,
						lt_count_decimal,
						_content.count),
					Ui::NameTextOptions());
				_textFull.clear();
			}
		}
		_userpics->update(_content.users, !_wrap.isHidden());
		updateNaturalWidth();
		_inner->update();
	}, lifetime());

	std::move(
		copy
	) | rpl::map([=](const RequestsBarContent &content) {
		return !content.count;
	}) | rpl::on_next_done([=](bool hidden) {
		_shouldBeShown = !hidden;
		if (!_forceHidden) {
			_wrap.toggle(_shouldBeShown, anim::type::normal);
		}
	}, [=] {
		_forceHidden = true;
		_wrap.toggle(false, anim::type::normal);
	}, lifetime());

	_userpics->widthValue(
	) | rpl::on_next([=](int width) {
		_userpicsWidth = width;
		updateNaturalWidth();
	}, lifetime());

	setupInner();
}

RequestsBar::~RequestsBar() = default;

void RequestsBar::setupInner() {
	_inner->resize(0, barHeight());
	_inner->paintRequest(
	) | rpl::on_next([=](QRect rect) {
		auto p = Painter(_inner);
		if (_shape == ChatBarShape::Capsule) {
			paintCapsule(p);
		} else {
			paint(p);
		}
	}, _inner->lifetime());
	if (_shape == ChatBarShape::Capsule) {
		setupCapsule();
	}

	// Clicks.
	_inner->setCursor(style::cur_pointer);
	_inner->events(
	) | rpl::filter([=](not_null<QEvent*> event) {
		return (event->type() == QEvent::MouseButtonPress);
	}) | rpl::map([=] {
		return _inner->events(
		) | rpl::filter([=](not_null<QEvent*> event) {
			return (event->type() == QEvent::MouseButtonRelease);
		}) | rpl::take(1) | rpl::filter([=](not_null<QEvent*> event) {
			return _inner->rect().contains(
				static_cast<QMouseEvent*>(event.get())->pos());
		});
	}) | rpl::flatten_latest(
	) | rpl::to_empty | rpl::start_to_stream(_barClicks, _inner->lifetime());

	_wrap.geometryValue(
	) | rpl::on_next([=](QRect rect) {
		updateShadowGeometry(rect);
		updateControlsGeometry(rect);
	}, _inner->lifetime());
}

void RequestsBar::paint(Painter &p) {
	const auto userpicsSize = st::historyRequestsUserpics.size;
	const auto userpicsTop = st::lineWidth + (st::historyRequestsHeight
		- st::lineWidth
		- userpicsSize) / 2;
	const auto userpicsLeft = userpicsTop * 2;
	const auto textTop = st::lineWidth + (st::historyRequestsHeight
		- st::lineWidth
		- st::semiboldFont->height) / 2;
	const auto width = _inner->width();
	const auto &font = st::defaultMessageBar.title.font;
	p.setPen(st::defaultMessageBar.titleFg);
	p.setFont(font);

	if (width >= st::columnMinimalWidthLeft / 2) {
		const auto textLeft = userpicsLeft + _userpicsWidth + userpicsLeft;
		const auto available = width - textLeft - userpicsLeft;
		if (_textFull.isEmpty() || available < _textFull.maxWidth()) {
			_textShort.drawElided(p, textLeft, textTop, available);
		} else {
			_textFull.drawElided(p, textLeft, textTop, available);
		}
	}

	// Skip shadow of the bar above.
	_userpics->paint(p, userpicsLeft, userpicsTop, userpicsSize);
}

void RequestsBar::setupCapsule() {
	_inner->events(
	) | rpl::filter([=](not_null<QEvent*> event) {
		return (event->type() == QEvent::Enter)
			|| (event->type() == QEvent::Leave);
	}) | rpl::on_next([=](not_null<QEvent*> event) {
		_over = (event->type() == QEvent::Enter);
		_inner->update();
	}, _inner->lifetime());
}

// 头像与胶囊左端同心，文字紧随其后。
int RequestsBar::capsuleTextLeft() const {
	const auto userpicsLeft = (st::chatBarCapsuleHeight
		- st::historyRequestsUserpics.size) / 2;
	return userpicsLeft + _userpicsWidth + st::chatBarCapsuleSkip;
}

void RequestsBar::updateNaturalWidth() {
	if (_shape != ChatBarShape::Capsule) {
		return;
	}
	const auto &text = _textFull.isEmpty() ? _textShort : _textFull;
	_inner->setNaturalWidth(capsuleTextLeft()
		+ text.maxWidth()
		+ st::chatBarCapsulePadding.right());
}

void RequestsBar::paintCapsule(Painter &p) {
	if (_over) {
		PaintChatBar(
			p,
			_inner,
			_inner->rect(),
			st::historyComposeButtonBg->c,
			st::historyComposeButtonBgOver->c);
	}
	const auto height = st::chatBarCapsuleHeight;
	const auto userpicsSize = st::historyRequestsUserpics.size;
	const auto userpicsTop = (height - userpicsSize) / 2;
	const auto textLeft = capsuleTextLeft();
	const auto available = _inner->width()
		- textLeft
		- st::chatBarCapsulePadding.right();
	const auto &font = st::defaultMessageBar.title.font;
	p.setPen(st::defaultMessageBar.titleFg);
	p.setFont(font);
	const auto &text = (_textFull.isEmpty() || available < _textFull.maxWidth())
		? _textShort
		: _textFull;
	text.drawElided(p, textLeft, (height - font->height) / 2, available);
	_userpics->paint(p, userpicsTop, userpicsTop, userpicsSize);
}

int RequestsBar::barHeight() const {
	return (_shape == ChatBarShape::Capsule)
		? st::chatBarCapsuleHeight
		: st::historyRequestsHeight;
}

void RequestsBar::updateControlsGeometry(QRect wrapGeometry) {
	_shadow->hide();
}

void RequestsBar::setShadowGeometryPostprocess(Fn<QRect(QRect)> postprocess) {
	_shadowGeometryPostprocess = std::move(postprocess);
	updateShadowGeometry(_wrap.geometry());
}

void RequestsBar::updateShadowGeometry(QRect wrapGeometry) {
	const auto regular = QRect(
		wrapGeometry.x(),
		wrapGeometry.y() + wrapGeometry.height(),
		wrapGeometry.width(),
		st::lineWidth);
	_shadow->setGeometry(_shadowGeometryPostprocess
		? _shadowGeometryPostprocess(regular)
		: regular);
}

void RequestsBar::show() {
	if (!_forceHidden) {
		return;
	}
	_forceHidden = false;
	if (_shouldBeShown) {
		_wrap.show(anim::type::instant);
	}
}

void RequestsBar::hide() {
	if (_forceHidden) {
		return;
	}
	_forceHidden = true;
	_wrap.hide(anim::type::instant);
	_shadow->hide();
}

void RequestsBar::raise() {
	_wrap.raise();
	_shadow->raise();
}

void RequestsBar::finishAnimating() {
	_wrap.finishAnimating();
}

void RequestsBar::move(int x, int y) {
	_wrap.move(x, y);
}

void RequestsBar::resizeToWidth(int width) {
	_wrap.entity()->resizeToWidth(width);
	_inner->resizeToWidth(width);
}

int RequestsBar::height() const {
	return !_forceHidden
		? _wrap.height()
		: _shouldBeShown
		? barHeight()
		: 0;
}

rpl::producer<int> RequestsBar::heightValue() const {
	return _wrap.heightValue();
}

int RequestsBar::naturalWidth() const {
	return _inner->naturalWidth();
}

rpl::producer<int> RequestsBar::naturalWidthValue() const {
	return _inner->naturalWidthValue();
}

rpl::producer<> RequestsBar::barClicks() const {
	return _barClicks.events();
}


} // namespace Ui
