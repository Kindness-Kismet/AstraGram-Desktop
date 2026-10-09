#include "wallet/wallet_session_internal.h"

namespace Wallet {
using namespace SessionDetails;

void Session::revealPhrase(
		KeyAuthorization auth,
		std::optional<Core::CloudPasswordResult> password,
		Fn<void(std::vector<QString>, CustodyOutcome outcome)> done,
		Fn<void(const QString &error)> fail,
		Fn<void()> authorized) {
	ensureLoaded();
	fail = loggedPhraseFail(u"phrase reveal"_q, std::move(fail));
	if (custodyBusy() || custody().pendingRotation) {
		LOG(("Wallet Error: reveal requested while another is in flight."));
		if (fail) {
			fail(u"PHRASE_BUSY"_q);
		}
		return;
	}
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize) {
		LOG(("Wallet Error: reveal requested without a settled wallet key."));
		if (fail) {
			fail(u"PHRASE_STATE_UNKNOWN"_q);
		}
		return;
	}
	retireCommentScopes();
	_phraseRevealing = true;
	// Every path below ends in exactly one of these two calls, which is
	// what clears the guard, so none of them is fenced by _networkGeneration:
	// a reveal owns no network-derived state, and dropping its callback
	// would either orphan a just-stored engine secret or leave the guard
	// set for the rest of the session.
	done = [this, done = std::move(done)](
			std::vector<QString> words,
			CustodyOutcome outcome) {
		_phraseRevealing = false;
		if (done) {
			done(std::move(words), outcome);
		}
	};
	fail = [this, fail = std::move(fail)](const QString &error) {
		_phraseRevealing = false;
		if (fail) {
			fail(error);
		}
	};
	const auto held = vaultKeyUnusable() ? nullptr : currentRecord();
	const auto record = (held && secretUnreadable(held->recordId))
		? nullptr
		: held;
	if (record) {
		if (!ReadAuthorized(*this, auth)) {
			fail(u"PHRASE_VAULT_LOCKED"_q);
			return;
		}
		revealLocally(std::move(auth), *record, [=](
				std::vector<QString> words) {
			done(std::move(words), CustodyOutcome::Installed);
		}, fail);
	} else {
		revealFromShares(
			std::move(auth),
			std::move(password),
			done,
			fail,
			nullptr,
			std::move(authorized));
	}
}

void Session::revealLocally(
		KeyAuthorization auth,
		const CustodyRecord &record,
		Fn<void(std::vector<QString>)> done,
		Fn<void(const QString &)> fail) {
	if (!ReadAuthorized(*this, auth)) {
		fail(u"PHRASE_VAULT_LOCKED"_q);
		return;
	}
	const auto initiatingRecordId = record.recordId;
	LOG(("Wallet Info: local phrase read address=%1 anchor=%2 signing=%3 "
		"network=%4 active=%5."
		).arg(record.address
		).arg(LogKey(record.publicKey)
		).arg(LogKey(record.signingKey)
		).arg(record.network
		).arg(record.active));
	const auto lifecycle = _engine->lifecycle();
	const auto descriptor = DescriptorFromRecord(record);
	_engine->runLocal([lifecycle, descriptor] {
		return lifecycle->reveal_recovery_phrase(descriptor);
	}, [=, grant = auth.grant](engine::RecoveryPhrase phrase) {
		auto words = SplitWords(QString::fromStdString(phrase.phrase));
		if (words.size() < 2) {
			LOG(("Wallet Error: local phrase reveal too short word_count=%1."
				).arg(words.size()));
			fail(u"PHRASE_EMPTY"_q);
			return;
		}
		done(std::move(words));
	}, [=, this, grant = auth.grant](EngineError error) {
		noteSecretReadFailure(
			ProtectedSecretFailure(error),
			initiatingRecordId);
		LOG(("Wallet Error: local phrase reveal failed: %1"
			).arg(LifecycleErrorDetails(error)));
		fail(IsVaultLocked(error)
			? u"PHRASE_VAULT_LOCKED"_q
			: u"PHRASE_LOCAL_FAILED"_q);
	});
}

void Session::revealFromShares(
		KeyAuthorization auth,
		std::optional<Core::CloudPasswordResult> password,
		Fn<void(std::vector<QString>, CustodyOutcome outcome)> done,
		Fn<void(const QString &)> fail,
		std::shared_ptr<CommentScope> scope,
		Fn<void()> authorized) {
	if (scope && !commentScopeCurrent(scope)) {
		fail(u"PHRASE_ORIGIN_EXPIRED"_q);
		return;
	}
	if (scope && !_capabilities.current().canExportPhrase) {
		fail(u"PHRASE_STATE_UNKNOWN"_q);
		return;
	}
	// Sent without a password first even when the account has one: the
	// server decides whether this export needs it, and answers
	// PASSWORD_MISSING when it does, which the caller turns into a repeat
	// that carries the password.
	using Flag = MTPwallet_exportSecretPhrase::Flag;
	const auto checked = password && *password;
	const auto revision = _walletIdentityRevision;
	const auto pending = std::make_shared<bool>(true);
	const auto request = _stateApi.request(MTPwallet_ExportSecretPhrase(
		MTP_flags(checked ? Flag::f_password : Flag(0)),
		checked ? password->result : MTP_inputCheckPasswordEmpty()
	)).done([=, this](
			const MTPwallet_SecretPhraseParts &result,
			mtpRequestId requestId) {
		if (!base::take(*pending)) {
			return;
		}
		if (scope) {
			scope->_state->cancelPending = nullptr;
		}
		if (scope && !commentScopeCurrent(scope)) {
			fail(u"PHRASE_ORIGIN_EXPIRED"_q);
			return;
		}
		const auto &data = result.data();
		const auto dcs = ParseHolderDcs(data);
		if (!dcs) {
			LOG(("Wallet Error: wallet.exportSecretPhrase invalid holders "
				"request=%1 count=%2."
				).arg(requestId).arg(data.vdcs().v.size()));
			fail(u"PHRASE_PARTS_INVALID"_q);
			return;
		}
		if (authorized) {
			authorized();
		}
		LOG(("Wallet Info: wallet.exportSecretPhrase request=%1 named "
			"%2 holder(s); requested_revision=%3 current_revision=%4."
			).arg(requestId
			).arg(int(dcs->size())
			).arg(revision
			).arg(_walletIdentityRevision));
		fetchShareParts(
			auth,
			qs(data.vtoken()),
			*dcs,
			done,
			fail,
			scope,
			requestId);
	}).fail([=, this](const MTP::Error &error, mtpRequestId requestId) {
		if (!base::take(*pending)) {
			return;
		}
		if (scope) {
			scope->_state->cancelPending = nullptr;
		}
		if (scope && !commentScopeCurrent(scope)) {
			fail(u"PHRASE_ORIGIN_EXPIRED"_q);
			return;
		}
		LOG(("Wallet Error: wallet.exportSecretPhrase failed: %1 "
			"code=%2 request=%3 password_supplied=%4 "
			"requested_revision=%5 current_revision=%6."
			).arg(error.type()
			).arg(error.code()
			).arg(requestId
			).arg(checked
			).arg(revision
			).arg(_walletIdentityRevision));
		fail((scope && MTP::IgnoreError(error))
			? u"PHRASE_SILENT_ERROR"_q
			: error.type());
	}).handleFloodErrors().send();
	LOG(("Wallet Info: wallet.exportSecretPhrase sent request=%1 "
		"address=%2 key=%3 revision=%4 state_age_ms=%5."
		).arg(request
		).arg(_address
		).arg(LogKey(_publicKey)
		).arg(revision
		).arg(_stateRefreshedAt ? (crl::now() - _stateRefreshedAt) : -1));
	if (scope) {
		scope->_state->cancelPending = crl::guard(_engine.get(), [=, this] {
			if (!base::take(*pending)) {
				return;
			}
			_stateApi.request(request).cancel();
			fail(u"PHRASE_ORIGIN_EXPIRED"_q);
		});
	}
}

void Session::fetchShareParts(
		KeyAuthorization auth,
		const QString &token,
		std::vector<int> dcs,
		Fn<void(std::vector<QString>, CustodyOutcome outcome)> done,
		Fn<void(const QString &)> fail,
		std::shared_ptr<CommentScope> scope,
		mtpRequestId exportRequestId) {
	if (scope && !commentScopeCurrent(scope)) {
		fail(u"PHRASE_ORIGIN_EXPIRED"_q);
		return;
	}
	auto keys = TdE2E::TemporaryKeyPair::Generate();
	if (!keys) {
		LOG(("Wallet Error: could not generate an ephemeral key "
			"export_request=%1 holders=%2."
			).arg(exportRequestId).arg(dcs.size()));
		fail(u"PHRASE_PARTS_INVALID"_q);
		return;
	}
	const auto count = int(dcs.size());
	const auto state = std::make_shared<ShareFetch>(ShareFetch{
		.keys = std::move(*keys),
		.shares = std::vector<QByteArray>(count),
		.requests = std::vector<mtpRequestId>(count),
		.sessions = std::vector<MTP::ShiftedDcId>(count),
		.dcs = dcs,
		.fail = [=](const QString &error) {
			if (scope) {
				scope->_state->cancelPending = nullptr;
			}
			fail(error);
		},
		.exportRequestId = exportRequestId,
		.startedAt = crl::now(),
		.pending = count,
	});
	const auto publicKey = state->keys.publicKey();
	for (auto i = 0; i != count; ++i) {
		state->sessions[i] = MTP::ShiftDcId(dcs[i], MTP::kWalletShareDcShift);
		state->requests[i] = _stateApi.request(
			MTPwallet_FetchEncryptedSecretPhrasePart(
				MTP_string(token),
				MTP_bytes(publicKey))
		).done([=, this](
				const MTPwallet_EncryptedSecretPhrasePart &result,
				mtpRequestId requestId) {
			if (!state->fail) {
				return;
			}
			state->requests[i] = 0;
			LOG(("Wallet Info: share fetch response export_request=%1 "
				"request=%2 index=%3 dc=%4 encrypted_bytes=%5 elapsed_ms=%6."
				).arg(exportRequestId
				).arg(requestId
				).arg(i
				).arg(dcs[i]
				).arg(result.data().vdata().v.size()
				).arg(crl::now() - state->startedAt));
			if (scope && !commentScopeCurrent(scope)) {
				FailShareFetch(
					_stateApi,
					_shareFetchTimer,
					state,
					u"PHRASE_ORIGIN_EXPIRED"_q);
				return;
			}
			if (!OpenSharePart(state, i, result.data().vdata().v)) {
				LOG(("Wallet Error: share part %1 of %2 did not open."
					).arg(i + 1).arg(count));
				FailShareFetch(
					_stateApi,
					_shareFetchTimer,
					state,
					u"PHRASE_PART_INVALID"_q);
				return;
			} else if (--state->pending) {
				return;
			}
			FinishShareFetch(_stateApi, _shareFetchTimer, state);
			const auto seed = PhraseShares::CombineShares(state->shares);
			if (!seed) {
				LOG(("Wallet Error: %1 share parts do not combine."
					).arg(state->shares.size()));
				FailShareFetch(
					_stateApi,
					_shareFetchTimer,
					state,
					u"PHRASE_PART_INVALID"_q);
				return;
			}
			if (scope) {
				scope->_state->cancelPending = nullptr;
			}
			restoreFromWords(
				auth,
				SplitWords(QString::fromUtf8(*seed)),
				done,
				base::take(state->fail),
				scope,
				exportRequestId);
		}).fail([=, this](const MTP::Error &error) {
			if (!state->fail) {
				return;
			}
			LOG(("Wallet Error: wallet.fetchEncryptedSecretPhrasePart "
				"failed: %1 code=%2 export_request=%3 request=%4 "
				"index=%5 dc=%6."
				).arg(error.type()
				).arg(error.code()
				).arg(exportRequestId
				).arg(state->requests[i]
				).arg(i
				).arg(dcs[i]));
			state->requests[i] = 0;
			FailShareFetch(
				_stateApi,
				_shareFetchTimer,
				state,
				(scope && !commentScopeCurrent(scope))
					? u"PHRASE_ORIGIN_EXPIRED"_q
					: (scope && MTP::IgnoreError(error))
					? u"PHRASE_SILENT_ERROR"_q
					: error.type());
		}).handleFloodErrors().toDC(state->sessions[i]).send();
		LOG(("Wallet Info: share fetch sent export_request=%1 request=%2 "
			"index=%3 dc=%4 total=%5."
			).arg(exportRequestId
			).arg(state->requests[i]
			).arg(i
			).arg(dcs[i]
			).arg(count));
	}
	if (scope) {
		scope->_state->cancelPending = crl::guard(_engine.get(), [=, this] {
			if (state->fail) {
				FailShareFetch(
					_stateApi,
					_shareFetchTimer,
					state,
					u"PHRASE_ORIGIN_EXPIRED"_q);
			}
		});
	}
	_shareFetch = state;
	_shareFetchTimer.setCallback([this] {
		const auto state = _shareFetch.lock();
		if (!state || !state->fail) {
			return;
		}
		LOG(("Wallet Error: share fetch timed out with %1 part(s) pending."
			).arg(state->pending));
		FailShareFetch(_stateApi, _shareFetchTimer, state, u"PHRASE_TIMEOUT"_q);
	});
	_shareFetchTimer.callOnce(kShareFetchTimeout);
}

void Session::validatePhraseIdentity(
		const std::vector<QString> &words,
		Fn<void(std::optional<PhraseIdentity>)> done) {
	auto normalized = QStringList();
	for (const auto &word : words) {
		normalized.push_back(NormalizeWord(word));
	}
	_engine->runLocal([normalized] {
		return DerivePhraseIdentity(normalized);
	}, done, [done, count = words.size()](EngineError error) {
		LOG(("Wallet Error: phrase validation worker failed word_count=%1: %2"
			).arg(count).arg(LifecycleErrorDetails(error)));
		done(std::nullopt);
	});
}

void Session::restoreFromWords(
		KeyAuthorization auth,
		std::vector<QString> words,
		Fn<void(std::vector<QString>, CustodyOutcome outcome)> done,
		Fn<void(const QString &)> fail,
		std::shared_ptr<CommentScope> scope,
		mtpRequestId exportRequestId) {
	if (scope && !commentScopeCurrent(scope)) {
		fail(u"PHRASE_ORIGIN_EXPIRED"_q);
		return;
	} else if (words.size() < 2) {
		LOG(("Wallet Error: reconstructed phrase too short "
			"word_count=%1 export_request=%2."
			).arg(words.size()).arg(exportRequestId));
		fail(u"PHRASE_EMPTY"_q);
		return;
	}
	// The wallet this restore is for, named once here. A scope carries it
	// from the transaction it was opened over; every other flow takes the
	// served one, and both are held to it for the rest of the ladder, so a
	// wallet replaced from another device while a prompt is open cannot end
	// with the old phrase parked on this one.
	const auto targetIdentity = scope
		? scope->_state->target.walletIdentity
		: transferWalletIdentity();
	if (!targetIdentity) {
		LOG(("Wallet Error: restore requested with no served wallet."));
		fail(u"PHRASE_STATE_UNKNOWN"_q);
		return;
	}
	const auto lifecycle = _engine->lifecycle();
	const auto expectedKey = targetIdentity->publicKey;
	const auto targetAddress = targetIdentity->address;
	const auto heldNow = custody().forAddress(targetAddress);
	LOG(("Wallet Info: restoring %1 word(s) for %2; served key %3, "
		"held anchor %4, held signing key %5; export_request=%6 "
		"revision=%7 state_age_ms=%8."
		).arg(int(words.size())
		).arg(targetAddress
		).arg(LogKey(expectedKey)
		).arg(LogKey(heldNow ? heldNow->publicKey : QByteArray())
		).arg(LogKey(heldNow ? heldNow->signingKey : QByteArray())
		).arg(exportRequestId
		).arg(targetIdentity->revision
		).arg(_stateRefreshedAt ? (crl::now() - _stateRefreshedAt) : -1));
	const auto crossed = std::make_shared<bool>(false);
	const auto weakSession = base::make_weak(_session);
	done = [weakSession, crossed, done = std::move(done)](
			std::vector<QString> phrase,
			CustodyOutcome outcome) {
		if (weakSession) {
			weakSession->wallet().settleVaultReset(
				crossed,
				outcome == CustodyOutcome::Installed);
			if (weakSession) {
				done(std::move(phrase), outcome);
			}
		}
	};
	fail = [weakSession, crossed, fail = std::move(fail)](
			const QString &error) {
		if (weakSession) {
			weakSession->wallet().settleVaultReset(crossed, false);
			if (weakSession) {
				fail(error);
			}
		}
	};
	// WHY: a scope binds the comment attempt, never the install. Once the
	// protection was chosen the key belongs on this device, so from here on
	// only a served wallet that is no longer the target undoes the import -
	// that is the one case that would park a foreign record here. An attempt
	// that merely expired loses its reveal, and the next press finds the key
	// held instead of restoring it all over again.
	const auto targetServed = [=, this] {
		const auto current = transferWalletIdentityCurrent(*targetIdentity);
		if (!current) {
			LOG(("Wallet Error: phrase target expired export_request=%1 "
				"target_address=%2 target_key=%3 target_revision=%4; %5."
				).arg(exportRequestId
				).arg(targetAddress
				).arg(LogKey(expectedKey)
				).arg(targetIdentity->revision
				).arg(phraseDiagnosticState()));
		}
		return current;
	};
	const auto keyChangeRefusal = [=, this](const PhraseIdentity &identity) {
		return AwaitingKeyRefusal(
			custody(),
			identity,
			u"PHRASE_OUTDATED"_q,
			u"PHRASE_KEY_CHANGING"_q);
	};
	// The resolved install travels into both continuations, which is what
	// holds the grant across the worker call: the runtime cleanses the key
	// as soon as the last handle goes, and the store runs on the worker.
	const auto store = [=, this](
			PhraseIdentity identity,
			CustodyInstall install,
			std::vector<QString> phrase) {
		// A scope still carrying a record is the no-record invariant the
		// confirmed reset re-establishes before an install - except when the
		// record is exactly the one whose secret could not be read, which no
		// reset touches: that record is what this restore replaces, and
		// persistCustody() writes over its anchor rather than beside it.
		if (!targetServed()
			|| (scope
				&& scope->_state->record
				&& !secretUnreadable(scope->_state->record->recordId))) {
			LOG(("Wallet Error: phrase install eligibility changed "
				"export_request=%1 scope_has_record=%2."
				).arg(exportRequestId
				).arg(scope && scope->_state->record.has_value()));
			fail(u"PHRASE_ORIGIN_EXPIRED"_q);
			return;
		} else if (!install.grant || !install.grant->valid()) {
			fail(u"PHRASE_VAULT_LOCKED"_q);
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
			stores,
			grant = install.grant,
			words = std::move(phrase)
		]() mutable -> std::optional<Restored> {
			// The grant is the vault term: a clear wipes every grant, so a
			// grant that is still valid is a vault no clear intervened on.
			// The scope's epoch is the comment attempt's term and says
			// nothing about whether this key may be stored.
			if (!grant->valid()) {
				return std::nullopt;
			}
			const auto recording = stores->record();
			auto descriptor = lifecycle->import_wallet(request);
			return Restored{ std::move(descriptor), std::move(words) };
		}, [=, this](std::optional<Restored> result) {
			if (!result) {
				LOG(("Wallet Error: phrase import grant expired before worker "
					"export_request=%1.").arg(exportRequestId));
				fail(u"PHRASE_ORIGIN_EXPIRED"_q);
				return;
			}
			if (scope && !scope->cancelled()
				&& install.grant && install.grant->valid()) {
				scope->_state->epoch = vault().clearEpoch();
			}
			auto restored = std::move(*result);
			const auto descriptor = restored.descriptor;
			auto record = RecordFromDescriptor(descriptor);
			const auto rollback = [=, this](Fn<void()> finished) {
				const auto cleanup = [=, this] {
					_engine->dropStoredSecrets(*stores);
					finished();
				};
				_engine->run([lifecycle, descriptor] {
					lifecycle->delete_wallet(descriptor);
				}, cleanup, [=](EngineError error) {
					LOG(("Wallet Error: phrase import rollback failed "
						"export_request=%1: %2"
						).arg(exportRequestId).arg(LifecycleErrorDetails(error)));
					cleanup();
				});
			};
			if (record.network != int(engine::Network::kMainnet)
				|| CanonicalAddress(record.address) != targetAddress) {
				LOG(("Wallet Error: the import made %1 on network %2, "
					"not %3."
					).arg(CanonicalAddress(record.address)
					).arg(record.network
					).arg(targetAddress));
				rollback([=] { fail(u"PHRASE_OTHER_WALLET"_q); });
				return;
			} else if (!targetServed()) {
				rollback([=] { fail(u"PHRASE_ORIGIN_EXPIRED"_q); });
				return;
			}
			const auto refusal = keyChangeRefusal(identity);
			if (!refusal.isEmpty()) {
				rollback([=] { fail(refusal); });
				return;
			}
			record.signingKey = identity.signing;
			if (scope) {
				record.active = true;
				scope->_state->record = record;
			}
			// The write swaps the signing client from inside, and that swap
			// retires every scope the outgoing record served. This one is
			// what the swap was for, so it is named across the write and
			// nowhere else: every other retire still cancels everything.
			_installingScope = scope;
			const auto persisted = persistCustody(record);
			_installingScope = nullptr;
			if (!persisted) {
				LOG(("Wallet Error: phrase custody write failed "
					"export_request=%1 address=%2 anchor=%3 signing=%4."
					).arg(exportRequestId
					).arg(record.address
					).arg(LogKey(record.publicKey)
					).arg(LogKey(record.signingKey)));
				if (scope) {
					scope->_state->record = std::nullopt;
				}
				rollback([=, words = std::move(restored.words)]() mutable {
					done(std::move(words), CustodyOutcome::WriteFailed);
				});
				return;
			} else if (!targetServed()) {
				const auto stored = custody().byAnchor(record.publicKey);
				if (stored && stored->recordId == record.recordId) {
					removeCustodyRecord(record.recordId);
				}
				rollback([=] { fail(u"PHRASE_ORIGIN_EXPIRED"_q); });
				return;
			}
			done(std::move(restored.words), CustodyOutcome::Installed);
		}, [=, this](EngineError error) {
			// Only stores recorded by this import are removed. The shared
			// ring may already protect other accounts, and its latest factor
			// stays committed even when this import failed after that write.
			// A new orphan entry is swept by the next necessary ring write.
			_engine->dropStoredSecrets(*stores);
			LOG(("Wallet Error: import_wallet failed export_request=%1 "
				"address=%2 anchor=%3 signing=%4: %5"
				).arg(exportRequestId
				).arg(targetAddress
				).arg(LogKey(identity.anchor)
				).arg(LogKey(identity.signing)
				).arg(LifecycleErrorDetails(error)));
			if (!targetServed()) {
				fail(u"PHRASE_ORIGIN_EXPIRED"_q);
				return;
			}
			const auto name = LifecycleErrorName(error);
			fail(IsVaultLocked(error)
				? u"PHRASE_VAULT_LOCKED"_q
				: (name == u"InvalidRecoveryPhrase"_q)
				? u"PHRASE_INVALID_PHRASE"_q
				: u"PHRASE_IMPORT_FAILED"_q);
		});
	};
	const auto continueInstall = [=, this](
			PhraseIdentity identity,
			CustodyInstall answer,
			std::vector<QString> phrase) {
		if (scope && !scope->cancelled()
			&& answer.passcodeCreatedFromEpoch
			&& scope->_state->epoch == *answer.passcodeCreatedFromEpoch
			&& answer.grant && answer.grant->valid()) {
			scope->_state->epoch = vault().clearEpoch();
		}
		if (!targetServed()) {
			fail(u"PHRASE_ORIGIN_EXPIRED"_q);
		} else if (!answer.grant) {
			done(std::move(phrase), CustodyOutcome::Cancelled);
		} else {
			store(identity, std::move(answer), std::move(phrase));
		}
	};
	// The phrase's keys are derived on the engine worker and compared before
	// the install ladder opens anything: the signing key with the served
	// key, and the anchor with the anchor of the record this device holds
	// of the target wallet, when it holds one. An invalid phrase, an
	// obsolete phrase of this wallet or one belonging to another wallet is
	// refused here while the keyring and the custody store are still
	// untouched, so no chooser, no store and no replacement of a vault this
	// process cannot open is ever reached by such a phrase. The anchor
	// check exists because each half of a 24-word phrase is checksummed on
	// its own: a phrase whose signing half is this wallet's and whose anchor
	// half is a valid phrase of another wallet derives another address, and
	// the signing check alone would let it cross the confirmed reset and
	// fail only at the import, with the vault it replaced already gone.
	const auto verified = [=, this](
			PhraseIdentity identity,
			std::vector<QString> words) {
		const auto held = custody().forAddress(targetAddress);
		LOG(("Wallet Info: the phrase derives anchor %1, signing key %2."
			).arg(LogKey(identity.anchor), LogKey(identity.signing)));
		if (held && held->publicKey != identity.anchor) {
			LOG(("Wallet Error: that anchor is not the held anchor %1."
				).arg(LogKey(held->publicKey)));
			fail(u"PHRASE_OTHER_WALLET"_q);
			return;
		}
		const auto refusal = keyChangeRefusal(identity);
		if (!refusal.isEmpty()) {
			fail(refusal);
			return;
		} else if (identity.signing != expectedKey) {
			LOG(("Wallet Error: that signing key is not the served key %1."
				).arg(LogKey(expectedKey)));
			fail(u"PHRASE_OUTDATED"_q);
			return;
		}
		// The installer resolves live policy immediately before storing.
		// It reuses this flow's valid secured grant or the shared retention
		// window, while Open and an empty ring still require a choice.
		// Preparing that choice does not write until the host's first store.
		if (const auto install = auth.install) {
			install(resettableInstallRequest(
				expectedKey,
				scope,
				crossed,
				[=, words = std::move(words)](CustodyInstall answer) mutable {
					continueInstall(
						identity,
						std::move(answer),
						std::move(words));
				}));
		} else if (auth.grant && auth.grant->valid()) {
			store(
				identity,
				CustodyInstall{ .grant = auth.grant },
				std::move(words));
		} else {
			fail(u"PHRASE_VAULT_LOCKED"_q);
		}
	};
	validatePhraseIdentity(words, [=, this](
			std::optional<PhraseIdentity> identity) mutable {
		if (!identity) {
			LOG(("Wallet Error: no identity derives from %1 word(s)."
				).arg(int(words.size())));
			fail(u"PHRASE_INVALID_PHRASE"_q);
			return;
		}
		_engine->runLocal([=, anchor = identity->anchor] {
			return AnchorDerivesOtherAddress(lifecycle, anchor, targetAddress);
		}, [=, words = std::move(words)](bool other) mutable {
			if (other) {
				LOG(("Wallet Error: anchor %1 derives an address "
					"other than %2."
					).arg(LogKey(identity->anchor), targetAddress));
				fail(u"PHRASE_OTHER_WALLET"_q);
			} else {
				verified(*identity, std::move(words));
			}
		}, [=](EngineError error) {
			LOG(("Wallet Error: the anchor address check failed "
				"export_request=%1 address=%2 anchor=%3: %4"
				).arg(exportRequestId
				).arg(targetAddress
				).arg(LogKey(identity->anchor)
				).arg(LifecycleErrorDetails(error)));
			fail(u"PHRASE_IMPORT_FAILED"_q);
		});
	});
}

void Session::restoreFromPhrase(
		KeyAuthorization auth,
		std::vector<QString> words,
		Fn<void()> done,
		Fn<void(const QString &error)> fail) {
	restoreFromPhrase(
		std::move(auth),
		std::move(words),
		nullptr,
		[done = std::move(done)](KeyAuthorization) {
			if (done) {
				done();
			}
		},
		std::move(fail));
}

void Session::restoreFromPhrase(
		KeyAuthorization auth,
		std::vector<QString> words,
		std::shared_ptr<CommentScope> scope,
		Fn<void(KeyAuthorization)> done,
		Fn<void(const QString &error)> fail) {
	fail = loggedPhraseFail(u"phrase restore"_q, std::move(fail));
	if (scope) {
		// A scope over a vault this process cannot open carries that vault's
		// record, which the confirmed reset drops before the install. The
		// vault can become usable before the install runs, and then no reset
		// comes, so restoreFromWords() re-establishes the no-record invariant
		// before it stores, refusing a scope that still carries a record.
		if (!commentScopeCurrent(scope)
			|| (scope->_state->record
				&& !vaultKeyUnusable()
				&& !secretUnreadable(scope->_state->record->recordId))) {
			if (fail) {
				fail(u"PHRASE_ORIGIN_EXPIRED"_q);
			}
			return;
		}
	} else {
		ensureLoaded();
	}
	if (custodyBusy()) {
		LOG(("Wallet Error: restore requested while another is in flight."));
		if (fail) {
			fail(u"PHRASE_BUSY"_q);
		}
		return;
	}
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize) {
		LOG(("Wallet Error: restore requested without a settled wallet key."));
		if (fail) {
			fail(u"PHRASE_STATE_UNKNOWN"_q);
		}
		return;
	}
	const auto match = DetectPhraseMatch(words);
	if (match != PhraseMatch::Rotation) {
		if (fail) {
			fail((match == PhraseMatch::Foreign)
				? u"PHRASE_FOREIGN_PHRASE"_q
				: u"PHRASE_INVALID_PHRASE"_q);
		}
		return;
	}
	const auto installed = std::make_shared<KeyAuthorization>();
	auth = TrackCommentInstallation(std::move(auth), installed);
	retireCommentScopes(scope);
	_phraseRevealing = true;
	// A store the install ladder was cancelled out of persists nothing, and
	// this flow has no words of its own to show, so it is a failure here.
	// The guard is cleared once, by whichever wrapped callback runs, and the
	// outer fail is called directly: routing through the wrapped one would
	// clear the guard a second time. The two not-installed outcomes are told
	// apart, because a cancelled chooser states nothing while a custody write
	// that failed must be stated.
	auto refused = [this, fail, installed](const QString &error) {
		_phraseRevealing = false;
		*installed = KeyAuthorization();
		if (fail) {
			fail(error);
		}
	};
	restoreFromWords(
		std::move(auth),
		std::move(words),
		[this, scope, installed, done = std::move(done), fail = std::move(fail)](
				std::vector<QString>,
				CustodyOutcome outcome) {
			_phraseRevealing = false;
			if (scope && !commentScopeCurrent(scope)) {
				*installed = KeyAuthorization();
				if (fail) {
					fail(u"PHRASE_ORIGIN_EXPIRED"_q);
				}
			} else if (outcome == CustodyOutcome::Installed) {
				if (done) {
					done(base::take(*installed));
				}
			} else if (fail) {
				fail((outcome == CustodyOutcome::WriteFailed)
					? u"PHRASE_INSTALL_FAILED"_q
					: u"PHRASE_INSTALL_CANCELLED"_q);
			}
		},
		std::move(refused),
		scope);
}

void Session::restoreFromBackup(
		KeyAuthorization auth,
		std::optional<Core::CloudPasswordResult> password,
		Fn<void()> done,
		Fn<void(const QString &error)> fail) {
	restoreFromBackup(
		std::move(auth),
		std::move(password),
		nullptr,
		[done = std::move(done)](KeyAuthorization) {
			if (done) {
				done();
			}
		},
		std::move(fail));
}

void Session::restoreFromBackup(
		KeyAuthorization auth,
		std::optional<Core::CloudPasswordResult> password,
		std::shared_ptr<CommentScope> scope,
		Fn<void(KeyAuthorization)> done,
		Fn<void(const QString &error)> fail) {
	fail = loggedPhraseFail(u"backup restore"_q, std::move(fail));
	if (scope) {
		// A scope over a vault this process cannot open carries that vault's
		// record, which the confirmed reset drops before the install. The
		// vault can become usable before the install runs, and then no reset
		// comes, so restoreFromWords() re-establishes the no-record invariant
		// before it stores, refusing a scope that still carries a record.
		if (!commentScopeCurrent(scope)
			|| (scope->_state->record
				&& !vaultKeyUnusable()
				&& !secretUnreadable(scope->_state->record->recordId))) {
			if (fail) {
				fail(u"PHRASE_ORIGIN_EXPIRED"_q);
			}
			return;
		}
	} else {
		ensureLoaded();
	}
	if (custodyBusy()) {
		LOG(("Wallet Error: restore requested while another is in flight."));
		if (fail) {
			fail(u"PHRASE_BUSY"_q);
		}
		return;
	}
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize) {
		LOG(("Wallet Error: restore requested without a settled wallet key."));
		if (fail) {
			fail(u"PHRASE_STATE_UNKNOWN"_q);
		}
		return;
	}
	const auto installed = std::make_shared<KeyAuthorization>();
	auth = TrackCommentInstallation(std::move(auth), installed);
	retireCommentScopes(scope);
	_phraseRevealing = true;
	// Same one-terminal-call shape as restoreFromPhrase: an install ladder
	// that stored nothing makes this flow fail, because a restore that stored
	// nothing restored nothing. The two not-installed outcomes are told apart,
	// because a cancelled chooser states nothing while a custody write that
	// failed must be stated.
	auto refused = [this, fail, installed](const QString &error) {
		_phraseRevealing = false;
		*installed = KeyAuthorization();
		if (fail) {
			fail(error);
		}
	};
	revealFromShares(
		std::move(auth),
		std::move(password),
		[this, scope, installed, done = std::move(done), fail = std::move(fail)](
				std::vector<QString>,
				CustodyOutcome outcome) {
			_phraseRevealing = false;
			if (scope && !commentScopeCurrent(scope)) {
				*installed = KeyAuthorization();
				if (fail) {
					fail(u"PHRASE_ORIGIN_EXPIRED"_q);
				}
			} else if (outcome == CustodyOutcome::Installed) {
				if (done) {
					done(base::take(*installed));
				}
			} else if (fail) {
				fail((outcome == CustodyOutcome::WriteFailed)
					? u"PHRASE_INSTALL_FAILED"_q
					: u"PHRASE_INSTALL_CANCELLED"_q);
			}
		},
		std::move(refused),
		scope);
}

void Session::revealParked(
		KeyAuthorization auth,
		const QByteArray &publicKey,
		Fn<void(std::vector<QString>, CustodyOutcome outcome)> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
	fail = loggedPhraseFail(u"parked reveal"_q, std::move(fail));
	LOG(("Wallet Info: parked reveal requested anchor=%1.")
		.arg(LogKey(publicKey)));
	if (custodyBusy()) {
		LOG(("Wallet Error: reveal requested while another is in flight."));
		if (fail) {
			fail(u"PHRASE_BUSY"_q);
		}
		return;
	}
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize) {
		LOG(("Wallet Error: reveal requested without a settled wallet key."));
		if (fail) {
			fail(u"PHRASE_STATE_UNKNOWN"_q);
		}
		return;
	}
	const auto record = custody().byAnchor(publicKey);
	if (!record || !parked(*record)) {
		LOG(("Wallet Error: parked reveal requested for a non-parked key."));
		if (fail) {
			fail(u"PHRASE_STATE_UNKNOWN"_q);
		}
		return;
	}
	if (!ReadAuthorized(*this, auth)) {
		if (fail) {
			fail(u"PHRASE_VAULT_LOCKED"_q);
		}
		return;
	}
	retireCommentScopes();
	_phraseRevealing = true;
	done = [this, done = std::move(done)](
			std::vector<QString> words,
			CustodyOutcome outcome) {
		_phraseRevealing = false;
		if (done) {
			done(std::move(words), outcome);
		}
	};
	fail = [this, fail = std::move(fail)](const QString &error) {
		_phraseRevealing = false;
		if (fail) {
			fail(error);
		}
	};
	// An unresolved record of the served wallet is parked only until its
	// phrase is read, and this reveal has to read it anyway: the identity
	// of the revealed words establishes its signing key under the same
	// latch, before the words go out, so a record that turns out to sign
	// with the served key is current by the time the box that listed it
	// rebuilds, and one that does not is settled as obsolete.
	const auto unresolved = (CanonicalAddress(record->address) == _address);
	const auto recordId = record->recordId;
	revealLocally(std::move(auth), *record, [=, this](
			std::vector<QString> words) {
		if (!unresolved) {
			done(std::move(words), CustodyOutcome::Installed);
			return;
		}
		validatePhraseIdentity(words, [=, this](
				std::optional<PhraseIdentity> identity) mutable {
			if (identity) {
				establishSigningKey(recordId, *identity);
			}
			done(std::move(words), CustodyOutcome::Installed);
		});
	}, fail);
}

void Session::dropParked(
		const QByteArray &publicKey,
		Fn<void()> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
	fail = loggedPhraseFail(u"parked drop"_q, std::move(fail));
	LOG(("Wallet Info: parked drop requested anchor=%1.")
		.arg(LogKey(publicKey)));
	if (custodyBusy()) {
		LOG(("Wallet Error: drop requested while another is in flight."));
		if (fail) {
			fail(u"PHRASE_BUSY"_q);
		}
		return;
	}
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize) {
		LOG(("Wallet Error: drop requested without a settled wallet key."));
		if (fail) {
			fail(u"PHRASE_STATE_UNKNOWN"_q);
		}
		return;
	}
	const auto record = custody().byAnchor(publicKey);
	if (!record || !parked(*record)) {
		LOG(("Wallet Error: drop requested for a non-parked key."));
		if (fail) {
			fail(u"PHRASE_STATE_UNKNOWN"_q);
		}
		return;
	}
	retireCommentScopes();
	_replacing = true;
	done = [this, done = std::move(done)] {
		_replacing = false;
		if (done) {
			done();
		}
	};
	fail = [this, fail = std::move(fail)](const QString &error) {
		_replacing = false;
		if (fail) {
			fail(error);
		}
	};
	const auto lifecycle = _engine->lifecycle();
	const auto recordId = record->recordId;
	_engine->run([lifecycle, descriptor = DescriptorFromRecord(*record)] {
		lifecycle->delete_wallet(descriptor);
	}, [=, this] {
		removeCustodyRecord(recordId);
		done();
	}, [=](EngineError error) {
		LOG(("Wallet Error: parked delete_wallet failed: %1"
			).arg(LifecycleErrorDetails(error)));
		fail(u"PHRASE_LOCAL_FAILED"_q);
	});
}

} // namespace Wallet
