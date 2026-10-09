#include "wallet/wallet_session_internal.h"

namespace Wallet {
using namespace SessionDetails;

Session::Session(not_null<Main::Session*> session)
: _session(session)
, _api(session)
, _stateApi(&session->mtp())
, _engine(std::make_unique<Engine>(session, &_api))
, _rates(std::make_unique<Rates>(session))
, _onramp(std::make_unique<Onramp>(session))
, _userAddresses(std::make_unique<UserAddresses>(session))
, _transferMessages(std::make_unique<TransferMessages>(session))
, _tonConnect(std::make_unique<TonConnect>(session))
, _stream(std::make_unique<Stream>(&_api, [=](StreamRefresh wanted) {
	applyStreamRefresh(wanted);
}))
, _pollTimer([=] { pollTick(); })
, _gaslessTimer([=] { refreshGaslessInfo(); })
, _decryptRetryTimer([=] { settleDeferredDecrypts(); })
, _walletAvailable(session->appConfig().walletAvailable())
, _transferMinNanos(TransferMinNanos(session)) {
	vault().protectionChanges() | rpl::on_next([=] {
		updateDeviceCustodyState(true);
	}, _lifetime);
	rpl::merge(
		_transferWalletIdentityChanges.events(),
		_custodyUpdates.events()
	) | rpl::on_next([=] {
		validateCommentScopes();
	}, _commentLifetime);
	_sendState.changes() | rpl::on_next([=](SendState state) {
		if (state != SendState::Idle) {
			retireCommentScopes();
		}
	}, _commentLifetime);
	transferWalletIdentityChanges() | rpl::on_next([=] {
		_lastReceipt.reset();
		const auto hadSubmission = _submission
			|| _pending
			|| (_sendState.current() != SendState::Idle);
		retireSubmission();
		_pending.reset();
		_sendUnresolved = _sendUnresolved || hadSubmission;
		_sendState = SendState::Idle;
		resetGaslessInfo();
		refreshGaslessInfo();
		if (hadSubmission) {
			updatePollingState();
			const auto weak = base::make_weak(_engine.get());
			syncEngineClient();
			if (weak) {
				requestEngineRefresh();
			}
		}
	}, _lifetime);
	transferWalletIdentityChanges() | rpl::on_next([=] {
		_tonConnect->walletChanged();
	}, _lifetime);
	rpl::merge(
		vault().granted(),
		vault().protectionChanges()
	) | rpl::on_next([=] {
		_tonConnect->vaultChanged();
	}, _lifetime);
	session->appConfig().refreshed() | rpl::on_next([=, this] {
		applyWalletAvailable();
		applyTransferMinNanos();
		refreshGaslessInfo();
	}, _lifetime);
	resetGaslessInfo();
}

Session::~Session() {
	_tonConnect->stop();
	retireCommentScopes();
	_commentLifetime.destroy();
	if (const auto state = _shareFetch.lock()) {
		FinishShareFetch(_stateApi, _shareFetchTimer, state);
	}
	_panel = nullptr;
	retireGaslessRequest();
}

Onramp &Session::onramp() {
	return *_onramp;
}

Rates &Session::rates() {
	return *_rates;
}

TransferMessages &Session::transferMessages() {
	return *_transferMessages;
}

UserAddresses &Session::userAddresses() {
	return *_userAddresses;
}

TonConnect &Session::tonConnect() {
	return *_tonConnect;
}

Ui::SeparatePanel *Session::panel() const {
	return _panel.get();
}

void Session::setPanel(std::unique_ptr<Ui::SeparatePanel> panel) {
	_panel = std::move(panel);
	if (!_panel) {
		// _historyPaged and _collectiblesPaged are facts about one
		// overview's scroll position, so they end with the panel that
		// owned them: otherwise the periodic refresh stays refused for
		// the rest of the session and a transaction or a collectible
		// received while the panel was closed never appears. The pages
		// spent looking for a row the feed can show are such a fact too,
		// so the next reader to open the panel gets the whole bound. The
		// Walt answer is one as well, so its request is cancelled and both
		// its flag and its value go with the panel, leaving the next
		// opening to ask exactly once on its own.
		_historyPaged = false;
		_collectiblesPaged = false;
		resetHiddenHistoryPages();
		_stateApi.request(base::take(_waltBalanceRequestId)).cancel();
		_waltBalanceRequested = false;
		_existingWaltBalanceUrl = QString();
		_windowSend.clear();
	}
}

void Session::ensureLoaded() {
	if (_loaded) {
		return;
	}
	_loaded = true;
	refreshState();
}

Presence Session::presence() {
	ensureLoaded();
	return _presence.current();
}

Presence Session::presenceCurrent() const {
	return _presence.current();
}

rpl::producer<Presence> Session::presenceValue() {
	ensureLoaded();
	return _presence.value();
}

std::optional<QString> Session::address() {
	ensureLoaded();
	if (_presence.current() != Presence::Ready) {
		return std::nullopt;
	}
	return _address;
}

QString Session::addressFriendly(bool bounceable) {
	ensureLoaded();
	if (_presence.current() != Presence::Ready) {
		return QString();
	}
	return FormatFriendly(_address, bounceable);
}

WalletCapabilities Session::capabilities() const {
	return _capabilities.current();
}

rpl::producer<WalletCapabilities> Session::capabilitiesValue() const {
	return _capabilities.value();
}

QByteArray Session::publicKey() const {
	return _publicKey;
}

auto Session::transferWalletIdentity() const
-> std::optional<TransferWalletIdentity> {
	if (_presence.current() == Presence::Unknown && _custody
		&& !_custodyReadFailed && !_custody->pendingRotation
		&& _custody->records.size() == 1) {
		const auto &record = _custody->records.front();
		const auto address = CanonicalAddress(record.address);
		if (record.active
			&& record.network == int(engine::Network::kMainnet)
			&& _custody->lastSeenServerKey.size() == kCustodyPublicKeySize
			&& record.signsWith(_custody->lastSeenServerKey)
			&& !record.recordId.isEmpty()
			&& !record.secretRef.isEmpty()
			&& !address.isEmpty()) {
			return TransferWalletIdentity{
				.address = address,
				.publicKey = _custody->lastSeenServerKey,
				.revision = _walletIdentityRevision,
			};
		}
	}
	if (_presence.current() != Presence::Ready
		|| _address.isEmpty()
		|| _publicKey.size() != kCustodyPublicKeySize) {
		return std::nullopt;
	}
	return TransferWalletIdentity{
		.address = _address,
		.publicKey = _publicKey,
		.revision = _walletIdentityRevision,
	};
}

bool Session::transferWalletIdentityCurrent(
		const TransferWalletIdentity &identity) const {
	const auto current = transferWalletIdentity();
	return current && (*current == identity);
}

rpl::producer<> Session::transferWalletIdentityChanges() const {
	return _transferWalletIdentityChanges.events();
}

void Session::refreshState() {
	if ((_presence.current() == Presence::Unavailable) || _stateRequestId) {
		return;
	}
	// The later of the two stamps is what the floor measures from, so a
	// pushed updateWalletState postpones the next request instead of only
	// failing to trigger one: the push genuinely replaces a poll round
	// rather than riding beside it.
	const auto since = std::max(_stateRequestedAt, _stateRefreshedAt);
	if (since && (crl::now() - since < kStateRefreshInterval)) {
		return;
	}
	requestState();
}

void Session::requestState(
		Fn<void(const MTPWalletState &)> done,
		Fn<void()> fail) {
	if (done) {
		_stateApi.request(base::take(_stateRequestId)).cancel();
	}
	const auto startedAt = _stateRequestedAt = crl::now();
	const auto revision = _walletIdentityRevision;
	auto request = _stateApi.request(MTPwallet_GetState());
	auto &policy = done ? request.handleAllErrors() : request;
	_stateRequestId = policy.done([=](
			const MTPWalletState &result,
			mtpRequestId requestId) {
		LOG(("Wallet Info: wallet.getState request=%1 elapsed_ms=%2 "
			"requested_revision=%3 current_revision=%4; %5."
			).arg(requestId
			).arg(crl::now() - startedAt
			).arg(revision
			).arg(_walletIdentityRevision
			).arg(LogWalletState(result)));
		_stateRequestId = 0;
		if (done) {
			done(result);
		} else {
			applyState(result, false);
		}
	}).fail([=](const MTP::Error &error, mtpRequestId requestId) {
		LOG(("Wallet Error: wallet.getState request=%1 elapsed_ms=%2 "
			"failed: %3"
			).arg(requestId).arg(crl::now() - startedAt).arg(error.type()));
		_stateRequestId = 0;
		if (fail) {
			fail();
			return;
		}
		if (error.type() == u"WALLET_UNAVAILABLE"_q) {
			LOG(("Wallet Error: the server has no wallet for this account."));
			setPresence(Presence::Unavailable);
			return;
		}
		++_stateFailures;
		updateListsGate();
	}).send();
	LOG(("Wallet Info: wallet.getState sent request=%1 revision=%2 "
		"state_age_ms=%3."
		).arg(_stateRequestId
		).arg(revision
		).arg(_stateRefreshedAt ? (startedAt - _stateRefreshedAt) : -1));
}

bool GaslessTerms::eligible(int64 amountNano) const {
	return usable && (amountNano > 0) && (amountNano >= effectiveMinNanos);
}

// The relayer does not sponsor a transfer back to the wallet that signed it:
// the server refuses the whole pair, and the mandatory normal variant beside
// it is not executed instead, so an amount that an ordinary paid send moves
// would fail as soon as the offer was accepted. The offer therefore stops at
// the destination, and such a send stays on its authorized normal fee.
bool GaslessTerms::eligible(
		int64 amountNano,
		const QString &destination) const {
	return eligible(amountNano)
		&& identity
		&& !destination.isEmpty()
		&& (CanonicalAddress(destination) != identity->address);
}

GaslessTerms Session::gaslessTerms() {
	const auto weak = base::make_weak(_engine.get());
	refreshGaslessInfo();
	return weak ? _gaslessTerms.current() : GaslessTerms();
}

rpl::producer<GaslessTerms> Session::gaslessTermsValue() {
	const auto weak = base::make_weak(_engine.get());
	refreshGaslessInfo();
	if (!weak) {
		return rpl::single(GaslessTerms());
	}
	return _gaslessTerms.value();
}

void Session::refreshGaslessInfo(bool force) {
	if (_gaslessRefreshing) {
		return;
	}
	const auto weak = base::make_weak(_engine.get());
	_gaslessRefreshing = true;
	const auto guard = gsl::finally([=, this] {
		if (weak) {
			_gaslessRefreshing = false;
		}
	});
	const auto terms = _gaslessTerms.current();
	const auto identity = transferWalletIdentity();
	if (terms.identity != identity) {
		resetGaslessInfo();
		if (!weak) {
			return;
		}
	} else if (terms.transferMinNanos != TransferMinNanos(_session)
		|| terms.configuredMinNanos != GaslessMinNanos(_session)) {
		retireGaslessRequest();
		_gaslessExpiresAt = 0;
	}
	applyGaslessTerms(_gaslessTerms.current());
	if (!weak) {
		return;
	}
	_gaslessRefreshWanted = _gaslessRefreshWanted || force;
	if (!_preview || _preview->owners.empty() || !identity) {
		retireGaslessRequest();
		return;
	}
	const auto now = crl::now();
	if (_gaslessRequestId
		&& (now - _gaslessRequestedAt >= crl::time(kClientRequestTimeoutMs))) {
		retireGaslessRequest();
		_gaslessExpiresAt = 0;
		applyGaslessTerms(_gaslessTerms.current());
		if (!weak) {
			return;
		}
	}
	// Every prepared transfer is bound to the exact terms it was estimated
	// with, so letting the info lapse and then fetching it again would flip
	// the terms to stale and back once a minute and throw away every
	// prepared transfer with them, even when the server answers with the
	// same info. The info is fetched ahead of its expiry instead, and an
	// unchanged answer then leaves the terms, and the transfers, as they are.
	const auto expiring = _gaslessTerms.current().fresh
		&& (now >= _gaslessExpiresAt - kGaslessRefreshAhead);
	if (!_gaslessRequestId
		&& (!_gaslessTerms.current().fresh
			|| expiring
			|| _gaslessRefreshWanted)
		&& (!_gaslessRequestedAt
			|| (now - _gaslessRequestedAt >= kGaslessRetryInterval))) {
		requestGaslessInfo();
	}
	if (!weak || !_preview || _preview->owners.empty()
		|| !transferWalletIdentityCurrent(*identity)) {
		return;
	}
	const auto &current = _gaslessTerms.current();
	auto deadline = _gaslessRequestId
		? _gaslessRequestedAt + crl::time(kClientRequestTimeoutMs)
		: (!current.fresh || expiring || _gaslessRefreshWanted)
		? _gaslessRequestedAt + kGaslessRetryInterval
		: (_gaslessExpiresAt - kGaslessRefreshAhead);
	if (current.fresh) {
		deadline = std::min(deadline, _gaslessExpiresAt);
		if (current.info->resetAt > 0) {
			const auto resetIn = crl::time(current.info->resetAt)
				- crl::time(base::unixtime::now());
			deadline = std::min(deadline, now + resetIn * 1000);
		}
	}
	_gaslessTimer.callOnce(std::max(crl::time(1), deadline - crl::now()));
}

void Session::requestGaslessInfo() {
	const auto identity = transferWalletIdentity();
	if (_gaslessRequestId
		|| !_preview
		|| _preview->owners.empty()
		|| !identity) {
		return;
	}
	const auto serial = ++_gaslessRequestSerial;
	const auto generation = _networkGeneration;
	const auto weak = base::make_weak(_engine.get());
	const auto weakSession = base::make_weak(_session);
	_gaslessRequestedAt = crl::now();
	_gaslessRefreshWanted = false;
	const auto ownsRequest = [=, this] {
		return weak
			&& (serial == _gaslessRequestSerial)
			&& (generation == _networkGeneration);
	};
	const auto current = [=, this] {
		return ownsRequest()
			&& transferWalletIdentityCurrent(*identity)
			&& _preview
			&& !_preview->owners.empty();
	};
	_gaslessRequestId = _stateApi.request(
		MTPwallet_GetGaslessInfo()
	).done([=, this](const MTPUpdates &result) {
		if (ownsRequest()) {
			_gaslessRequestId = 0;
		}
		if (weakSession) {
			weakSession->api().applyUpdates(result);
		}
		if (weak) {
			refreshGaslessInfo();
		}
	}).fail([=](const MTP::Error &error) {
		if (!current()) {
			return;
		}
		LOG(("Wallet Error: the gasless request failed: %1"
			).arg(error.type()));
		_gaslessRequestId = 0;
		_gaslessExpiresAt = 0;
		refreshGaslessInfo();
	}).handleAllErrors().send();
}

void Session::retireGaslessRequest() {
	++_gaslessRequestSerial;
	_stateApi.request(base::take(_gaslessRequestId)).cancel();
	_gaslessTimer.cancel();
	_gaslessRefreshWanted = false;
}

void Session::resetGaslessInfo() {
	const auto weak = base::make_weak(_engine.get());
	const auto refreshing = std::exchange(_gaslessRefreshing, true);
	const auto guard = gsl::finally([=, this] {
		if (weak) {
			_gaslessRefreshing = refreshing;
		}
	});
	retireGaslessRequest();
	_gaslessRequestedAt = 0;
	_gaslessExpiresAt = 0;
	applyGaslessTerms(GaslessTerms());
}

void Session::applyGaslessInfo(GaslessInfo info, bool refreshed) {
	if (refreshed) {
		_gaslessExpiresAt = crl::now() + kGaslessRefreshInterval;
	}
	auto terms = _gaslessTerms.current();
	terms.info = std::move(info);
	applyGaslessTerms(std::move(terms));
}

void Session::applyGaslessTerms(GaslessTerms terms) {
	const auto &previous = _gaslessTerms.current();
	terms.identity = transferWalletIdentity();
	terms.transferMinNanos = TransferMinNanos(_session);
	terms.configuredMinNanos = GaslessMinNanos(_session);
	terms.effectiveMinNanos = std::max(
		terms.transferMinNanos,
		terms.configuredMinNanos);
	const auto &info = terms.info;
	if (info && (info->minAmount > 0)) {
		terms.effectiveMinNanos = std::max(
			terms.effectiveMinNanos,
			info->minAmount);
	}
	const auto now = base::unixtime::now();
	if ((_gaslessExpiresAt <= crl::now())
		|| (info && (info->resetAt > 0) && (info->resetAt <= now))) {
		_gaslessExpiresAt = 0;
	}
	terms.fresh = terms.identity && info && (_gaslessExpiresAt > 0);
	terms.usable = terms.fresh
		&& info->available
		&& (info->left > 0)
		&& (info->resetAt >= 0)
		&& (info->minAmount > 0)
		&& !info->relayer.isEmpty();
	terms.revision = previous.revision;
	if (terms != previous) {
		++terms.revision;
		_gaslessTerms = std::move(terms);
	}
}

QString Session::existingWaltBalanceUrl() const {
	return _existingWaltBalanceUrl.current();
}

rpl::producer<QString> Session::existingWaltBalanceUrlValue() {
	requestExistingWaltBalance();
	return _existingWaltBalanceUrl.value();
}

rpl::producer<std::optional<int64>> Session::parkedBalanceNanoValue() {
	requestParkedChecks(true);
	return _parkedBalanceNano.value();
}

void Session::syncParkedChecks(
		const CustodyStore &store,
		const QString &servedAddress) {
	auto addresses = std::vector<QString>();
	for (const auto &record : store.records) {
		const auto address = CheckableParkedAddress(record, servedAddress);
		if (!address.isEmpty() && !ranges::contains(addresses, address)) {
			addresses.push_back(address);
		}
	}
	for (auto i = begin(_parkedChecks); i != end(_parkedChecks);) {
		if (ranges::contains(addresses, i->first)) {
			++i;
			continue;
		} else if (i->second.requestId) {
			_api.cancelRequest(i->second.requestId);
		}
		i = _parkedChecks.erase(i);
	}
	for (const auto &address : addresses) {
		_parkedChecks.emplace(address, ParkedCheck());
	}
}

void Session::requestParkedFunds(const QString &address) {
	const auto i = _parkedChecks.find(address);
	if (i == end(_parkedChecks)
		|| i->second.requestId
		|| _presence.current() != Presence::Ready) {
		return;
	}
	const auto revision = i->second.revision = ++_parkedCheckRevision;
	const auto requestId = _api.request(
		Gram::AddressInformationRequest(FormatFriendly(address, false)),
		[=](const QByteArray &json) {
			const auto funds = Gram::ParseAddressFunds(json);
			if (!funds) {
				LOG(("Wallet Error: parked balance parse failed."));
			}
			finishParkedCheck(address, revision, funds);
		},
		[=](const Gram::ApiError &error) {
			LOG(("Wallet Error: parked balance request failed: %1"
				).arg(error.message));
			finishParkedCheck(address, revision, std::nullopt);
		});
	const auto j = _parkedChecks.find(address);
	if (j != end(_parkedChecks) && j->second.revision == revision) {
		j->second.requestId = requestId;
	}
}

void Session::finishParkedCheck(
		const QString &address,
		uint64 revision,
		std::optional<Gram::AddressFunds> funds) {
	const auto i = _parkedChecks.find(address);
	if (i == end(_parkedChecks) || i->second.revision != revision) {
		return;
	}
	auto &check = i->second;
	check.requestId = 0;
	if (!funds) {
		check.funds = ParkedFunds::Unknown;
	} else if (!funds->balanceNano && funds->neverUsed) {
		check.funds = ParkedFunds::Empty;
	} else {
		check.funds = ParkedFunds::Funded;
		check.nano = funds->balanceNano;
	}
	updateDeviceCustodyState();
}

void Session::requestParkedChecks(bool refresh) {
	auto addresses = std::vector<QString>();
	for (const auto &[address, check] : _parkedChecks) {
		if ((check.funds == ParkedFunds::Checking)
			|| (refresh
				&& (check.funds == ParkedFunds::Funded
					|| check.funds == ParkedFunds::Unknown))) {
			addresses.push_back(address);
		}
	}
	for (const auto &address : addresses) {
		requestParkedFunds(address);
	}
}

void Session::dropEmptyParked() {
	if (_parkedDropping
		|| custodyBusy()
		|| _presence.current() != Presence::Ready) {
		return;
	}
	for (const auto &record : custody().records) {
		const auto address = CheckableParkedAddress(record, _address);
		const auto i = _parkedChecks.find(address);
		if (i == end(_parkedChecks) || i->second.funds != ParkedFunds::Empty) {
			continue;
		}
		const auto key = record.publicKey;
		_parkedDropping = true;
		LOG(("Wallet Info: dropping an empty unused parked wallet."));
		dropParked(key, [=] {
			_parkedDropping = false;
			dropEmptyParked();
		}, [=](const QString &error) {
			_parkedDropping = false;
			const auto j = _parkedChecks.find(address);
			if (j != end(_parkedChecks)
				&& j->second.funds == ParkedFunds::Empty) {
				j->second.funds = ParkedFunds::Unknown;
			}
			updateDeviceCustodyState();
		});
		return;
	}
}

void Session::publishParkedBalance() {
	auto sum = int64(0);
	for (const auto &[address, check] : _parkedChecks) {
		if (check.funds == ParkedFunds::Unknown) {
			_parkedBalanceNano = std::nullopt;
			return;
		} else if (check.funds == ParkedFunds::Funded) {
			sum += check.nano;
		}
	}
	_parkedBalanceNano = sum;
}

bool Session::parkedHidden(
		const CustodyRecord &record,
		const QString &servedAddress) const {
	const auto i = _parkedChecks.find(
		CheckableParkedAddress(record, servedAddress));
	return (i != end(_parkedChecks))
		&& (i->second.funds == ParkedFunds::Checking
			|| i->second.funds == ParkedFunds::Empty);
}

void Session::requestExistingWaltBalance() {
	// One opening of the wallet panel asks once. setPanel() cancels and
	// clears both of these together when the panel is dropped, so this one
	// flag is the whole "already asked this opening" state, and a failed or
	// cancelled request gets exactly one retry - on the next opening.
	if (_waltBalanceRequested) {
		return;
	}
	_waltBalanceRequested = true;
	const auto generation = _networkGeneration;
	_waltBalanceRequestId = _stateApi.request(
		MTPwallet_GetExistingWaltBalance()
	).done([=](const MTPwallet_ExistingBalance &result) {
		_waltBalanceRequestId = 0;
		if (generation == _networkGeneration) {
			const auto &data = result.data();
			_existingWaltBalanceUrl = data.is_has_balance()
				? qs(data.vurl())
				: QString();
		}
	}).fail([=](const MTP::Error &error) {
		LOG(("Wallet Error: wallet.getExistingWaltBalance failed: %1"
			).arg(error.type()));
		_waltBalanceRequestId = 0;
	}).handleAllErrors().send();
}

void Session::applyState(const MTPWalletState &state, bool pushed) {
	LOG(("Wallet Info: applying state source=%1 previous_address=%2 "
		"previous_key=%3 previous_revision=%4; %5."
		).arg(pushed ? u"push"_q : u"response"_q
		).arg(_address
		).arg(LogKey(_publicKey)
		).arg(_walletIdentityRevision
		).arg(LogWalletState(state)));
	_stateRefreshedAt = crl::now();
	_stateFailures = 0;
	const auto clear = [&] {
		if (!_address.isEmpty() || !_publicKey.isEmpty()) {
			retireCommentScopes();
			++_walletIdentityRevision;
		}
		_address = QString();
		_publicKey = QByteArray();
		_balanceNano = 0;
		_capabilities = WalletCapabilities();
	};
	state.match([&](const MTPDwalletState &data) {
		const auto parsed = ParseAddress(qs(data.vaddress()));
		if (!parsed) {
			LOG(("Wallet Error: server wallet address is not parseable."));
			clear();
			setPresence(Presence::AddressUnreadable);
			return;
		}
		const auto wasReady = (_presence.current() == Presence::Ready);
		const auto addressChanged = (_address != parsed->raw);
		const auto keyChanged = (_publicKey != data.vpublic_key().v);
		const auto identityChanged = addressChanged || keyChanged;
		if (identityChanged) {
			retireCommentScopes();
			++_walletIdentityRevision;
		}
		_address = parsed->raw;
		_publicKey = data.vpublic_key().v;
		if (wasReady && addressChanged) {
			const auto weak = base::make_weak(_engine.get());
			const auto revision = _walletIdentityRevision;
			clearHistory();
			if (!weak || revision != _walletIdentityRevision) {
				return;
			}
			clearCollectibles();
			if (!weak || revision != _walletIdentityRevision) {
				return;
			}
			_engineStatus = AccountStatus::NonExisting;
		}
		_balanceNano = int64(data.vbalance().v);
		_capabilities = WalletCapabilities{
			.backupEnabled = data.is_backup_enabled(),
			.canExportPhrase = data.is_can_export_phrase(),
			.canEnableBackup = data.is_can_enable_backup(),
		};
		setPresence(Presence::Ready);
		// A presence that was not Ready has already had its drain and its
		// first page from setPresence(); the case that write structurally
		// cannot see is a presence that stayed Ready while the served
		// address changed. That is a different wallet, so both lanes leave
		// with the transfer submission in flight, the engine status returns
		// to its unknown value and the new wallet's head page is asked for at
		// once. It runs before reconcileCustody() because that reconciliation
		// may stop and restart the engine client, and a restart must find an
		// already-drained collectibles lane rather than have its first
		// delivery wiped afterwards.
		// A same-address key change is the same wallet under a rotated key:
		// its lists and engine status stay, the history lane is marked stale
		// because the rotation is one new row in it, and reconcileCustody()
		// settles which record still signs for it. A pushed state on the
		// same wallet is the transfer notification the server sends as a
		// transfer progresses, so it too is news about the history lane
		// alone and invalidates only that one. The arms are ordered so that
		// a push which also changed the address takes the first one and gets
		// exactly one head page from the drain, never a second one from the
		// marker.
		if (wasReady && addressChanged) {
			refreshHistory();
			refreshCollectibles();
		} else if (wasReady && (pushed || keyChanged)) {
			_historyStale = true;
			refreshStaleHistory();
		}
		reconcileCustody();
		if (wasReady && identityChanged) {
			_transferWalletIdentityChanges.fire({});
		}
	}, [&](const MTPDwalletStateEmpty &data) {
		clear();
		setPresence(data.is_creating()
			? Presence::Provisioning
			: Presence::Missing);
	});
}

void Session::applyUpdate(const MTPDupdateWalletState &data) {
	applyState(data.vstate(), true);
}

void Session::applyUpdate(const MTPDupdateSentWalletTransaction &data) {
	const auto &hash = data.vmsg_hash().v;
	if (hash.isEmpty()) {
		return;
	}
	auto operationId = std::string();
	auto ambiguous = false;
	const auto match = [&](const std::string &id) {
		if (operationId.empty()) {
			operationId = id;
		} else if (operationId != id) {
			ambiguous = true;
		}
	};
	for (const auto &entry : _submitted) {
		if (entry.generation == _networkGeneration
			&& transferWalletIdentityCurrent(entry.identity)
			&& entry.receipt
			&& entry.receipt->messageHash == hash) {
			match(entry.operationId);
		}
	}
	if (_submission
		&& submissionCurrent(_submission->operationId, _submission->prepared)
		&& _submission->receipt
		&& _submission->receipt->messageHash == hash) {
		match(_submission->operationId);
	}
	if (!operationId.empty() && !ambiguous) {
		if (!applySubmittedUpdate(operationId, data)) {
			LOG(("Wallet Error: conflicting pushed transfer receipt."));
		}
	}
}

void Session::applyUpdate(const MTPDupdateWalletGaslessInfo &data) {
	const auto weak = base::make_weak(_engine.get());
	applyGaslessInfo(GaslessInfoFromServer(data), true);
	if (weak) {
		refreshGaslessInfo();
	}
}

void Session::applyUpdate(const MTPDupdateWalletTonConnectSession &data) {
	_tonConnect->apply(data.vsession());
}

void Session::applyUpdate(
		const MTPDupdateWalletTonConnectPendingDisconnect &data) {
	_tonConnect->applyPendingDisconnect(data.vsession_ids().v);
}

void Session::setPresence(Presence presence) {
	if (_presence.current() == presence) {
		return;
	}
	retireCommentScopes();
	if (_presence.current() == Presence::Ready) {
		++_walletIdentityRevision;
		clearSubmittedTransfers();
	}
	const auto weak = base::make_weak(_engine.get());
	const auto revision = _walletIdentityRevision;
	const auto current = [=] {
		return weak
			&& revision == _walletIdentityRevision
			&& _presence.current() == presence;
	};
	_presence = presence;
	if (!current()) {
		return;
	}
	// The gate is recomputed before the lanes are drained, because both
	// drains publish into the same derived faces the gate does: a face
	// evaluated between them reads emptied lists under the gate this
	// wallet held while it was Ready, which was never true of it. Doing it
	// first costs nothing — for a presence that is not Ready every term
	// in updateListsGate() that reads the history lane is conjoined with
	// `ready`, so it writes the same two values before the drain as after.
	updateListsGate();
	if (!current()) {
		return;
	}
	if (presence != Presence::Ready) {
		clearHistory();
		if (!current()) {
			return;
		}
		clearCollectibles();
		if (!current()) {
			return;
		}
	}
	updatePollingState();
	if (!current()) {
		return;
	}
	if (presence == Presence::Ready) {
		refreshHistory();
		if (current()) {
			refreshCollectibles();
		}
	}
	if (current()) {
		_transferWalletIdentityChanges.fire({});
	}
}

#ifdef _DEBUG
void Session::debugClearNetworkState() {
	if (_presence.current() != Presence::Ready) {
		return;
	}
	_debugClearedPollingCount = _pollingCount;
	clearNetworkState();
}

void Session::debugRestoreNetworkState() {
	_pollingCount += base::take(_debugClearedPollingCount);
	updatePollingState();
}

void Session::debugApplyLists(
		const MTPwallet_Transactions &transactions,
		const MTPwallet_NftItems &collectibles) {
	Expects(_presence.current() == Presence::Ready);
	if (const auto request = base::take(_historyRequest)) {
		_stateApi.request(request->id).cancel();
		FinishHistoryWaiters(base::take(request->done));
	}
	if (const auto request = base::take(_collectiblesRequest)) {
		_stateApi.request(request->id).cancel();
	}
	applyTransactions(transactions, false, HistoryRequest{
		.identity = transferWalletIdentity(),
		.identityRevision = _walletIdentityRevision,
		.generation = _networkGeneration,
	});
	applyCollectibles(collectibles, CollectiblesRequest{
		.identity = transferWalletIdentity(),
		.identityRevision = _walletIdentityRevision,
		.generation = _networkGeneration,
	});
}

#endif

} // namespace Wallet
