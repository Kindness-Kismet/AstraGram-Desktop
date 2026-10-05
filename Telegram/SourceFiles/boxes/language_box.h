/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/box_content.h"
#include "base/binary_guard.h"
#include "settings/sections/settings_language.h"

struct LanguageId;

namespace Ui {
class MultiSelect;
struct ScrollToRequest;
class VerticalLayout;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

struct LanguageListContent {
	object_ptr<Ui::RpWidget> widget = { nullptr };
	Fn<void(const QString&)> filter;
	Fn<void()> submit;
	Fn<Ui::ScrollToRequest(int)> jump;
	rpl::producer<Ui::ScrollToRequest> scrollRequests;
	int rowHeight = 0;
};

[[nodiscard]] LanguageListContent CreateLanguageList(
	QWidget *parent,
	bool boxPadding = true);

class LanguageBox : public Ui::BoxContent {
public:
	LanguageBox(
		QWidget*,
		Window::SessionController *controller,
		const QString &highlightId = QString());

	void setInnerFocus() override;

	[[nodiscard]] static base::binary_guard Show(
		Window::SessionController *controller,
		const QString &highlightId = QString());

protected:
	void prepare() override;
	void showFinished() override;

	void keyPressEvent(QKeyEvent *e) override;

private:
	void setupTop(not_null<Ui::VerticalLayout*> container);
	[[nodiscard]] int rowsInPage() const;

	Window::SessionController *_controller = nullptr;
	QString _highlightId;
	rpl::event_stream<> _showFinished;
	Fn<void()> _setInnerFocus;
	Fn<Ui::ScrollToRequest(int rows)> _jump;

};
