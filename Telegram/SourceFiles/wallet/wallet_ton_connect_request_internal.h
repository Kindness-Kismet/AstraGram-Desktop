#pragma once

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_ton_connect_request.h"

#include "base/timer.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "data/data_changes.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "ui/delayed_activation.h"
#include "ui/widgets/separate_panel.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_panel.h"
#include "wallet/wallet_session.h"
#include "wallet/wallet_ton_connect_request_box.h"
#include "wallet/wallet_unlock.h"
#include "window/window_session_controller.h"

#include <QtCore/QUuid>

namespace Wallet {

namespace RequestDetails {}

using namespace RequestDetails;

namespace RequestDetails {

using Phase = TonConnectRequestPhase;

constexpr auto kWalletResolveTimeout = 20 * crl::time(1000);

constexpr auto kDeadlineMaxDelay = 24 * 3600 * crl::time(1000);

[[nodiscard]] bool SessionGone(const QString &type);

[[nodiscard]] bool RequestDropped(const QString &type);

enum class SubmitResult : uchar {
	Delivered,
	Refused,
	RetryLater,
};

void SubmitResponse(
		MTP::Sender &api,
		TonConnectSessionId sessionId,
		MsgId msgId,
		const QByteArray &body,
		const QString &traceId,
		Fn<void(SubmitResult)> done);

[[nodiscard]] QString SessionName(const TonConnectSessionInfo *info);

[[nodiscard]] QString SessionDomain(const TonConnectSessionInfo *info);

[[nodiscard]] WebFileLocation SessionIcon(
		const TonConnectSessionInfo *info);

[[nodiscard]] QString AccessNoticeText(TonConnectAccess access);

}

class TonConnectRequests::Flow final : public base::has_weak_ptr {
public:
	Flow(
		not_null<TonConnectRequests*> owner,
		base::weak_ptr<Window::SessionController> controller,
		std::shared_ptr<Main::SessionShow> show,
		Entry entry,
		bool silent);
	~Flow();

	void start();
	void activate();
	void editedElsewhere();
	[[nodiscard]] bool matches(MsgId msgId) const;

private:
	friend class TonConnectRequests;

	enum class Decision : uchar {
		None,
		Confirm,
		Sign,
		Decline,
		Reject,
		Disconnect,
	};

	[[nodiscard]] bool showBox();
	void fetch();
	void fetched(const MTPwallet_TonConnectPending &result);
	void fetchFailed(const MTP::Error &error);
	void waitWallet();
	void resolve();
	void resolveTimeout();
	void stopResolving();
	void keyNeeded();
	void waitForKey();
	void requestKey();
	void locked();
	void unlockPressed();
	[[nodiscard]] bool offerRestore();
	void restorePressed();
	void restored(KeyAuthorization auth);
	void keyReady(TonConnectKeyResult result);
	void decrypt();
	void decrypted(TonConnectAppRequest request);
	void answerRecovered();
	void preview();
	void previewed(FeeResult result);
	void showSignData();
	void confirmPressed();
	void confirmKeyReady(TonConnectKeyResult result);
	void decisionKeyFailed(TonConnectKeyError error);
	void sign();
	void declinePressed();
	void showUnhandled(const QString &text);
	void answerDisconnect();
	void answerUnknownApp();
	void encryptAnswer(TonConnectResponse response);
	void encryptNotSent();
	void registerKey();
	void registered(const QByteArray &challenge);
	void registerFailed(const MTP::Error &error);
	void claim(const QByteArray &answer);
	void claimed(const MTPBool &result);
	void claimFailed(const MTP::Error &error);
	void send();
	void sent(TonConnectSendResult result);
	void answered(QByteArray body);
	void publish();
	void published(SubmitResult result);
	void decisionFailed(const QString &type = QString());
	void armDeadline(std::optional<TimeId> validUntil);
	void expired();
	void unavailable();
	void accessNotice(TonConnectAccess access);
	void notice(const QString &text);
	void closeWithToast(const QString &text);
	void closeWithTransfer();
	void backToConfirm(const QString &error);
	void dismissed();
	void lateStop();
	void lateCloseBox();
	void closeBox();
	void finish();
	[[nodiscard]] bool late() const;
	[[nodiscard]] bool recovered() const;
	[[nodiscard]] bool stopped() const;
	[[nodiscard]] bool claiming() const;
	[[nodiscard]] bool declines() const;
	[[nodiscard]] std::shared_ptr<Main::SessionShow> showNow() const;

	const not_null<TonConnectRequests*> _owner;
	const not_null<Main::Session*> _session;
	const base::weak_ptr<Window::SessionController> _controller;
	const TonConnectSessionId _sessionId = 0;
	const MsgId _msgId = 0;
	const bool _chosen = false;
	const TonConnectRecovery _recovery = TonConnectRecovery::None;
	MTP::Sender _api;
	std::shared_ptr<Main::SessionShow> _show;
	base::weak_qptr<Ui::GenericBox> _box;
	rpl::variable<TonConnectRequestBoxState> _state;
	QString _topic;
	QString _traceId;
	QByteArray _body;
	TonConnectKey _key;
	TonConnectAppRequest _request;
	std::optional<TonConnectSessionInfo> _closedSession;
	std::shared_ptr<const PreparedSend> _prepared;
	KeyAuthorization _auth;
	QByteArray _response;
	QByteArray _notSent;
	std::string _operationId;
	TimeId _expires = 0;
	uint64 _previewOwner = 0;
	Decision _decision = Decision::None;
	bool _silent = false;
	bool _claimSent = false;
	bool _claimed = false;
	bool _sendStarted = false;
	bool _sentBoc = false;
	bool _retriedChallenge = false;
	bool _polling = false;
	bool _closingBox = false;
	bool _terminal = false;
	bool _finished = false;
	base::Timer _deadlineTimer;
	base::Timer _resolveTimer;
	rpl::lifetime _resolveLifetime;
	rpl::lifetime _previewLifetime;
	rpl::lifetime _idleLifetime;
	rpl::lifetime _keyLifetime;
	rpl::lifetime _lifetime;

};

}
