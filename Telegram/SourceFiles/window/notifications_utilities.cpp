/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/notifications_utilities.h"

#include "window/main_window.h"
#include "base/platform/base_platform_file_utilities.h"
#include "base/random.h"
#include "core/application.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "extras/features/secret_chats/secret_chat_dialog.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "ui/empty_userpic.h"
#include "styles/style_window.h"

namespace Window::Notifications {
namespace {

// Delete notify photo file after 1 minute of not using.
constexpr int kNotifyDeletePhotoAfterMs = 60000;

base::flat_map<const HistoryItem*, qint64> SecretNotificationIdentities;

} // namespace

void SetNotificationMessageId(not_null<HistoryItem*> item, qint64 id) {
	Expects(item->history()->peer->isSecretChat());
	// 编号跟随消息生命周期，析构时用 0 清理。
	if (!id) {
		SecretNotificationIdentities.remove(item.get());
		return;
	}
	SecretNotificationIdentities[item.get()] = id;
}

MsgId NotificationMessageId(not_null<HistoryItem*> item) {
	if (!item->history()->peer->isSecretChat()) {
		return item->id;
	}
	const auto i = SecretNotificationIdentities.find(item.get());
	Expects(i != SecretNotificationIdentities.end());
	return MsgId(i->second);
}

HistoryItem *ResolveNotificationMessage(not_null<History*> history, MsgId id) {
	if (!history->peer->isSecretChat()) {
		return history->owner().message(history->peer, id);
	}
	const auto chatId = int(history->peer->id.to<SecretChatId>().bare);
	const auto dialog = history->session().secretChats().dialog(chatId);
	return dialog
		? history->owner().message(history->peer, dialog->localId(id.bare))
		: nullptr;
}

QImage GenerateUserpic(not_null<PeerData*> peer, Ui::PeerUserpicView &view) {
	return peer->isSelf()
		? Ui::EmptyUserpic::GenerateSavedMessages(st::notifyMacPhotoSize)
		: peer->isRepliesChat()
		? Ui::EmptyUserpic::GenerateRepliesMessages(st::notifyMacPhotoSize)
		: PeerData::GenerateUserpicImage(peer, view, st::notifyMacPhotoSize);
}

CachedUserpics::CachedUserpics()
: _clearTimer([=] { clear(); }) {
	QDir().mkpath(cWorkingDir() + u"tdata/temp"_q);
}

CachedUserpics::~CachedUserpics() {
	if (_someSavedFlag) {
		for (const auto &item : std::as_const(_images)) {
			QFile(item.path).remove();
		}

		// This works about 1200ms on Windows for a folder with one image O_o
		//base::Platform::DeleteDirectory(cWorkingDir() + u"tdata/temp"_q);
	}
}

QString CachedUserpics::get(
		const InMemoryKey &key,
		not_null<PeerData*> peer,
		Ui::PeerUserpicView &view) {
	auto ms = crl::now();
	auto i = _images.find(key);
	if (i != _images.cend()) {
		if (i->until) {
			i->until = ms + kNotifyDeletePhotoAfterMs;
			clearInMs(-kNotifyDeletePhotoAfterMs);
		}
	} else {
		Image v;
		if (key.first) {
			v.until = ms + kNotifyDeletePhotoAfterMs;
			clearInMs(-kNotifyDeletePhotoAfterMs);
		} else {
			v.until = 0;
		}
		v.path = u"%1tdata/temp/%2.png"_q.arg(
			cWorkingDir(),
			QString::number(base::RandomValue<uint64>(), 16));
		if (key.first || key.second) {
			GenerateUserpic(peer, view).save(v.path, "PNG");
		} else {
			LogoNoMargin().save(v.path, "PNG");
		}
		i = _images.insert(key, v);
		_someSavedFlag = true;
	}
	return i->path;
}

crl::time CachedUserpics::clear(crl::time ms) {
	crl::time result = 0;
	for (auto i = _images.begin(); i != _images.end();) {
		if (!i->until) {
			++i;
			continue;
		}
		if (i->until <= ms) {
			QFile(i->path).remove();
			i = _images.erase(i);
		} else {
			if (!result) {
				result = i->until;
			} else {
				accumulate_min(result, i->until);
			}
			++i;
		}
	}
	return result;
}

void CachedUserpics::clearInMs(int ms) {
	if (ms < 0) {
		ms = -ms;
		if (_clearTimer.isActive() && _clearTimer.remainingTime() <= ms) {
			return;
		}
	}
	_clearTimer.callOnce(ms);
}

void CachedUserpics::clear() {
	auto ms = crl::now();
	auto minuntil = clear(ms);
	if (minuntil) {
		clearInMs(int(minuntil - ms));
	}
}

} // namespace Window::Notifications
