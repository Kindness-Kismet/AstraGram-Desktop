#include "wallet/wallet_session_internal.h"

namespace Wallet {
using namespace SessionDetails;

void Session::resolvePending() {
	const auto identity = transferWalletIdentity();
	const auto client = signingClient();
	const auto generation = _networkGeneration;
	if (_resolveRequestPending
		|| !identity
		|| !transferOperationCurrent(*identity, generation, client)) {
		return;
	}
	_resolveRequestPending = true;
	const auto sendRevision = _sendRevision;
	_engine->run([client] {
		return client->resolve_pending();
	}, [=, this](engine::SendSnapshot snapshot) {
		const auto weak = base::make_weak(_engine.get());
		const auto current = [=] {
			return weak && transferOperationCurrent(
				*identity,
				generation,
				client);
		};
		if (!current()) {
			_resolveRequestPending = false;
			return;
		}
		const auto hadPending = bool(_pending);
		restoreSubmittedTransfers();
		if (!current()) {
			if (weak) {
				_resolveRequestPending = false;
			}
			return;
		}
		applySendSnapshot(snapshot, true, sendRevision);
		if (!current()) {
			if (weak) {
				_resolveRequestPending = false;
			}
			return;
		}
		applyRotationSnapshot(snapshot, true);
		if (!weak) {
			return;
		}
		_resolveRequestPending = false;
		if (!current()) {
			return;
		}
		if (sendRevision == _sendRevision) {
			_sendRecoveryReady = true;
			updateSigningReady();
			expireStaleSubmittedTransfers();
		}
		if (hadPending && !_pending) {
			requestEngineRefresh();
		}
		updatePollingState();
	}, [=, this](EngineError error) {
		_resolveRequestPending = false;
		if (!transferOperationCurrent(*identity, generation, client)) {
			return;
		}
		LOG(("Wallet Error: engine resolve_pending failed: %1, "
			"keeping last-good state.").arg(error.message));
	});
}

void Session::finishPending() {
	LOG(("Wallet: pending send resolved."));
	_pending.reset();
	_submission.reset();
	_sendUnresolved = false;
	_unresolvedOperationId.clear();
	const auto weak = base::make_weak(_engine.get());
	// Waiters follow this even when an unresolved send settles while Idle.
	_sendState.force_assign(SendState::Idle);
	if (!weak) {
		return;
	}
	syncEngineClient();
}

void Session::applySendSnapshot(
		const engine::SendSnapshot &snapshot,
		bool journalAuthoritative,
		uint64 sendRevision) {
	const auto weak = base::make_weak(_engine.get());
	const auto identity = transferWalletIdentity();
	const auto generation = _networkGeneration;
	const auto client = signingClient();
	const auto current = [=] {
		return weak && identity && transferOperationCurrent(
			*identity,
			generation,
			client);
	};
	if (!current()) {
		return;
	}
	const auto operationId = snapshot.operation_id.value_or(std::string());
	auto changed = false;
	if (journalAuthoritative
		&& sendRevision == _sendRevision
		&& !_submission
		&& _sendState.current() != SendState::Sending) {
		const auto record = submittedTransferRecord(operationId, *identity);
		if (record
			&& record->recordId == _clientRecordId
			&& record->handoff == TransferHandoff::Preparation
			&& snapshot.phase != engine::SendPhase::kIdle) {
			record->handoff = TransferHandoff::Possible;
			_submittedTransfersDirty = true;
			if (upsertSubmittedTransfer(
					operationId,
					*identity,
					generation,
					client)) {
				changed = true;
			}
		}
		const auto custodyRecord = custody().current(
			identity->address,
			identity->publicKey);
		if (custodyRecord && custodyRecord->recordId == _clientRecordId) {
			auto &records = submittedTransferStore().records;
			const auto size = records.size();
			records.erase(ranges::remove_if(records, [&](const auto &record) {
				return record.recordId == custodyRecord->recordId
					&& record.network == custodyRecord->network
					&& record.address == identity->address
					&& record.publicKey == identity->publicKey
					&& record.handoff == TransferHandoff::Preparation
					&& (record.operationId != operationId
						|| snapshot.phase == engine::SendPhase::kIdle);
			}), end(records));
			_submittedTransfersDirty = _submittedTransfersDirty
				|| records.size() != size;
		}
	}
	const auto found = submittedTransfer(operationId);
	const auto entry = (found && found->client.lock() == client)
		? found
		: nullptr;
	const auto terminal = snapshot.phase != engine::SendPhase::kIdle
		&& TerminalSendPhase(snapshot.phase);
	// Only the clock writes kExpired ahead of the journal, which outranks it.
	const auto phase = entry
		? PairedSendPhase(snapshot.phase, entry->paired)
		: snapshot.phase;
	const auto inferred = entry
		&& (entry->terminal == engine::SendPhase::kExpired)
		&& (phase != engine::SendPhase::kExpired);
	if (entry && terminal && (!entry->terminal || inferred)) {
		// The journal holds the normal delivery form of a paired send, so
		// the server executing the fee-free alternative instead advances the
		// sequence number without that exact message ever landing. The
		// engine reads its own message as replaced; for a send that offered
		// the alternative this is the ordinary outcome of the offer, and the
		// receipt's message hash names the transaction that did execute.
		entry->terminal = phase;
		changed = true;
		switch (phase) {
		case engine::SendPhase::kConfirmed:
		case engine::SendPhase::kSequenceNumberConsumed:
		case engine::SendPhase::kSuperseded:
			if (inferred && entry->fallback) {
				entry->fallback->status = TransferItem::Status::Pending;
			}
			break;
		default:
			if (entry->fallback) {
				entry->fallback->status = TransferItem::Status::Failure;
			}
			break;
		}
	}
	if (entry
		&& entry->terminal == engine::SendPhase::kConfirmed
		&& snapshot.phase == engine::SendPhase::kConfirmed
		&& entry->canonicalId.isEmpty()
		&& entry->confirmedHash.isEmpty()
		&& snapshot.resolution
		&& snapshot.resolution->transaction_hash) {
		const auto encoded = QByteArray::fromStdString(
			*snapshot.resolution->transaction_hash);
		auto decoded = QByteArray::fromBase64Encoding(
			encoded,
			QByteArray::AbortOnBase64DecodingErrors);
		if (decoded
			&& decoded.decoded.size() == 32
			&& decoded.decoded.toBase64() == encoded) {
			entry->confirmedHash = std::move(decoded.decoded);
			changed = true;
		}
	}
	const auto local = _pending
		&& _submission
		&& _pending->operationId == operationId
		&& _pending->walletIdentity == *identity
		&& submissionCurrent(operationId, _submission->prepared);
	auto settled = local && terminal;
	if (!_pending
		&& !_submission
		&& _sendState.current() == SendState::Idle
		&& sendRevision == _sendRevision) {
		if (!TerminalSendPhase(snapshot.phase)) {
			if (!operationId.empty()
				&& (!_sendUnresolved
					|| _unresolvedOperationId.empty()
					|| _unresolvedOperationId == operationId)) {
				_sendUnresolved = true;
				_unresolvedOperationId = operationId;
			}
		} else if (_sendUnresolved) {
			settled = (terminal
				&& !operationId.empty()
				&& _unresolvedOperationId == operationId)
				|| journalAuthoritative;
		}
	}
	_submittedTransfersDirty = _submittedTransfersDirty || changed;
	dropSubmittedIfListed();
	if (settled) {
		if (_submission
			&& _submission->rpcStarted
			&& FailedTransferTerminal(StoredTransferTerminal(
				PairedSendPhase(snapshot.phase, _submission->paired)))) {
			_transferMessages->failSending(
				_submission->draft,
				TransferTerminalCode(StoredTransferTerminal(
					PairedSendPhase(snapshot.phase, _submission->paired))));
		}
		finishPending();
	} else if (_sendUnresolved) {
		retireCommentScopes();
	}
	if (!current()) {
		return;
	}
	if (changed) {
		_historyUpdates.fire({});
		if (!current()) {
			return;
		}
		updateListsGate();
		if (!current()) {
			return;
		}
	}
	updatePollingState();
}

} // namespace Wallet
