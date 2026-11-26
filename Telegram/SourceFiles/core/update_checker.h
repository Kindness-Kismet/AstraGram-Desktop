/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/dedicated_file_loader.h"

namespace Main {
class Session;
} // namespace Main

namespace Core {

bool UpdaterDisabled();
void SetUpdaterDisabledAtStartup();

class Updater;

class UpdateChecker {
public:
	enum class State {
		None,
		Download,
		Ready,
	};
	using Progress = MTP::AbstractDedicatedLoader::Progress;

	UpdateChecker();

	rpl::producer<> checking() const;
	rpl::producer<> isLatest() const;
	rpl::producer<Progress> progress() const;
	rpl::producer<> failed() const;
	rpl::producer<> ready() const;

	void start(bool forceWait = false);
	void checkNow();
	void stop();
	void test();

	void setMtproto(base::weak_ptr<Main::Session> session);

	State state() const;
	int already() const;
	int size() const;
	bool percent() const;

private:
	const std::shared_ptr<Updater> _updater;

};

[[nodiscard]] bool WriteUpdateVersionFile(
	const QString &directory,
	quint32 version,
	quint64 canaryVersion);

bool checkReadyUpdate();
// 已下载就绪的更新版本整数，读不到或为测试通道时返回 0。
[[nodiscard]] uint64 ReadyUpdateVersion();
enum class ReadyUpdateInstall {
	Failed,
	Unpacked,
	Protected,
};
[[nodiscard]] ReadyUpdateInstall prepareReadyUpdate();
void UpdateApplication();

#ifdef _DEBUG
// 本地验收只允许独立离线配置使用回环更新源。
[[nodiscard]] bool SetDebugUpdatePrefix(const QString &prefix);
[[nodiscard]] QString DebugUpdatePrefix();
#endif // _DEBUG

} // namespace Core
