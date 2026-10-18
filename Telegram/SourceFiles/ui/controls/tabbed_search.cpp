/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/tabbed_search.h"
#include "ui/search_field_controller.h"
#include "styles/style_td_common.h"

#include "lang/lang_keys.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/rect.h"
#include "ui/ui_utility.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_widgets.h"

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
	const style::TabbedSearch &st,
	bool content)
: RpWidget(parent)
, _st(st)
, _content(content)
, _fieldStyle([&] {
	auto result = content ? st::contentSearchField : _st.field;
	result.search = false;
	if (!content) {
		result.textBg = result.textBgActive = st::transparent;
		result.border = result.borderActive = result.borderRadius = 0;
	}
	return result;
}())
, _field(CreateChild<InputField>(this, _fieldStyle, tr::lng_dlg_filter()))
, _cancel(CreateChild<CrossButton>(this,
	content ? st::contentSearchClose : _st.cancel))
, _debounceTimer([=] { _debouncedQuery = _query.current(); }) {
	_field->setObjectName(u"panelSearchField"_q);
	_cancel->setObjectName(u"panelSearchClear"_q);
	_cancel->setAccessibleName(tr::lng_sr_cancel_search(tr::now));
	_cancel->toggle(false, anim::type::instant);
	if (_content) {
		setupContentSearchField(_field);
		_field->hide();
		_open = CreateChild<IconButton>(this, st::contentSearchOpen);
		_open->setObjectName(u"contentSearch.open"_q);
		_open->setClickedCallback([=] {
			_expanded = true;
			_field->show();
			_open->hide();
			_field->setFocusFast();
			updateCancelShown();
		});
	}

	_field->changes(
	) | rpl::on_next([=] {
		const auto query = FieldQuery(_field);
		_query = query;
		updateCancelShown();
		if (query.empty()) {
			_debounceTimer.cancel();
			_debouncedQuery = query;
		} else {
			_debounceTimer.callOnce(kDebounceTimeout);
		}
	}, _field->lifetime());
	_field->focusedChanges() | rpl::on_next([=] { update(); }, lifetime());

	_cancel->setClickedCallback([=] {
		if (_content && _query.current().empty()) {
			_expanded = false;
			_field->hide();
			_open->show();
			_cancel->hide(anim::type::instant);
			dropFocus();
			return;
		}
		const auto focused = _field->hasFocus();
		cancel();
		if (focused) {
			dropFocus();
		}
	});

	_field->focusedChanges(
	) | rpl::on_next([=] {
		updateCancelShown();
	}, _field->lifetime());

	// 记下聚焦前的控件供交还；焦点移到别处就作废，窗口失活不算。
	QObject::connect(qApp, &QApplication::focusChanged, this, [=](
			QWidget *was,
			QWidget *now) {
		const auto inside = [&](QWidget *widget) {
			return widget && (widget == this || isAncestorOf(widget));
		};
		if (inside(now) && was && !inside(was)) {
			if (!_focusTakenFrom) {
				_focusTakenFrom = was;
			}
		} else if (inside(was) && now && !inside(now)) {
			_focusTakenFrom = nullptr;
		}
	});
}

// 与对话列表搜索框一致：聚焦或有内容时显示 X，点击即可退出输入框。
void TabbedSearchField::updateCancelShown() {
	const auto empty = _query.current().empty();
	_cancel->toggle(_content ? _expanded : (!empty || _field->hasFocus()),
		anim::type::normal);
	_cancel->setAccessibleName(empty
		? tr::lng_sr_cancel_search(tr::now)
		: tr::lng_sr_clear_search(tr::now));
}

// 焦点交还给聚焦前的控件；它已不可见或在别的窗口时由自身接住。
void TabbedSearchField::dropFocus() {
	const auto to = base::take(_focusTakenFrom);
	if (to && to->isVisible() && to->window() == window()) {
		to->setFocus();
	} else {
		setFocus();
	}
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
	if (_content && !_expanded) {
		setFocus();
		return;
	}
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

void TabbedSearchField::paintEvent(QPaintEvent *e) {
	if (_content) {
		return;
	}
	auto p = QPainter(this);
	// 底色和输入内容共用切页缓存坐标，避免图形特效分离两者。
	InputField::paintSearchBackground(p, _field->geometry(), _st.field,
		_field->hasFocus() ? 1. : 0., underMouse());
	st::defaultSearchFieldIcon.paint(p,
		_field->x() + st::searchFieldPadding,
		_field->y() + (_field->height() - st::defaultSearchFieldIcon.height()) / 2,
		width());
}

int TabbedSearchField::resizeGetHeight(int newWidth) {
	const auto margins = getMargins();
	const auto outerWidth = newWidth + margins.left() + margins.right();
	_field->resizeToWidth(newWidth);
	_field->moveToLeft(margins.left(), margins.top(), outerWidth);
	_cancel->moveToRight(margins.right(), margins.top(), outerWidth);
	if (_open) {
		_open->moveToRight(0, 0, outerWidth);
	}
	return _st.height;
}

QMargins TabbedSearchField::getMargins() const {
	if (_content) {
		return {};
	}
	// 外层控件覆盖完整阴影，内容尺寸仍按输入框本身计算。
	const auto tight = BoxShadow::ExtendFor(st::searchFieldShadowTight);
	const auto wide = BoxShadow::ExtendFor(st::searchFieldShadowWide);
	return {
		std::max(tight.left(), wide.left()),
		std::max(tight.top(), wide.top()),
		std::max(tight.right(), wide.right()),
		std::max(tight.bottom(), wide.bottom()),
	};
}

TabbedSearch::TabbedSearch(
	not_null<RpWidget*> parent,
	const style::EmojiPan &st)
: _st(st)
, _search(parent, st.search) {
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
	_search.moveToLeft(_st.searchMargin.left(), _st.searchMargin.top());
}

int TabbedSearch::height() const {
	return _search.heightNoMargins() + rect::m::sum::v(_st.searchMargin);
}

QPoint TabbedSearch::position() const {
	return _search.pos();
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
