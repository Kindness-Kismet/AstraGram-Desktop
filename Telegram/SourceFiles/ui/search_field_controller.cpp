/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/search_field_controller.h"

#include "styles/style_widgets.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "lang/lang_keys.h"

namespace Ui {

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
