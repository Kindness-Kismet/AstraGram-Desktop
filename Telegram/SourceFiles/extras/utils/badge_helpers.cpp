#include "extras/utils/badge_helpers.h"

#include "extras/utils/rc_manager.h"
#include "extras/extras_settings.h"
#include "extras/ui/boxes/donation_box.h"
#include "extras/ui/toasts.h"
#include "core/application.h"
#include "data/data_changes.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "styles/style_extras_styles.h"
#include "styles/style_info.h"
#include "ui/rp_widget.h"
#include "ui/text/format_values.h"
#include "ui/toast/toast.h"
#include "window/window_controller.h"

namespace {

class BadgeToastIcon final : public Ui::RpWidget {
public:
	BadgeToastIcon(
		QWidget *parent,
		not_null<PeerData*> peer,
		Info::Profile::Badge::Content content);

private:
	void updateInnerGeometry();

	Info::Profile::Badge _badge;

};

BadgeToastIcon::BadgeToastIcon(
	QWidget *parent,
	not_null<PeerData*> peer,
	Info::Profile::Badge::Content content)
: Ui::RpWidget(parent)
, _badge(
	this,
	st::infoPeerBadge,
	&peer->session(),
	rpl::single(content),
	nullptr,
	[] { return false; },
	0,
	Info::Profile::BadgeType::Extera
		| Info::Profile::BadgeType::ExteraSupporter
		| Info::Profile::BadgeType::ExteraCustom) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	_badge.setOverrideStyle(&st::exteraBadgeToastBadge);
	_badge.updated() | rpl::on_next([=] {
		updateInnerGeometry();
	}, lifetime());
	updateInnerGeometry();
}

void BadgeToastIcon::updateInnerGeometry() {
	const auto widget = _badge.widget();
	const auto size = widget ? widget->size() : QSize();
	resize(size.width(), size.height());
	if (widget) {
		widget->moveToLeft(0, 0);
	}
}

[[nodiscard]] object_ptr<Ui::RpWidget> makeBadgeToastIcon(
		not_null<PeerData*> peer,
		Info::Profile::Badge::Content content) {
	return (content.badge == Info::Profile::BadgeType::None)
		? object_ptr<Ui::RpWidget>(nullptr)
		: object_ptr<BadgeToastIcon>(nullptr, peer, content);
}

[[nodiscard]] Info::Profile::Badge::Content computeExteraBadgeContent(
		not_null<PeerData*> peer) {
	const auto custom = getCustomBadge(peer->id.value & PeerId::kChatTypeMask);
	if (custom.emojiStatusId) {
		return { .badge = Info::Profile::BadgeType::ExteraCustom, .emojiStatusId = custom.emojiStatusId };
	}
	if (isExteraPeer(peer)) {
		return { .badge = Info::Profile::BadgeType::Extera };
	}
	if (isSupporterPeer(peer)) {
		return { .badge = Info::Profile::BadgeType::ExteraSupporter };
	}
	return {};
}

void applyBadgeDefaults(not_null<Main::Session*> session) {
	const auto &roster = RCManager::getInstance().roster();
	const auto id = ID(session->userId().bare);
	if (roster.developers.contains(id)
		|| roster.supporters.contains(id)
		|| roster.customBadges.contains(id)) {
		ExtrasSettings::getInstance().enableBadgeDevFeatures();
	}
}

} // namespace

bool isExteraPeer(not_null<PeerData*> peer) {
	const auto &roster = RCManager::getInstance().roster();
	return peer->isUser()
		? roster.developers.contains(peerToUser(peer->id).bare)
		: peer->isChannel() && roster.officialChannels.contains(peerToChannel(peer->id).bare);
}

bool isSupporterPeer(not_null<PeerData*> peer) {
	const auto &roster = RCManager::getInstance().roster();
	return peer->isUser()
		? roster.supporters.contains(peerToUser(peer->id).bare)
		: peer->isChannel() && roster.supporterChannels.contains(peerToChannel(peer->id).bare);
}

CustomBadge getCustomBadge(ID peerId) {
	const auto &badges = RCManager::getInstance().roster().customBadges;
	const auto found = badges.find(peerId);
	return found != badges.end() ? found->second : CustomBadge();
}

rpl::producer<Info::Profile::Badge::Content> ExteraBadgeTypeFromPeer(
		not_null<PeerData*> peer) {
	return rpl::single(rpl::empty) | rpl::then(
		RCManager::getInstance().changes() | rpl::to_empty
	) | rpl::map([=] {
		return computeExteraBadgeContent(peer);
	}) | rpl::distinct_until_changed();
}

void watchBadgeChanges(not_null<Main::Session*> session) {
	applyBadgeDefaults(session);
	RCManager::getInstance().changes() | rpl::on_next([=](const std::vector<PeerId> &ids) {
		applyBadgeDefaults(session);
		for (const auto id : ids) {
			if (const auto peer = session->data().peerLoaded(id)) {
				session->changes().peerUpdated(peer, Data::PeerUpdate::Flag::EmojiStatus);
			}
		}
	}, session->lifetime());
}

Fn<void()> badgeClickHandler(not_null<PeerData*> peer) {
	return [=] {
		const auto content = computeExteraBadgeContent(peer);
		if (content.badge == Info::Profile::BadgeType::None) {
			return;
		}
		const auto custom = getCustomBadge(peer->id.value & PeerId::kChatTypeMask);
		const auto phrase = isExteraPeer(peer)
			? (peer->isUser() ? tr::extras_DeveloperPopup : tr::extras_OfficialResourcePopup)
			: isSupporterPeer(peer) ? tr::extras_SupporterPopup : tr::extras_CustomBadgePopup;
		const auto text = custom.text.isEmpty()
			? phrase(tr::now, lt_item, TextWithEntities{ peer->name() }, tr::rich)
			: tr::rich(custom.text);
		auto config = Ui::Toast::Config{
			.text = text,
			.iconContent = makeBadgeToastIcon(peer, content),
			.st = &st::exteraBadgeToast,
			.adaptive = true,
			.duration = 3 * crl::time(1000),
		};
		if (content.badge != Info::Profile::BadgeType::ExteraSupporter) {
			Ui::Toast::Show(std::move(config));
			return;
		}
		Extras::Ui::ShowToastWithAction(
			std::move(config),
			tr::lng_collectible_learn_more(tr::now),
			[] {
				const auto window = Core::App().activeWindow();
				const auto controller = window ? window->sessionController() : nullptr;
				if (controller) {
					ExtrasUi::showDonationBox(controller);
					window->activate();
				}
			});
	};
}
