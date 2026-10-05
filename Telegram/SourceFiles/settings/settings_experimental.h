/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_common_session.h"

#ifdef _DEBUG
namespace base::options::details {
class BasicOption;
} // namespace base::options::details
#endif

namespace Settings {

#ifdef _DEBUG
// 调试命令与实验设置页共用选项清单。
[[nodiscard]] std::vector<not_null<base::options::details::BasicOption*>> experimentalOptionsForDebug();
#endif

class Experimental : public Section<Experimental> {
public:
	Experimental(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~Experimental();

	[[nodiscard]] rpl::producer<QString> title() override;
	void fillTopBarMenu(const Ui::Menu::MenuCallback &addAction) override;
	void showFinished() override;

private:
	void setupContent();

	rpl::event_stream<> _reloadOptionsRequests;
	std::vector<std::pair<QString, QPointer<QWidget>>> _highlights;

};

} // namespace Settings
