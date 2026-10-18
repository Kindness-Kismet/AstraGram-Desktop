/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "ui/wrap/slide_wrap.h"

#include <memory>

namespace Ui {
class CrossButton;
class FlatLabel;
class IconButton;
class InputField;
} // namespace Ui

namespace Iv {

enum class SearchBarMode : uchar {
	WindowBottom,
	EditorContent,
};

class SearchBar final {
public:
	SearchBar(
		not_null<QWidget*> parent,
		rpl::producer<int> width,
		SearchBarMode mode = SearchBarMode::WindowBottom);

	void toggle(bool shown, anim::type animated);
	void show(anim::type animated);
	void hide(anim::type animated);
	[[nodiscard]] bool shown() const;
	void setInnerFocus();
	void raise();
	void move(int x, int y);

	void setResults(int current, int total);

	[[nodiscard]] rpl::producer<QString> queryChanges() const;
	[[nodiscard]] rpl::producer<int> navigateRequests() const;
	[[nodiscard]] rpl::producer<> closeRequests() const;
	[[nodiscard]] rpl::producer<bool> focusChanges() const;
	[[nodiscard]] rpl::producer<int> heightValue() const;

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void setup(rpl::producer<int> width);
	void updateControlsGeometry();

	const SearchBarMode _mode;
	Ui::SlideWrap<Ui::RpWidget> _wrap;
	Ui::RpWidget *_surface = nullptr;
	Ui::InputField *_field = nullptr;
	Ui::FlatLabel *_counter = nullptr;
	Ui::IconButton *_up = nullptr;
	Ui::IconButton *_down = nullptr;
	Ui::CrossButton *_close = nullptr;
	Ui::IconButton *_windowClose = nullptr;
	rpl::event_stream<QString> _queryChanges;
	rpl::event_stream<int> _navigates;
	rpl::event_stream<> _closeRequests;
	rpl::event_stream<bool> _focusChanges;

};

} // namespace Iv
