#include "extras/ui/components/profile_attachments.h"

#include "apiwrap.h"
#include "base/call_delayed.h"
#include "base/qt/qt_key_modifiers.h"
#include "boxes/choose_filter_box.h"
#include "core/click_handler_types.h"
#include "core/ui_integration.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_chat_filters.h"
#include "data/data_community.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "extras/extras_settings.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "history/view/history_view_item_preview.h"
#include "info/info_controller.h"
#include "info/profile/info_profile_values.h"
#include "info/saved/info_saved_music_common.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/ui_utility.h"
#include "ui/painter.h"
#include "ui/userpic_view.h"
#include "ui/text/format_values.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/rp_window.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "window/window_separate_id.h"
#include "styles/style_chat.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "styles/style_info_profile_actions.h"
#include "styles/style_menu_icons.h"

namespace Extras::Profile {
namespace Ui = ::Ui;
namespace {

using Wrap = Ui::SlideWrap<Ui::VerticalLayout>;
using PeerFlag = Data::PeerUpdate::Flag;

int px(int value) {
	return style::ConvertScale(value);
}

class AttachmentCard final : public Ui::RpWidget {
public:
	AttachmentCard(QWidget *parent, bool warning = false)
	: RpWidget(parent)
	, _content(Ui::CreateChild<Ui::VerticalLayout>(this))
	, _warning(warning) {
		_content->heightValue() | rpl::on_next([=] {
			if (!_resizing) {
				resizeToWidth(width());
			}
		}, lifetime());
		ExtrasSettings::getInstance().adaptiveCoverColorValue(
		) | rpl::on_next([=](bool value) {
			_adaptiveColor = value;
			update();
		}, lifetime());
		style::PaletteChanged() | rpl::on_next([=] { update(); }, lifetime());
	}

	not_null<Ui::VerticalLayout*> content() const {
		return _content;
	}

	void setTint(std::optional<QColor> color) {
		_tint = color;
		update();
	}

private:
	int resizeGetHeight(int newWidth) override {
		_resizing = true;
		const auto &padding = st::infoProfileTopBarActionButtonsPadding;
		_content->resizeToWidth(std::max(newWidth
			- padding.left() - padding.right() - 2 * px(14), 0));
		_content->moveToLeft(padding.left() + px(14), 0);
		_resizing = false;
		return _content->height() ? _content->height() + px(10) : 0;
	}

	void paintEvent(QPaintEvent *e) override {
		if (!height()) {
			return;
		}
		auto p = Painter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto tint = _warning
			? QColor(173, 123, 47)
			: (_adaptiveColor && _tint) ? *_tint : st::windowActiveTextFg->c;
		const auto bg = Ui::BlendColors(st::windowBg->c, tint, _warning ? 0.09 : 0.055);
		p.setPen(Ui::BlendColors(bg, st::windowSubTextFg->c, 0.12));
		p.setBrush(bg);
		const auto &padding = st::infoProfileTopBarActionButtonsPadding;
		p.drawRoundedRect(QRectF(padding.left() + 0.5, 0.5,
			std::max(width() - padding.left() - padding.right() - 1, 0),
			height() - px(10) - 1),
			px(15), px(15));
	}

	const not_null<Ui::VerticalLayout*> _content;
	std::optional<QColor> _tint;
	bool _adaptiveColor = true;
	bool _warning = false;
	bool _resizing = false;
};

void addHeading(
		not_null<Ui::VerticalLayout*> layout,
		rpl::producer<QString> text,
		const style::icon &icon) {
	const auto row = layout->add(object_ptr<Ui::FixedHeightWidget>(
		layout, px(14) + st::infoLabel.style.font->height + px(10)));
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		row, std::move(text), st::infoLabel);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	row->widthValue() | rpl::on_next([=](int width) {
		label->resizeToWidth(std::max(width - px(20), 0));
		label->moveToLeft(px(20), px(14));
	}, label->lifetime());
	row->paintRequest() | rpl::on_next([=, &icon] {
		auto p = Painter(row);
		p.translate(0, px(14) + (st::infoLabel.style.font->height - px(14)) / 2);
		const auto factor = float64(px(14)) / std::max(icon.width(), icon.height());
		p.scale(factor, factor);
		icon.paint(p, 0, 0, icon.width(), st::windowSubTextFg->c);
	}, row->lifetime());
}

Wrap *addWrap(not_null<Ui::VerticalLayout*> layout) {
	return layout->add(object_ptr<Wrap>(
		layout, object_ptr<Ui::VerticalLayout>(layout)));
}

void addDivider(not_null<Ui::VerticalLayout*> layout) {
	const auto line = layout->add(object_ptr<Ui::FixedHeightWidget>(layout, st::lineWidth));
	line->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(line);
		p.fillRect(line->rect(), Ui::WindowDividerColor());
	}, line->lifetime());
}

class PeerButton final : public Ui::RippleButton {
public:
	PeerButton(
		QWidget *parent,
		not_null<PeerData*> peer,
		rpl::producer<QString> status,
		Fn<void()> open)
	: RippleButton(parent, st::infoMusicButtonRipple)
	, _peer(peer) {
		setClickedCallback(std::move(open));
		Info::Profile::NameValue(peer) | rpl::on_next([=](const QString &name) {
			_name.setText(st::semiboldTextStyle, name);
			setAccessibleName(name);
			setToolTip(name);
			update();
		}, lifetime());
		std::move(status) | rpl::on_next([=](const QString &text) {
			_status.setText(st::infoLabel.style, text);
			update();
		}, lifetime());
		peer->session().changes().peerUpdates(peer, PeerFlag::Photo
		) | rpl::on_next([=] { update(); }, lifetime());
		peer->session().downloaderTaskFinished(
		) | rpl::on_next([=] { update(); }, lifetime());
		peer->loadUserpic();
	}

private:
	int resizeGetHeight(int newWidth) override {
		return std::max(px(44), st::semiboldFont->height + st::infoLabel.style.font->height + px(6));
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = Painter(this);
		paintRipple(p, QPoint());
		const auto size = px(34);
		_peer->paintUserpicLeft(p, _userpic, 0, (height() - size) / 2, width(), size);
		const auto &arrow = st::topicButtonArrow;
		const auto left = size + px(11);
		const auto available = std::max(width() - left - arrow.width() - px(10), 0);
		const auto top = (height() - st::semiboldFont->height
			- st::infoLabel.style.font->height - px(3)) / 2;
		p.setPen(st::windowBoldFg);
		_name.draw(p, { .position = { left, top }, .availableWidth = available, .elisionLines = 1 });
		p.setPen(st::windowSubTextFg);
		_status.draw(p, {
			.position = { left, top + st::semiboldFont->height + px(3) },
			.availableWidth = available,
			.elisionLines = 1,
		});
		arrow.paint(p, width() - arrow.width(), (height() - arrow.height()) / 2,
			width(), st::windowSubTextFg->c);
	}

	const not_null<PeerData*> _peer;
	Ui::PeerUserpicView _userpic;
	Ui::Text::String _name;
	Ui::Text::String _status;
};

class PostButton final : public Ui::RippleButton {
public:
	PostButton(QWidget *parent, not_null<HistoryItem*> item, Fn<void()> removed)
	: RippleButton(parent, st::infoMusicButtonRipple)
	, _item(item) {
		setObjectName(u"profile.attachments.post"_q);
		setAccessibleName(tr::lng_profile_view_channel(tr::now));
		refresh();
		const auto &changes = item->history()->session().changes();
		rpl::merge(
			changes.realtimeMessageUpdates(Data::MessageUpdate::Flag::Destroyed),
			changes.realtimeMessageUpdates(Data::MessageUpdate::Flag::Edited)
		) | rpl::on_next([=](const Data::MessageUpdate &update) {
			if (update.item != _item) {
				return;
			}
			if (update.flags & Data::MessageUpdate::Flag::Destroyed) {
				_item = nullptr;
				removed();
			} else {
				refresh();
			}
		}, lifetime());
		item->history()->session().downloaderTaskFinished(
		) | rpl::on_next([=] { refresh(); }, lifetime());
	}

private:
	void refresh() {
		if (!_item) {
			return;
		}
		_preview = _item->toPreview({ .hideSender = true });
		_text.setMarkedText(st::defaultTextStyle, _preview.text, kMarkupTextOptions,
			Core::TextContext({
				.session = &_item->history()->session(),
				.repaint = [=] { update(); },
			}));
		_date = Ui::FormatDialogsDate(ItemDateTime(_item));
		update();
	}

	int resizeGetHeight(int newWidth) override {
		return px(20) + std::max(px(48),
			2 * st::normalFont->height + st::infoLabel.style.font->height + px(4));
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = Painter(this);
		auto hq = PainterHighQualityEnabler(p);
		p.setBrush(st::windowBg);
		p.setPen(Ui::WindowDividerColor());
		p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), px(10), px(10));
		paintRipple(p, QPoint());
		if (!_item) {
			return;
		}
		const auto imageSize = px(48);
		auto left = px(10);
		if (!_preview.images.empty()) {
			const auto &image = _preview.images.front();
			const auto target = QRect(left, (height() - imageSize) / 2, imageSize, imageSize);
			auto path = QPainterPath();
			path.addRoundedRect(target, px(6), px(6));
			p.save();
			p.setClipPath(path);
			if (!image.data.isNull() && !image.hasSpoiler()) {
				p.drawImage(target, image.data);
			} else {
				p.fillRect(target, st::windowBgOver);
				st::infoIconMediaPhoto.paintInCenter(p, target);
			}
			p.restore();
			left += imageSize + px(10);
		}
		const auto &arrow = st::topicButtonArrow;
		const auto available = std::max(width() - left - arrow.width() - px(20), 0);
		p.setPen(st::windowFg);
		_text.draw(p, {
			.position = { left, px(10) },
			.availableWidth = available,
			.elisionLines = 2,
		});
		p.setFont(st::infoLabel.style.font);
		p.setPen(st::windowSubTextFg);
		p.drawText(left, height() - px(10) - st::infoLabel.style.font->descent,
			st::infoLabel.style.font->elided(_date, available));
		arrow.paint(p, width() - px(10) - arrow.width(), (height() - arrow.height()) / 2,
			width(), st::windowSubTextFg->c);
	}

	HistoryItem *_item = nullptr;
	HistoryView::ItemPreview _preview;
	Ui::Text::String _text;
	QString _date;
};

void setupPostNavigation(
		not_null<PostButton*> button,
		not_null<Window::SessionController*> window,
		not_null<ChannelData*> channel,
		MsgId id) {
	const auto openWindow = [=] { window->showInNewWindow(Window::SeparateId(channel), id); };
	const auto openCurrent = [=] {
		window->showPeerHistory(channel, Window::SectionShow::Way::Forward, id);
	};
	const auto menu = button->lifetime().make_state<base::unique_qptr<Ui::PopupMenu>>();
	button->setAcceptBoth();
	button->addClickHandler([=](Qt::MouseButton mouse) {
		if (mouse == Qt::RightButton) {
			*menu = base::make_unique_q<Ui::PopupMenu>(button, st::popupMenuWithIcons);
			(*menu)->addAction(tr::lng_context_new_window(tr::now), [=] {
				base::call_delayed(st::popupMenuWithIcons.showDuration,
					crl::guard(button, openWindow));
			}, &st::menuIconNewWindow);
			(*menu)->popup(QCursor::pos());
			return;
		}
		if (base::IsCtrlPressed() || mouse == Qt::MiddleButton) {
			openWindow();
			return;
		}
		openCurrent();
	});
}

void setupChannel(
		not_null<Ui::VerticalLayout*> layout,
		not_null<Info::Controller*> controller,
		not_null<UserData*> user,
		rpl::producer<bool> hasMusic) {
	const auto wrap = addWrap(layout);
	wrap->setObjectName(u"profile.attachments.channelModule"_q);
	const auto body = wrap->entity();
	const auto separator = body->add(object_ptr<Ui::SlideWrap<>>(
		body, object_ptr<Ui::FixedHeightWidget>(body, st::lineWidth)));
	separator->entity()->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(separator->entity());
		p.fillRect(separator->entity()->rect(), Ui::WindowDividerColor());
	}, separator->lifetime());
	separator->toggleOn(std::move(hasMusic), anim::type::instant);
	addHeading(body, tr::extras_ProfilePersonalChannel(), st::infoIconMediaChannel);
	const auto content = body->add(object_ptr<Ui::VerticalLayout>(body));
	Ui::AddSkip(body, px(14));
	const auto window = controller->parentController();
	Info::Profile::PersonalChannelValue(user
	) | rpl::on_next([=](ChannelData *channel) {
		content->clear();
		wrap->toggle(channel != nullptr, anim::type::normal);
		if (!channel) {
			return;
		}
		auto status = channel->session().changes().peerFlagsValue(channel, PeerFlag::Members
		) | rpl::map([=] {
			return tr::lng_chat_status_subscribers(tr::now, lt_count_decimal, channel->membersCount());
		});
		const auto entry = content->add(object_ptr<PeerButton>(
			content, channel, std::move(status), [=] { window->showPeerInfo(channel); }));
		entry->setObjectName(u"profile.attachments.channel"_q);
		if (user->isSelf()) {
			const auto menu = entry->lifetime().make_state<base::unique_qptr<Ui::PopupMenu>>();
			entry->setAcceptBoth(true);
			entry->clicks() | rpl::filter([](Qt::MouseButton mouse) {
				return mouse == Qt::RightButton;
			}) | rpl::on_next([=] {
				*menu = base::make_unique_q<Ui::PopupMenu>(entry, st::defaultPopupMenu);
				(*menu)->addAction(tr::lng_settings_channel_menu_remove(tr::now), [] {
					UrlClickHandler::Open(u"internal:edit_personal_channel:remove"_q);
				});
				(*menu)->popup(QCursor::pos());
			}, entry->lifetime());
		}
		const auto id = FullMsgId(channel->id, user->personalChannelMessageId());
		if (!id.msg) {
			return;
		}
		const auto post = addWrap(content);
		post->toggle(false, anim::type::instant);
		const auto rebuild = crl::guard(post, [=] {
			const auto item = user->owner().message(id);
			if (!item) {
				return;
			}
			post->entity()->clear();
			Ui::AddSkip(post->entity(), px(10));
			const auto button = post->entity()->add(object_ptr<PostButton>(
				post->entity(), item, [=] { post->toggle(false, anim::type::normal); }));
			setupPostNavigation(button, window, channel, id.msg);
			post->toggle(true, anim::type::normal);
		});
		if (user->owner().message(id)) {
			rebuild();
		} else {
			user->session().api().requestMessageData(channel, id.msg, rebuild);
		}
	}, content->lifetime());
	wrap->finishAnimating();
}

void setupWarning(not_null<Ui::VerticalLayout*> layout, not_null<UserData*> user) {
	const auto card = layout->add(object_ptr<AttachmentCard>(layout, true));
	card->setObjectName(u"profile.attachments.warning"_q);
	const auto wrap = addWrap(card->content());
	addHeading(wrap->entity(), tr::extras_ProfileSecurityNotice(), st::infoIconInformation);
	const auto text = wrap->entity()->add(object_ptr<Ui::FlatLabel>(
		wrap->entity(),
		tr::lng_profile_unofficial_warning(
			lt_icon, rpl::single(TextWithEntities()),
			lt_name, Info::Profile::NameValue(user) | rpl::map([](QString name) {
				return TextWithEntities{ name };
			}), tr::marked),
		st::defaultDividerLabel.label));
	text->setObjectName(u"profile.attachments.warningText"_q);
	Ui::AddSkip(wrap->entity(), px(14));
	wrap->toggleOn(user->session().changes().peerFlagsValue(user, PeerFlag::FullInfo
	) | rpl::map([=] { return user->unofficialSecurityRisk(); }));
	wrap->finishAnimating();
}

void setupCommunity(
		not_null<Ui::VerticalLayout*> layout,
		not_null<Info::Controller*> controller,
		not_null<PeerData*> peer) {
	const auto card = layout->add(object_ptr<AttachmentCard>(layout));
	card->setObjectName(u"profile.attachments.communityCard"_q);
	const auto wrap = addWrap(card->content());
	const auto body = wrap->entity();
	const auto updates = wrap->lifetime().make_state<rpl::lifetime>();
	peer->session().changes().peerFlagsValue(peer, PeerFlag::FullInfo
	) | rpl::map([=] {
		return Data::PeerLinkedCommunityId(peer);
	}) | rpl::distinct_until_changed() | rpl::on_next([=](ChannelId id) {
		updates->destroy();
		body->clear();
		wrap->toggle(id != 0, anim::type::normal);
		if (!id) {
			return;
		}
		const auto community = peer->owner().channel(id);
		addHeading(body, tr::extras_ProfileCommunity(), st::infoIconMediaGroup);
		auto changes = community->session().changes().peerFlagsValue(community, PeerFlag::FullInfo
		) | rpl::map([=]() -> rpl::producer<> {
			const auto info = community->communityInfo();
			return info ? info->linkedPeersValue() : rpl::single(rpl::empty);
		}) | rpl::flatten_latest() | rpl::start_spawning(*updates);
		auto status = rpl::duplicate(changes) | rpl::map([=] {
			const auto info = community->communityInfo();
			const auto count = info ? int(info->linkedPeers().size()) : 0;
			return count ? tr::lng_community_profile_status(tr::now, lt_count, count)
				: tr::lng_community_title(tr::now);
		});
		const auto window = controller->parentController();
		const auto entry = body->add(object_ptr<PeerButton>(
			body, community, std::move(status), [=] { window->showPeerInfo(community); }));
		entry->setObjectName(u"profile.attachments.community"_q);
		entry->setAcceptBoth(true);
		const auto menu = entry->lifetime().make_state<base::unique_qptr<Ui::PopupMenu>>();
		entry->clicks() | rpl::filter([](Qt::MouseButton mouse) {
			return mouse == Qt::RightButton;
		}) | rpl::on_next([=] {
			const auto history = community->owner().history(community);
			if (!history->owner().chatsFilters().has() || !history->inChatList()
				|| (community->isCommunity() && !community->collapsedInDialogs())) {
				return;
			}
			*menu = base::make_unique_q<Ui::PopupMenu>(entry, st::popupMenuWithIcons);
			Ui::Menu::CreateAddActionCallback(menu->get())({
				.text = tr::lng_filters_menu_add(tr::now),
				.handler = nullptr,
				.icon = &st::menuIconAddToFolder,
				.fillSubmenu = [=](not_null<Ui::PopupMenu*> submenu) {
					FillChooseFilterMenu(window, submenu, history);
				},
				.submenuSt = &st::foldersMenu,
			});
			(*menu)->popup(QCursor::pos());
		}, entry->lifetime());
		const auto note = addWrap(body);
		note->setObjectName(u"profile.attachments.hiddenNote"_q);
		Ui::AddSkip(note->entity(), px(10));
		addDivider(note->entity());
		Ui::AddSkip(note->entity(), px(10));
		note->entity()->add(object_ptr<Ui::FlatLabel>(
			note->entity(), tr::lng_community_hidden_chat_about(), st::defaultDividerLabel.label));
		note->toggleOn(std::move(changes) | rpl::map([=] {
			const auto info = community->communityInfo();
			return info && info->isHidden(peer);
		}));
		note->finishAnimating();
		Ui::AddSkip(body, px(14));
		if (!community->wasFullUpdated()) {
			community->session().api().requestFullPeer(community);
		}
	}, wrap->lifetime());
	wrap->finishAnimating();
}

} // namespace

void setupAttachments(
		not_null<Ui::VerticalLayout*> layout,
		not_null<Info::Controller*> controller,
		not_null<PeerData*> peer,
		bool musicOnly) {
	const auto group = layout->add(object_ptr<AttachmentCard>(layout));
	group->setObjectName(u"profile.attachments.content"_q);
	const auto music = addWrap(group->content());
	music->setObjectName(u"profile.attachments.musicModule"_q);
	addHeading(music->entity(), tr::extras_ProfileMusic(), st::infoIconMediaAudio);
	const auto musicRow = music->entity()->add(object_ptr<Ui::VerticalLayout>(music->entity()));
	Ui::AddSkip(music->entity(), px(14));
	auto hasMusic = Info::Saved::SetupSavedMusic(
		musicRow, controller, peer, [=](std::optional<QColor> color) {
			group->setTint(color);
		}) | rpl::start_spawning(group->lifetime());
	music->toggleOn(rpl::duplicate(hasMusic));
	music->finishAnimating();
	if (musicOnly) {
		return;
	}
	if (const auto user = peer->asUser()) {
		setupChannel(group->content(), controller, user, std::move(hasMusic));
		setupWarning(layout, user);
	}
	setupCommunity(layout, controller, peer);
}

} // namespace Extras::Profile
