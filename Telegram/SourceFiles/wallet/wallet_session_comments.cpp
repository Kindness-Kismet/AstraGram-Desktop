#include "wallet/wallet_session_internal.h"

namespace Wallet {
using namespace SessionDetails;

bool Session::revealsLocally() {
	ensureLoaded();
	const auto record = currentRecord();
	return (_presence.current() == Presence::Ready)
		&& (_publicKey.size() == kCustodyPublicKeySize)
		&& (record != nullptr)
		&& !vaultKeyUnusable()
		&& !secretUnreadable(record->recordId);
}

std::optional<BackupDisableApproval> Session::backupDisableApproval() {
	ensureLoaded();
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize
		|| vaultKeyUnusable()) {
		return std::nullopt;
	}
	const auto record = currentRecord();
	if (!record
		|| record->recordId.isEmpty()
		|| secretUnreadable(record->recordId)) {
		return std::nullopt;
	}
	return BackupDisableApproval{
		.address = _address,
		.recordId = record->recordId,
		.networkGeneration = _networkGeneration,
	};
}

VaultRuntime &Session::vault() const {
	return _engine->vault();
}

bool Session::custodyBusy() const {
	return _phraseRevealing || _replacing || _backupChanging || _rotating
		|| _custodyResetting;
}

std::shared_ptr<CommentScope> Session::createCommentScope(
		TransferItem target,
		rpl::lifetime &lifetime) {
	const auto &store = custody();
	if (!target.walletIdentity) {
		target.walletIdentity = transferWalletIdentity();
	}
	if (!target.walletIdentity
		|| !transferWalletIdentityCurrent(*target.walletIdentity)
		|| !EncryptedCommentRevealable(target)
		|| !commentAccessAvailable()
		|| custodyBusy()) {
		return nullptr;
	}
	const auto sender = CanonicalAddress(target.incoming
		? target.counterparty
		: target.walletIdentity->address);
	if (sender.isEmpty()) {
		return nullptr;
	}
	auto body = EncryptedCommentBody(target);
	if (!ValidEncryptedCommentBody(body)) {
		return nullptr;
	}
	auto state = std::make_shared<CommentScope::State>();
	state->session = this;
	state->target = std::move(target);
	state->body = std::move(body);
	state->sender = sender;
	state->vault = vault().shared_from_this();
	state->generation = _networkGeneration;
	state->epoch = vault().clearEpoch();
	const auto record = store.current(
		state->target.walletIdentity->address,
		state->target.walletIdentity->publicKey);
	if (record) {
		state->record = *record;
	}
	updateDeviceCustodyState();
	// A signing client bound to another record refuses the scope. A record
	// whose signing client is not up yet does not: decryptComment() starts
	// that client, or waits for the swap that brings it up.
	if (signingClient()
		&& (!state->record || _clientRecordId != state->record->recordId)) {
		return nullptr;
	}
	const auto scope = std::shared_ptr<CommentScope>(new CommentScope(state));
	if (!commentScopeCurrent(scope)) {
		return nullptr;
	}
	validateCommentScopes();
	_commentScopes.push_back(scope);
	lifetime.add([weak = std::weak_ptr(scope)] {
		if (const auto scope = weak.lock()) {
			scope->cancel();
		}
	});
	return scope;
}

bool Session::secretUnreadable(const QString &recordId) const {
	return !recordId.isEmpty() && (recordId == _unreadableRecordId);
}

void Session::validateUnreadableRecord() {
	if (_unreadableRecordId.isEmpty() || !_custody) {
		return;
	}
	const auto &records = _custody->records;
	const auto i = ranges::find(
		records,
		_unreadableRecordId,
		&CustodyRecord::recordId);
	if (i == end(records)) {
		_unreadableRecordId = QString();
	}
}

void Session::noteSecretReadFailure(
		SecretReadFailure failure,
		const QString &recordId) {
	// A verdict names the record whose secret was read. Without one it
	// judges nothing, and demoting the served wallet on evidence that is
	// not about it is exactly what this subject exists to prevent.
	if (recordId.isEmpty()) {
		return;
	} else if (failure == SecretReadFailure::Missing) {
		LOG(("Wallet Error: the protected secret of a held record is "
			"gone, dropping the record."));
		removeCustodyRecord(recordId);
	} else if (failure == SecretReadFailure::Unreadable
		&& _unreadableRecordId != recordId) {
		LOG(("Wallet Error: the protected secret of a held record could "
			"not be read; while that record serves this wallet the device "
			"serves read-only, until the next launch."));
		_unreadableRecordId = recordId;
		updateDeviceCustodyState();
	}
}

bool Session::commentAccessReady() const {
	return commentAccessAvailable() && !custodyBusy();
}

bool Session::commentAccessAvailable() const {
	const auto identity = transferWalletIdentity();
	return identity.has_value()
		&& _custody.has_value()
		&& !_custodyReadFailed
		&& _custody->records.size() <= 1
		&& !_custody->pendingRotation
		&& !(_clientStopping && !_clientRecordId.isEmpty())
		&& !_pending
		&& !_sendUnresolved
		&& _sendState.current() == SendState::Idle
		&& !ranges::any_of(_custody->records, [&](const CustodyRecord &record) {
			return &record != _custody->current(
				identity->address,
				identity->publicKey);
		});
}

bool Session::commentScopeCurrent(
		const std::shared_ptr<CommentScope> &scope) const {
	if (!scope || scope->cancelled()) {
		return false;
	}
	const auto &state = *scope->_state;
	if (state.session != this
		|| state.generation != _networkGeneration
		|| state.epoch != state.vault->clearEpoch()
		|| !transferWalletIdentityCurrent(*state.target.walletIdentity)
		|| !commentAccessAvailable()) {
		scope->cancel();
		return false;
	}
	const auto current = _custody->current(
		state.target.walletIdentity->address,
		state.target.walletIdentity->publicKey);
	if ((current != nullptr) != state.record.has_value()
		|| (current && (*current != *state.record
			|| !current->active
			|| current->recordId.isEmpty()
			|| current->secretRef.isEmpty()
			|| current->network != int(engine::Network::kMainnet)
			|| CanonicalAddress(current->address)
				!= state.target.walletIdentity->address))) {
		scope->cancel();
		return false;
	}
	return true;
}

void Session::validateCommentScopes() {
	const auto scopes = _commentScopes;
	for (const auto &weak : scopes) {
		if (const auto scope = weak.lock()) {
			if (commentScopeCurrent(scope)) {
				continue;
			}
		}
		_commentScopes.erase(ranges::remove_if(
			_commentScopes,
			[](const std::weak_ptr<CommentScope> &weak) {
				const auto scope = weak.lock();
				return !scope || scope->cancelled();
			}), end(_commentScopes));
	}
}

void Session::retireCommentScopes(
		const std::shared_ptr<CommentScope> &except) {
	const auto scopes = _commentScopes;
	for (const auto &weak : scopes) {
		if (const auto scope = weak.lock()) {
			if (scope != except) {
				scope->cancel();
			}
		}
	}
	validateCommentScopes();
}

void Session::decryptComment(
		KeyAuthorization auth,
		std::shared_ptr<CommentScope> scope,
		Fn<void(CommentDecryptResult)> done) {
	decryptComment({
		.auth = std::move(auth),
		.scope = std::move(scope),
		.done = std::move(done),
	});
}

void Session::decryptComment(DeferredDecrypt request) {
	using Error = CommentDecryptError;
	auto &auth = request.auth;
	const auto &scope = request.scope;
	const auto &done = request.done;
	const auto finish = [=, this](CommentDecryptResult result) {
		if (!commentScopeCurrent(scope)) {
			result = { .error = Error::Cancelled };
		}
		if (done) {
			done(std::move(result));
		}
	};
	// WHY: every refusal below is a state that settles on its own - a
	// custody operation in flight, a client still being swapped - so the
	// decryption waits for it instead of telling the user that a comment
	// which is perfectly readable cannot be read.
	const auto wait = [&] {
		if (request.attempts++ >= kDecryptBusyRetries) {
			return false;
		}
		_deferredDecrypts.push_back(std::move(request));
		_decryptRetryTimer.callOnce(kDecryptBusyRetryDelay);
		return true;
	};
	if (!commentScopeCurrent(scope)) {
		finish({ .error = Error::Cancelled });
		return;
	} else if (!scope->_state->record) {
		finish({ .error = Error::Unavailable });
		return;
	} else if (custodyBusy()) {
		if (!wait()) {
			finish({ .error = Error::Unavailable });
		}
		return;
	} else if (!ReadAuthorized(*this, auth)) {
		finish({ .error = Error::Locked });
		return;
	}
	// The record a restore under this scope has just stored gets its signing
	// client asynchronously: the public-key-only client that served the
	// previews stops first and the signing one starts from that stop's
	// callback. The decryption waits that swap out, and only a client that
	// settled on another record, or on none, refuses it.
	const auto recordId = scope->_state->record->recordId;
	if (!_clientStopping
		&& (!_engine->client() || _clientRecordId != recordId)) {
		syncEngineClient();
	}
	if (_clientStopping) {
		_deferredDecrypts.push_back(std::move(request));
		return;
	} else if (!_engine->client() || _clientRecordId != recordId) {
		if (!wait()) {
			finish({ .error = Error::Unavailable });
		}
		return;
	}
	const auto state = scope->_state;
	const auto client = _engine->client();
	const auto body = engine::DecryptCommentRequest{
		.sender = state->sender.toStdString(),
		.body = state->body.toStdString(),
	};
	// The retry keeps its own handle of the grant: the job takes the
	// original with it and drops it when the call ends.
	auto retry = request;
	++retry.attempts;
	_engine->runLocal([
		state,
		client,
		request = body,
		grant = std::move(auth.grant)
	]() mutable {
		const auto authorization = base::take(grant);
		if (state->cancelled || state->epoch != state->vault->clearEpoch()) {
			return DecryptedComment{ .error = Error::Cancelled };
		} else if (!authorization->valid() || !state->vault->unlocked()) {
			return DecryptedComment{ .error = Error::Locked };
		}
		auto result = DecryptCommentBody(client, request);
		if (state->cancelled || state->epoch != state->vault->clearEpoch()) {
			return DecryptedComment{ .error = Error::Cancelled };
		}
		return result;
	}, [=, this](DecryptedComment result) {
		// A secret this device holds and could not read is news about the
		// key, not about this comment, so it settles after the comment has
		// been answered with whatever it could be answered with.
		const auto settle = gsl::finally([this, recordId, secret = result.secret] {
			noteSecretReadFailure(secret, recordId);
		});
		if (!commentScopeCurrent(scope) || client != _engine->client()) {
			finish({ .error = Error::Cancelled });
		} else if (result.error == Error::Busy) {
			// The engine keeps one resolution slot, which the journal
			// recovery following a client start, a transfer preparation or
			// a name lookup may hold: the decryption tries again a bounded
			// number of times before it is stated unavailable.
			if (retry.attempts <= kDecryptBusyRetries) {
				_deferredDecrypts.push_back(retry);
				_decryptRetryTimer.callOnce(kDecryptBusyRetryDelay);
			} else {
				finish({ .error = Error::Unavailable });
			}
		} else if (result.error != Error::None) {
			finish({ .error = result.error });
		} else {
			const auto text = result.text.span();
			finish({ .text = QString::fromUtf8(
				reinterpret_cast<const char*>(text.data()),
				text.size()) });
		}
	}, [=](EngineError) {
		finish({ .error = Error::Failed });
	});
}

QString Session::phraseDiagnosticState() const {
	const auto now = crl::now();
	const auto held = _custody ? _custody->forAddress(_address) : nullptr;
	const auto capabilities = _capabilities.current();
	return u"address=%1 key=%2 revision=%3 presence=%4 device_mode=%5 "
		"conflict=%6 state_age_ms=%7 engine_age_ms=%8 state_request=%9 "
		"state_failures=%10 backup_enabled=%11 can_export=%12 "
		"can_enable_backup=%13; custody_loaded=%14 custody_read_failed=%15 "
		"held_anchor=%16 held_signing=%17 held_unreadable=%18 "
		"awaiting_server_key=%19 pending_rotation=%20; "
		"busy_reveal=%21 busy_replace=%22 busy_backup=%23 busy_rotate=%24 "
		"busy_reset=%25 vault_unlocked=%26 vault_unusable=%27"_q
		.arg(_address)
		.arg(LogKey(_publicKey))
		.arg(_walletIdentityRevision)
		.arg(int(_presence.current()))
		.arg(int(_deviceCustody.current().mode))
		.arg(_deviceCustody.current().conflict)
		.arg(_stateRefreshedAt ? now - _stateRefreshedAt : -1)
		.arg(_engineRefreshedAt ? now - _engineRefreshedAt : -1)
		.arg(_stateRequestId)
		.arg(_stateFailures)
		.arg(capabilities.backupEnabled)
		.arg(capabilities.canExportPhrase)
		.arg(capabilities.canEnableBackup)
		.arg(_custody.has_value())
		.arg(_custodyReadFailed)
		.arg(LogKey(held ? held->publicKey : QByteArray()))
		.arg(LogKey(held ? held->signingKey : QByteArray()))
		.arg(held && secretUnreadable(held->recordId))
		.arg(held && held->awaitingServerKey)
		.arg(_custody && _custody->pendingRotation.has_value())
		.arg(_phraseRevealing)
		.arg(_replacing)
		.arg(_backupChanging)
		.arg(_rotating)
		.arg(_custodyResetting)
		.arg(vault().unlocked())
		.arg(vault().unusable());
}

Fn<void(const QString &)> Session::loggedPhraseFail(
		const QString &stage,
		Fn<void(const QString &)> fail) {
	const auto initial = phraseDiagnosticState();
	const auto startedAt = crl::now();
	return [=, this, fail = std::move(fail)](const QString &error) {
		LOG(("Wallet Error: %1 refused: %2 elapsed_ms=%3; initial: %4; "
			"current: %5."
			).arg(stage
			).arg(error
			).arg(crl::now() - startedAt
			).arg(initial
			).arg(phraseDiagnosticState()));
		if (fail) {
			fail(error);
		}
	};
}

} // namespace Wallet
