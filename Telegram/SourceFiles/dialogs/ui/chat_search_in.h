/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_search_filter.h"
#include "base/unique_qptr.h"
#include "ui/rp_widget.h"
#include "ui/style/style_core_types.h"

namespace Ui {
class PlainShadow;
class DynamicImage;
class IconButton;
class PopupMenu;
} // namespace Ui

namespace Dialogs {

enum class ChatSearchTab : uchar {
	MyMessages,
	ThisTopic,
	ThisPeer,
	PublicPosts,
	Archive,
	ThisCommunity,
};

enum class ChatSearchPeerTabType : uchar {
	Chat,
	Channel,
	Group,
};

void FillSearchTypeMenu(
	not_null<Ui::PopupMenu*> menu,
	Api::SearchFilter current,
	Fn<void(Api::SearchFilter)> callback);

// 类型筛选的菜单图标与名称；置顶类型由置顶消息页固定，没有名称。
[[nodiscard]] const style::icon &SearchFilterIcon(Api::SearchFilter filter);
[[nodiscard]] QString SearchFilterName(Api::SearchFilter filter);

class ChatSearchIn final : public Ui::RpWidget {
public:
	explicit ChatSearchIn(QWidget *parent);
	~ChatSearchIn();

	struct PossibleTab {
		ChatSearchTab tab = {};
		std::shared_ptr<Ui::DynamicImage> icon;
	};
	void apply(
		std::vector<PossibleTab> tabs,
		ChatSearchTab active,
		ChatSearchPeerTabType peerTabType,
		std::shared_ptr<Ui::DynamicImage> fromUserpic,
		QString fromName);
	void updateType(
		Api::SearchFilter filter,
		std::shared_ptr<Ui::DynamicImage> icon,
		QString name);

	[[nodiscard]] rpl::producer<> cancelInRequests() const;
	[[nodiscard]] rpl::producer<> cancelFromRequests() const;
	[[nodiscard]] rpl::producer<> changeFromRequests() const;
	[[nodiscard]] rpl::producer<> cancelTypeRequests() const;
	[[nodiscard]] rpl::producer<Api::SearchFilter> typeChanges() const;
	[[nodiscard]] rpl::producer<ChatSearchTab> tabChanges() const;

private:
	struct Section {
		std::unique_ptr<Ui::RpWidget> outer;
		std::unique_ptr<Ui::IconButton> cancel;
		std::unique_ptr<Ui::PlainShadow> shadow;
		std::shared_ptr<Ui::DynamicImage> image;
		Ui::Text::String text;
		rpl::event_stream<> clicks;
		rpl::event_stream<> cancelRequests;
		bool subscribed = false;

		void update();
	};

	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void showMenu();
	void showTypeMenu();

	void updateSection(
		not_null<Section*> section,
		std::shared_ptr<Ui::DynamicImage> image,
		TextWithEntities text);

	Section _in;
	Section _from;
	Section _type;
	Api::SearchFilter _typeFilter = {};
	rpl::event_stream<Api::SearchFilter> _typeFilterChanges;
	rpl::variable<ChatSearchTab> _active;

	base::unique_qptr<Ui::PopupMenu> _menu;

	std::vector<PossibleTab> _tabs;
	ChatSearchPeerTabType _peerTabType = ChatSearchPeerTabType::Chat;

};

enum class HashOrCashtag : uchar {
	None,
	Hashtag,
	Cashtag,
};

struct FixedHashtagSearchQuery {
	QString text;
	int cursorPosition = 0;
};
[[nodiscard]] FixedHashtagSearchQuery FixHashtagSearchQuery(
	const QString &query,
	int cursorPosition,
	HashOrCashtag tag);

[[nodiscard]] HashOrCashtag IsHashOrCashtagSearchQuery(const QString &query);

} // namespace Dialogs
