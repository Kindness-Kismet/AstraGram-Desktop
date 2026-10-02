/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/chat_search_in.h"

#include "lang/lang_keys.h"
#include "menu/menu_checked_action.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/shadow.h"
#include "ui/dynamic_image.h"
#include "ui/painter.h"
#include "styles/style_dialogs.h"
#include "styles/style_menu_icons.h"
#include "styles/style_window.h"

namespace Dialogs {
namespace {

[[nodiscard]] QString TabLabel(
		ChatSearchTab tab,
		ChatSearchPeerTabType type = {}) {
	switch (tab) {
	case ChatSearchTab::MyMessages:
		return tr::lng_search_tab_my_messages(tr::now);
	case ChatSearchTab::ThisTopic:
		return tr::lng_search_tab_this_topic(tr::now);
	case ChatSearchTab::ThisPeer:
		switch (type) {
		case ChatSearchPeerTabType::Chat:
			return tr::lng_search_tab_this_chat(tr::now);
		case ChatSearchPeerTabType::Channel:
			return tr::lng_search_tab_this_channel(tr::now);
		case ChatSearchPeerTabType::Group:
			return tr::lng_search_tab_this_group(tr::now);
		}
		Unexpected("Type in Dialogs::TabLabel.");
	case ChatSearchTab::PublicPosts:
		return tr::lng_search_tab_public_posts(tr::now);
	case ChatSearchTab::Archive:
		return tr::lng_search_tab_archive(tr::now);
	case ChatSearchTab::ThisCommunity:
		return tr::lng_search_tab_this_community(tr::now);
	}
	Unexpected("Tab in Dialogs::TabLabel.");
}

} // namespace

FixedHashtagSearchQuery FixHashtagSearchQuery(
		const QString &query,
		int cursorPosition,
		HashOrCashtag tag) {
	const auto trimmed = query.trimmed();
	const auto hash = int(trimmed.isEmpty()
		? query.size()
		: query.indexOf(trimmed));
	const auto start = std::min(cursorPosition, hash);
	const auto first = QChar(tag == HashOrCashtag::Cashtag ? '$' : '#');
	auto result = query.mid(0, start);
	for (const auto &ch : query.mid(start)) {
		if (ch.isSpace()) {
			if (cursorPosition > result.size()) {
				--cursorPosition;
			}
			continue;
		} else if (result.size() == start) {
			result += first;
			if (ch != first) {
				++cursorPosition;
			}
		}
		if (ch != first) {
			result += ch;
		}
	}
	if (result.size() == start) {
		result += first;
		++cursorPosition;
	}
	return { result, cursorPosition };
}

HashOrCashtag IsHashOrCashtagSearchQuery(const QString &query) {
	const auto trimmed = query.trimmed();
	const auto first = trimmed.isEmpty() ? QChar() : trimmed[0];
	if (first == '#') {
		for (const auto &ch : trimmed) {
			if (ch.isSpace()) {
				return HashOrCashtag::None;
			}
		}
		return HashOrCashtag::Hashtag;
	} else if (first == '$') {
		for (auto it = trimmed.begin() + 1; it != trimmed.end(); ++it) {
			if ((*it) < 'A' || (*it) > 'Z') {
				return HashOrCashtag::None;
			}
		}
		return HashOrCashtag::Cashtag;
	}
	return HashOrCashtag::None;
}

void ChatSearchIn::Section::update() {
	outer->update();
}

ChatSearchIn::ChatSearchIn(QWidget *parent)
: RpWidget(parent) {
	_in.clicks.events() | rpl::on_next([=] {
		showMenu();
	}, lifetime());
	_type.clicks.events() | rpl::on_next([=] {
		showTypeMenu();
	}, lifetime());
}

void FillSearchTypeMenu(
		not_null<Ui::PopupMenu*> menu,
		Api::SearchFilter current,
		Fn<void(Api::SearchFilter)> callback) {
	const auto addAction = [&](Api::SearchFilter filter,
			const style::icon &icon,
			const QString &text) {
		Menu::AddCheckedAction(
			menu,
			text,
			[=] { callback(filter); },
			&icon,
			(current == filter));
	};
	addAction(
		Api::SearchFilter::NoFilter,
		st::menuIconTagFilter,
		tr::extras_SearchFilterAll(tr::now));
	addAction(
		Api::SearchFilter::Text,
		st::menuIconChatBubble,
		tr::extras_SearchFilterText(tr::now));
	addAction(
		Api::SearchFilter::Photo,
		st::menuIconPhoto,
		tr::extras_SearchFilterPhoto(tr::now));
	addAction(
		Api::SearchFilter::Video,
		st::menuIconVideoChat,
		tr::extras_SearchFilterVideo(tr::now));
	addAction(
		Api::SearchFilter::Voice,
		st::menuIconSoundOn,
		tr::extras_SearchFilterVoice(tr::now));
	addAction(
		Api::SearchFilter::Round,
		st::menuIconVideoChat,
		tr::extras_SearchFilterRound(tr::now));
	addAction(
		Api::SearchFilter::File,
		st::menuIconFile,
		tr::extras_SearchFilterFile(tr::now));
	addAction(
		Api::SearchFilter::Music,
		st::menuIconSoundSelect,
		tr::extras_SearchFilterMusic(tr::now));
	addAction(
		Api::SearchFilter::Gif,
		st::menuIconGif,
		tr::extras_SearchFilterGif(tr::now));
}

ChatSearchIn::~ChatSearchIn() = default;

void ChatSearchIn::apply(
		std::vector<PossibleTab> tabs,
		ChatSearchTab active,
		ChatSearchPeerTabType peerTabType,
		std::shared_ptr<Ui::DynamicImage> fromUserpic,
		QString fromName) {
	_tabs = std::move(tabs);
	_peerTabType = peerTabType;
	_active = active;
	const auto i = ranges::find(_tabs, active, &PossibleTab::tab);
	Assert(i != end(_tabs));
	Assert(i->icon != nullptr);
	updateSection(
		&_in,
		i->icon->clone(),
		tr::semibold(TabLabel(active, peerTabType)));

	auto text = tr::lng_dlg_search_from(
		tr::now,
		lt_user,
		tr::semibold(fromName),
		tr::marked);
	updateSection(&_from, std::move(fromUserpic), std::move(text));

	resizeToWidth(width());
}

rpl::producer<> ChatSearchIn::cancelInRequests() const {
	return _in.cancelRequests.events();
}

rpl::producer<> ChatSearchIn::cancelFromRequests() const {
	return _from.cancelRequests.events();
}

rpl::producer<> ChatSearchIn::changeFromRequests() const {
	return _from.clicks.events();
}

rpl::producer<> ChatSearchIn::cancelTypeRequests() const {
	return _type.cancelRequests.events();
}

rpl::producer<Api::SearchFilter> ChatSearchIn::typeChanges() const {
	return _typeFilterChanges.events();
}

rpl::producer<ChatSearchTab> ChatSearchIn::tabChanges() const {
	return _active.changes();
}

void ChatSearchIn::updateType(
		Api::SearchFilter filter,
		std::shared_ptr<Ui::DynamicImage> icon,
		QString name) {
	_typeFilter = filter;
	const auto accessibleName = tr::extras_SearchFilterType(tr::now)
		+ u": "_q
		+ name;
	updateSection(
		&_type,
		filter == Api::SearchFilter::NoFilter ? nullptr : std::move(icon),
		tr::marked(std::move(name)));
	if (_type.outer) {
		_type.outer->setAccessibleName(accessibleName);
	}
	resizeToWidth(width());
}

void ChatSearchIn::showTypeMenu() {
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	FillSearchTypeMenu(
		_menu.get(),
		_typeFilter,
		[=](Api::SearchFilter filter) {
			if (_typeFilter != filter) {
				_typeFilter = filter;
				_typeFilterChanges.fire_copy(filter);
			}
		});
	_menu->popup(QCursor::pos());
}

void ChatSearchIn::showMenu() {
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	const auto active = _active.current();
	for (const auto &tab : _tabs) {
		if (!tab.icon) {
			continue;
		}
		const auto value = tab.tab;
		Menu::AddCheckedAction(
			_menu.get(),
			TabLabel(value, _peerTabType),
			[=] { _active = value; },
			tab.icon,
			st::menuIconChats.width(),
			(value == active));
	}
	_menu->popup(_in.outer->mapToGlobal(QPoint(0, _in.outer->height())));
}

void ChatSearchIn::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	p.fillRect(rect(), st::dialogsBg);
}

int ChatSearchIn::resizeGetHeight(int newWidth) {
	auto result = 0;
	if (const auto raw = _in.outer.get()) {
		raw->resizeToWidth(newWidth);
		raw->move(0, result);
		result += raw->height();
		_in.shadow->setGeometry(0, result, newWidth, st::lineWidth);
		result += st::lineWidth;
	}
	if (const auto raw = _from.outer.get()) {
		raw->resizeToWidth(newWidth);
		raw->move(0, result);
		result += raw->height();
		_from.shadow->setGeometry(0, result, newWidth, st::lineWidth);
		result += st::lineWidth;
	}
	if (const auto raw = _type.outer.get()) {
		raw->resizeToWidth(newWidth);
		raw->move(0, result);
		result += raw->height();
		_type.shadow->setGeometry(0, result, newWidth, st::lineWidth);
		result += st::lineWidth;
	}
	return result;
}

void ChatSearchIn::updateSection(
		not_null<Section*> section,
		std::shared_ptr<Ui::DynamicImage> image,
		TextWithEntities text) {
	if (section->subscribed) {
		section->image->subscribeToUpdates(nullptr);
		section->subscribed = false;
	}
	if (!image) {
		if (section->outer) {
			section->cancel = nullptr;
			section->shadow = nullptr;
			section->outer = nullptr;
			section->subscribed = false;
		}
		return;
	} else if (!section->outer) {
		auto button = std::make_unique<Ui::AbstractButton>(this);
		const auto raw = button.get();
		section->outer = std::move(button);

		raw->resize(
			st::columnMinimalWidthLeft,
			st::dialogsSearchInHeight);

		raw->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(raw);
			if (!section->subscribed) {
				section->subscribed = true;
				section->image->subscribeToUpdates([=] {
					raw->update();
				});
			}
			const auto outer = raw->width();
			const auto size = st::dialogsSearchInPhotoSize;
			const auto left = st::dialogsSearchInPhotoPadding;
			const auto top = (st::dialogsSearchInHeight - size) / 2;
			p.drawImage(
				QRect{ left, top, size, size },
				section->image->image(size));

			const auto x = left + size + st::dialogsSearchInSkip;
			const auto available = outer
				- st::dialogsSearchInSkip
				- section->cancel->width()
				- 2 * st::dialogsSearchInDownSkip
				- st::dialogsSearchInDown.width()
				- x;
			const auto use = std::min(section->text.maxWidth(), available);
			const auto iconx = x + use + st::dialogsSearchInDownSkip;
			const auto icony = st::dialogsSearchInDownTop;
			st::dialogsSearchInDown.paint(p, iconx, icony, outer);
			p.setPen(st::windowBoldFg);
			section->text.draw(p, {
				.position = QPoint(x, st::dialogsSearchInNameTop),
				.outerWidth = outer,
				.availableWidth = available,
				.elisionLines = 1,
			});
		}, raw->lifetime());

		section->shadow = std::make_unique<Ui::PlainShadow>(this);
		section->shadow->show();

		const auto st = &st::dialogsCancelSearchInPeer;
		section->cancel = std::make_unique<Ui::IconButton>(raw, *st);
		section->cancel->setAccessibleName(tr::lng_cancel(tr::now));
		section->cancel->show();
		raw->sizeValue() | rpl::on_next([=](QSize size) {
			const auto left = size.width() - section->cancel->width();
			const auto top = (size.height() - st->height) / 2;
			section->cancel->moveToLeft(left, top);
		}, section->cancel->lifetime());
		section->cancel->clicks() | rpl::to_empty | rpl::start_to_stream(
			section->cancelRequests,
			section->cancel->lifetime());

		raw->clicks() | rpl::to_empty | rpl::start_to_stream(
			section->clicks,
			raw->lifetime());

		raw->show();
	}
	section->image = std::move(image);
	section->text.setMarkedText(st::dialogsSearchFromStyle, std::move(text));
}

} // namespace Dialogs
