#pragma once

#include "extras/utils/badge_roster.h"
#include "info/profile/info_profile_badge.h"

[[nodiscard]] bool isExteraPeer(not_null<PeerData*> peer);
[[nodiscard]] bool isSupporterPeer(not_null<PeerData*> peer);
[[nodiscard]] CustomBadge getCustomBadge(ID peerId);
[[nodiscard]] rpl::producer<Info::Profile::Badge::Content> ExteraBadgeTypeFromPeer(
	not_null<PeerData*> peer);
[[nodiscard]] Fn<void()> badgeClickHandler(not_null<PeerData*> peer);
void watchBadgeChanges(not_null<Main::Session*> session);
