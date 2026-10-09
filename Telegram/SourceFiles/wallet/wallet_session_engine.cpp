#include "wallet/wallet_session_internal.h"

namespace Wallet {
using namespace SessionDetails;

void Session::sendReplaceWallet(
		const MTPInputWalletReplacement &wallet,
		std::optional<Core::CloudPasswordResult> password,
		Fn<void(const MTPWalletState &)> applied,
		Fn<void(const MTP::Error &)> fail) {
	using Flag = MTPwallet_replaceWallet::Flag;
	const auto checked = password && *password;
	auto request = _stateApi.request(MTPwallet_ReplaceWallet(
		MTP_flags(checked ? Flag::f_password : Flag(0)),
		wallet,
		checked ? password->result : MTP_inputCheckPasswordEmpty()));
	// An imported replacement carries a one-shot ownership proof: the
	// server admits one attempt per challenge, and the MTP instance's
	// automatic resend of a request answered with a negative or 500-class
	// code repeats the identical body, so a resent proof could only be
	// refused as spent or spend the challenge behind the flow's back. Such
	// an answer therefore reaches .fail() here and the import checks the
	// served state with a read, without repeating the proof. A new wallet
	// carries nothing one-shot and keeps the transport's resend.
	auto &policy = (wallet.type() == mtpc_inputWalletImported)
		? request.handleAllErrors()
		: request.handleFloodErrors();
	policy.done([=](const MTPWalletState &result) {
		applied(result);
	}).fail([=](const MTP::Error &error) {
		LOG(("Wallet Error: wallet.replaceWallet failed: %1"
			).arg(error.type()));
		fail(error);
	}).send();
}

void Session::recoverImportedReplace(
		QString canonicalAddress,
		QByteArray signingKey,
		Fn<void(const MTPWalletState &)> applied,
		Fn<void(const QString &)> abandon) {
	const auto unconfirmed = [=] {
		abandon(u"REPLACE_STATE_UNCONFIRMED"_q);
	};
	requestState([=, this](const MTPWalletState &state) {
		if (state.type() != mtpc_walletState) {
			unconfirmed();
			return;
		}
		const auto &data = state.c_walletState();
		const auto parsed = ParseAddress(qs(data.vaddress()));
		if (data.vpublic_key().v.size() != kCustodyPublicKeySize || !parsed) {
			unconfirmed();
			return;
		}
		if (parsed->raw != canonicalAddress) {
			applyState(state, false);
			abandon(u"REPLACE_KEY_MISMATCH"_q);
			return;
		} else if (data.vpublic_key().v != signingKey) {
			LOG(("Wallet Error: the recovered wallet state does not serve "
				"the imported signing key %1.").arg(LogKey(signingKey)));
			applyState(state, false);
			abandon(u"REPLACE_OUTDATED_PHRASE"_q);
			return;
		}
		applied(state);
	}, unconfirmed);
}

void Session::finishConfirmedReplace(
		QString oldAddress,
		std::optional<CustodyRecord> newActive,
		const MTPWalletState &state,
		Fn<void(CustodyOutcome)> done,
		Fn<void(const QString &)> fail) {
	applyState(state, false);
	const auto lifecycle = _engine->lifecycle();
	if (newActive) {
		if (!persistCustody(*newActive)) {
			_engine->run([
				lifecycle,
				descriptor = DescriptorFromRecord(*newActive)
			] {
				lifecycle->delete_wallet(descriptor);
			}, [] {}, [](EngineError) {});
			// wallet.replaceWallet has already succeeded and applyState() has
			// already run, so the replacement is real and cannot be taken
			// back: this answers done, not fail. The delete_wallet above is
			// local cleanup of a secret nothing points at, not a retraction,
			// and only the custody install on this device did not happen.
			done(CustodyOutcome::WriteFailed);
			return;
		}
	}
	if (oldAddress != _address) {
		const auto records = custody().records;
		for (const auto &record : records) {
			if (CanonicalAddress(record.address) != oldAddress) {
				continue;
			}
			removeCustodyRecord(record.recordId);
			_engine->run([
				lifecycle,
				descriptor = DescriptorFromRecord(record)
			] {
				lifecycle->delete_wallet(descriptor);
			}, [] {}, [](EngineError) {
				LOG(("Wallet Error: delete_wallet of the replaced wallet "
					"failed."));
			});
		}
	}
	done(CustodyOutcome::Installed);
}

void Session::reconcileCustody() {
	if (_publicKey.size() != kCustodyPublicKeySize) {
		return;
	}
	// The served key is this device's own replacement key, so the chain
	// confirmed the rotation before the journal did and the promotion runs
	// here. The in-flight send_boc, if any, precedes the queued client stop
	// on the serial worker, and its later verdict finds no pending and
	// returns. A third served key never matches: it leaves the pending to
	// the journal, or to a restore of the current phrase whose client's
	// empty journal then discards it.
	const auto &pending = custody().pendingRotation;
	const auto confirmedByServer = pending
		&& !pending->newPublicKey.isEmpty()
		&& pending->newPublicKey == _publicKey
		&& custody().forAddress(_address) != nullptr;
	if (confirmedByServer) {
		promotePendingRotation();
	}
	auto store = custody();
	const auto previousServed = store.lastSeenServerKey;
	auto changed = false;
	for (auto &record : store.records) {
		const auto sameWallet = (CanonicalAddress(record.address) == _address);
		if (sameWallet
			&& record.signingKey.isEmpty()
			&& record.publicKey == _publicKey) {
			record.signingKey = record.publicKey;
			changed = true;
		}
		if (record.awaitingServerKey
			&& (!sameWallet
				|| record.signingKey == _publicKey
				|| _publicKey != previousServed)) {
			record.awaitingServerKey = false;
			changed = true;
		}
		const auto active = sameWallet && record.signsWith(_publicKey);
		if (record.active != active) {
			record.active = active;
			changed = true;
		}
	}
	if (store.lastSeenServerKey != _publicKey) {
		if (!store.lastSeenServerKey.isEmpty()) {
			LOG(("Wallet Info: server wallet key changed."));
		}
		store.lastSeenServerKey = _publicKey;
		changed = true;
	}
	if (changed) {
		if (WriteCustodyStore(_session->local(), store)) {
			_custody = std::move(store);
		} else {
			LOG(("Wallet Error: custody parking write failed."));
		}
	}
	updateDeviceCustodyState();
	if (custody().pendingRotation || custody().anyAwaitingServerKey()) {
		updatePollingState();
	}
	if (confirmedByServer && !custody().pendingRotation) {
		finishRotation(QString());
	}
}

void Session::updateDeviceCustodyState(bool cachedOnly) {
	if (cachedOnly && !_custody) {
		return;
	}
	const auto &store = cachedOnly ? *_custody : custody();
	const auto identity = transferWalletIdentity();
	if (!identity) {
		return;
	}
	syncParkedChecks(store, identity->address);
	publishParkedBalance();
	const auto conflict = ranges::any_of(
		store.records,
		[&](const CustodyRecord &record) {
			return RecordParked(
				record,
				identity->address,
				identity->publicKey)
				&& !parkedHidden(record, identity->address);
		});
	const auto current = store.current(
		identity->address,
		identity->publicKey);
	const auto mode = (current
		&& !vaultKeyUnusable()
		&& !secretUnreadable(current->recordId))
		? DeviceMode::Full
		: _capabilities.current().canExportPhrase
		? DeviceMode::ReadOnlyRestorable
		: DeviceMode::ReadOnlyNotRestorable;
	const auto state = DeviceCustodyState{
		.mode = mode,
		.conflict = conflict,
	};
	if (cachedOnly && _deviceCustody.current() == state) {
		return;
	}
	_deviceCustody = state;
	_custodyUpdates.fire({});
	if (!cachedOnly) {
		syncEngineClient();
	}
	requestParkedChecks(false);
	dropEmptyParked();
}

void Session::syncEngineClient() {
	const auto identity = transferWalletIdentity();
	const auto wanted = identity
		? custody().current(identity->address, identity->publicKey)
		: nullptr;
	// Without a record that signs for the served wallet the client runs
	// public-key-only: fees are still emulated and state still read from
	// the address and key alone, while send() and every secret-reading path
	// stay refused until a restore or import lands a record and this swap
	// runs again to replace it with the signing client.
	const auto previewOnly = !wanted
		&& identity
		&& (_presence.current() == Presence::Ready);
	const auto previewMatches = previewOnly
		&& _clientRecordId.isEmpty()
		&& _clientPreviewIdentity
		&& (_clientPreviewIdentity->address == identity->address)
		&& (_clientPreviewIdentity->publicKey == identity->publicKey);
	auto started = false;
	if (_clientStopping || _custodyResetting) {
		return;
	} else if (_engine->client()) {
		const auto matches = wanted
			? (_clientRecordId == wanted->recordId)
			: previewMatches;
		if (!matches) {
			if (_submission && submissionCurrent(
					_submission->operationId,
					_submission->prepared)) {
				retirePreviews(SendError::SigningUnavailable);
				return;
			}
			_sendRecoveryReady = false;
			_clientStopping = true;
			updateSigningReady();
			// WHY: the record goes before the retire, not after it and not
			// in the stop's callback. From here nothing may sign with this
			// client, and the retire validates every scope it keeps - so a
			// record still named here would make comment access read as
			// gone and cancel the very scope the exemption below spares.
			const auto bound = !base::take(_clientRecordId).isEmpty();
			// A scope opened under the public-key-only client holds no
			// record yet, and the record it restores is what this swap
			// binds: it waits for the signing client instead of dying with
			// the client that could not have served it anyway. The install
			// running right now is the same case one step later - its scope
			// held the record this install just replaced - so it is the one
			// scope a record-bound swap keeps.
			if (bound) {
				retireCommentScopes(_installingScope);
			}
			_engine->stopClient([this] {
				_clientStopping = false;
				_clientRecordId = QString();
				_clientPreviewIdentity.reset();
				syncEngineClient();
			});
			retirePreviews(SendError::SigningUnavailable);
			return;
		}
	} else if (!wanted && !previewOnly) {
		settleDeferredDecrypts();
		return;
	} else {
		try {
			_engine->startClient(wanted
				? ClientConfigFromRecord(*wanted)
				: ClientConfigForPreview(*identity));
			_clientRecordId = wanted ? wanted->recordId : QString();
			_clientPreviewIdentity = wanted
				? std::optional<TransferWalletIdentity>()
				: identity;
			_sendRecoveryReady = false;
			updateSigningReady();
			started = true;
		} catch (...) {
			LOG(("Wallet Error: engine client start refused: %1"
				).arg(ClientErrorName(std::current_exception())));
			settleDeferredDecrypts();
			return;
		}
	}
	settleDeferredDecrypts();
	const auto weak = base::make_weak(_engine.get());
	const auto generation = _networkGeneration;
	const auto client = _engine->client();
	const auto current = [=] {
		return weak && identity && transferOperationCurrent(
			*identity,
			generation,
			client);
	};
	restoreSubmittedTransfers();
	if (!current()) {
		return;
	}
	if (sendRecoveryNeeded()) {
		resolvePending();
		updatePollingState();
	}
	if (current() && started && _presence.current() == Presence::Ready) {
		requestEngineRefresh();
	}
}

// Completion outlives a session which logs out during the stop. Engine
// shutdown then finishes in its destructor, and the last callback owner
// posts completion to main after that destructor has returned. This keeps
// the domain reset moving without allowing deletion ahead of a live client.
void Session::stopEngineClientForReset(Fn<void()> done) {
	const auto completion = std::make_shared<ResetClientCompletion>();
	completion->done = std::move(done);
	const auto weak = base::make_weak(_session);
	const auto finish = [=] {
		if (weak) {
			weak->wallet()._clientStopping = false;
			weak->wallet()._clientRecordId = QString();
			weak->wallet()._clientPreviewIdentity.reset();
			weak->wallet().settleDeferredDecrypts();
		}
		if (auto done = base::take(completion->done)) {
			done();
		}
	};
	_sendRecoveryReady = false;
	_clientStopping = true;
	updateSigningReady();
	retirePreviews(SendError::SigningUnavailable);
	if (weak) {
		_engine->stopClient(finish);
	}
}

void Session::removeCustodyRecord(const QString &recordId) {
	auto store = custody();
	store.records.erase(
		ranges::remove(
			store.records,
			recordId,
			&CustodyRecord::recordId),
		end(store.records));
	if (!WriteCustodyStore(_session->local(), store)) {
		LOG(("Wallet Error: custody record removal write failed."));
		return;
	}
	_custody = std::move(store);
	validateUnreadableRecord();
	updateDeviceCustodyState();
}

// The one write that establishes a pre-v4 record's signing key from its
// own phrase. The anchor the words derive must be the record's: the engine
// derived the record's address from that anchor at the import, so words
// that derive another anchor are not this record's phrase, and the record
// stays unresolved rather than being settled by a foreign identity.
void Session::establishSigningKey(
		const QString &recordId,
		const PhraseIdentity &identity) {
	auto store = custody();
	const auto i = ranges::find(
		store.records,
		recordId,
		&CustodyRecord::recordId);
	if (i == end(store.records) || !i->signingKey.isEmpty()) {
		return;
	} else if (i->publicKey != identity.anchor) {
		LOG(("Wallet Error: revealed phrase does not derive its record's "
			"anchor, the record stays unresolved."));
		return;
	}
	i->signingKey = identity.signing;
	i->awaitingServerKey = false;
	i->active = (CanonicalAddress(i->address) == _address)
		&& i->signsWith(_publicKey);
	if (!WriteCustodyStore(_session->local(), store)) {
		LOG(("Wallet Error: signing key establishment write failed."));
		return;
	}
	_custody = std::move(store);
	updateDeviceCustodyState();
}

void Session::clearNetworkState() {
	retireCommentScopes();
	++_networkGeneration;
	++_walletIdentityRevision;
	const auto weak = base::make_weak(_engine.get());
	const auto generation = _networkGeneration;
	const auto revision = _walletIdentityRevision;
	const auto current = [=] {
		return weak
			&& generation == _networkGeneration
			&& revision == _walletIdentityRevision;
	};
	_engineStatus = AccountStatus::NonExisting;
	_stateApi.request(base::take(_stateRequestId)).cancel();
	_stateRequestedAt = 0;
	_stateRefreshedAt = 0;
	_engineRefreshedAt = 0;
	_stateFailures = 0;
	if (const auto request = base::take(_collectiblesRequest)) {
		_stateApi.request(request->id).cancel();
	}
	_pollingCount = 0;
	_pollTimer.cancel();
	_stream->stop();
	clearHistory();
	if (!current()) {
		return;
	}
	_balanceNano = 0;
	resetGaslessInfo();
	if (!current()) {
		return;
	}
	clearCollectibles();
	if (!current()) {
		return;
	}
	updateListsGate();
	if (!current()) {
		return;
	}
	_transferWalletIdentityChanges.fire({});
	if (!current()) {
		return;
	}
	retirePreviews(SendError::Failed);
}

void Session::requestEngineRefresh() {
	const auto identity = transferWalletIdentity();
	const auto client = _engine->client();
	const auto generation = _networkGeneration;
	if (_engineRefreshPending
		|| !identity
		|| !transferOperationCurrent(*identity, generation, client)) {
		return;
	}
	_engineRefreshPending = true;
	const auto sendRevision = _sendRevision;
	_engine->run([client] {
		return client->refresh();
	}, [=, this](engine::WalletUpdate update) {
		_engineRefreshPending = false;
		if (!transferOperationCurrent(*identity, generation, client)) {
			requestEngineRefresh();
			return;
		}
		applyEngineUpdate(update, sendRevision);
	}, [=, this](EngineError error) {
		_engineRefreshPending = false;
		if (!transferOperationCurrent(*identity, generation, client)) {
			requestEngineRefresh();
			return;
		}
		LOG(("Wallet Error: engine refresh failed: %1, "
			"keeping last-good state.").arg(error.message));
	});
}

void Session::applyEngineUpdate(
		const engine::WalletUpdate &update,
		uint64 sendRevision) {
	if (update.outcome != engine::WalletOperationOutcome::kCompleted) {
		LOG(("Wallet: engine refresh outcome %1, keeping last-good state."
			).arg(int(update.outcome)));
		return;
	}
	const auto weak = base::make_weak(_engine.get());
	const auto identity = transferWalletIdentity();
	const auto generation = _networkGeneration;
	const auto client = _engine->client();
	const auto current = [=] {
		return weak && identity && transferOperationCurrent(
			*identity,
			generation,
			client);
	};
	if (signingClient()) {
		applySendSnapshot(update.snapshot.send, false, sendRevision);
		if (!current()) {
			return;
		}
		applyRotationSnapshot(update.snapshot.send, false);
		if (!current()) {
			return;
		}
	}
	const auto &snapshot = update.snapshot;
	if (snapshot.account_resource.phase != engine::ResourcePhase::kReady
		|| !snapshot.account) {
		return;
	}
	const auto &account = *snapshot.account;
	auto ok = false;
	const auto balance = QString::fromStdString(
		account.balance_nanograms).toLongLong(&ok);
	if (!ok) {
		LOG(("Wallet Error: engine balance parse failed: %1"
			).arg(QString::fromStdString(account.balance_nanograms)));
		return;
	}
	auto mapped = _engineStatus;
	switch (account.status) {
	case engine::AccountStatus::kNonexistent:
		mapped = AccountStatus::NonExisting;
		break;
	case engine::AccountStatus::kUninitialized:
		mapped = AccountStatus::Uninit;
		break;
	case engine::AccountStatus::kActive:
		mapped = AccountStatus::Active;
		break;
	case engine::AccountStatus::kFrozen:
		mapped = AccountStatus::Frozen;
		break;
	case engine::AccountStatus::kUnknown:
		LOG(("Wallet: engine account status unknown, keeping last-good."));
		break;
	}
	_engineStatus = mapped;
	_engineRefreshedAt = crl::now();
	_balanceNano = balance;
}

} // namespace Wallet
