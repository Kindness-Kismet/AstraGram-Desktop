#include "wallet/wallet_session_internal.h"

namespace Wallet {
using namespace SessionDetails;

bool Session::rotationOffered() {
	ensureLoaded();
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize) {
		return false;
	}
	const auto matching = currentRecord();
	return (matching != nullptr)
		&& !matching->rotatedSinceBackup
		&& !custody().pendingRotation
		&& (signingClient() != nullptr)
		&& !_clientStopping;
}

void Session::quoteRotationFee(
		KeyAuthorization auth,
		Fn<void(FeeResult)> done) {
	ensureLoaded();
	if (!rotationOffered()) {
		if (done) {
			done(FeeResult{ .error = SendError::SigningUnavailable });
		}
		return;
	} else if (custodyBusy()
		|| _previewPending
		|| _sendState.current() != SendState::Idle
		|| _pending
		|| _sendUnresolved) {
		if (done) {
			done(FeeResult{ .error = SendError::AlreadySending });
		}
		return;
	} else if (!ReadAuthorized(*this, auth)) {
		if (done) {
			done(FeeResult{ .error = SendError::Locked });
		}
		return;
	}
	retireCommentScopes();
	_rotating = true;
	const auto client = signingClient();
	// The record this client signs with, named now: a swap can rebind the
	// session's own id before the answer comes back.
	const auto signingRecordId = _clientRecordId;
	const auto generation = _networkGeneration;
	const auto finish = [=, this](FeeResult result) {
		_rotating = false;
		if (done) {
			done(result);
		}
	};
	const auto failed = [=](const QString &log) {
		LOG(("Wallet Error: key rotation quote failed: %1").arg(log));
		finish(FeeResult{ .error = SendError::Failed });
	};
	_engine->run([
		client,
		request = RotationRequest(kRotationQuoteValiditySeconds)
	] {
		auto prepared = client->prepare_key_rotation(request);
		return ThrowawayRotation{ Gram::BreakRotationSignature(
			QString::fromStdString(prepared.signed_boc)) };
	}, [=, this, grant = auth.grant](ThrowawayRotation throwaway) {
		if (throwaway.signedBoc.isEmpty()) {
			failed(u"unexpected rotation message shape"_q);
			return;
		} else if (generation != _networkGeneration) {
			failed(u"stale generation"_q);
			return;
		}
		_api.request(Gram::EmulateTraceRequest(
			throwaway.signedBoc
		), [=, this](const QByteArray &json) {
			const auto trace = Gram::ParseEmulatedTrace(json);
			if (generation != _networkGeneration) {
				failed(u"stale generation"_q);
			} else if (!trace
				|| CanonicalAddress(trace->account) != _address) {
				failed(u"unusable emulation trace"_q);
			} else {
				const auto fee = trace->feeNano;
				finish(FeeResult{
					.feeNano = fee,
					.error = (_balanceNano.current() < fee)
						? SendError::InsufficientFees
						: SendError::None,
				});
			}
		}, [=](const Gram::ApiError &error) {
			failed(u"MTP %1: %2"_q.arg(error.code).arg(error.message));
		});
	}, [=, this, grant = auth.grant](EngineError error) {
		LOG(("Wallet Error: engine prepare_key_rotation (quote) failed: %1"
			).arg(error.message));
		noteSecretReadFailure(ProtectedSecretFailure(error), signingRecordId);
		finish(FeeResult{ .error = (generation != _networkGeneration)
			? SendError::Failed
			: SendErrorFrom(error) });
	});
}

void Session::prepareRotation(
		KeyAuthorization auth,
		int64 quotedFeeNano,
		Fn<void(std::vector<QString>)> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
	fail = LoggedFail(u"rotation prepare"_q, std::move(fail));
	if (custodyBusy()) {
		LOG(("Wallet Error: rotation requested while another is in flight."));
		if (fail) {
			fail(u"ROTATION_BUSY"_q);
		}
		return;
	}
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize) {
		LOG(("Wallet Error: rotation requested "
			"without a settled wallet key."));
		if (fail) {
			fail(u"ROTATION_STATE_UNKNOWN"_q);
		}
		return;
	}
	const auto matching = currentRecord();
	if (!matching) {
		LOG(("Wallet Error: rotation requested without local custody."));
		if (fail) {
			fail(u"ROTATION_NO_CUSTODY"_q);
		}
		return;
	}
	if (matching->rotatedSinceBackup || custody().pendingRotation) {
		LOG(("Wallet Error: rotation requested "
			"with one already applied or pending."));
		if (fail) {
			fail(u"ROTATION_BUSY"_q);
		}
		return;
	}
	const auto client = signingClient();
	if (!client || _clientStopping) {
		LOG(("Wallet Error: rotation requested without a signing client."));
		if (fail) {
			fail(u"ROTATION_SIGNING_UNAVAILABLE"_q);
		}
		return;
	}
	if (_sendState.current() != SendState::Idle
		|| _pending
		|| _sendUnresolved) {
		LOG(("Wallet Error: rotation requested while a send is in flight."));
		if (fail) {
			fail(u"ROTATION_ALREADY_SENDING"_q);
		}
		return;
	}
	if (!ReadAuthorized(*this, auth)) {
		if (fail) {
			fail(u"ROTATION_VAULT_LOCKED"_q);
		}
		return;
	}
	retireCommentScopes();
	_rotating = true;
	const auto signingRecordId = _clientRecordId;
	fail = [this, fail = std::move(fail)](const QString &error) {
		_rotating = false;
		if (fail) {
			fail(error);
		}
	};
	_engine->run([
		client,
		request = RotationRequest(kClientSendValiditySeconds)
	] {
		return client->prepare_key_rotation(request);
	}, [=, this, grant = auth.grant](engine::PreparedKeyRotation prepared) {
		auto words = SplitWords(QString::fromStdString(
			prepared.replacement_recovery_phrase.phrase));
		auto newPublicKey = QByteArray(
			reinterpret_cast<const char*>(prepared.new_public_key.data()),
			prepared.new_public_key.size());
		if (words.size() < 2
			|| newPublicKey.size() != kCustodyPublicKeySize) {
			LOG(("Wallet Error: key rotation prepare produced "
				"no words or no key."));
			fail(u"ROTATION_PREPARE_FAILED"_q);
			return;
		}
		_preparedRotation = std::make_unique<PreparedRotation>(
			PreparedRotation{
				.words = words,
				.newPublicKey = std::move(newPublicKey),
				.signedBoc = std::move(prepared.signed_boc),
				.seqno = prepared.seqno,
				.validUntil = prepared.valid_until,
				.quotedFeeNano = quotedFeeNano,
			});
		if (done) {
			done(std::move(words));
		}
	}, [=, this, grant = auth.grant](EngineError error) {
		LOG(("Wallet Error: engine prepare_key_rotation failed: %1"
			).arg(error.message));
		noteSecretReadFailure(ProtectedSecretFailure(error), signingRecordId);
		fail(RotationErrorToken(error));
	});
}

void Session::abandonRotation() {
	if (_rotationConfirmed || custody().pendingRotation) {
		return;
	}
	_preparedRotation = nullptr;
	_rotating = false;
}

void Session::submitRotation(
		KeyAuthorization auth,
		Fn<void()> confirmed,
		Fn<void(const QString &error)> fail) {
	fail = LoggedFail(u"rotation submit"_q, std::move(fail));
	if (_rotationConfirmed) {
		LOG(("Wallet Error: rotation submitted while another is in flight."));
		if (fail) {
			fail(u"ROTATION_BUSY"_q);
		}
		return;
	}
	if (!_preparedRotation) {
		LOG(("Wallet Error: rotation submitted without a prepared one."));
		if (fail) {
			fail(u"ROTATION_NOT_PREPARED"_q);
		}
		return;
	}
	const auto refuse = [&](const QString &error) {
		abandonRotation();
		if (fail) {
			fail(error);
		}
	};
	if (uint64(base::unixtime::now()) >= _preparedRotation->validUntil) {
		LOG(("Wallet Error: rotation submitted after its validity window."));
		refuse(u"ROTATION_EXPIRED"_q);
		return;
	}
	if (_balanceNano.current() < _preparedRotation->quotedFeeNano) {
		LOG(("Wallet Error: rotation submitted with a balance below the fee."));
		refuse(u"ROTATION_FEES"_q);
		return;
	}
	const auto client = signingClient();
	if (!client || _clientStopping) {
		LOG(("Wallet Error: rotation submitted without a signing client."));
		refuse(u"ROTATION_SIGNING_UNAVAILABLE"_q);
		return;
	}
	const auto identity = transferWalletIdentity();
	const auto generation = _networkGeneration;
	if (!identity || !_sendRecoveryReady) {
		refuse(u"ROTATION_STATE_UNKNOWN"_q);
		return;
	} else if (_sendState.current() != SendState::Idle
		|| _pending
		|| _sendUnresolved) {
		refuse(u"ROTATION_ALREADY_SENDING"_q);
		return;
	} else if (!persistSubmittedTransfers()) {
		refuse(u"ROTATION_STORE_FAILED"_q);
		return;
	}
	// abandonRotation() reads a set _rotationConfirmed as "a submit is in
	// flight", so the latch is armed with a callable whatever was passed.
	_rotationConfirmed = [confirmed = std::move(confirmed)] {
		if (confirmed) {
			confirmed();
		}
	};
	_rotationFailed = std::move(fail);
	storePendingRotation(std::move(auth), [=, this] {
		if (!transferOperationCurrent(*identity, generation, client)
			|| !_sendRecoveryReady) {
			discardPendingRotation();
			finishRotation(u"ROTATION_STATE_UNKNOWN"_q);
			return;
		} else if (_sendState.current() != SendState::Idle
			|| _pending
			|| _sendUnresolved) {
			discardPendingRotation();
			finishRotation(u"ROTATION_ALREADY_SENDING"_q);
			return;
		} else if (!persistSubmittedTransfers()) {
			discardPendingRotation();
			finishRotation(u"ROTATION_STORE_FAILED"_q);
			return;
		}
		const auto awaitResolution = [=, this] {
			_preparedRotation = nullptr;
			updatePollingState();
			requestEngineRefresh();
		};
		const auto &pending = *custody().pendingRotation;
		auto request = engine::SendBocRequest{
			.operation_id = pending.operationId.toStdString(),
			.force = false,
			.signed_boc = std::move(_preparedRotation->signedBoc),
			.seqno = _preparedRotation->seqno,
			.valid_until = _preparedRotation->validUntil,
		};
		_engine->run([client, request = std::move(request)] {
			return client->send_boc(request);
		}, [=, this](engine::SendResult result) {
			if (TerminalSendPhase(result.phase)) {
				applyRotationSnapshot(engine::SendSnapshot{
					.operation_id = std::move(result.operation_id),
					.phase = result.phase,
				}, false);
			} else {
				awaitResolution();
			}
		}, [=, this](EngineError error) {
			LOG(("Wallet Error: engine send_boc failed: %1"
				).arg(error.message));
			if (IsSubmissionUnknown(error)) {
				awaitResolution();
				return;
			}
			discardPendingRotation();
			finishRotation(RotationErrorToken(error));
		});
	}, [=, this](const QString &error) {
		finishRotation(error);
	});
}

std::vector<CustodyRecord> Session::parkedRecords() {
	auto result = std::vector<CustodyRecord>();
	for (const auto &record : custody().records) {
		if (parked(record) && !parkedHidden(record, _address)) {
			result.push_back(record);
		}
	}
	ranges::reverse(result);
	return result;
}

bool Session::parked(const CustodyRecord &record) const {
	return RecordParked(record, _address, _publicKey);
}

DeviceCustodyState Session::deviceCustodyState() const {
	return _deviceCustody.current();
}

auto Session::deviceCustodyStateValue() const
-> rpl::producer<DeviceCustodyState> {
	return _deviceCustody.value();
}

void Session::applyRotationSnapshot(
		const engine::SendSnapshot &snapshot,
		bool journalAuthoritative) {
	if (!custody().pendingRotation) {
		return;
	}
	const auto operationId = snapshot.operation_id
		? QString::fromStdString(*snapshot.operation_id)
		: QString();
	if (operationId != custody().pendingRotation->operationId) {
		// A journal naming nothing for the pending is conclusive only from
		// a successful standalone resolve_pending(): refresh() swallows the
		// failure of its own embedded resolve (refresh.rs) and still
		// reports kCompleted with the client's fresh, empty send snapshot,
		// so an update's kIdle is not journal-derived, and the result of
		// send_boc speaks for its own submission only. Even the standalone
		// answer waits for no submit to be in flight: a resolve_pending
		// queued on the serial worker before the store write answers after
		// it, while the send_boc behind it is still queued, so with
		// _rotating set the result of send_boc is the authority. Without it
		// (after a restart) the journal is, and it re-reports even a
		// terminal record with its operation id, so an empty or foreign one
		// means the broadcast never reached it.
		if (journalAuthoritative && !_rotating) {
			LOG(("Wallet Error: pending rotation has no journal record."));
			discardPendingRotation();
			finishRotation(u"ROTATION_FAILED"_q);
		}
		return;
	}
	switch (snapshot.phase) {
	case engine::SendPhase::kConfirmed:
		promotePendingRotation();
		finishRotation(QString());
		return;
	case engine::SendPhase::kReplaced:
		discardPendingRotation();
		finishRotation(u"ROTATION_REPLACED"_q);
		return;
	case engine::SendPhase::kExpired:
		discardPendingRotation();
		finishRotation(u"ROTATION_EXPIRED"_q);
		return;
	case engine::SendPhase::kFailed:
	case engine::SendPhase::kCancelled:
	case engine::SendPhase::kSuperseded:
	case engine::SendPhase::kSequenceNumberConsumed:
		LOG(("Wallet Error: rotation ended in send phase %1."
			).arg(int(snapshot.phase)));
		discardPendingRotation();
		finishRotation(u"ROTATION_FAILED"_q);
		return;
	case engine::SendPhase::kIdle:
	case engine::SendPhase::kValidating:
	case engine::SendPhase::kAuthorizing:
	case engine::SendPhase::kPreparing:
	case engine::SendPhase::kPersisting:
	case engine::SendPhase::kReadyToSubmit:
	case engine::SendPhase::kSubmitting:
	case engine::SendPhase::kSubmissionUnknown:
	case engine::SendPhase::kSubmitted:
	case engine::SendPhase::kHandedOff:
		return;
	}
}

void Session::storePendingRotation(
		KeyAuthorization auth,
		Fn<void()> done,
		Fn<void(const QString &)> fail) {
	const auto active = currentRecord();
	if (!active) {
		LOG(("Wallet Error: rotation stored without local custody."));
		fail(u"ROTATION_NO_CUSTODY"_q);
		return;
	}
	// A store, so the seam requires a live grant and the retention window
	// alone is never enough; it also carries no install ladder, because the
	// rotation replaces the secret of a wallet whose vault already exists.
	if (!auth.grant || !auth.grant->valid()) {
		LOG(("Wallet Error: rotation stored without an unlocked vault."));
		fail(u"ROTATION_VAULT_LOCKED"_q);
		return;
	}
	const auto lifecycle = _engine->lifecycle();
	const auto expectedAnchor = active->publicKey;
	const auto address = CanonicalAddress(active->address);
	auto recoveryWords = std::vector<std::string>();
	recoveryWords.reserve(_preparedRotation->words.size());
	for (const auto &word : _preparedRotation->words) {
		recoveryWords.push_back(word.toStdString());
	}
	auto request = engine::ImportWalletRequest{
		.record_id = NewRecordId(),
		.network = engine::Network::kMainnet,
		.recovery_words = std::move(recoveryWords),
	};
	_engine->run([lifecycle, request = std::move(request)]() mutable {
		return lifecycle->import_wallet(request);
	}, [=, this, grant = auth.grant](engine::WalletDescriptor descriptor) {
		const auto record = RecordFromDescriptor(descriptor);
		const auto rollBack = [=, this](const QString &error) {
			_engine->run([lifecycle, descriptor] {
				lifecycle->delete_wallet(descriptor);
			}, [=] {
				fail(error);
			}, [=](EngineError) {
				LOG(("Wallet Error: delete_wallet after a refused rotation "
					"store failed."));
				fail(error);
			});
		};
		if (record.publicKey != expectedAnchor
			|| CanonicalAddress(record.address) != address) {
			LOG(("Wallet Error: prepared rotation phrase derives "
				"another wallet."));
			rollBack(u"ROTATION_KEY_MISMATCH"_q);
			return;
		}
		auto store = custody();
		store.pendingRotation = PendingRotation{
			.recordId = record.recordId,
			.secretRef = record.secretRef,
			.operationId = QString::fromStdString(NewRecordId()),
			.newPublicKey = _preparedRotation->newPublicKey,
		};
		if (!WriteCustodyStore(_session->local(), store)) {
			LOG(("Wallet Error: pending rotation write failed."));
			rollBack(u"ROTATION_STORE_FAILED"_q);
			return;
		}
		_custody = std::move(store);
		done();
	}, [=, grant = auth.grant](EngineError error) {
		LOG(("Wallet Error: import_wallet for a rotation failed: %1"
			).arg(LifecycleErrorName(error)));
		// The grant this store ran under can lapse between the confirmation
		// and the store, and the host then refuses it typed. That arm, not
		// the seam check above, is what states a vault emptied mid-flow.
		fail(IsVaultLocked(error)
			? u"ROTATION_VAULT_LOCKED"_q
			: u"ROTATION_STORE_FAILED"_q);
	});
}

void Session::discardPendingRotation() {
	auto store = custody();
	const auto pending = base::take(store.pendingRotation);
	if (!pending) {
		return;
	}
	// delete_wallet re-derives the address from the descriptor's anchor key
	// and refuses a record that disagrees, and the pending shares both with
	// the record of the served wallet by the import-time check, whether or
	// not that record still signs for it, so its descriptor is that
	// record's identity under the pending's handle.
	if (const auto held = store.forAddress(_address)) {
		const auto lifecycle = _engine->lifecycle();
		_engine->run([
			lifecycle,
			descriptor = DescriptorFromRecord(CustodyRecord{
				.recordId = pending->recordId,
				.address = held->address,
				.publicKey = held->publicKey,
				.network = held->network,
				.secretRef = pending->secretRef,
			})
		] {
			lifecycle->delete_wallet(descriptor);
		}, [] {}, [](EngineError error) {
			LOG(("Wallet Error: delete_wallet of a discarded rotation "
				"failed: %1").arg(LifecycleErrorName(error)));
		});
	} else {
		LOG(("Wallet Error: discarded rotation has no custody record, "
			"its secret is left in place."));
	}
	if (!WriteCustodyStore(_session->local(), store)) {
		LOG(("Wallet Error: pending rotation removal write failed."));
		return;
	}
	_custody = std::move(store);
	updatePollingState();
}

void Session::promotePendingRotation() {
	auto store = custody();
	const auto pending = base::take(store.pendingRotation);
	if (!pending) {
		return;
	}
	const auto i = ranges::find_if(store.records, [&](const auto &record) {
		return (CanonicalAddress(record.address) == _address);
	});
	if (i == end(store.records)) {
		LOG(("Wallet Error: confirmed rotation has no custody record."));
		discardPendingRotation();
		return;
	}
	// The record's identity survives and only its engine handle changes,
	// so the handle to delete is read before the swap, and the swap and
	// the pending's removal go down in one write: with two, a crash between
	// them would leave a promoted record beside a stale pending whose
	// recordId now IS the live one, and the next start's discard would
	// delete the live secret. The signing key and the awaiting mark go down
	// in that same write: the mark says the chain already holds the new key
	// while the server still serves the old one, and reconcileCustody()
	// clears it once the served key catches up or moves elsewhere. A pre-v4
	// pending names no key, so it promotes to an unknown signing key and
	// the record reads obsolete until its phrase is restored.
	const auto lifecycle = _engine->lifecycle();
	const auto superseded = DescriptorFromRecord(*i);
	i->recordId = pending->recordId;
	i->secretRef = pending->secretRef;
	i->signingKey = pending->newPublicKey;
	i->awaitingServerKey = !pending->newPublicKey.isEmpty()
		&& pending->newPublicKey != _publicKey;
	i->rotatedSinceBackup = true;
	if (!WriteCustodyStore(_session->local(), store)) {
		LOG(("Wallet Error: rotation promotion write failed, "
			"retrying on the next snapshot."));
		return;
	}
	_custody = std::move(store);
	_engine->run([lifecycle, superseded] {
		lifecycle->delete_wallet(superseded);
	}, [] {}, [](EngineError error) {
		LOG(("Wallet Error: delete_wallet of the rotated-out record "
			"failed: %1").arg(LifecycleErrorName(error)));
	});
	updateDeviceCustodyState();
	updatePollingState();
}

void Session::finishRotation(const QString &error) {
	_rotating = false;
	_preparedRotation = nullptr;
	const auto confirmed = base::take(_rotationConfirmed);
	const auto failed = base::take(_rotationFailed);
	if (!error.isEmpty()) {
		if (failed) {
			failed(error);
		}
	} else if (confirmed) {
		confirmed();
	}
}

void Session::clearRotatedSinceBackup() {
	auto store = custody();
	const auto i = ranges::find_if(store.records, [&](const auto &record) {
		return (CanonicalAddress(record.address) == _address);
	});
	if (i == end(store.records) || !i->rotatedSinceBackup) {
		return;
	}
	i->rotatedSinceBackup = false;
	if (!WriteCustodyStore(_session->local(), store)) {
		LOG(("Wallet Error: rotation guard reset write failed."));
		return;
	}
	_custody = std::move(store);
}

} // namespace Wallet
