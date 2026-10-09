#include "wallet/wallet_ton_connect_request_internal.h"

namespace Wallet {

namespace RequestDetails {}

using namespace RequestDetails;

namespace RequestDetails {

[[nodiscard]] bool SessionGone(const QString &type) {
	return (type == u"TONCONNECT_SESSION_CLOSED"_q)
		|| (type == u"TONCONNECT_SESSION_NOT_FOUND"_q);
}

[[nodiscard]] bool RequestDropped(const QString &type) {
	const auto list = std::array{
		u"TONCONNECT_BAD_REQUEST_ID"_q,
		u"TONCONNECT_REQUEST_EXPIRED"_q,
		u"TONCONNECT_REQUEST_NOT_FOUND"_q,
		u"TONCONNECT_SESSION_NOT_ACTIVE"_q,
	};
	return SessionGone(type) || ranges::contains(list, type);
}

void SubmitResponse(
		MTP::Sender &api,
		TonConnectSessionId sessionId,
		MsgId msgId,
		const QByteArray &body,
		const QString &traceId,
		Fn<void(SubmitResult)> done) {
	using Flag = MTPwallet_TonConnectSubmitResponse::Flag;
	api.request(MTPwallet_TonConnectSubmitResponse(
		MTP_flags(traceId.isEmpty() ? Flag(0) : Flag::f_trace_id),
		MTP_long(sessionId),
		MTP_int(msgId.bare),
		MTP_bytes(body),
		MTP_string(traceId)
	)).done([=](const MTPBool &result) {
		if (mtpIsTrue(result)) {
			done(SubmitResult::Delivered);
			return;
		}
		LOG(("Wallet Error: wallet.tonConnectSubmitResponse failed: FALSE"));
		done(SubmitResult::Refused);
	}).fail([=](const MTP::Error &error) {
		const auto &type = error.type();
		LOG(("Wallet Error: wallet.tonConnectSubmitResponse failed: %1"
			).arg(type));
		done(RequestDropped(type)
			? SubmitResult::Refused
			: SubmitResult::RetryLater);
	}).send();
}

[[nodiscard]] QString SessionName(const TonConnectSessionInfo *info) {
	return (info && info->manifest)
		? TonConnectManifestName(*info->manifest)
		: QString();
}

[[nodiscard]] QString SessionDomain(const TonConnectSessionInfo *info) {
	return (info && info->manifest)
		? TonConnectHost(info->manifest->url)
		: QString();
}

[[nodiscard]] WebFileLocation SessionIcon(
		const TonConnectSessionInfo *info) {
	return (info && info->manifest)
		? info->manifest->icon
		: WebFileLocation();
}

[[nodiscard]] QString AccessNoticeText(TonConnectAccess access) {
	switch (access) {
	case TonConnectAccess::KeyChanging:
		return tr::lng_wallet_import_key_changing(tr::now);
	case TonConnectAccess::WalletNotReady:
	case TonConnectAccess::Busy:
		return tr::lng_wallet_state_error(tr::now);
	case TonConnectAccess::NoCurrentKey:
	case TonConnectAccess::Allowed:
		return tr::lng_wallet_connect_request_failed(tr::now);
	}
	Unexpected("Access in TON Connect AccessNoticeText.");
}

}

TonConnectRequests::TonConnectRequests(
	not_null<Main::Session*> session,
	not_null<TonConnect*> store)
: _session(session)
, _store(store)
, _api(&session->mtp()) {
	_session->data().newItemAdded(
	) | rpl::on_next([=](not_null<HistoryItem*> item) {
		arrived(item);
	}, _lifetime);

	_session->changes().messageUpdates(
		Data::MessageUpdate::Flag::Edited
	) | rpl::on_next([=](const Data::MessageUpdate &update) {
		edited(update.item);
	}, _lifetime);

	_store->updates(
	) | rpl::on_next([=](TonConnectSessionId id) {
		sessionClosed(id);
	}, _lifetime);

	crl::on_main(this, [=] { loadClaims(); });
}

TonConnectRequests::~TonConnectRequests() = default;

std::optional<TonConnectRequests::Entry> TonConnectRequests::PendingEntry(
		not_null<HistoryItem*> item) {
	if (item->history()->peer->id != PeerData::kServiceNotificationsId) {
		return std::nullopt;
	}
	const auto request = item->Get<HistoryServiceTonConnectRequest>();
	if (!request
		|| request->accepted
		|| request->declined
		|| request->expires <= base::unixtime::now()) {
		return std::nullopt;
	}
	return Entry{
		.sessionId = request->sessionId,
		.msgId = item->id,
		.topic = request->topic,
		.expires = request->expires,
	};
}

bool TonConnectRequests::OpensByItself(const Entry &entry) {
	return (entry.topic != u"disconnect"_q);
}

Window::SessionController *TonConnectRequests::autoWindow() const {
	const auto &windows = _session->windows();
	for (const auto &window : windows) {
		if (window->isPrimary()) {
			return window.get();
		}
	}
	return windows.empty() ? nullptr : windows.front().get();
}

bool TonConnectRequests::enqueue(Entry entry) {
	const auto recovered = (entry.recovery != TonConnectRecovery::None);
	if (!OpensByItself(entry)
		|| (!recovered && _claimedIds.contains(entry.msgId))
		|| (_active && _active->matches(entry.msgId))
		|| silentOwns(entry.msgId)
		|| ranges::contains(_waiting, entry.msgId, &Entry::msgId)) {
		return false;
	}
	entry.order = ++_order;
	_waiting.push_back(std::move(entry));
	return true;
}

void TonConnectRequests::arrived(not_null<HistoryItem*> item) {
	if (_stopped) {
		return;
	}
	auto entry = PendingEntry(item);
	if (!entry
		|| _claimedIds.contains(entry->msgId)
		|| (_active && _active->matches(entry->msgId))
		|| silentOwns(entry->msgId)
		|| ranges::contains(_waiting, entry->msgId, &Entry::msgId)) {
		return;
	} else if (!OpensByItself(*entry)) {
		startSilent(std::move(*entry));
	} else if (enqueue(std::move(*entry))) {
		crl::on_main(this, [=] { showNext(); });
	}
}

void TonConnectRequests::edited(not_null<HistoryItem*> item) {
	if (item->history()->peer->id != PeerData::kServiceNotificationsId) {
		return;
	}
	const auto request = item->Get<HistoryServiceTonConnectRequest>();
	if (!request || (!request->accepted && !request->declined)) {
		return;
	}
	const auto i = ranges::find(_waiting, item->id, &Entry::msgId);
	if (i != end(_waiting) && i->recovery == TonConnectRecovery::None) {
		_waiting.erase(i);
	}
	if (_active && _active->matches(item->id) && !_active->recovered()) {
		_active->editedElsewhere();
	}
	for (const auto &flow : _silent) {
		if (flow->matches(item->id)) {
			flow->editedElsewhere();
		}
	}
}

void TonConnectRequests::showNext() {
	if (_stopped || _active) {
		return;
	}
	const auto now = base::unixtime::now();
	auto offerExpired = false;
	_waiting.erase(ranges::remove_if(_waiting, [&](const Entry &entry) {
		switch (entry.recovery) {
		case TonConnectRecovery::Answer:
			return false;
		case TonConnectRecovery::Offer:
			if (entry.expires > now) {
				return false;
			}
			offerExpired = true;
			return true;
		case TonConnectRecovery::None:
			break;
		}
		return (entry.expires <= now) || _claimedIds.contains(entry.msgId);
	}), end(_waiting));
	if (offerExpired) {
		recoverClaims();
	}
	if (_waiting.empty()) {
		return;
	}
	const auto window = autoWindow();
	if (!window) {
		return;
	}
	auto i = ranges::min_element(_waiting, ranges::less(), &Entry::order);
	if (!i->chosen) {
		const auto sessionId = i->sessionId;
		for (auto j = begin(_waiting); j != end(_waiting); ++j) {
			if ((j->sessionId == sessionId) && (j->msgId < i->msgId)) {
				i = j;
			}
		}
	}
	auto entry = std::move(*i);
	_waiting.erase(i);
	start(
		std::move(entry),
		base::make_weak(window),
		TonConnectBoxShowNoActivate(window));
}

void TonConnectRequests::open(
		not_null<Window::SessionController*> controller,
		FullMsgId itemId) {
	if (_stopped || itemId.peer != PeerData::kServiceNotificationsId) {
		return;
	}
	const auto item = _session->data().message(itemId);
	if (!item) {
		return;
	}
	const auto entry = PendingEntry(item);
	if (!entry || _claimedIds.contains(entry->msgId)) {
		return;
	}
	opened(*entry, controller);
}

void TonConnectRequests::opened(
		Entry entry,
		not_null<Window::SessionController*> controller) {
	if (silentOwns(entry.msgId)) {
		return;
	}
	entry.chosen = true;
	if (_active && _active->matches(entry.msgId)) {
		_active->activate();
		return;
	}
	const auto i = ranges::find(_waiting, entry.msgId, &Entry::msgId);
	if (i != end(_waiting)) {
		_waiting.erase(i);
	}
	if (_active) {
		entry.order = 0;
		_waiting.insert(begin(_waiting), std::move(entry));
		_active->activate();
		return;
	}
	start(
		std::move(entry),
		base::make_weak(controller),
		TonConnectBoxShow(controller));
}

void TonConnectRequests::openPending(
		not_null<Window::SessionController*> controller,
		const QString &dappClientId) {
	if (_stopped) {
		return;
	}
	_api.request(base::take(_pendingRequestId)).cancel();
	const auto weak = base::make_weak(controller);
	using Flag = MTPwallet_TonConnectGetPending::Flag;
	_pendingRequestId = _api.request(MTPwallet_TonConnectGetPending(
		MTP_flags(Flag::f_dapp_client_id),
		MTP_string(dappClientId),
		MTPlong()
	)).done([=](const MTPwallet_TonConnectPending &result) {
		_pendingRequestId = 0;
		pendingLoaded(weak, result);
	}).fail([=](const MTP::Error &error) {
		_pendingRequestId = 0;
		const auto &type = error.type();
		if (type != u"TONCONNECT_SESSION_NOT_FOUND"_q) {
			LOG(("Wallet Error: wallet.tonConnectGetPending failed: %1"
				).arg(type));
		}
	}).send();
}

void TonConnectRequests::pendingLoaded(
		base::weak_ptr<Window::SessionController> controller,
		const MTPwallet_TonConnectPending &result) {
	const auto &data = result.data();
	_store->apply(data.vsession());
	const auto closed = data.vsession().data().is_closed();
	const auto now = base::unixtime::now();
	auto oldest = std::optional<Entry>();
	for (const auto &request : data.vrequests().v) {
		const auto &fields = request.data();
		const auto msgId = MsgId(fields.vmsg_id().v);
		if (fields.vexpires().v <= now
			|| _claimedIds.contains(msgId)
			|| silentOwns(msgId)) {
			continue;
		} else if (!oldest || msgId < oldest->msgId) {
			oldest = Entry{
				.sessionId = uint64(fields.vsession_id().v),
				.msgId = msgId,
				.topic = qs(fields.vtopic().value_or_empty()),
				.expires = fields.vexpires().v,
			};
		}
	}
	if (!oldest) {
		if (!closed) {
			ShowWallet(_session);
		}
		return;
	}
	const auto strong = controller.get();
	if (!strong) {
		return;
	}
	opened(std::move(*oldest), strong);
}

void TonConnectRequests::sessionClosed(TonConnectSessionId id) {
	if (_stopped || !id || _store->session(id)) {
		return;
	}
	if (_active
		&& (_active->_sessionId == id)
		&& !_active->late()
		&& !_active->claiming()
		&& !_active->stopped()) {
		_active->closeWithToast(
			tr::lng_wallet_connect_request_closed(tr::now));
	}
	using Flag = MTPwallet_TonConnectGetPending::Flag;
	_api.request(MTPwallet_TonConnectGetPending(
		MTP_flags(Flag::f_session_id),
		MTPstring(),
		MTP_long(id)
	)).done([=](const MTPwallet_TonConnectPending &result) {
		closedLoaded(result);
	}).fail([=](const MTP::Error &error) {
		const auto &type = error.type();
		if (type != u"TONCONNECT_SESSION_NOT_FOUND"_q) {
			LOG(("Wallet Error: wallet.tonConnectGetPending failed: %1"
				).arg(type));
		}
	}).send();
}

void TonConnectRequests::closedLoaded(
		const MTPwallet_TonConnectPending &result) {
	const auto &data = result.data();
	if (_stopped || !data.vsession().data().is_closed()) {
		return;
	}
	const auto now = base::unixtime::now();
	auto queued = false;
	for (const auto &request : data.vrequests().v) {
		const auto &fields = request.data();
		const auto expires = fields.vexpires().v;
		if (expires <= now) {
			continue;
		} else if (enqueue({
				.sessionId = uint64(fields.vsession_id().v),
				.msgId = MsgId(fields.vmsg_id().v),
				.topic = qs(fields.vtopic().value_or_empty()),
				.expires = expires,
			})) {
			queued = true;
		}
	}
	if (queued) {
		crl::on_main(this, [=] { showNext(); });
	}
}

void TonConnectRequests::start(
		Entry entry,
		base::weak_ptr<Window::SessionController> controller,
		std::shared_ptr<Main::SessionShow> show) {
	_active = std::make_unique<Flow>(
		this,
		std::move(controller),
		std::move(show),
		std::move(entry),
		false);
	_active->start();
}

void TonConnectRequests::startSilent(Entry entry) {
	_silent.push_back(std::make_unique<Flow>(
		this,
		nullptr,
		nullptr,
		std::move(entry),
		true));
	_silent.back()->start();
}

bool TonConnectRequests::silentOwns(MsgId msgId) const {
	return ranges::any_of(_silent, [&](const std::unique_ptr<Flow> &flow) {
		return flow->matches(msgId);
	});
}

void TonConnectRequests::flowDone(not_null<Flow*> flow, bool claimed) {
	const auto sessionId = flow->_sessionId;
	const auto msgId = flow->_msgId;
	const auto recovery = flow->_recovery;
	const auto sendStarted = flow->_sendStarted;
	crl::on_main(this, [=] {
		const auto active = (_active.get() == flow);
		const auto i = ranges::find(
			_silent,
			flow.get(),
			&std::unique_ptr<Flow>::get);
		if (!active && i == end(_silent)) {
			return;
		} else if (claimed) {
			_claimedIds.emplace(msgId);
		}
		if (active) {
			_active = nullptr;
		} else {
			_silent.erase(i);
		}
		if (recovery == TonConnectRecovery::Offer && !sendStarted) {
			if (const auto record = claimRecord(sessionId, msgId)) {
				const auto copy = *record;
				submitStored(
					copy,
					copy.answer.isEmpty() ? copy.notSent : copy.answer);
			}
		} else if (recovery == TonConnectRecovery::Answer) {
			_recoveryHeld.emplace(msgId);
		}
		if (recovery != TonConnectRecovery::None) {
			recoverClaims();
		}
		if (active) {
			showNext();
		}
	});
}

TonConnectClaimStore &TonConnectRequests::claims() {
	if (!_claims) {
		_claims = ReadTonConnectClaims(_session->local());
		if (!_claims) {
			LOG(("Wallet Error: TON Connect claims could not be read."));
			_claims = TonConnectClaimStore();
		}
	}
	return *_claims;
}

TonConnectClaimRecord *TonConnectRequests::claimRecord(
		TonConnectSessionId sessionId,
		MsgId msgId) {
	auto &records = claims().records;
	const auto i = ranges::find_if(records, [&](const auto &record) {
		return TonConnectClaimMatches(record, sessionId, msgId.bare);
	});
	return (i != end(records)) ? &*i : nullptr;
}

bool TonConnectRequests::updateClaims(
		Fn<void(TonConnectClaimStore&)> change) {
	auto updated = claims();
	change(updated);
	const auto now = base::unixtime::now();
	updated.records.erase(ranges::remove_if(updated.records, [&](
			const TonConnectClaimRecord &record) {
		return now > record.expires + kTonConnectClaimLifetime;
	}), end(updated.records));
	if (!WriteTonConnectClaims(_session->local(), updated)) {
		return false;
	}
	_claims = std::move(updated);
	return true;
}

bool TonConnectRequests::updateClaim(
		TonConnectSessionId sessionId,
		MsgId msgId,
		Fn<void(TonConnectClaimRecord&)> change) {
	if (!claimRecord(sessionId, msgId)) {
		return false;
	}
	return updateClaims([&](TonConnectClaimStore &store) {
		auto &records = store.records;
		const auto i = ranges::find_if(records, [&](const auto &record) {
			return TonConnectClaimMatches(record, sessionId, msgId.bare);
		});
		change(*i);
	});
}

void TonConnectRequests::forgetClaim(
		TonConnectSessionId sessionId,
		MsgId msgId) {
	const auto forgotten = updateClaims([&](TonConnectClaimStore &store) {
		store.records.erase(ranges::remove_if(store.records, [&](
				const TonConnectClaimRecord &record) {
			return TonConnectClaimMatches(record, sessionId, msgId.bare);
		}), end(store.records));
	});
	if (!forgotten) {
		LOG(("Wallet Error: TON Connect claim could not be forgotten."));
	}
}

void TonConnectRequests::loadClaims() {
	if (_stopped) {
		return;
	}
	const auto now = base::unixtime::now();
	const auto stale = ranges::any_of(claims().records, [&](
			const TonConnectClaimRecord &record) {
		return now > record.expires + kTonConnectClaimLifetime;
	});
	if (stale && !updateClaims([](TonConnectClaimStore &) {})) {
		LOG(("Wallet Error: TON Connect claims could not be pruned."));
	}
	const auto records = claims().records;
	if (records.empty()) {
		return;
	}
	auto confirmWaits = false;
	for (const auto &record : records) {
		_claimedIds.emplace(MsgId(record.msgId));
	}
	for (const auto &record : records) {
		if (!record.answer.isEmpty()) {
			submitStored(record, record.answer);
		} else if (record.decision == TonConnectClaimDecision::Confirm) {
			confirmWaits = true;
		}
	}
	if (!confirmWaits) {
		return;
	}
	auto &wallet = _session->wallet();
	rpl::merge(
		wallet.presenceValue() | rpl::to_empty,
		wallet.signingReadyValue() | rpl::to_empty,
		wallet.transferWalletIdentityChanges(),
		wallet.historyUpdates(),
		_store->updates() | rpl::to_empty
	) | rpl::on_next([=] {
		recoverClaims();
	}, _recoveryLifetime);
	recoverClaims();
}

void TonConnectRequests::recoverClaims() {
	if (_stopped) {
		return;
	}
	auto &wallet = _session->wallet();
	const auto identity = (wallet.presenceCurrent() == Presence::Ready)
		? wallet.transferWalletIdentity()
		: std::nullopt;
	auto journalWaits = false;
	const auto records = claims().records;
	for (const auto &record : records) {
		const auto msgId = MsgId(record.msgId);
		if (_recoveryHeld.contains(msgId)
			|| (_active && _active->matches(msgId))
			|| ranges::contains(_waiting, msgId, &Entry::msgId)) {
			continue;
		} else if (!record.answer.isEmpty()) {
			submitStored(record, record.answer);
		} else if (record.decision == TonConnectClaimDecision::Confirm
			&& identity
			&& record.address == identity->address
			&& record.publicKey == identity->publicKey
			&& recoverClaim(record)) {
			journalWaits = true;
		}
	}
	updateRecoveryPolling(journalWaits);
}

bool TonConnectRequests::recoverClaim(const TonConnectClaimRecord &record) {
	using Fate = TonConnectSendFate;
	const auto fate = record.operationId.empty()
		? Fate::Absent
		: _session->wallet().tonConnectSendFate(record.operationId);
	const auto handedOff = !record.signedBoc.isEmpty();
	if (fate == Fate::Unknown) {
		return false;
	} else if (fate == Fate::Sending
		|| (fate == Fate::Unresolved && handedOff)) {
		return true;
	}
	const auto queue = [&](TonConnectRecovery recovery) {
		const auto queued = enqueue({
			.sessionId = record.sessionId,
			.msgId = MsgId(record.msgId),
			.topic = u"sendTransaction"_q,
			.expires = record.expires,
			.recovery = recovery,
		});
		if (queued) {
			crl::on_main(this, [=] { showNext(); });
		}
	};
	// WHY: a BoC is stored before wallet.sendTransfer and the engine never
	// resubmits, so an operation without one never left this device; an
	// unresolved one still blocks a new send, so it gets no Offer.
	const auto executed = (fate == Fate::Settled)
		|| (fate == Fate::Absent && handedOff);
	if (executed) {
		if (!handedOff) {
			forgetClaim(record.sessionId, MsgId(record.msgId));
		} else {
			queue(TonConnectRecovery::Answer);
		}
	} else if (fate == Fate::Unresolved
		|| base::unixtime::now() >= record.expires) {
		submitStored(record, record.notSent);
	} else {
		queue(TonConnectRecovery::Offer);
	}
	return false;
}

void TonConnectRequests::submitStored(
		const TonConnectClaimRecord &record,
		QByteArray body) {
	const auto sessionId = record.sessionId;
	const auto msgId = MsgId(record.msgId);
	_recoveryHeld.emplace(msgId);
	SubmitResponse(
		_api,
		sessionId,
		msgId,
		body,
		record.traceId,
		[=](SubmitResult result) {
			if (result != SubmitResult::RetryLater) {
				forgetClaim(sessionId, msgId);
				_recoveryHeld.remove(msgId);
			}
		});
}

void TonConnectRequests::updateRecoveryPolling(bool wanted) {
	if (_recoveryPolling == wanted) {
		return;
	}
	_recoveryPolling = wanted;
	if (wanted) {
		_session->wallet().startPolling();
	} else {
		_session->wallet().stopPolling();
	}
}

void TonConnectRequests::stop() {
	_stopped = true;
	_recoveryLifetime.destroy();
	_recoveryPolling = false;
	_waiting.clear();
	_api.request(base::take(_pendingRequestId)).cancel();
	_active = nullptr;
	_silent.clear();
	_lifetime.destroy();
}

}
