#include "wallet/wallet_session_internal.h"

namespace Wallet {
using namespace SessionDetails;

bool Session::applySubmittedUpdate(
		const std::string &operationId,
		const MTPDupdateSentWalletTransaction &data) {
	const auto receipt = ReceiptFromServer(data);
	if (!receipt) {
		return false;
	}
	const auto weak = base::make_weak(_engine.get());
	const auto identity = transferWalletIdentity();
	const auto generation = _networkGeneration;
	if (!identity) {
		return false;
	}
	auto changed = false;
	{
		const auto active = _submission
			&& submissionCurrent(operationId, _submission->prepared);
		auto entry = submittedTransfer(operationId);
		const auto record = submittedTransferRecord(operationId, *identity);
		const auto conflicts = [&](const QByteArray &hash) {
			return !hash.isEmpty() && hash != receipt->messageHash;
		};
		if ((active && entry
				&& entry->client.lock() != _submission->prepared->client)
			|| (record && record->handoff != TransferHandoff::Possible)
			|| (entry && entry->receipt
				&& conflicts(entry->receipt->messageHash))
			|| (active && _submission->receipt
				&& conflicts(_submission->receipt->messageHash))
			|| (record && record->messageHash
				&& conflicts(*record->messageHash))) {
			return false;
		}
		if (ranges::any_of(_submitted, [&](const auto &other) {
				return other.operationId != operationId
					&& other.generation == generation
					&& other.identity == *identity
					&& other.receipt
					&& other.receipt->messageHash == receipt->messageHash;
			})) {
			return false;
		}
		if (!entry && active) {
			entry = upsertSubmittedTransfer(
				operationId,
				*identity,
				generation,
				_submission->prepared->client);
			changed = (entry != nullptr);
		}
		if (!entry) {
			return false;
		}
		if (active
			&& (!_submission->receipt
				|| _submission->receipt->messageHash.isEmpty())) {
			_submission->receipt = receipt;
			_lastReceipt = receipt;
			changed = true;
		}
		if (record && (!record->messageHash || record->messageHash->isEmpty())) {
			record->messageHash = receipt->messageHash;
			_submittedTransfersDirty = true;
			changed = true;
		}
		if ((!entry->receipt || entry->receipt->messageHash.isEmpty())
			&& (entry->canonicalId.isEmpty() || entry->item)) {
			entry->receipt = receipt;
			changed = true;
		}
		if (const auto transaction = data.vtransaction()) {
			changed = !entry->lookupStopped || changed;
			entry->lookupStopped = true;
			auto items = std::vector{
				HistoryItemFromServer(*transaction, identity),
			};
			rememberCollectibles(items);
			changed = adoptSubmittedTransaction(
				operationId,
				std::move(items.front())) || changed;
		}
	}
	if (!persistSubmittedTransfers()) {
		LOG(("Wallet Error: received transfer facts remain dirty."));
	}
	dropSubmittedIfListed();
	if (!changed) {
		return true;
	}
	_historyUpdates.fire({});
	if (!weak
		|| generation != _networkGeneration
		|| !transferWalletIdentityCurrent(*identity)) {
		return true;
	}
	updateListsGate();
	if (weak
		&& generation == _networkGeneration
		&& transferWalletIdentityCurrent(*identity)) {
		startSubmittedLookup();
	}
	return true;
}

bool Session::transferOperationCurrent(
		const TransferWalletIdentity &identity,
		int generation,
		const std::shared_ptr<engine::WalletClient> &client) const {
	return generation == _networkGeneration
		&& transferWalletIdentityCurrent(identity)
		&& client
		&& client == _engine->client()
		&& !_clientStopping;
}

Session::SubmittedTransfer *Session::submittedTransfer(
		const std::string &operationId) {
	if (operationId.empty()) {
		return nullptr;
	}
	const auto i = ranges::find(
		_submitted,
		operationId,
		&SubmittedTransfer::operationId);
	return (i != end(_submitted)
		&& i->generation == _networkGeneration
		&& transferWalletIdentityCurrent(i->identity))
		? &*i
		: nullptr;
}

SubmittedTransferStore &Session::submittedTransferStore() {
	if (!_submittedTransferStore) {
		_submittedTransferStore = ReadSubmittedTransferStore(_session->local());
		if (!_submittedTransferStore) {
			LOG(("Wallet Error: submitted transfer store unreadable."));
			_submittedTransferStore = SubmittedTransferStore();
		}
	}
	return *_submittedTransferStore;
}

SubmittedTransferRecord *Session::submittedTransferRecord(
		const std::string &operationId,
		const TransferWalletIdentity &identity) {
	const auto custodyRecord = custody().current(
		identity.address,
		identity.publicKey);
	if (!custodyRecord) {
		return nullptr;
	}
	auto &records = submittedTransferStore().records;
	const auto found = ranges::find_if(records, [&](const auto &record) {
		return record.network == custodyRecord->network
			&& record.address == identity.address
			&& record.publicKey == identity.publicKey
			&& record.operationId == operationId;
	});
	return (found != end(records)) ? &*found : nullptr;
}

bool Session::persistSubmittedTransfers() {
	auto &store = submittedTransferStore();
	for (const auto &entry : _submitted) {
		if (entry.generation != _networkGeneration
			|| !transferWalletIdentityCurrent(entry.identity)) {
			continue;
		}
		const auto record = submittedTransferRecord(
			entry.operationId,
			entry.identity);
		if (!record) {
			continue;
		} else if (!entry.canonicalId.isEmpty() && !entry.item) {
			retireSubmittedTransferRecord(entry.operationId, entry.identity);
			continue;
		}
		const auto was = *record;
		if (entry.receipt && !record->messageHash) {
			record->messageHash = entry.receipt->messageHash;
		}
		if (entry.terminal) {
			record->terminal = StoredTransferTerminal(*entry.terminal);
		}
		record->confirmedHash = entry.confirmedHash;
		record->lookupAttempts = entry.lookupAttempts;
		record->lookupStopped = entry.lookupStopped;
		if (entry.item) {
			record->served = StoredTransferProjection(*entry.item);
		}
		_submittedTransfersDirty = _submittedTransfersDirty || (*record != was);
	}
	if (!_submittedTransfersDirty) {
		return true;
	}
	auto pruned = store;
	auto size = SubmittedTransferStoreSize(pruned);
	if (!size) {
		return false;
	}
	const auto now = base::unixtime::now();
	while (pruned.records.size() > kSubmittedTransferMaxRecords
		|| *size > kSubmittedTransferMaxBytes) {
		auto oldest = end(pruned.records);
		for (auto i = begin(pruned.records); i != end(pruned.records); ++i) {
			const auto live = submittedTransferRecord(
				i->operationId,
				TransferWalletIdentity{
					.address = i->address,
					.publicKey = i->publicKey,
				});
			const auto current = live
				&& live->recordId == i->recordId
				&& live->network == i->network
				&& ((_sendUnresolved
						&& _unresolvedOperationId == i->operationId)
					|| (_submission
						&& _submission->operationId == i->operationId));
			if (current
				|| (!i->served
					&& i->terminal == TransferTerminal::None
					&& !StaleSubmittedRecord(*i, now))) {
				continue;
			}
			if (oldest == end(pruned.records) || i->posted < oldest->posted) {
				oldest = i;
			}
		}
		if (oldest == end(pruned.records)) {
			return false;
		}
		pruned.records.erase(oldest);
		size = SubmittedTransferStoreSize(pruned);
		if (!size) {
			return false;
		}
	}
	if (!WriteSubmittedTransferStore(_session->local(), pruned)) {
		return false;
	}
	store = std::move(pruned);
	auto hidden = false;
	for (auto &entry : _submitted) {
		if (submittedTransferRecord(entry.operationId, entry.identity)) {
			continue;
		}
		entry.lookupStopped = true;
		if (!entry.terminal && !entry.item && entry.canonicalId.isEmpty()) {
			entry.fallback.reset();
			hidden = true;
		}
	}
	_submittedTransfersDirty = false;
	if (hidden) {
		_historyUpdates.fire({});
	}
	return true;
}

void Session::retireSubmittedTransferRecord(
		const std::string &operationId,
		const TransferWalletIdentity &identity) {
	if (const auto record = submittedTransferRecord(operationId, identity)) {
		auto &records = submittedTransferStore().records;
		records.erase(begin(records) + (record - records.data()));
		_submittedTransfersDirty = true;
	}
}

Session::SubmittedTransfer *Session::upsertSubmittedTransfer(
		const std::string &operationId,
		const TransferWalletIdentity &identity,
		int generation,
		const std::shared_ptr<engine::WalletClient> &client) {
	if (const auto entry = submittedTransfer(operationId)) {
		return (entry->client.lock() == client) ? entry : nullptr;
	}
	const auto record = submittedTransferRecord(operationId, identity);
	if (!record || record->handoff != TransferHandoff::Possible) {
		return nullptr;
	}
	if (_submitted.size() >= kSubmittedTransferMaxRecords) {
		_submitted.erase(ranges::remove_if(_submitted, [&](const auto &entry) {
			const auto active = (_submission
					&& _submission->operationId == entry.operationId)
				|| (_sendUnresolved
					&& _unresolvedOperationId == entry.operationId);
			return !active
				&& ((!entry.fallback && !entry.item)
					|| ((entry.terminal || entry.item)
						&& !submittedTransferRecord(
							entry.operationId,
							entry.identity)));
		}), end(_submitted));
	}
	if (_submitted.size() >= kSubmittedTransferMaxRecords) {
		return nullptr;
	}
	auto fallback = std::make_unique<TransferItem>(ItemFromPending({
		.operationId = operationId,
		.walletIdentity = identity,
		.posted = record->posted,
		.amountNano = record->amountNano,
		.destination = record->destination,
		.collectible = record->collectible,
		.comment = record->comment,
		.recipient = record->recipient,
		.bounce = record->bounce,
	}));
	if (FailedTransferTerminal(record->terminal)) {
		fallback->status = TransferItem::Status::Failure;
	}
	auto item = std::unique_ptr<TransferItem>();
	if (record->served) {
		const auto &stored = *record->served;
		item = std::make_unique<TransferItem>();
		item->source = TransferItem::Source::Server;
		item->id = stored.id;
		item->walletIdentity = identity;
		item->kind = !stored.collectible.isEmpty()
			? TransferItem::Kind::Collectible
			: stored.peerTransfer
			? TransferItem::Kind::PeerTransfer
			: TransferItem::Kind::Transfer;
		item->counterparty = stored.counterparty;
		item->counterpartyBounceable = stored.counterpartyBounceable;
		item->counterpartyName = stored.counterpartyName;
		item->counterpartyPeer = stored.counterpartyPeer;
		item->collectible = stored.collectible;
		item->amountNano = stored.amountNano;
		item->feeNano = stored.feeNano;
		item->gasless = stored.gasless;
		item->comment = stored.comment;
		item->commentEncrypted = stored.commentEncrypted;
		item->date = stored.date;
		item->status = stored.failed
			? TransferItem::Status::Failure
			: TransferItem::Status::Success;
		fallback.reset();
	}
	auto receipt = std::optional<TransferReceipt>();
	if (record->messageHash) {
		receipt = TransferReceipt{ .messageHash = *record->messageHash };
	}
	const auto canonicalId = item ? item->id : QString();
	_submitted.push_back(SubmittedTransfer{
		.operationId = operationId,
		.identity = identity,
		.client = (record->recordId == _clientRecordId) ? client : nullptr,
		.fallback = std::move(fallback),
		.item = std::move(item),
		.canonicalId = canonicalId,
		.collectible = record->collectible,
		.confirmedHash = record->confirmedHash,
		.receipt = std::move(receipt),
		.terminal = RestoredTransferTerminal(record->terminal),
		.generation = generation,
		.lookupAttempts = record->lookupAttempts,
		.lookupStopped = record->lookupStopped,
		.paired = record->paired,
	});
	return &_submitted.back();
}

bool Session::submittedLookupNeeded() const {
	return _lookup || ranges::any_of(_submitted, [&](const auto &entry) {
		return entry.receipt
			&& !entry.lookupStopped
			&& entry.lookupAttempts < kSubmittedLookupAttempts
			&& entry.canonicalId.isEmpty()
			&& entry.generation == _networkGeneration
			&& transferWalletIdentityCurrent(entry.identity);
	});
}

bool Session::submittedLookupCurrent(
		const std::shared_ptr<SubmittedLookup> &request) const {
	if (_lookup != request
		|| request->generation != _networkGeneration
		|| !transferWalletIdentityCurrent(request->identity)) {
		return false;
	}
	const auto i = ranges::find(
		_submitted,
		request->operationId,
		&SubmittedTransfer::operationId);
	return i != end(_submitted)
		&& i->identity == request->identity
		&& i->generation == request->generation
		&& i->canonicalId.isEmpty()
		&& i->receipt
		&& i->receipt->messageHash == request->messageHash;
}

void Session::startSubmittedLookup() {
	updatePollingState();
}

void Session::lookupSubmittedTransaction() {
	if (_lookup || _submitted.empty()) {
		return;
	}
	const auto last = ranges::find(
		_submitted,
		_lastLookupOperationId,
		&SubmittedTransfer::operationId);
	const auto start = (last == end(_submitted))
		? size_t(0)
		: size_t(last - begin(_submitted) + 1);
	for (auto i = size_t(0); i != _submitted.size(); ++i) {
		auto &entry = _submitted[(start + i) % _submitted.size()];
		if (!entry.receipt
			|| entry.lookupStopped
			|| !entry.canonicalId.isEmpty()
			|| entry.lookupAttempts >= kSubmittedLookupAttempts
			|| entry.generation != _networkGeneration
			|| !transferWalletIdentityCurrent(entry.identity)) {
			continue;
		}
		if (!submittedTransferRecord(entry.operationId, entry.identity)) {
			continue;
		}
		++entry.lookupAttempts;
		entry.lookupStopped = (entry.lookupAttempts >= kSubmittedLookupAttempts);
		const auto request = std::make_shared<SubmittedLookup>(SubmittedLookup{
			.operationId = entry.operationId,
			.identity = entry.identity,
			.messageHash = entry.receipt->messageHash,
			.generation = entry.generation,
		});
		_lastLookupOperationId = entry.operationId;
		if (!persistSubmittedTransfers()) {
			--entry.lookupAttempts;
			entry.lookupStopped = false;
			if (const auto record = submittedTransferRecord(
					entry.operationId,
					entry.identity)) {
				record->lookupAttempts = entry.lookupAttempts;
				record->lookupStopped = entry.lookupStopped;
			}
			LOG(("Wallet Error: transfer lookup budget could not be stored."));
			return;
		} else if (!submittedTransferRecord(
				request->operationId,
				request->identity)) {
			return;
		}
		_lookup = request;
		// The token is the server's own opaque message hash, echoed exactly
		// as it arrived: MTP_string(const std::string &) copies the bytes
		// verbatim, so no encoding is imposed on a value whose contract
		// states none. MTP_string(const QString &) would re-encode through
		// QString::toUtf8(), which is why the QByteArray overload is deleted;
		// neither is used here.
		request->id = _stateApi.request(MTPwallet_GetTransactionsByMsgHash(
			MTP_vector<MTPstring>(
				1,
				MTP_string(request->messageHash.toStdString()))
		)).done([=](const MTPwallet_Transactions &result) {
			if (_lookup != request) {
				return;
			}
			const auto weak = base::make_weak(_engine.get());
			if (submittedLookupCurrent(request)) {
				applySubmittedLookup(result, request);
			}
			if (!weak || _lookup != request) {
				return;
			}
			_lookup = nullptr;
			updatePollingState();
		}).fail([=](const MTP::Error &error) {
			if (_lookup != request) {
				return;
			}
			const auto current = submittedLookupCurrent(request);
			_lookup = nullptr;
			if (current) {
				LOG(("Wallet Error: wallet.getTransactionsByMsgHash failed: %1"
					).arg(error.type()));
			}
			updatePollingState();
		}).handleAllErrors().send();
		return;
	}
}

void Session::applySubmittedLookup(
		const MTPwallet_Transactions &result,
		const std::shared_ptr<SubmittedLookup> &request) {
	const auto weak = base::make_weak(_engine.get());
	const auto &data = result.data();
	_session->data().processUsers(data.vusers());
	if (!weak || !submittedLookupCurrent(request)) {
		return;
	}
	_session->data().processChats(data.vchats());
	if (!weak || !submittedLookupCurrent(request)) {
		return;
	}
	// The answer's balance and next_offset are read by neither this lane
	// nor applyTransactions(): the state lane and the engine refresh are
	// the balance authority, and a by-message answer is not the paged
	// feed, so its offset would page a list that nobody renders.
	auto loaded = HistoryFromServer(data.vtransactions().v, request->identity);
	rememberCollectibles(loaded);
	// wallet.transactions echoes neither the requested message hash nor
	// any per-row link to it, so the attribution is made by the request:
	// one hash per lookup, and the whole answer belongs to it. Within the
	// answer only a record this wallet signed as an ordinary transfer can
	// be the submitted operation - a self-transfer also returns the
	// incoming half, and HistoryItemFromServer marks every key change
	// outgoing whatever the server's bit says - and only a record that
	// names itself, because the identity this lane needs is the server's
	// own transaction id and an empty string is not one. Two candidates
	// naming different transactions cannot be told apart, and a message's
	// records only grow, so no later answer would resolve it: the lane
	// stops with nothing attached, the head page still brings the real
	// row in, no payment is declared failed and no second send is freed.
	const auto candidate = [](const TransferItem &item) {
		return !item.incoming
			&& !item.id.isEmpty()
			&& (item.kind != TransferItem::Kind::KeyChange);
	};
	const auto found = ranges::find_if(loaded, candidate);
	if (found == end(loaded)) {
		return;
	}
	const auto id = found->id;
	const auto ambiguous = ranges::any_of(loaded, [&](const auto &item) {
		return candidate(item) && item.id != id;
	});
	if (ambiguous) {
		if (const auto entry = submittedTransfer(request->operationId)) {
			entry->lookupStopped = true;
		}
		LOG(("Wallet Error: wallet.getTransactionsByMsgHash sent "
			"ambiguous transaction identity."));
		if (!persistSubmittedTransfers()) {
			LOG(("Wallet Error: stopped transfer lookup remains dirty."));
		}
		return;
	}
	const auto changed = adoptSubmittedTransaction(
		request->operationId,
		std::move(*found));
	dropSubmittedIfListed();
	if (!changed) {
		return;
	}
	_historyUpdates.fire({});
	if (!weak
		|| request->generation != _networkGeneration
		|| !transferWalletIdentityCurrent(request->identity)) {
		return;
	}
	updateListsGate();
	if (weak
		&& request->generation == _networkGeneration
		&& transferWalletIdentityCurrent(request->identity)) {
		updatePollingState();
	}
}

bool Session::adoptSubmittedTransaction(
		const std::string &operationId,
		TransferItem item) {
	const auto entry = submittedTransfer(operationId);
	if (!entry || item.walletIdentity != entry->identity) {
		return false;
	}
	auto changed = !entry->lookupStopped;
	entry->lookupStopped = true;
	if (_lookup
		&& _lookup->operationId == operationId
		&& _lookup->identity == entry->identity
		&& _lookup->generation == entry->generation) {
		dropSubmittedLookup();
		changed = true;
	}
	const auto conflict = item.id.isEmpty()
		|| (!entry->canonicalId.isEmpty() && entry->canonicalId != item.id)
		|| ranges::any_of(_submitted, [&](const auto &other) {
			return other.operationId != operationId
				&& other.generation == entry->generation
				&& other.identity == entry->identity
				&& other.canonicalId == item.id;
		});
	if (conflict) {
		LOG(("Wallet Error: sent transfer has unusable or conflicting "
			"transaction identity."));
	} else if (entry->canonicalId.isEmpty()) {
		if (const auto record = submittedTransferRecord(
				operationId,
				entry->identity)) {
			KeepSubmittedRecipient(item, *record);
		}
		entry->canonicalId = item.id;
		entry->item = std::make_unique<TransferItem>(std::move(item));
		entry->fallback.reset();
		entry->confirmedHash.clear();
		changed = true;
	}
	if (!persistSubmittedTransfers()) {
		LOG(("Wallet Error: canonical transfer facts remain dirty."));
	}
	return changed;
}

void Session::dropSubmittedIfListed() {
	auto changed = false;
	auto leaving = false;
	for (auto &entry : _submitted) {
		if (entry.generation != _networkGeneration
			|| !transferWalletIdentityCurrent(entry.identity)
			|| (!entry.fallback && !entry.item)) {
			continue;
		}
		// WHY: the engine confirms only the wallet's own message; the item
		// changes owner in a later transaction and wallet.getNfts follows the
		// chain with a lag, so the forced cadence runs until the item is gone.
		if (!entry.leaving
			&& !entry.collectible.isEmpty()
			&& ((entry.terminal == engine::SendPhase::kConfirmed)
				|| (entry.item
					&& !entry.item->incoming
					&& (entry.item->status
						== TransferItem::Status::Success)))) {
			entry.leaving = true;
			followCollectible(entry.collectible, false);
			leaving = true;
		}
		if (entry.canonicalId.isEmpty() && !entry.confirmedHash.isEmpty()) {
			const auto candidate = [&](const TransferItem &item) {
				return !item.incoming
					&& item.kind != TransferItem::Kind::KeyChange
					&& !item.id.isEmpty()
					&& item.walletIdentity == entry.identity
					&& item.traceId == entry.confirmedHash;
			};
			const auto found = ranges::find_if(_history, candidate);
			if (found != end(_history)) {
				const auto ambiguous = ranges::any_of(
					_history,
					[&](const auto &item) {
						return candidate(item) && item.id != found->id;
					}) || ranges::any_of(_submitted, [&](const auto &other) {
						return other.operationId != entry.operationId
							&& (other.confirmedHash == entry.confirmedHash
								|| other.canonicalId == found->id);
					});
				if (!ambiguous) {
					entry.canonicalId = found->id;
				}
			}
		}
		if (entry.canonicalId.isEmpty()
			|| !ranges::contains(
				_history,
				entry.canonicalId,
				&TransferItem::id)) {
			continue;
		}
		changed = true;
		entry.fallback.reset();
		entry.item.reset();
		entry.receipt.reset();
		entry.confirmedHash.clear();
		entry.lookupStopped = true;
		if (_lookup && _lookup->operationId == entry.operationId) {
			dropSubmittedLookup();
		}
	}
	if ((changed || _submittedTransfersDirty) && !persistSubmittedTransfers()) {
		LOG(("Wallet Error: reconciled transfer facts remain dirty."));
	}
	if (leaving) {
		refreshCollectibles(true);
	}
}

void Session::dropSubmittedLookup() {
	if (const auto request = base::take(_lookup)) {
		_stateApi.request(request->id).cancel();
	}
}

void Session::SettleTonConnect(
		TransferSubmissionState &submission,
		SendError error) {
	if (auto report = base::take(submission.tonConnect)) {
		report({
			submission.rpcStarted
				? QString::fromLatin1(submission.normal.toBase64())
				: QString(),
			error,
		});
	}
}

void Session::retireSubmission() {
	auto retired = base::take(_submission);
	if (!retired) {
		return;
	}
	SettleTonConnect(*retired, SendError::Failed);
	if (retired->rpcStarted) {
		_transferMessages->dropSending(retired->draft);
	}
}

void Session::clearSubmittedTransfers() {
	dropSubmittedLookup();
	_submitted.clear();
	_pending.reset();
	retireSubmission();
	_lastReceipt.reset();
	_lastLookupOperationId.clear();
	_unresolvedOperationId.clear();
	_windowSend.clear();
	_sendUnresolved = false;
	_sendRecoveryReady = false;
	++_sendRevision;
	updateSigningReady();
}

bool Session::sendRecoveryNeeded() const {
	return !_sendRecoveryReady
		&& !_clientStopping
		&& signingClient()
		&& transferWalletIdentity().has_value();
}

// WHY: past valid_until the contract refuses the signed message, so a row
// still pending then can never execute. The verdict is inferred, so it
// waits for the journal: a guess here would outrank the real answer.
void Session::expireStaleSubmittedTransfers() {
	const auto identity = transferWalletIdentity();
	if (!_sendRecoveryReady || !identity) {
		return;
	}
	const auto now = base::unixtime::now();
	auto changed = false;
	for (auto &record : submittedTransferStore().records) {
		if (record.terminal != TransferTerminal::None
			|| record.served
			|| record.handoff != TransferHandoff::Possible
			|| record.recordId != _clientRecordId
			|| submittedTransferRecord(record.operationId, *identity) != &record
			|| !StaleSubmittedRecord(record, now)
			|| record.operationId == _unresolvedOperationId
			|| (_submission
				&& _submission->operationId == record.operationId)) {
			continue;
		}
		record.terminal = TransferTerminal::Expired;
		changed = true;
		if (const auto entry = submittedTransfer(record.operationId)) {
			if (!entry->terminal) {
				entry->terminal = engine::SendPhase::kExpired;
			}
			if (entry->fallback) {
				entry->fallback->status = TransferItem::Status::Failure;
			}
		}
	}
	if (!changed) {
		return;
	}
	_submittedTransfersDirty = true;
	if (!persistSubmittedTransfers()) {
		LOG(("Wallet Error: expired transfer facts remain dirty."));
	}
	_historyUpdates.fire({});
}

void Session::dropForeignSubmittedTransfers(
		const TransferWalletIdentity &identity) {
	const auto custodyRecord = custody().current(
		identity.address,
		identity.publicKey);
	if (!custodyRecord || _clientRecordId.isEmpty()) {
		return;
	}
	const auto foreign = [&](const SubmittedTransferRecord &record) {
		return record.recordId != _clientRecordId
			&& record.network == custodyRecord->network
			&& record.address == identity.address
			&& record.publicKey == identity.publicKey;
	};
	auto &records = submittedTransferStore().records;
	auto dropped = std::vector<std::string>();
	for (const auto &record : records) {
		if (foreign(record)) {
			dropped.push_back(record.operationId);
		}
	}
	if (dropped.empty()) {
		return;
	}
	records.erase(ranges::remove_if(records, foreign), end(records));
	_submitted.erase(ranges::remove_if(_submitted, [&](const auto &entry) {
		return entry.identity.address == identity.address
			&& entry.identity.publicKey == identity.publicKey
			&& ranges::contains(dropped, entry.operationId);
	}), end(_submitted));
	_submittedTransfersDirty = true;
	if (!persistSubmittedTransfers()) {
		LOG(("Wallet Error: dropped foreign transfer facts remain dirty."));
	}
	_historyUpdates.fire({});
	updateListsGate();
}

void Session::restoreSubmittedTransfers() {
	const auto identity = transferWalletIdentity();
	const auto generation = _networkGeneration;
	const auto client = signingClient();
	if (!identity || !transferOperationCurrent(*identity, generation, client)) {
		return;
	}
	dropForeignSubmittedTransfers(*identity);
	auto changed = false;
	for (const auto &record : submittedTransferStore().records) {
		if (record.handoff != TransferHandoff::Possible
			|| (_submission && _submission->operationId == record.operationId)
			|| submittedTransfer(record.operationId)
			|| submittedTransferRecord(
				record.operationId,
				*identity) != &record) {
			continue;
		}
		if (upsertSubmittedTransfer(
				record.operationId,
				*identity,
				generation,
				client)) {
			changed = true;
		}
	}
	dropSubmittedIfListed();
	if (!changed) {
		return;
	}
	const auto weak = base::make_weak(_engine.get());
	_historyUpdates.fire({});
	if (weak && transferOperationCurrent(*identity, generation, client)) {
		updateListsGate();
	}
}

} // namespace Wallet
