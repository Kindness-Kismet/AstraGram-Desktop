/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/search_field_controller.h"

#include "styles/style_widgets.h"
#include "styles/style_td_common.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "lang/lang_keys.h"

namespace Ui {

void setupContentSearchField(not_null<InputField*> field) {
	const auto icon = CreateChild<IconButton>(field, st::contentSearchOpen);
	icon->setObjectName(u"contentSearch.icon"_q);
	icon->setAccessibleName(tr::lng_dlg_filter(tr::now));
	icon->setClickedCallback([=] { field->setFocusFast(); });
	field->heightValue() | rpl::on_next([=](int height) {
		icon->moveToLeft(0, (height - icon->height()) / 2);
		icon->raise();
	}, icon->lifetime());
}

ContentSearchRow::ContentSearchRow(
	QWidget *parent,
	rpl::producer<QString> placeholder,
	const QString &query)
: RpWidget(parent)
, _field(CreateChild<InputField>(this, st::contentSearchField,
	std::move(placeholder), query))
, _cancel(CreateChild<CrossButton>(this, st::contentSearchClose)) {
	setObjectName(u"contentSearch.row"_q);
	_field->setObjectName(u"contentSearch.field"_q);
	_field->customUpDown(true);
	setupContentSearchField(_field);
	_cancel->setObjectName(u"contentSearch.close"_q);
	_cancel->show(anim::type::instant);
	_cancel->setClickedCallback([=] { cancel(); });
	_field->cancelled() | rpl::on_next([=] { cancel(); }, lifetime());
	_field->changes() | rpl::on_next([=] {
		_cancel->setAccessibleName(_field->empty()
			? tr::lng_sr_cancel_search(tr::now)
			: tr::lng_sr_clear_search(tr::now));
	}, lifetime());
	_cancel->setAccessibleName(tr::lng_sr_cancel_search(tr::now));
	setExpanded(!query.isEmpty(), false);
	resizeToWidth(width());
}

InputField *ContentSearchRow::field() const {
	return _field;
}

bool ContentSearchRow::expanded() const {
	return _expanded.current();
}

rpl::producer<bool> ContentSearchRow::expandedValue() const {
	return _expanded.value();
}

void ContentSearchRow::setExpanded(bool expanded, bool focus) {
	_expanded = expanded;
	_field->setVisible(expanded);
	_cancel->setVisible(expanded);
	resizeToWidth(width());
	if (expanded && focus) {
		_field->setFocusFast();
	}
}

void ContentSearchRow::cancel() {
	if (!_field->empty()) {
		_field->clear();
		_field->setFocusFast();
		return;
	}
	setExpanded(false);
	parentWidget()->setFocus();
}

int ContentSearchRow::resizeGetHeight(int newWidth) {
	const auto padding = st::contentSearchPadding;
	_field->resizeToWidth(std::max(newWidth - 2 * padding, 0));
	_field->moveToLeft(padding, style::ConvertScale(4));
	_cancel->moveToRight(padding, style::ConvertScale(4), newWidth);
	return expanded() ? st::contentSearchHeight : 0;
}

SearchFieldController::SearchFieldController(const QString &query)
: _query(query) {
}

auto SearchFieldController::createRowView(
		QWidget *parent,
		const style::SearchFieldRow &st) -> RowView {
	auto result = base::make_unique_q<Ui::FixedHeightWidget>(
		parent,
		st.height);
	auto wrap = result.get();

	auto field = createField(wrap, st.field).release();
	field->show();

	auto cancel = CreateChild<Ui::CrossButton>(
		wrap,
		st.fieldCancel);
	cancel->addClickHandler([=] {
		field->setText(QString());
	});
	queryValue(
	) | rpl::map([](const QString &value) {
		return !value.isEmpty();
	}) | rpl::on_next([cancel](bool shown) {
		cancel->toggle(shown, anim::type::normal);
		cancel->setAccessibleName(tr::lng_sr_clear_search(tr::now));
	}, cancel->lifetime());
	cancel->finishAnimating();

	wrap->widthValue(
	) | rpl::on_next([=, &st](int newWidth) {
		const auto availableWidth = newWidth
			- st.padding.left()
			- st.padding.right();
		field->setGeometryToLeft(
			st.padding.left(),
			(st.height - field->height()) / 2,
			availableWidth,
			field->height());
		cancel->moveToRight(st.padding.right(), (st.height - cancel->height()) / 2);
	}, wrap->lifetime());

	_view.release();
	_view.reset(wrap);
	return { std::move(result), field };
}

QString SearchFieldController::query() const {
	return _query.current();
}

rpl::producer<QString> SearchFieldController::queryValue() const {
	return _query.value();
}

rpl::producer<QString> SearchFieldController::queryChanges() const {
	return _query.changes();
}

void SearchFieldController::setQuery(const QString &query) {
	_query = query;
}

base::unique_qptr<Ui::InputField> SearchFieldController::createField(
		QWidget *parent,
		const style::InputField &st) {
	auto result = base::make_unique_q<Ui::InputField>(
		parent,
		st,
		tr::lng_dlg_filter(),
		_query.current());
	auto field = result.get();
	field->changes(
	) | rpl::on_next([=] {
		_query = field->getLastText();
	}, field->lifetime());
	_view.reset(field);
	return result;
}

} // namespace Ui
