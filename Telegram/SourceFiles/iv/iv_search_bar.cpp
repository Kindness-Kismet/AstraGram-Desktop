/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/iv_search_bar.h"

#include "chat_helpers/message_field.h"
#include "extras/features/window_material/window_material.h"
#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/search_field_controller.h"
#include "styles/style_td_common.h"
#include "ui/widgets/fields/input_field.h"
#include "extras/ui/components/floating_surface.h"
#include "ui/painter.h"

#include "styles/palette.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_iv.h"
#include "styles/style_widgets.h"

#include <QtGui/QPainter>

namespace Iv {

SearchBar::SearchBar(
	not_null<QWidget*> parent,
	rpl::producer<int> width,
	SearchBarMode mode)
: _mode(mode)
, _wrap(parent, object_ptr<Ui::RpWidget>(parent.get())) {
	_wrap.setObjectName(u"iv.search"_q);
	_wrap.setDirectionUp(true);
	setup(std::move(width));
	_wrap.hide(anim::type::instant);
}

void SearchBar::setup(rpl::producer<int> width) {
	const auto inner = _wrap.entity();
	ExtrasFeatures::WindowMaterial::watchSurface(inner);
	const auto content = (_mode == SearchBarMode::EditorContent);
	inner->resize(inner->width(), content
		? st::contentSearchHeight
		: (st::historyComposeSearchButton.height
			+ 2 * st::historyComposeCapsuleMargin));
	if (!content) {
		_surface = Ui::CreateChild<Ui::RpWidget>(inner);
		_surface->setObjectName(u"iv.search.surface"_q);
		_surface->setAttribute(Qt::WA_TransparentForMouseEvents);
		ExtrasUi::FloatingSurface::attach(_surface, {
			.radius = MessageFieldCapsuleRadius(),
			.background = ExtrasUi::chatSurfaceBackground,
			.border = ExtrasUi::chatSurfaceBorder,
			.borderWidth = st::lineWidth,
		});
		_surface->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(_surface);
			const auto &icon = st::historyComposeSearchIcon;
			icon.paint(p, st::historyComposeSearchIconLeft,
				(_surface->height() - icon.height()) / 2, _surface->width());
		}, _surface->lifetime());
	}
	_field = Ui::CreateChild<Ui::InputField>(inner,
		content ? st::contentSearchField : st::historyComposeSearchField,
		tr::lng_dlg_filter());
	_field->setObjectName(u"iv.search.field"_q);
	_field->setFocusPolicy(Qt::StrongFocus);
	_field->customUpDown(true);
	if (content) {
		Ui::setupContentSearchField(_field);
	}
	_counter = Ui::CreateChild<Ui::FlatLabel>(
		inner,
		QString(),
		st::ivSearchCounterLabel);
	_counter->setAttribute(Qt::WA_TransparentForMouseEvents);
	_counter->hide();
	const auto &upSt = content
		? st::ivEditorSearchPrevious
		: st::historyComposeSearchOlder;
	const auto &downSt = content
		? st::ivEditorSearchNext
		: st::historyComposeSearchNewer;
	_up = Ui::CreateChild<Ui::IconButton>(inner, upSt);
	_down = Ui::CreateChild<Ui::IconButton>(inner, downSt);
	if (content) {
		_close = Ui::CreateChild<Ui::CrossButton>(inner, st::contentSearchClose);
		_close->setObjectName(u"iv.search.close"_q);
		_close->show(anim::type::instant);
	} else {
		_windowClose = Ui::CreateChild<Ui::IconButton>(
			inner, st::historyComposeSearchClose);
		_windowClose->setObjectName(u"iv.search.close"_q);
	}
	_up->setObjectName(u"iv.search.previous"_q);
	_down->setObjectName(u"iv.search.next"_q);

	std::move(width) | rpl::on_next([=](int width) {
		_wrap.resizeToWidth(width);
	}, _wrap.lifetime());

	inner->sizeValue(
	) | rpl::on_next([=] {
		updateControlsGeometry();
	}, inner->lifetime());

	_field->changes() | rpl::on_next([=] {
		_queryChanges.fire_copy(_field->getLastText());
	}, _field->lifetime());
	_field->submits() | rpl::on_next([=](Qt::KeyboardModifiers modifiers) {
		_navigates.fire((modifiers & Qt::ShiftModifier) ? -1 : 1);
	}, _field->lifetime());
	_field->cancelled() | rpl::on_next([=] {
		_closeRequests.fire({});
	}, _field->lifetime());
	_field->focusedChanges() | rpl::on_next([=](bool focused) {
		_focusChanges.fire_copy(focused);
	}, _field->lifetime());

	setResults(0, 0);
}

void SearchBar::updateControlsGeometry() {
	const auto inner = _wrap.entity();
	const auto width = inner->width();
	if (_mode == SearchBarMode::EditorContent) {
		const auto centered = [&](int h) { return (inner->height() - h) / 2; };
		auto right = st::contentSearchPadding;
		_close->moveToRight(right, centered(_close->height()));
		right += _close->width();
		_down->moveToRight(right, centered(_down->height()));
		right += _down->width();
		_up->moveToRight(right, centered(_up->height()));
		right += _up->width();
		if (!_counter->isHidden()) {
			right += st::ivSearchBarCounterSkip;
			_counter->moveToRight(right, centered(_counter->height()));
			right += _counter->width() + st::ivSearchBarCounterSkip;
		}
		_field->resizeToWidth(std::max(0, width - st::contentSearchPadding - right));
		_field->moveToLeft(st::contentSearchPadding, centered(_field->height()));
		return;
	}
	const auto margin = st::historyComposeCapsuleMargin;
	const auto rowHeight = st::historyComposeSearchButton.height;
	_surface->setGeometry(margin, margin, width - 2 * margin, rowHeight);
	const auto centered = [&](int controlHeight) {
		return margin + (rowHeight - controlHeight) / 2;
	};
	auto right = margin + st::historyComposeCapsulePadding;
	const auto placeRight = [&](not_null<Ui::RpWidget*> widget) {
		widget->moveToRight(right, centered(widget->height()));
		right += widget->width();
	};
	placeRight(_windowClose);
	placeRight(_down);
	placeRight(_up);
	if (!_counter->isHidden()) {
		right += st::historyComposeSearchCounterSkip;
		_counter->moveToRight(right, centered(_counter->height()));
		right += _counter->width() + st::historyComposeSearchCounterSkip;
	}
	const auto left = margin + st::historyComposeSearchIconLeft
		+ st::historyComposeSearchIcon.width()
		+ st::historyComposeSearchChipSkip;
	_field->resizeToWidth(width - left - right);
	_field->moveToLeft(left, centered(_field->height()));
}

void SearchBar::setResults(int current, int total) {
	if (total > 0) {
		_counter->setText(u"%1 / %2"_q.arg(current).arg(total));
		_counter->show();
	} else {
		_counter->hide();
	}
	const auto disabled = (total <= 0);
	const auto content = (_mode == SearchBarMode::EditorContent);
	const auto upIcon = disabled
		? (content ? &st::ivEditorSearchPrevious.icon
			: &st::historyComposeSearchOlderDisabled)
		: nullptr;
	const auto downIcon = disabled
		? (content ? &st::ivEditorSearchNext.icon
			: &st::historyComposeSearchNewerDisabled)
		: nullptr;
	_up->setIconOverride(upIcon, upIcon);
	_down->setIconOverride(downIcon, downIcon);
	_up->setAttribute(Qt::WA_TransparentForMouseEvents, disabled);
	_down->setAttribute(Qt::WA_TransparentForMouseEvents, disabled);
	updateControlsGeometry();
}

void SearchBar::toggle(bool shown, anim::type animated) {
	_wrap.toggle(shown, animated);
}

void SearchBar::show(anim::type animated) {
	toggle(true, animated);
}

void SearchBar::hide(anim::type animated) {
	toggle(false, animated);
}

bool SearchBar::shown() const {
	return _wrap.toggled();
}

void SearchBar::setInnerFocus() {
	_field->setFocusFast();
}

void SearchBar::raise() {
	_wrap.raise();
}

void SearchBar::move(int x, int y) {
	_wrap.move(x, y);
}

rpl::producer<QString> SearchBar::queryChanges() const {
	return _queryChanges.events();
}

rpl::producer<int> SearchBar::navigateRequests() const {
	return rpl::merge(
		_navigates.events(),
		_up->clicks() | rpl::map_to(-1),
		_down->clicks() | rpl::map_to(1));
}

rpl::producer<> SearchBar::closeRequests() const {
	return rpl::merge(
		_closeRequests.events(),
		(_windowClose ? _windowClose->clicks() : _close->clicks())
			| rpl::to_empty);
}

rpl::producer<bool> SearchBar::focusChanges() const {
	return _focusChanges.events();
}

rpl::producer<int> SearchBar::heightValue() const {
	return _wrap.heightValue();
}

rpl::lifetime &SearchBar::lifetime() {
	return _wrap.lifetime();
}

} // namespace Iv
