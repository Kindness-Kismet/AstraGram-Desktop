/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "ui/rp_widget.h"
#include <rpl/variable.h>

namespace style {
struct EmojiPan;
struct TabbedSearch;
} // namespace style

namespace Ui {

class InputField;
class IconButton;
class CrossButton;

class TabbedSearchField final : public RpWidget {
public:
	TabbedSearchField(QWidget *parent, const style::TabbedSearch &st);

	[[nodiscard]] rpl::producer<> escapes() const;
	[[nodiscard]] rpl::producer<std::vector<QString>> queryValue() const;
	[[nodiscard]] auto debouncedQueryValue() const
		-> rpl::producer<std::vector<QString>>;
	[[nodiscard]] not_null<InputField*> field() const {
		return _field;
	}

	void cancel();
	void setLoading(bool loading);
	void stealFocus();
	void returnFocus();

private:
	int resizeGetHeight(int newWidth) override;
	void updateCancelShown();
	void dropFocus();

	const style::TabbedSearch &_st;
	not_null<InputField*> _field;
	not_null<IconButton*> _search;
	not_null<CrossButton*> _cancel;
	QPointer<QWidget> _focusTakenFrom;

	rpl::variable<std::vector<QString>> _query;
	rpl::variable<std::vector<QString>> _debouncedQuery;
	base::Timer _debounceTimer;

};

class TabbedSearch final {
public:
	TabbedSearch(
		not_null<RpWidget*> parent,
		const style::EmojiPan &st);

	[[nodiscard]] int height() const;
	[[nodiscard]] QImage grab();

	[[nodiscard]] rpl::producer<> escapes() const;
	[[nodiscard]] rpl::producer<std::vector<QString>> queryValue() const;
	[[nodiscard]] auto debouncedQueryValue() const
		->rpl::producer<std::vector<QString>>;

	void cancel();
	void setLoading(bool loading);
	void stealFocus();
	void returnFocus();
	void setRightReserved(int value);

private:
	void updateSearchGeometry();

	const style::EmojiPan &_st;
	TabbedSearchField _search;
	int _outerWidth = 0;
	int _rightReserved = 0;

};

} // namespace Ui
