/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/variable.h>
#include "ui/rp_widget.h"
#include "base/unique_qptr.h"

namespace style {
struct SearchFieldRow;
struct InputField;
} // namespace style

namespace Ui {

class CrossButton;
class InputField;
class IconButton;

// 轻量查询固定在内容首行，入口按钮由所属标题或工具栏提供。
class ContentSearchRow final : public RpWidget {
public:
	ContentSearchRow(
		QWidget *parent,
		rpl::producer<QString> placeholder,
		const QString &query = QString());
	[[nodiscard]] InputField *field() const;
	void setExpanded(bool expanded, bool focus = true);
	[[nodiscard]] bool expanded() const;
	[[nodiscard]] rpl::producer<bool> expandedValue() const;
	void cancel();

protected:
	int resizeGetHeight(int newWidth) override;

private:
	InputField *_field = nullptr;
	CrossButton *_cancel = nullptr;
	rpl::variable<bool> _expanded = false;
};

void setupContentSearchField(not_null<InputField*> field);

class SearchFieldController {
public:
	SearchFieldController(const QString &query);

	base::unique_qptr<Ui::InputField> createField(
		QWidget *parent,
		const style::InputField &st);
	struct RowView {
		base::unique_qptr<Ui::RpWidget> wrap;
		QPointer<Ui::InputField> field;
	};
	RowView createRowView(
		QWidget *parent,
		const style::SearchFieldRow &st);

	QString query() const;
	rpl::producer<QString> queryValue() const;
	rpl::producer<QString> queryChanges() const;

	void setQuery(const QString &query);

	rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	base::unique_qptr<QWidget> _view;
	rpl::variable<QString> _query;

	rpl::lifetime _lifetime;

};

} // namespace Ui
