#include "wallet/wallet_ton_connect_request_internal.h"

namespace Wallet {

namespace RequestDetails {}

using namespace RequestDetails;

TonConnectRequests::Flow::Flow(
	not_null<TonConnectRequests*> owner,
	base::weak_ptr<Window::SessionController> controller,
	std::shared_ptr<Main::SessionShow> show,
	Entry entry,
	bool silent)
: _owner(owner)
, _session(owner->_session)
, _controller(std::move(controller))
, _sessionId(entry.sessionId)
, _msgId(entry.msgId)
, _chosen(entry.chosen)
, _recovery(entry.recovery)
, _api(&_session->mtp())
, _show(std::move(show))
, _topic(entry.topic)
, _expires(entry.expires)
, _silent(silent)
, _deadlineTimer([=] { expired(); })
, _resolveTimer([=] { resolveTimeout(); }) {
}

TonConnectRequests::Flow::~Flow() {
	_terminal = true;
	_keyLifetime.destroy();
	closeBox();
}

void TonConnectRequests::Flow::start() {
	if (_silent) {
		armDeadline(std::nullopt);
		fetch();
		return;
	}
	const auto info = _owner->_store->session(_sessionId);
	_state = TonConnectRequestBoxState{
		.phase = Phase::Loading,
		.name = SessionName(info),
		.domain = SessionDomain(info),
		.icon = SessionIcon(info),
	};
	// WHY: a request of a session the store does not hold may belong to
	// a closed session, which is answered without ever showing a box.
	if (_recovery == TonConnectRecovery::Answer) {
		fetch();
		return;
	} else if ((_chosen || (info && TonConnectSessionConnected(*info)))
		&& !showBox()) {
		return;
	}
	armDeadline(std::nullopt);
	fetch();
}

bool TonConnectRequests::Flow::showBox() {
	auto box = Box(TonConnectRequestBox, TonConnectRequestBoxArgs{
		.session = _session,
		.state = _state.value(),
		.unlock = crl::guard(this, [=] { unlockPressed(); }),
		.restore = crl::guard(this, [=] { restorePressed(); }),
		.confirm = crl::guard(this, [=] { confirmPressed(); }),
		.decline = crl::guard(this, [=] { declinePressed(); }),
		.dismissed = crl::guard(this, [=] { dismissed(); }),
	});
	_box = box.data();
	_show->showBox(std::move(box));
	if (!_box) {
		finish();
		return false;
	}
	return true;
}

void TonConnectRequests::Flow::activate() {
	if (const auto box = _box.get()) {
		Ui::ActivateWindow(box->window());
	}
}

void TonConnectRequests::Flow::editedElsewhere() {
	if (recovered() || claiming() || stopped()) {
		return;
	}
	closeWithToast(tr::lng_wallet_connect_request_handled(tr::now));
}

bool TonConnectRequests::Flow::matches(MsgId msgId) const {
	return (_msgId == msgId);
}

void TonConnectRequests::Flow::fetch() {
	using Flag = MTPwallet_TonConnectGetPending::Flag;
	_api.request(MTPwallet_TonConnectGetPending(
		MTP_flags(Flag::f_session_id),
		MTPstring(),
		MTP_long(_sessionId)
	)).done([=](const MTPwallet_TonConnectPending &result) {
		fetched(result);
	}).fail([=](const MTP::Error &error) {
		fetchFailed(error);
	}).send();
}

void TonConnectRequests::Flow::fetched(
		const MTPwallet_TonConnectPending &result) {
	if (stopped()) {
		return;
	}
	const auto &data = result.data();
	const auto &session = data.vsession().data();
	const auto closed = session.is_closed();
	if (closed) {
		// The apply below drops the session and its key, and reports it.
		_key = _owner->_store->key(_sessionId);
		_closedSession = TonConnect::Parse(data.vsession());
	}
	_owner->_store->apply(data.vsession());
	const auto answer = (_recovery == TonConnectRecovery::Answer);
	if (closed) {
		if (_silent) {
			_terminal = true;
			finish();
			return;
		} else if (_recovery == TonConnectRecovery::Offer) {
			lateStop();
			return;
		} else if (!_chosen) {
			closeBox();
		}
	} else if (!_silent && !answer && !_box && !showBox()) {
		return;
	} else if (session.is_pending()) {
		unavailable();
		return;
	}
	const auto now = base::unixtime::now();
	for (const auto &request : data.vrequests().v) {
		const auto &fields = request.data();
		if (fields.vmsg_id().v != _msgId.bare) {
			continue;
		}
		_body = fields.vbody().v;
		_expires = fields.vexpires().v;
		if (const auto topic = fields.vtopic()) {
			_topic = qs(*topic);
		}
		_traceId = qs(fields.vtrace_id().value_or_empty());
		const auto info = late()
			? &*_closedSession
			: _owner->_store->session(_sessionId);
		auto state = _state.current();
		state.name = SessionName(info);
		state.domain = SessionDomain(info);
		state.icon = SessionIcon(info);
		_state = std::move(state);
		if (!answer) {
			armDeadline(std::nullopt);
		}
		waitWallet();
		return;
	}
	if (answer) {
		waitWallet();
		return;
	} else if (late() || recovered()) {
		lateStop();
		return;
	}
	const auto item = _session->data().message(
		FullMsgId(PeerData::kServiceNotificationsId, _msgId));
	const auto loaded = item
		? item->Get<HistoryServiceTonConnectRequest>()
		: nullptr;
	if (loaded && (loaded->accepted || loaded->declined)) {
		closeWithToast(tr::lng_wallet_connect_request_handled(tr::now));
	} else if (_expires <= now) {
		expired();
	} else {
		unavailable();
	}
}

void TonConnectRequests::Flow::waitWallet() {
	auto &wallet = _session->wallet();
	rpl::merge(
		wallet.transferWalletIdentityChanges(),
		wallet.custodyUpdates()
	) | rpl::on_next([=] {
		resolve();
	}, _resolveLifetime);
	_resolveTimer.callOnce(kWalletResolveTimeout);
	resolve();
	if (_resolveTimer.isActive()) {
		_polling = true;
		wallet.startPolling();
	}
}

void TonConnectRequests::Flow::fetchFailed(const MTP::Error &error) {
	if (stopped()) {
		return;
	}
	const auto &type = error.type();
	if (SessionGone(type)) {
		unavailable();
		return;
	}
	LOG(("Wallet Error: wallet.tonConnectGetPending failed: %1"
		).arg(type));
	if (!_silent && !recovered() && !_box && !showBox()) {
		return;
	}
	notice(ErrorWithType(
		tr::lng_wallet_connect_request_failed(tr::now),
		type));
}

void TonConnectRequests::Flow::resolve() {
	if (stopped()) {
		return;
	}
	auto &wallet = _session->wallet();
	const auto presence = wallet.presence();
	if (presence == Presence::Unknown
		|| (presence == Presence::Ready
			&& wallet.deviceCustodyState().mode == DeviceMode::Unknown)) {
		return;
	}
	stopResolving();
	if (presence == Presence::Ready) {
		keyNeeded();
	} else {
		notice(tr::lng_wallet_state_error(tr::now));
	}
}

void TonConnectRequests::Flow::resolveTimeout() {
	if (!stopped()) {
		notice(tr::lng_wallet_state_error(tr::now));
	}
}

void TonConnectRequests::Flow::stopResolving() {
	_resolveTimer.cancel();
	_resolveLifetime.destroy();
	if (base::take(_polling)) {
		_session->wallet().stopPolling();
	}
}

void TonConnectRequests::Flow::keyNeeded() {
	auto &wallet = _session->wallet();
	if (!_lifetime) {
		wallet.transferWalletIdentityChanges(
		) | rpl::on_next([=] {
			if (!claiming()) {
				unavailable();
			}
		}, _lifetime);
	}
	if (_silent) {
		_owner->_store->acquireSilentKey(
			_sessionId,
			crl::guard(this, [=](TonConnectKeyResult result) {
				keyReady(std::move(result));
			}));
		return;
	}
	const auto access = wallet.tonConnectAccess();
	if (access != TonConnectAccess::Allowed) {
		accessNotice(access);
		return;
	}
	auto cached = late()
		? base::take(_key)
		: _owner->_store->key(_sessionId);
	if (cached) {
		_key = std::move(cached);
		decrypt();
	} else if (VaultUnlockSilent(_session)) {
		requestKey();
	} else if (late() && !_chosen) {
		lateStop();
	} else {
		locked();
	}
}

void TonConnectRequests::Flow::waitForKey() {
	auto &vault = _session->wallet().vault();
	_keyLifetime.destroy();
	rpl::merge(
		vault.granted(),
		vault.protectionChanges()
	) | rpl::take(1) | rpl::on_next([=] {
		crl::on_main(this, [=] {
			if (!stopped()) {
				keyNeeded();
			}
		});
	}, _keyLifetime);
}

void TonConnectRequests::Flow::requestKey() {
	const auto done = crl::guard(this, [=](TonConnectKeyResult result) {
		keyReady(std::move(result));
	});
	if (late()) {
		_owner->_store->acquireClosedKey(showNow(), *_closedSession, done);
	} else {
		_owner->_store->acquireKey(showNow(), _sessionId, false, done);
	}
}

void TonConnectRequests::Flow::locked() {
	if (_recovery == TonConnectRecovery::Answer && !_box && !showBox()) {
		return;
	}
	const auto &current = _state.current();
	_state = TonConnectRequestBoxState{
		.phase = Phase::Locked,
		.name = current.name,
		.domain = current.domain,
		.icon = current.icon,
		.topic = TonConnectRequestText(_topic, current.name).text,
	};
}

void TonConnectRequests::Flow::unlockPressed() {
	auto state = _state.current();
	if (stopped() || state.phase != Phase::Locked || state.busy) {
		return;
	}
	state.busy = true;
	_state = std::move(state);
	requestKey();
}

bool TonConnectRequests::Flow::offerRestore() {
	if (stopped()
		|| _silent
		|| late()
		|| recovered()
		|| !_box
		|| claiming()) {
		return false;
	}
	_decision = Decision::None;
	_retriedChallenge = false;
	_auth = KeyAuthorization();
	_response = QByteArray();
	_notSent = QByteArray();
	_prepared = nullptr;
	_idleLifetime.destroy();
	const auto &current = _state.current();
	_state = TonConnectRequestBoxState{
		.phase = Phase::Restore,
		.name = current.name,
		.domain = current.domain,
		.icon = current.icon,
		.topic = TonConnectRequestText(_topic, current.name).text,
	};
	return true;
}

void TonConnectRequests::Flow::restorePressed() {
	if (stopped()
		|| _state.current().phase != Phase::Restore
		|| _state.current().busy) {
		return;
	}
	_keyLifetime.destroy();
	auto state = _state.current();
	state.busy = true;
	_state = std::move(state);
	AcquireWalletKey(
		showNow(),
		[weak = base::make_weak(this)] { return weak && !weak->stopped(); },
		_keyLifetime,
		crl::guard(this, [=](KeyAuthorization auth) {
			restored(std::move(auth));
		}),
		tr::lng_wallet_restore_ton_connect_text());
}

void TonConnectRequests::Flow::restored(KeyAuthorization auth) {
	if (stopped()) {
		return;
	} else if (!auth.grant) {
		auto state = _state.current();
		if (state.phase == Phase::Restore) {
			state.busy = false;
			_state = std::move(state);
		}
		return;
	}
	const auto access = _session->wallet().tonConnectAccess();
	if (access != TonConnectAccess::Allowed) {
		notice(AccessNoticeText(access));
		return;
	}
	_owner->_store->acquireKeyWith(
		_sessionId,
		KeyAuthorization{ .grant = std::move(auth.grant) },
		crl::guard(this, [=](TonConnectKeyResult result) {
			keyReady(std::move(result));
		}));
}

void TonConnectRequests::Flow::keyReady(TonConnectKeyResult result) {
	using Error = TonConnectKeyError;
	if (stopped()) {
		return;
	} else if (_silent && result.error != Error::None) {
		if (result.error == Error::Locked || result.error == Error::Blocked) {
			waitForKey();
		} else {
			_terminal = true;
			finish();
		}
		return;
	} else if (late() && !_chosen && result.error != Error::None) {
		lateStop();
		return;
	}
	switch (result.error) {
	case Error::None:
		_key = std::move(result.key);
		decrypt();
		return;
	case Error::Cancelled:
		locked();
		return;
	case Error::Locked:
		if (_box) {
			showNow()->showToast(tr::lng_wallet_vault_locked(tr::now));
		}
		locked();
		return;
	case Error::Blocked:
		accessNotice(_session->wallet().tonConnectAccess());
		return;
	case Error::OtherKey:
		notice(tr::lng_wallet_connect_request_other_key(tr::now));
		return;
	case Error::Failed:
		notice(tr::lng_wallet_connect_request_failed(tr::now));
		return;
	}
	Unexpected("Error in TonConnectRequests::Flow::keyReady.");
}

void TonConnectRequests::Flow::decrypt() {
	const auto &current = _state.current();
	_state = TonConnectRequestBoxState{
		.phase = Phase::Loading,
		.name = current.name,
		.domain = current.domain,
		.icon = current.icon,
	};
	if (_recovery == TonConnectRecovery::Answer) {
		answerRecovered();
		return;
	}
	_session->wallet().decryptTonConnectRequest(
		_key,
		_body,
		crl::guard(this, [=](TonConnectAppRequest request) {
			decrypted(std::move(request));
		}),
		crl::guard(this, [=] { unavailable(); }));
}

void TonConnectRequests::Flow::decrypted(TonConnectAppRequest request) {
	using Kind = TonConnectRequestKind;
	if (stopped()) {
		return;
	}
	_request = std::move(request);
	if (late()) {
		answerUnknownApp();
		return;
	} else if (_silent && _request.kind != Kind::Disconnect) {
		_terminal = true;
		finish();
		return;
	} else if (_recovery == TonConnectRecovery::Offer) {
		const auto record = _owner->claimRecord(_sessionId, _msgId);
		if (!record
			|| record->requestId != _request.id
			|| _request.kind != Kind::SendTransaction) {
			lateStop();
			return;
		}
	}
	switch (_request.kind) {
	case Kind::Disconnect:
		answerDisconnect();
		return;
	case Kind::Unsupported:
	case Kind::Invalid:
	case Kind::SendTransaction:
	case Kind::SignData:
		break;
	}
	const auto transfer = _request.transfer;
	if (!TonConnectRequestIdValid(_request.id)
		|| (_request.kind == Kind::SendTransaction && !transfer)
		|| (_request.kind == Kind::SignData && !_request.signData)) {
		unavailable();
	} else if (_request.kind == Kind::Unsupported) {
		showUnhandled(tr::lng_wallet_connect_request_unsupported(tr::now));
	} else if (_request.kind == Kind::Invalid) {
		showUnhandled(tr::lng_wallet_connect_request_unsupported(tr::now));
	} else if (_request.kind == Kind::SignData) {
		showSignData();
	} else {
		armDeadline(transfer->validUntil);
		preview();
	}
}

void TonConnectRequests::Flow::answerRecovered() {
	const auto record = _owner->claimRecord(_sessionId, _msgId);
	if (!record || record->signedBoc.isEmpty()) {
		lateStop();
		return;
	}
	const auto traceId = record->traceId;
	_session->wallet().encryptTonConnectResponse(
		_key,
		record->requestId,
		{ .signedBoc = record->signedBoc },
		crl::guard(this, [=](QByteArray body) {
			if (stopped()) {
				return;
			}
			_decision = Decision::Confirm;
			_sentBoc = true;
			_traceId = traceId;
			_claimSent = _claimed = true;
			answered(std::move(body));
		}),
		crl::guard(this, [=] {
			LOG(("Wallet Error: "
				"TON Connect recovered answer could not be encrypted."));
			lateStop();
		}));
}

void TonConnectRequests::Flow::preview() {
	auto &wallet = _session->wallet();
	if (!_previewOwner) {
		_previewOwner = wallet.createPreviewOwner(_previewLifetime);
	}
	const auto &current = _state.current();
	_state = TonConnectRequestBoxState{
		.phase = Phase::Confirm,
		.name = current.name,
		.domain = current.domain,
		.icon = current.icon,
		.transfer = _request.transfer,
		.feeLoading = true,
	};
	wallet.estimateTonConnect(
		_previewOwner,
		_request.transfer,
		crl::guard(this, [=](FeeResult result) {
			previewed(std::move(result));
		}));
}

void TonConnectRequests::Flow::previewed(FeeResult result) {
	if (stopped()
		|| _decision != Decision::None
		|| _state.current().phase != Phase::Confirm) {
		return;
	}
	auto state = _state.current();
	state.feeLoading = false;
	if (result.error == SendError::None && result.prepared) {
		_prepared = std::move(result.prepared);
		state.feeNano = result.feeNano;
		state.emulation = std::move(result.emulation);
		state.confirmable = true;
		state.error = QString();
		_state = std::move(state);
		return;
	}
	_prepared = nullptr;
	state.emulation = nullptr;
	state.confirmable = false;
	if (result.error == SendError::SigningUnavailable
		&& !_session->wallet().signingReady()) {
		state.feeLoading = true;
		state.error = QString();
		_state = std::move(state);
		_idleLifetime.destroy();
		_session->wallet().signingReadyValue(
		) | rpl::filter([](bool ready) {
			return ready;
		}) | rpl::take(1) | rpl::on_next([=](bool) {
			if (_decision == Decision::None && !stopped()) {
				preview();
			}
		}, _idleLifetime);
		return;
	}
	const auto text = SendErrorText(result.error, TransferMinNanos(_session));
	state.error = text.isEmpty()
		? tr::lng_wallet_connect_request_fee_failed(tr::now)
		: text;
	_state = std::move(state);
	if (result.error == SendError::PreviousUnresolved
		|| result.error == SendError::AlreadySending) {
		_idleLifetime.destroy();
		_session->wallet().sendStateValue(
		) | rpl::skip(1) | rpl::take(1) | rpl::on_next([=](SendState) {
			if (_decision == Decision::None && !stopped()) {
				preview();
			}
		}, _idleLifetime);
	}
}

void TonConnectRequests::Flow::showSignData() {
	const auto &current = _state.current();
	_state = TonConnectRequestBoxState{
		.phase = Phase::Confirm,
		.name = current.name,
		.domain = current.domain,
		.icon = current.icon,
		.signData = _request.signData,
		.confirmable = true,
	};
}

void TonConnectRequests::Flow::confirmPressed() {
	const auto signs = (_request.kind == TonConnectRequestKind::SignData);
	auto state = _state.current();
	if (stopped()
		|| _decision != Decision::None
		|| state.phase != Phase::Confirm
		|| (signs ? !_request.signData : !_prepared)
		|| state.busy) {
		return;
	}
	_decision = signs ? Decision::Sign : Decision::Confirm;
	state.busy = true;
	state.declining = false;
	state.error = QString();
	_state = std::move(state);
	_owner->_store->acquireKey(
		showNow(),
		_sessionId,
		true,
		crl::guard(this, [=](TonConnectKeyResult result) {
			confirmKeyReady(std::move(result));
		}));
}

void TonConnectRequests::Flow::confirmKeyReady(TonConnectKeyResult result) {
	if (stopped()) {
		return;
	} else if (result.error != TonConnectKeyError::None) {
		decisionKeyFailed(result.error);
		return;
	}
	auto &wallet = _session->wallet();
	_key = std::move(result.key);
	_auth = KeyAuthorization{ .grant = std::move(result.grant) };
	const auto access = wallet.tonConnectAccess();
	if (access != TonConnectAccess::Allowed) {
		accessNotice(access);
		return;
	} else if (_decision == Decision::Sign) {
		sign();
		return;
	}
	const auto refusal = wallet.sendRefusal(_prepared, _auth);
	if (refusal != SendError::None) {
		backToConfirm(SendErrorText(refusal, TransferMinNanos(_session)));
		return;
	}
	encryptNotSent();
}

void TonConnectRequests::Flow::decisionKeyFailed(TonConnectKeyError error) {
	using Error = TonConnectKeyError;
	switch (error) {
	case Error::None:
		break;
	case Error::Cancelled:
		backToConfirm(QString());
		return;
	case Error::Locked:
		if (_box) {
			showNow()->showToast(tr::lng_wallet_vault_locked(tr::now));
		}
		backToConfirm(QString());
		return;
	case Error::Blocked:
		accessNotice(_session->wallet().tonConnectAccess());
		return;
	case Error::OtherKey:
		notice(tr::lng_wallet_connect_request_other_key(tr::now));
		return;
	case Error::Failed:
		backToConfirm(tr::lng_wallet_connect_request_failed(tr::now));
		return;
	}
	Unexpected("Error in TonConnectRequests::Flow::decisionKeyFailed.");
}

void TonConnectRequests::Flow::sign() {
	const auto domain = SessionDomain(_owner->_store->session(_sessionId));
	_session->wallet().signTonConnectData(
		base::take(_auth),
		_key,
		_request.id,
		_request.signData,
		domain,
		crl::guard(this, [=](QByteArray body) {
			if (!stopped()) {
				_response = std::move(body);
				registerKey();
			}
		}),
		crl::guard(this, [=](TonConnectKeyError error) {
			if (!stopped()) {
				decisionKeyFailed(error);
			}
		}));
}

void TonConnectRequests::Flow::declinePressed() {
	auto state = _state.current();
	const auto unhandled = (state.phase == Phase::Unhandled);
	if (stopped()
		|| _decision != Decision::None
		|| (state.phase != Phase::Confirm && !unhandled)
		|| state.busy) {
		return;
	}
	_decision = unhandled ? Decision::Reject : Decision::Decline;
	state.busy = true;
	state.declining = true;
	state.error = QString();
	_state = std::move(state);
	const auto access = _session->wallet().tonConnectAccess();
	if (access != TonConnectAccess::Allowed) {
		accessNotice(access);
		return;
	}
	encryptAnswer({
		.error = (unhandled
			? _request.rejection
			: TonConnectError::UserDeclined),
	});
}

void TonConnectRequests::Flow::showUnhandled(const QString &text) {
	if (!_box) {
		finish();
		return;
	}
	const auto &current = _state.current();
	_state = TonConnectRequestBoxState{
		.phase = Phase::Unhandled,
		.name = current.name,
		.domain = current.domain,
		.icon = current.icon,
		.notice = text,
	};
}

void TonConnectRequests::Flow::answerDisconnect() {
	_silent = true;
	closeBox();
	_decision = Decision::Disconnect;
	if (!TonConnectRequestIdValid(_request.id)) {
		unavailable();
		return;
	}
	encryptAnswer({ .disconnected = true });
}

void TonConnectRequests::Flow::answerUnknownApp() {
	if (_request.kind == TonConnectRequestKind::Disconnect
		|| !TonConnectRequestIdValid(_request.id)) {
		lateStop();
		return;
	}
	lateCloseBox();
	encryptAnswer({ .error = TonConnectError::UnknownApp });
}

void TonConnectRequests::Flow::encryptAnswer(TonConnectResponse response) {
	_session->wallet().encryptTonConnectResponse(
		_key,
		_request.id,
		std::move(response),
		crl::guard(this, [=](QByteArray body) {
			if (!stopped()) {
				_response = std::move(body);
				if (late()) {
					claim(QByteArray());
				} else {
					registerKey();
				}
			}
		}),
		crl::guard(this, [=] {
			if (!stopped()) {
				decisionFailed();
			}
		}));
}

void TonConnectRequests::Flow::encryptNotSent() {
	_session->wallet().encryptTonConnectResponse(
		_key,
		_request.id,
		{ .error = TonConnectError::Unknown },
		crl::guard(this, [=](QByteArray body) {
			if (!stopped()) {
				_notSent = std::move(body);
				registerKey();
			}
		}),
		crl::guard(this, [=] {
			if (!stopped()) {
				decisionFailed();
			}
		}));
}

void TonConnectRequests::Flow::registerKey() {
	if (!_key) {
		decisionFailed();
		return;
	}
	_api.request(MTPwallet_TonConnectRegisterKey(
		MTP_long(_sessionId),
		MTP_string(_key.clientId)
	)).done([=](const MTPwallet_TonConnectChallenge &result) {
		registered(result.data().vchallenge().v);
	}).fail([=](const MTP::Error &error) {
		registerFailed(error);
	}).send();
}

void TonConnectRequests::Flow::registered(const QByteArray &challenge) {
	if (stopped()) {
		return;
	}
	_session->wallet().answerTonConnectChallenge(
		_key,
		challenge,
		crl::guard(this, [=](QByteArray answer) {
			claim(answer);
		}),
		crl::guard(this, [=] {
			if (!stopped()) {
				decisionFailed();
			}
		}));
}

void TonConnectRequests::Flow::registerFailed(const MTP::Error &error) {
	if (stopped()) {
		return;
	}
	const auto &type = error.type();
	LOG(("Wallet Error: wallet.tonConnectRegisterKey failed: %1"
		).arg(type));
	if (type == u"TONCONNECT_CLIENT_ID_OCCUPIED"_q) {
		notice(tr::lng_wallet_connect_request_other_key(tr::now));
	} else if (SessionGone(type)) {
		unavailable();
	} else {
		decisionFailed(type);
	}
}

void TonConnectRequests::Flow::claim(const QByteArray &answer) {
	if (stopped() || _claimSent) {
		return;
	} else if (base::unixtime::now() >= _expires) {
		expired();
		return;
	}
	const auto access = _session->wallet().tonConnectAccess();
	const auto allowed = (_decision == Decision::Disconnect)
		? _owner->_store->participates(_sessionId)
		: (access == TonConnectAccess::Allowed);
	if (!allowed) {
		accessNotice(access);
		return;
	} else if (!TonConnectRequestIdValid(_request.id)) {
		unavailable();
		return;
	}
	const auto identity = _session->wallet().transferWalletIdentity();
	const auto confirm = (_decision == Decision::Confirm);
	const auto record = TonConnectClaimRecord{
		.sessionId = _sessionId,
		.msgId = int32(_msgId.bare),
		.requestId = _request.id,
		.traceId = _traceId,
		.expires = _expires,
		.created = base::unixtime::now(),
		.address = identity ? identity->address : QString(),
		.publicKey = identity ? identity->publicKey : QByteArray(),
		.decision = (confirm
			? TonConnectClaimDecision::Confirm
			: TonConnectClaimDecision::Answer),
		.answer = confirm ? QByteArray() : _response,
		.notSent = confirm ? _notSent : QByteArray(),
	};
	const auto recorded = identity
		&& _owner->updateClaims([&](TonConnectClaimStore &store) {
			auto &records = store.records;
			const auto i = ranges::find_if(records, [&](const auto &entry) {
				return TonConnectClaimMatches(
					entry,
					record.sessionId,
					record.msgId);
			});
			if (i != end(records)) {
				*i = record;
			} else {
				records.push_back(record);
			}
		});
	if (!recorded) {
		LOG(("Wallet Error: TON Connect claim could not be recorded."));
		decisionFailed();
		return;
	}
	_claimSent = true;
	using Flag = MTPwallet_TonConnectClaimRequest::Flag;
	_api.request(MTPwallet_TonConnectClaimRequest(
		MTP_flags((late() ? Flag(0) : Flag::f_challenge_answer)
			| (declines() ? Flag::f_declined : Flag(0))),
		MTP_long(_sessionId),
		MTP_int(_msgId.bare),
		MTP_string(_request.id),
		MTP_bytes(answer)
	)).done([=](const MTPBool &result) {
		claimed(result);
	}).fail([=](const MTP::Error &error) {
		claimFailed(error);
	}).send();
}

void TonConnectRequests::Flow::claimed(const MTPBool &result) {
	if (stopped()) {
		return;
	} else if (!mtpIsTrue(result)) {
		_claimSent = false;
		if (_recovery != TonConnectRecovery::Offer) {
			_owner->forgetClaim(_sessionId, _msgId);
		}
		unavailable();
		return;
	}
	_claimed = true;
	if (_decision == Decision::Confirm) {
		send();
	} else {
		publish();
	}
}

void TonConnectRequests::Flow::claimFailed(const MTP::Error &error) {
	if (stopped()) {
		return;
	}
	const auto &type = error.type();
	const auto code = error.code();
	LOG(("Wallet Error: wallet.tonConnectClaimRequest failed: %1 (%2)"
		).arg(type).arg(code));
	_claimSent = false;
	if (type == u"TONCONNECT_CHALLENGE_INVALID"_q
		&& !_retriedChallenge
		&& !late()) {
		_retriedChallenge = true;
		registerKey();
		return;
	}
	const auto claimedElsewhere
		= (type == u"TONCONNECT_REQUEST_ALREADY_CLAIMED"_q);
	if (claimedElsewhere || _recovery != TonConnectRecovery::Offer) {
		_owner->forgetClaim(_sessionId, _msgId);
	}
	if (claimedElsewhere) {
		closeWithToast(tr::lng_wallet_connect_request_handled(tr::now));
	} else if (RequestDropped(type)) {
		unavailable();
	} else {
		decisionFailed(type);
	}
}

void TonConnectRequests::Flow::send() {
	if (_sendStarted) {
		return;
	}
	_sendStarted = true;
	const auto sessionId = _sessionId;
	const auto msgId = _msgId;
	const auto operationId = QUuid::createUuid().toString(
		QUuid::WithoutBraces).toStdString();
	_operationId = operationId;
	const auto linked = _owner->updateClaim(sessionId, msgId, [&](
			TonConnectClaimRecord &record) {
		record.operationId = operationId;
		record.signedBoc = QString();
	});
	if (!linked) {
		LOG(("Wallet Error: TON Connect send could not be linked."));
		sent({ .error = SendError::Failed });
		return;
	}
	const auto owner = base::make_weak(_owner.get());
	auto link = TonConnectSendLink{
		.operationId = operationId,
		.handoff = [=](const QString &signedBoc) {
			const auto strong = owner.get();
			auto current = false;
			const auto stored = strong && strong->updateClaim(
				sessionId,
				msgId,
				[&](TonConnectClaimRecord &record) {
					current = (record.operationId == operationId);
					if (current) {
						record.signedBoc = signedBoc;
					}
				});
			return stored && current;
		},
	};
	_session->wallet().sendTonConnect(
		base::take(_auth),
		_prepared,
		std::move(link),
		crl::guard(this, [=](TonConnectSendResult result) {
			sent(std::move(result));
		}),
		[show = showNow()](SendError error) {
			if (error == SendError::KeyChanged && show->valid()) {
				ShowWalletKeyChanged(show);
			}
		});
}

void TonConnectRequests::Flow::sent(TonConnectSendResult result) {
	if (stopped()) {
		return;
	}
	_sentBoc = !result.signedBoc.isEmpty();
	if (!_sentBoc) {
		answered(_notSent);
		return;
	}
	_session->wallet().encryptTonConnectResponse(
		_key,
		_request.id,
		{ .signedBoc = result.signedBoc },
		crl::guard(this, [=](QByteArray body) {
			if (!stopped()) {
				answered(std::move(body));
			}
		}),
		crl::guard(this, [=] {
			if (stopped()) {
				return;
			}
			LOG(("Wallet Error: TON Connect success answer could not "
				"be encrypted, answering an error."));
			_sentBoc = false;
			answered(_notSent);
		}));
}

void TonConnectRequests::Flow::answered(QByteArray body) {
	_response = std::move(body);
	const auto sessionId = _sessionId;
	const auto msgId = _msgId;
	const auto stored = _owner->updateClaim(sessionId, msgId, [&](
			TonConnectClaimRecord &record) {
		record.answer = _response;
	});
	if (!stored) {
		LOG(("Wallet Error: TON Connect answer could not be recorded."));
	}
	publish();
}

void TonConnectRequests::Flow::publish() {
	if (stopped()) {
		return;
	}
	SubmitResponse(
		_api,
		_sessionId,
		_msgId,
		_response,
		_traceId,
		[=](SubmitResult result) { published(result); });
}

void TonConnectRequests::Flow::published(SubmitResult result) {
	if (stopped()) {
		return;
	} else if (result != SubmitResult::RetryLater) {
		_owner->forgetClaim(_sessionId, _msgId);
	}
	if (result != SubmitResult::Delivered) {
		closeWithToast(tr::lng_wallet_connect_request_undelivered(tr::now));
		return;
	}
	switch (_decision) {
	case Decision::Confirm:
		if (_sentBoc) {
			closeWithTransfer();
		} else {
			closeWithToast(tr::lng_wallet_connect_request_not_sent(tr::now));
		}
		return;
	case Decision::Sign:
		closeWithToast(tr::lng_wallet_connect_sign_done(tr::now));
		return;
	case Decision::Disconnect:
		// WHY: the server keeps the session active after the {} answer, and
		// it accepts a close without a body after a dApp's disconnect, which
		// the spec asks the wallet not to echo back as a disconnect event.
		_owner->_store->closeAnswered(_sessionId);
		[[fallthrough]];
	case Decision::Decline:
	case Decision::Reject:
	case Decision::None:
		_terminal = true;
		closeBox();
		finish();
		return;
	}
	Unexpected("Decision in TonConnectRequests::Flow::published.");
}

void TonConnectRequests::Flow::decisionFailed(const QString &type) {
	backToConfirm(ErrorWithType(
		tr::lng_wallet_connect_request_failed(tr::now),
		type));
}

void TonConnectRequests::Flow::armDeadline(std::optional<TimeId> validUntil) {
	const auto deadline = validUntil
		? std::min(_expires, *validUntil)
		: _expires;
	const auto left = std::max(
		crl::time(deadline) - base::unixtime::now(),
		crl::time(0));
	_deadlineTimer.callOnce(
		std::min(left * 1000, kDeadlineMaxDelay),
		Qt::PreciseTimer);
}

void TonConnectRequests::Flow::expired() {
	if (claiming()) {
		return;
	}
	closeWithToast(tr::lng_wallet_connect_request_expired(tr::now));
}

void TonConnectRequests::Flow::unavailable() {
	closeWithToast(tr::lng_wallet_connect_request_unavailable(tr::now));
}

void TonConnectRequests::Flow::accessNotice(TonConnectAccess access) {
	if (access == TonConnectAccess::NoCurrentKey && offerRestore()) {
		return;
	}
	notice(AccessNoticeText(access));
}

void TonConnectRequests::Flow::notice(const QString &text) {
	if (late() || (recovered() && !claiming())) {
		lateStop();
		return;
	}
	_terminal = true;
	_auth = KeyAuthorization();
	stopResolving();
	_deadlineTimer.cancel();
	if (!_box) {
		finish();
		return;
	}
	const auto &current = _state.current();
	_state = TonConnectRequestBoxState{
		.phase = Phase::Notice,
		.name = current.name,
		.domain = current.domain,
		.icon = current.icon,
		.notice = text,
	};
}

void TonConnectRequests::Flow::closeWithToast(const QString &text) {
	if (stopped()) {
		return;
	} else if (late()
		|| (recovered() && !claiming())
		|| (_recovery == TonConnectRecovery::Answer && !_box)) {
		lateStop();
		return;
	}
	_terminal = true;
	if (_silent) {
		closeBox();
		finish();
		return;
	}
	const auto show = showNow();
	closeBox();
	if (show->valid()) {
		show->showToast(text);
	}
	finish();
}

void TonConnectRequests::Flow::closeWithTransfer() {
	if (late()
		|| _silent
		|| !_box
		|| _operationId.empty()
		|| Core::App().passcodeLocked()) {
		closeWithToast(tr::lng_wallet_connect_request_sent(tr::now));
		return;
	} else if (stopped()) {
		return;
	}
	_terminal = true;
	closeBox();
	const auto session = _session;
	const auto operationId = _operationId;
	const auto transfer = _request.transfer;
	const auto single = transfer && (transfer->messages.size() == 1);
	finish();
	const auto panel = ShowWallet(session);
	if (single) {
		ShowSubmittedTransfer(
			Main::MakeSessionShow(panel->uiShow(), session),
			operationId);
	}
}

void TonConnectRequests::Flow::backToConfirm(const QString &error) {
	if (late()) {
		lateStop();
		return;
	}
	const auto rejecting = (_decision == Decision::Reject);
	_decision = Decision::None;
	_retriedChallenge = false;
	_auth = KeyAuthorization();
	_response = QByteArray();
	if (!_box) {
		finish();
		return;
	}
	auto state = _state.current();
	state.phase = rejecting ? Phase::Unhandled : Phase::Confirm;
	state.busy = false;
	state.declining = false;
	state.error = error;
	_state = std::move(state);
}

void TonConnectRequests::Flow::dismissed() {
	if (_closingBox || _finished) {
		return;
	}
	_box.reset();
	if (_decision != Decision::None && !_terminal) {
		return;
	}
	finish();
}

void TonConnectRequests::Flow::lateStop() {
	if (stopped()) {
		return;
	}
	_terminal = true;
	lateCloseBox();
	finish();
}

void TonConnectRequests::Flow::lateCloseBox() {
	if (!_box) {
		return;
	}
	const auto show = showNow();
	closeBox();
	if (_chosen && show->valid()) {
		show->showToast(tr::lng_wallet_connect_request_closed(tr::now));
	}
}

void TonConnectRequests::Flow::closeBox() {
	_closingBox = true;
	if (const auto box = _box.get()) {
		_box.reset();
		if (box->hasDelegate()) {
			box->closeBox();
		}
	}
}

void TonConnectRequests::Flow::finish() {
	if (std::exchange(_finished, true)) {
		return;
	}
	_auth = KeyAuthorization();
	stopResolving();
	_deadlineTimer.cancel();
	_previewLifetime.destroy();
	_idleLifetime.destroy();
	_keyLifetime.destroy();
	_lifetime.destroy();
	_owner->flowDone(this, claiming());
}

bool TonConnectRequests::Flow::late() const {
	return _closedSession.has_value();
}

bool TonConnectRequests::Flow::recovered() const {
	return (_recovery != TonConnectRecovery::None);
}

bool TonConnectRequests::Flow::stopped() const {
	return _terminal || _finished;
}

bool TonConnectRequests::Flow::claiming() const {
	return _claimSent || _claimed;
}

bool TonConnectRequests::Flow::declines() const {
	return (_decision == Decision::Decline)
		|| (_decision == Decision::Reject)
		|| late();
}

std::shared_ptr<Main::SessionShow> TonConnectRequests::Flow::showNow() const {
	if (_show->valid()) {
		return _show;
	} else if (const auto controller = _controller.get()) {
		return TonConnectBoxShowNoActivate(controller);
	}
	return _show;
}

}
