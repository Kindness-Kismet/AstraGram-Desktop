/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/history_widget.h"
#include "history/view/controls/history_view_compose_stash.h"
#include "history/view/controls/history_view_compose_stash_hint.h"
#include "data/data_compose_stash.h"

namespace HistoryWidgetDetails {

constexpr auto kMessagesPerPageFirst = 30;

constexpr auto kMessagesPerPage = 50;

constexpr auto kPreloadHeightsCount = 3;

// when 3 screens to scroll left make a preload request
constexpr auto kScrollToVoiceAfterScrolledMs = 1000;

constexpr auto kSkipRepaintWhileScrollMs = 100;

constexpr auto kShowMembersDropdownTimeoutMs = 300;

constexpr auto kDisplayEditTimeWarningMs = 300 * 1000;

constexpr auto kFullDayInMs = 86400 * 1000;

constexpr auto kSaveDraftTimeout = crl::time(1000);

constexpr auto kSaveDraftAnywayTimeout = 5 * crl::time(1000);

constexpr auto kSaveCloudDraftIdleTimeout = 14 * crl::time(1000);

constexpr auto kRefreshSlowmodeLabelTimeout = crl::time(200);

constexpr auto kCommonModifiers = 0
	| Qt::ShiftModifier
	| Qt::MetaModifier
	| Qt::ControlModifier;

const auto kPsaAboutPrefix = "cloud_lng_about_psa_";

[[nodiscard]] rpl::producer<PeerData*> ActivePeerValue(
		not_null<Window::SessionController*> controller);

[[nodiscard]] QString FirstEmoji(const QString &s);

} // namespace HistoryWidgetDetails

#define SWITCH_BUTTON(button, show_v) \
	if (show_v) { \
		(button)->show(); \
	} else { \
		(button)->hide(); \
	}
