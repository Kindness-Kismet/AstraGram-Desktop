/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/const_string.h"

// 版本数值由 Telegram/build/version 在配置阶段生成。
#include "ayugram_app_version.h"

#define TDESKTOP_REQUESTED_ALPHA_VERSION (0ULL)

#ifdef TDESKTOP_ALLOW_CLOSED_ALPHA
#define TDESKTOP_ALPHA_VERSION TDESKTOP_REQUESTED_ALPHA_VERSION
#else // TDESKTOP_ALLOW_CLOSED_ALPHA
#define TDESKTOP_ALPHA_VERSION (0ULL)
#endif // TDESKTOP_ALLOW_CLOSED_ALPHA

// used in Updater.cpp and Setup.iss for Windows
constexpr auto AppId = "{53F49750-6209-4FBF-9CA8-7A333C87D666}"_cs;
constexpr auto AppNameOld = "AyuGram for Windows"_cs;
constexpr auto AppName = "AstraGram"_cs;
constexpr auto AppFile = "AstraGram"_cs;
constexpr auto AppVersion = AYUGRAM_APP_VERSION;
constexpr auto AppUpdateVersion = AYUGRAM_APP_UPDATE_VERSION;
constexpr auto AppStorageReadVersion = AYUGRAM_APP_STORAGE_READ_VERSION;
constexpr auto AppVersionStr = AYUGRAM_APP_VERSION_STR;
constexpr auto AppBetaVersion = AYUGRAM_APP_BETA_VERSION;
constexpr auto AppAlphaVersion = TDESKTOP_ALPHA_VERSION;
