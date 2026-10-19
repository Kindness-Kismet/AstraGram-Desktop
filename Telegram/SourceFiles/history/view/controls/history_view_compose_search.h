/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Dialogs {
class Key;
} // namespace Dialogs

namespace Window {
class SessionController;
} // namespace Window

namespace Api {
enum class SearchFilter;
} // namespace Api

namespace Ui {
class RpWidget;
} // namespace Ui

class History;

namespace HistoryView {

// 查找栏占用输入区胶囊的位置，宿主需按 height() 预留底部空间。
class ComposeSearch final {
public:
	ComposeSearch(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> window,
		not_null<History*> history,
		PeerData *from = nullptr,
		const QString &query = QString());
	~ComposeSearch();

	void hideAnimated();
	void setInnerFocus();
	void setQuery(const QString &query);
	void setFrom(PeerData *from);
	void raise();

	void setTopMsgId(MsgId topMsgId);
	void setSearchFilter(Api::SearchFilter filter);
	void setCalendarChat(const Dialogs::Key &chat);
	void setCalendarJumpHandler(Fn<void(FullMsgId, Fn<void()>)> jump);

	[[nodiscard]] int height() const;
	[[nodiscard]] rpl::producer<int> heightChanges() const;
	[[nodiscard]] bool listShown() const;
	[[nodiscard]] rpl::producer<bool> listShownValue() const;

	struct Activation {
		not_null<HistoryItem*> item;
		QString query;
	};
	[[nodiscard]] rpl::producer<Activation> activations() const;
	[[nodiscard]] rpl::producer<> destroyRequests() const;

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	class Inner;
	const std::unique_ptr<Inner> _inner;

};

} // namespace HistoryView
