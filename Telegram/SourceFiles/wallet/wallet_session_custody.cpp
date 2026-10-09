#include "wallet/wallet_session_internal.h"

namespace Wallet {
using namespace SessionDetails;

rpl::producer<> Session::custodyUpdates() const {
	return _custodyUpdates.events();
}

rpl::producer<> Session::keyProtectionUpdates() const {
	return vault().protectionChanges();
}

void Session::notifyKeyProtectionChanged(bool vaultKeyStillUnusable) {
	vault().notifyProtectionChanged(vaultKeyStillUnusable);
}

bool Session::vaultKeyUnusable() const {
	return vault().unusable();
}

void Session::setVaultKeyUnusable(bool unusable) {
	vault().setUnusable(unusable);
}

void Session::resetCustodyAfterForgottenPasscode(
		Fn<void(CustodyResetResult)> done) {
	const auto weak = base::make_weak(_session);
	const auto current = [weak] {
		return weak && !weak->domain().local().appLockEnabled()
			&& LiveKeyProtection() == VaultKind::Passcode;
	};
	if (custodyBusy() || !current()) {
		done(CustodyResetResult::Refused);
		return;
	}
	resetDeviceCustody(nullptr, current, [done](CustodyResetResult result) {
		DropUnusedPasscode();
		done(result);
	});
}

// The confirmation belongs to one still-current custody flow, but destroys
// the device authority. Check that flow before starting and after every
// client has stopped. A deliberate clear restamps only its surviving scope;
// any later clear, identity change or cancellation still refuses the install.
void Session::resetUnusableVault(
		const QByteArray &expected,
		const std::shared_ptr<CommentScope> &scope,
		Fn<void(CustodyResetResult)> done) {
	const auto weak = base::make_weak(_session);
	const auto current = [=] {
		if (!weak || weak->account().maybeSession() != weak.get()) {
			return false;
		}
		const auto &wallet = weak->wallet();
		return (wallet._phraseRevealing || wallet._replacing
				|| wallet._backupChanging || wallet._rotating)
			&& (scope
				? wallet.commentScopeCurrent(scope)
				: (wallet._presence.current() == Presence::Ready
					&& wallet._publicKey == expected))
			&& wallet.vault().reading().state == KeyringReading::State::Read
			&& wallet.vaultKeyUnusable();
	};
	if (!current()) {
		done(CustodyResetResult::Refused);
		return;
	}
	resetDeviceCustody(scope, current, std::move(done));
}

void Session::resetDeviceCustody(
		const std::shared_ptr<CommentScope> &scope,
		Fn<bool()> current,
		Fn<void(CustodyResetResult)> done) {
	struct State {
		std::vector<base::weak_ptr<Main::Session>> sessions;
		int pending = 0;
	};
	const auto state = std::make_shared<State>();
	const auto domain = base::make_weak(&_session->domain());
	const auto runtime = vault().shared_from_this();
	for (const auto &[index, account] : domain->accounts()) {
		if (const auto session = account->maybeSession()) {
			const auto &wallet = session->wallet();
			if (wallet._custodyResetting || wallet._clientStopping
				|| (&wallet != this && wallet.custodyBusy())) {
				done(CustodyResetResult::Refused);
				return;
			}
			state->sessions.push_back(base::make_weak(session));
		}
	}
	state->pending = int(state->sessions.size());
	for (const auto &weak : state->sessions) {
		weak->wallet()._custodyResetting = true;
	}
	runtime->clear();
	const auto epoch = runtime->clearEpoch();
	if (scope) {
		scope->_state->epoch = epoch;
	}
	const auto finish = [=] {
		if (--state->pending) {
			return;
		}
		auto unchanged = bool(domain);
		auto signedIn = 0;
		if (domain) {
			for (const auto &[index, account] : domain->accounts()) {
				if (const auto session = account->maybeSession()) {
					++signedIn;
					unchanged = unchanged && !session->wallet()._engine->client()
						&& ranges::any_of(state->sessions, [=](const auto &weak) {
							return weak.get() == session;
						});
				}
			}
		}
		auto result = CustodyResetResult::Refused;
		if (unchanged && signedIn == int(state->sessions.size())
			&& runtime->clearEpoch() == epoch && current()) {
			for (const auto &weak : state->sessions) {
				if (weak) {
					auto &wallet = weak->wallet();
					wallet._custody = CustodyStore();
					wallet._custodyReadFailed = false;
					wallet._unreadableRecordId = QString();
					wallet._preparedRotation.reset();
					wallet.retireSubmission();
					wallet._pending.reset();
					++wallet._sendRevision;
				}
			}
			if (scope) {
				scope->_state->record.reset();
			}
			result = ResetVaultAndCustody(*domain)
				? CustodyResetResult::Done
				: CustodyResetResult::Failed;
			runtime->setUnusable(false);
		}
		for (const auto &weak : state->sessions) {
			if (weak) {
				weak->wallet()._custodyResetting = false;
			}
		}
		if (result != CustodyResetResult::Refused) {
			for (const auto &weak : state->sessions) {
				if (weak) {
					weak->wallet()._sendState = SendState::Idle;
				}
			}
			runtime->notifyProtectionChanged();
		} else {
			for (const auto &weak : state->sessions) {
				if (!weak) {
					continue;
				}
				auto &wallet = weak->wallet();
				const auto interrupted = wallet._submission
					|| (wallet._sendState.current() != SendState::Idle);
				if (interrupted) {
					// WHY: the stopped client can't answer its send, so retire it
					// here; a broadcast that may have landed stays unresolved.
					const auto &submission = wallet._submission;
					if (submission && submission->rpcStarted) {
						wallet._sendUnresolved = true;
						wallet._unresolvedOperationId = submission->operationId;
					}
					wallet.retireSubmission();
					wallet._pending.reset();
					++wallet._sendRevision;
				}
				wallet.syncEngineClient();
				if (interrupted && weak) {
					auto &restarted = weak->wallet();
					const auto entry = restarted._sendUnresolved
						? restarted.submittedTransfer(
							restarted._unresolvedOperationId)
						: nullptr;
					const auto record = entry
						? restarted.submittedTransferRecord(
							entry->operationId,
							entry->identity)
						: nullptr;
					if (record
						&& record->recordId == restarted._clientRecordId) {
						entry->client = restarted.signingClient();
					}
					restarted._sendState = SendState::Idle;
				}
			}
		}
		done(result);
	};
	for (const auto &weak : state->sessions) {
		if (!weak) {
			finish();
			continue;
		}
		auto &wallet = weak->wallet();
		wallet.retireCommentScopes((&wallet == this) ? scope : nullptr);
		if (weak) {
			weak->wallet().stopEngineClientForReset(finish);
		} else {
			finish();
		}
	}
}

CustodyInstallRequest Session::resettableInstallRequest(
		QByteArray expected,
		std::shared_ptr<CommentScope> scope,
		std::shared_ptr<bool> crossed,
		Fn<void(CustodyInstall)> proceed) {
	return {
		.ready = crl::guard(_session, std::move(proceed)),
		.passcodeCreated = [scope, weak = base::make_weak(_session)](
				quint32 previousEpoch,
				quint32 epoch) {
			if (!weak || weak->account().maybeSession() != weak.get()
				|| epoch != quint32(previousEpoch + 1)
				|| weak->wallet().vault().clearEpoch() != epoch) {
				return false;
			}
			// The handoff is what the scope needs, not what the install
			// needs: an attempt that already lapsed loses only its reveal,
			// so a scope that cannot be restamped is left to die and the
			// key still lands under the protection the user just chose.
			if (scope
				&& !scope->cancelled()
				&& scope->_state->epoch == previousEpoch) {
				scope->_state->epoch = epoch;
			}
			return true;
		},
		.resetUnusableVault = [=, weak = base::make_weak(_session)](
				std::optional<quint32> createdFromEpoch,
				Fn<void(CustodyResetResult)> done) {
			if (!weak) {
				done(CustodyResetResult::Refused);
				return;
			}
			if (scope && !scope->cancelled() && createdFromEpoch
				&& scope->_state->epoch == *createdFromEpoch) {
				scope->_state->epoch = weak->wallet().vault().clearEpoch();
			}
			weak->wallet().resetUnusableVault(expected, scope, [=](
					CustodyResetResult result) {
				if (result != CustodyResetResult::Refused) {
					*crossed = true;
				}
				done(result);
			});
		},
	};
}

void Session::settleVaultReset(
		const std::shared_ptr<bool> &crossed,
		bool installed) {
	const auto runtime = vault().shared_from_this();
	if (base::take(*crossed)) {
		if (!installed) {
			updateDeviceCustodyState();
		}
		runtime->notifyProtectionChanged(runtime->unusable());
	}
	DropUnusedPasscode();
}

const CustodyStore &Session::custody() {
	if (!_custody) {
		_custody = ReadCustodyStore(_session->local());
		_custodyReadFailed = !_custody.has_value();
		if (!_custody) {
			LOG(("Wallet Error: custody store unreadable, treating as empty."));
			_custody = CustodyStore();
		} else {
			DropPreVaultCustody(_session->local(), *_custody);
		}
	}
	return *_custody;
}

const CustodyRecord *Session::currentRecord() {
	return custody().current(_address, _publicKey);
}

bool Session::persistCustody(const CustodyRecord &record) {
	auto store = custody();
	auto superseded = std::vector<CustodyRecord>();
	for (const auto &existing : store.records) {
		if (existing.publicKey == record.publicKey
			&& existing.secretRef != record.secretRef) {
			superseded.push_back(existing);
		}
	}
	store.records.erase(
		ranges::remove(
			store.records,
			record.publicKey,
			&CustodyRecord::publicKey),
		end(store.records));
	for (auto &existing : store.records) {
		existing.active = false;
	}
	store.records.push_back(record);
	store.records.back().active = true;
	if (!WriteCustodyStore(_session->local(), store)) {
		LOG(("Wallet Error: custody record write failed."));
		return false;
	}
	_custodyReadFailed = false;
	_custody = std::move(store);
	validateUnreadableRecord();
	// The superseded secrets go only after the write landed, so a failed
	// write leaves the old record and its secret exactly as before. The
	// pending rotation is not a record and its secretRef never equals a
	// record's, so it is never in this set; the new record's own secret is
	// kept out by the secretRef comparison.
	const auto lifecycle = _engine->lifecycle();
	// WHY: callers install a record signing with the served key, and never
	// while a record of its anchor awaits the server key, so what this
	// supersedes is the same phrase or an obsolete one, never the chain's key.
	for (const auto &each : superseded) {
		_engine->run([lifecycle, descriptor = DescriptorFromRecord(each)] {
			lifecycle->delete_wallet(descriptor);
		}, [] {}, [](EngineError) {
			LOG(("Wallet Error: delete_wallet of a superseded record "
				"failed."));
		});
	}
	updateDeviceCustodyState();
	return true;
}

void Session::replaceWithNew(
		std::optional<Core::CloudPasswordResult> password,
		Fn<void(CustodyOutcome)> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
	fail = LoggedFail(u"wallet replace"_q, std::move(fail));
	// A replace and a reveal must never overlap: a shares-restore that
	// finished after a replace landed would write an active custody record
	// for the replaced key and show stale words. Both flows write the same
	// custody store, so each one's busy check refuses the other.
	if (custodyBusy()) {
		LOG(("Wallet Error: replace requested while another is in flight."));
		if (fail) {
			fail(u"REPLACE_BUSY"_q);
		}
		return;
	}
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize) {
		LOG(("Wallet Error: replace requested without a settled wallet key."));
		if (fail) {
			fail(u"REPLACE_STATE_UNKNOWN"_q);
		}
		return;
	}
	retireCommentScopes();
	_replacing = true;
	done = [this, done = std::move(done)](CustodyOutcome outcome) {
		_replacing = false;
		if (done) {
			done(outcome);
		}
	};
	fail = [this, fail = std::move(fail)](const QString &error) {
		_replacing = false;
		if (fail) {
			fail(error);
		}
	};
	const auto oldAddress = _address;
	sendReplaceWallet(
		MTP_inputWalletNew(),
		std::move(password),
		[=, this](const MTPWalletState &state) {
			finishConfirmedReplace(oldAddress, std::nullopt, state, done, fail);
		},
		[=](const MTP::Error &error) {
			fail(error.type());
		});
}

void Session::replaceWithImported(
		KeyAuthorization auth,
		std::vector<QString> words,
		std::optional<Core::CloudPasswordResult> password,
		Fn<void(CustodyOutcome)> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
	fail = LoggedFail(u"wallet replace"_q, std::move(fail));
	if (custodyBusy()) {
		LOG(("Wallet Error: replace requested while another is in flight."));
		if (fail) {
			fail(u"REPLACE_BUSY"_q);
		}
		return;
	}
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize) {
		LOG(("Wallet Error: replace requested without a settled wallet key."));
		if (fail) {
			fail(u"REPLACE_STATE_UNKNOWN"_q);
		}
		return;
	}
	const auto match = DetectPhraseMatch(words);
	if (match != PhraseMatch::Rotation) {
		if (fail) {
			fail((match == PhraseMatch::Foreign)
				? u"REPLACE_FOREIGN_PHRASE"_q
				: u"REPLACE_INVALID_PHRASE"_q);
		}
		return;
	}
	retireCommentScopes();
	_replacing = true;
	const auto expected = _publicKey;
	const auto crossed = std::make_shared<bool>(false);
	const auto weakSession = base::make_weak(_session);
	done = [weakSession, crossed, done = std::move(done)](CustodyOutcome outcome) {
		if (weakSession) {
			weakSession->wallet()._replacing = false;
			weakSession->wallet().settleVaultReset(
				crossed,
				outcome == CustodyOutcome::Installed);
			if (weakSession && done) {
				done(outcome);
			}
		}
	};
	fail = [weakSession, crossed, fail = std::move(fail)](
			const QString &error) {
		if (weakSession) {
			weakSession->wallet()._replacing = false;
			weakSession->wallet().settleVaultReset(crossed, false);
			if (weakSession && fail) {
				fail(error);
			}
		}
	};
	const auto oldAddress = _address;
	const auto lifecycle = _engine->lifecycle();
	const auto keyChangeRefusal = [=, this](const PhraseIdentity &identity) {
		return AwaitingKeyRefusal(
			custody(),
			identity,
			u"REPLACE_OUTDATED_PHRASE"_q,
			u"REPLACE_KEY_CHANGING"_q);
	};
	// The store authority is resolved the way restoreFromWords resolves it,
	// and for the same reason the install ladder runs first: a cancelled
	// chooser has to abort before wallet.replaceWallet is sent, so nothing
	// on the server can name a key this device never stored.
	const auto store = [=, this](
			PhraseIdentity identity,
			CustodyInstall install,
			std::vector<QString> phrase) {
		const auto refusal = keyChangeRefusal(identity);
		if (!refusal.isEmpty()) {
			fail(refusal);
			return;
		}
		auto recoveryWords = std::vector<std::string>();
		recoveryWords.reserve(phrase.size());
		for (const auto &word : phrase) {
			recoveryWords.push_back(word.toStdString());
		}
		auto request = engine::ImportWalletRequest{
			.record_id = NewRecordId(),
			.network = engine::Network::kMainnet,
			.recovery_words = std::move(recoveryWords),
		};
		const auto stores = std::make_shared<EngineSecretStores>();
		_engine->run([
			lifecycle,
			request = std::move(request),
			stores
		]() mutable {
			const auto recording = stores->record();
			return lifecycle->import_wallet(request);
		}, [=, this](engine::WalletDescriptor descriptor) {
			auto record = RecordFromDescriptor(descriptor);
			record.signingKey = identity.signing;
			const auto address = CanonicalAddress(record.address);
			const auto abandon = [=, this](const QString &error) {
				const auto cleanup = [=, this] {
					_engine->dropStoredSecrets(*stores);
					fail(error);
				};
				_engine->run([lifecycle, descriptor] {
					lifecycle->delete_wallet(descriptor);
				}, cleanup, [=](EngineError) {
					LOG(("Wallet Error: delete_wallet after an abandoned "
						"import failed."));
					cleanup();
				});
			};
			const auto applied = [=, this](const MTPWalletState &state) {
				const auto answered = (state.type() == mtpc_walletState)
					? ParseAddress(qs(state.c_walletState().vaddress()))
					: std::optional<ParsedAddress>();
				if (!answered || answered->raw != address) {
					LOG(("Wallet Error: wallet.replaceWallet answered "
						"another address."));
					abandon(u"REPLACE_KEY_MISMATCH"_q);
					return;
				}
				const auto refusal = keyChangeRefusal(identity);
				if (!refusal.isEmpty()) {
					abandon(refusal);
					return;
				}
				finishConfirmedReplace(
					oldAddress,
					record,
					state,
					done,
					fail);
			};
			const auto send = [=, this](
					TimeId timestamp,
					const std::vector<uint8_t> &signature) {
				using Flag = MTPDinputWalletImported::Flag;
				const auto rotated = (record.publicKey != record.signingKey);
				const auto anchor = rotated ? record.publicKey : QByteArray();
				sendReplaceWallet(
					MTP_inputWalletImported(
						MTP_flags(rotated
							? Flag::f_anchor_public_key
							: Flag(0)),
						MTP_bytes(record.signingKey),
						MTP_bytes(anchor), // anchor_public_key
						MTP_walletOwnershipProof(
							MTP_int(timestamp),
							MTP_bytes(bytes::make_span(signature)))),
					password,
					applied,
					[=, this](const MTP::Error &error) {
						if (!MTP::IsTemporaryError(error)
							|| MTP::IsFloodError(error)) {
							abandon(error.type());
							return;
						}
						recoverImportedReplace(
							address,
							record.signingKey,
							applied,
							abandon);
					});
			};
			// The challenge lives 300 seconds and admits one attempt, so it
			// is fetched only here - after the install ladder answered and
			// the engine stored the words - and signed at once. A cancelled
			// chooser, a refused phrase or a failed import never reaches
			// this continuation and issues no challenge.
			requestOwnershipProof(
				descriptor,
				record.signingKey,
				install.grant,
				[=](OwnershipProof proof) {
					send(proof.timestamp, proof.signature);
				},
				[=](OwnershipProofError error) {
					abandon((error == OwnershipProofError::VaultLocked)
						? u"REPLACE_VAULT_LOCKED"_q
						: u"REPLACE_PROOF_FAILED"_q);
				});
		}, [=, this](EngineError error) {
			_engine->dropStoredSecrets(*stores);
			const auto name = LifecycleErrorName(error);
			LOG(("Wallet Error: import_wallet failed: %1").arg(name));
			fail(IsVaultLocked(error)
				? u"REPLACE_VAULT_LOCKED"_q
				: (name == u"InvalidRecoveryPhrase"_q)
				? u"REPLACE_INVALID_PHRASE"_q
				: u"REPLACE_IMPORT_FAILED"_q);
		});
	};
	const auto continueInstall = [=](
			PhraseIdentity identity,
			CustodyInstall answer,
			std::vector<QString> phrase) {
		if (!answer.grant) {
			fail(u"REPLACE_INSTALL_CANCELLED"_q);
		} else {
			store(identity, std::move(answer), std::move(phrase));
		}
	};
	// The phrase's keys are derived on the engine worker before the install
	// ladder opens anything, as restoreFromWords does, so an invalid phrase
	// reaches no chooser and no store; they are deliberately not compared
	// with the served key - importing another wallet's phrase is what
	// this replace is for. The address the server's wallet.replaceWallet
	// answer serves is what confirms the wallet it accepted; the key it
	// serves classifies the record through reconciliation.
	validatePhraseIdentity(words, [=, this](
			std::optional<PhraseIdentity> identity) mutable {
		if (!identity) {
			fail(u"REPLACE_INVALID_PHRASE"_q);
			return;
		}
		const auto refusal = keyChangeRefusal(*identity);
		if (!refusal.isEmpty()) {
			fail(refusal);
			return;
		}
		if (const auto install = auth.install) {
			install(resettableInstallRequest(
				expected,
				nullptr,
				crossed,
				[=, words = std::move(words)](CustodyInstall answer) mutable {
					continueInstall(
						*identity,
						std::move(answer),
						std::move(words));
				}));
		} else if (auth.grant && auth.grant->valid()) {
			store(
				*identity,
				CustodyInstall{ .grant = auth.grant },
				std::move(words));
		} else {
			fail(u"REPLACE_VAULT_LOCKED"_q);
		}
	});
}

void Session::requestOwnershipProof(
		engine::WalletDescriptor descriptor,
		QByteArray signingKey,
		VaultAuthorization grant,
		Fn<void(OwnershipProof)> done,
		Fn<void(OwnershipProofError)> fail) {
	const auto lifecycle = _engine->lifecycle();
	// The challenge lives 300 seconds and admits one attempt, so each caller
	// asks for it only once nothing but the signature stands between it and
	// the send, and it is signed at once. A resend of this request mints a
	// new challenge server-side and only the final answer is used, so it
	// keeps the ordinary flood policy; the send that spends the proof does
	// not. The engine signs with the stored phrase's current signing key
	// and names that key back; a proof under any key but the one the
	// caller is about to send is dropped here, so a record whose signing
	// key disagrees with its phrase never reaches the server.
	_stateApi.request(MTPwallet_GetProofChallenge(
	)).done([=, this](const MTPwallet_ProofChallenge &result) {
		const auto &challenge = result.data();
		_tonConnectOwnershipDomain = qs(challenge.vdomain());
		const auto timestamp = base::unixtime::now();
		if (timestamp <= 0) {
			LOG(("Wallet Error: no usable timestamp for the ownership "
				"proof."));
			fail(OwnershipProofError::Failed);
			return;
		}
		auto request = engine::TonConnectProofSignRequest{
			.descriptor = descriptor,
			.domain = challenge.vdomain().v.toStdString(),
			.timestamp = uint64_t(timestamp),
			.payload = challenge.vpayload().v.toStdString(),
		};
		_engine->runLocal([lifecycle, request = std::move(request)] {
			return lifecycle->sign_ton_connect_proof(request);
		}, [=, grant = grant](engine::TonConnectProofSignature proof) {
			const auto size = int(proof.signature.size());
			if (size != kOwnershipProofSignatureSize) {
				LOG(("Wallet Error: the ownership proof signature has "
					"%1 bytes.").arg(size));
				fail(OwnershipProofError::Failed);
				return;
			}
			const auto signer = QByteArray(
				reinterpret_cast<const char*>(proof.public_key.data()),
				proof.public_key.size());
			if (signer != signingKey) {
				LOG(("Wallet Error: the ownership proof was signed "
					"with another key."));
				fail(OwnershipProofError::Failed);
				return;
			}
			done(OwnershipProof{
				.timestamp = timestamp,
				.signature = std::move(proof.signature),
			});
		}, [=, this, grant = grant](EngineError error) {
			LOG(("Wallet Error: ownership proof signing failed: %1"
				).arg(LifecycleErrorName(error)));
			noteSecretReadFailure(
				ProtectedSecretFailure(error),
				QString::fromStdString(descriptor.record_id));
			fail(IsVaultLocked(error)
				? OwnershipProofError::VaultLocked
				: OwnershipProofError::Failed);
		});
	}).fail([=](const MTP::Error &error) {
		LOG(("Wallet Error: wallet.getProofChallenge failed: %1"
			).arg(error.type()));
		fail(OwnershipProofError::Failed);
	}).handleFloodErrors().send();
}

TonConnectAccess Session::tonConnectAccess() {
	ensureLoaded();
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize) {
		return TonConnectAccess::WalletNotReady;
	} else if (_rotating
		|| custody().pendingRotation
		|| custody().anyAwaitingServerKey()) {
		return TonConnectAccess::KeyChanging;
	}
	const auto record = vaultKeyUnusable() ? nullptr : currentRecord();
	if (!record || secretUnreadable(record->recordId)) {
		return TonConnectAccess::NoCurrentKey;
	}
	const auto &signingKey = record->signingKey.isEmpty()
		? record->publicKey
		: record->signingKey;
	if (signingKey != _publicKey) {
		return TonConnectAccess::KeyChanging;
	} else if (custodyBusy()) {
		return TonConnectAccess::Busy;
	}
	return TonConnectAccess::Allowed;
}

} // namespace Wallet
