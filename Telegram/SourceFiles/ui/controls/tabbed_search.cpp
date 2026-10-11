/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/tabbed_search.h"

#include "lang/lang_keys.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/rect.h"
#include "ui/ui_utility.h"
#include "styles/style_chat_helpers.h"

#include <QtWidgets/QApplication>

namespace Ui {
namespace {

constexpr auto kDebounceTimeout = crl::time(400);

[[nodiscard]] std::vector<QString> FieldQuery(not_null<InputField*> field) {
	if (const auto last = field->getLastText().trimmed(); !last.isEmpty()) {
		return { last };
	}
	return {};
}

} // namespace

TabbedSearchField::TabbedSearchField(
	QWidget *parent,
	const style::TabbedSearch &st)
: RpWidget(parent)
, _st(st)
, _field(CreateChild<InputField>(this, _st.field, tr::lng_dlg_filter()))
, _search(CreateChild<IconButton>(this, _st.search))
, _cancel(CreateChild<CrossButton>(this, _st.cancel))
, _debounceTimer([=] { _debouncedQuery = _query.current(); }) {
	_field->setObjectName(u"panelSearchField"_q);
	_search->setObjectName(u"panelSearchIcon"_q);
	_search->setAccessibleName(tr::lng_dlg_filter(tr::now));
	_cancel->setObjectName(u"panelSearchClear"_q);
	_cancel->setAccessibleName(tr::lng_sr_clear_search(tr::now));
	_cancel->toggle(false, anim::type::instant);

	_field->changes(
	) | rpl::on_next([=] {
		const auto query = FieldQuery(_field);
		_query = query;
		_cancel->toggle(!query.empty(), anim::type::normal);
		if (query.empty()) {
			_debounceTimer.cancel();
			_debouncedQuery = query;
		} else {
			_debounceTimer.callOnce(kDebounceTimeout);
		}
	}, _field->lifetime());

	_search->setClickedCallback([=] {
		_field->setFocus();
	});
	_cancel->setClickedCallback([=] {
		cancel();
	});
}

rpl::producer<> TabbedSearchField::escapes() const {
	return _field->cancelled();
}

rpl::producer<std::vector<QString>> TabbedSearchField::queryValue() const {
	return _query.value();
}

auto TabbedSearchField::debouncedQueryValue() const
-> rpl::producer<std::vector<QString>> {
	return _debouncedQuery.value();
}

void TabbedSearchField::cancel() {
	_field->setText(QString());
}

void TabbedSearchField::setLoading(bool loading) {
	_cancel->setLoadingAnimation(loading);
}

void TabbedSearchField::stealFocus() {
	if (!_focusTakenFrom) {
		_focusTakenFrom = QApplication::focusWidget();
	}
	_field->setFocus();
}

void TabbedSearchField::returnFocus() {
	if (!_focusTakenFrom) {
		return;
	}
	if (_field->hasFocus()) {
		_focusTakenFrom->setFocus();
	}
	_focusTakenFrom = nullptr;
}

int TabbedSearchField::resizeGetHeight(int newWidth) {
	_field->resizeToWidth(newWidth);
	_field->moveToLeft(0, 0, newWidth);
	_search->moveToLeft(0, 0, newWidth);
	_cancel->moveToRight(0, 0, newWidth);
	return _st.height;
}

TabbedSearch::TabbedSearch(
	not_null<RpWidget*> parent,
	const style::EmojiPan &st)
: _st(st)
, _search(parent, st.search) {
	_search.move(_st.searchMargin.left(), _st.searchMargin.top());

	parent->widthValue(
	) | rpl::on_next([=](int width) {
		_outerWidth = width;
		updateSearchGeometry();
	}, _search.lifetime());
}

void TabbedSearch::updateSearchGeometry() {
	const auto inner = _outerWidth
		- rect::m::sum::h(_st.searchMargin)
		- _rightReserved;
	_search.resizeToWidth(std::max(inner, 0));
}

int TabbedSearch::height() const {
	return _search.height() + rect::m::sum::v(_st.searchMargin);
}

QImage TabbedSearch::grab() {
	return Ui::GrabWidgetToImage(&_search);
}

void TabbedSearch::cancel() {
	_search.cancel();
}

void TabbedSearch::setLoading(bool loading) {
	_search.setLoading(loading);
}

void TabbedSearch::stealFocus() {
	_search.stealFocus();
}

void TabbedSearch::returnFocus() {
	_search.returnFocus();
}

void TabbedSearch::setRightReserved(int value) {
	if (_rightReserved == value) {
		return;
	}
	_rightReserved = value;
	updateSearchGeometry();
}

rpl::producer<> TabbedSearch::escapes() const {
	return _search.escapes();
}

rpl::producer<std::vector<QString>> TabbedSearch::queryValue() const {
	return _search.queryValue();
}

auto TabbedSearch::debouncedQueryValue() const
-> rpl::producer<std::vector<QString>> {
	return _search.debouncedQueryValue();
}

} // namespace Ui
