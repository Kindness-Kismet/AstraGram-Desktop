#include "wallet/wallet_session_internal.h"

namespace Wallet {
using namespace SessionDetails;

void Session::enableBackup(
		KeyAuthorization auth,
		Fn<void()> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
	fail = LoggedFail(u"backup enable"_q, std::move(fail));
	if (custodyBusy() || custody().pendingRotation) {
		LOG(("Wallet Error: backup requested while another is in flight."));
		if (fail) {
			fail(u"BACKUP_BUSY"_q);
		}
		return;
	}
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize) {
		LOG(("Wallet Error: backup requested without a settled wallet key."));
		if (fail) {
			fail(u"BACKUP_STATE_UNKNOWN"_q);
		}
		return;
	}
	const auto matching = vaultKeyUnusable() ? nullptr : currentRecord();
	const auto proofKey = !matching
		? QByteArray()
		: matching->signingKey.isEmpty()
		? matching->publicKey
		: matching->signingKey;
	if (proofKey.size() != kCustodyPublicKeySize) {
		LOG(("Wallet Error: backup requested without local custody."));
		if (fail) {
			fail(u"BACKUP_NO_CUSTODY"_q);
		}
		return;
	}
	const auto record = *matching;
	if (!ReadAuthorized(*this, auth)) {
		if (fail) {
			fail(u"BACKUP_VAULT_LOCKED"_q);
		}
		return;
	}
	const auto address = _address;
	const auto generation = _networkGeneration;
	retireCommentScopes();
	_backupChanging = true;
	done = [this, done = std::move(done)] {
		_backupChanging = false;
		if (done) {
			done();
		}
	};
	fail = [this, fail = std::move(fail)](const QString &error) {
		_backupChanging = false;
		if (fail) {
			fail(error);
		}
	};
	const auto current = [=, this] {
		const auto now = vaultKeyUnusable() ? nullptr : currentRecord();
		return (generation == _networkGeneration)
			&& (address == _address)
			&& now
			&& (now->recordId == record.recordId);
	};
	const auto proofFailed = [=](OwnershipProofError error) {
		fail(!current()
			? u"BACKUP_WALLET_CHANGED"_q
			: (error == OwnershipProofError::VaultLocked)
			? u"BACKUP_VAULT_LOCKED"_q
			: u"BACKUP_PROOF_FAILED"_q);
	};
	const auto send = [=, this](const MTPVector<MTPbytes> &parts) {
		if (!current()) {
			fail(u"BACKUP_WALLET_CHANGED"_q);
			return;
		}
		const auto proofReady = [=, this](OwnershipProof proof) {
			if (!current()) {
				fail(u"BACKUP_WALLET_CHANGED"_q);
				return;
			}
			using Flag = MTPwallet_enableBackup::Flag;
			// One-shot proof: a refused or lost answer is settled, not resent.
			_stateApi.request(MTPwallet_EnableBackup(
				MTP_flags(Flag::f_new_public_key | Flag::f_proof),
				parts,
				MTP_bytes(proofKey),
				MTP_walletOwnershipProof(
					MTP_int(proof.timestamp),
					MTP_bytes(bytes::make_span(proof.signature)))
			)).done([=, this](const MTPWalletState &result) {
				clearRotatedSinceBackup();
				applyState(result, false);
				done();
			}).fail([=, this](const MTP::Error &error) {
				LOG(("Wallet Error: wallet.enableBackup with a proof failed: %1"
					).arg(error.type()));
				settleRefusedBackupChange(
					address,
					proofKey,
					true,
					error.type(),
					done,
					[=, this](const QString &refused) {
						fail(backupEnableRefusal(
							address,
							record.recordId,
							proofKey,
							refused));
					});
			}).handleAllErrors().send();
		};
		requestOwnershipProof(
			DescriptorFromRecord(record),
			proofKey,
			auth.grant,
			proofReady,
			proofFailed);
	};
	_stateApi.request(MTPwallet_GetBackupHolderDcs(
	)).done([=, this](const MTPVector<MTPwallet_HolderDc> &result) {
		const auto keys = ParseBackupHolderKeys(result.v);
		if (!keys) {
			LOG(("Wallet Error: wallet.getBackupHolderDcs answered "
				"%1 holder(s).").arg(result.v.size()));
			fail(u"BACKUP_HOLDERS_INVALID"_q);
			return;
		}
		if (!current()) {
			fail(u"BACKUP_WALLET_CHANGED"_q);
			return;
		}
		revealLocally(auth, record, [=, this](std::vector<QString> words) {
			const auto parts = SealBackupParts(*keys, words);
			if (!parts) {
				LOG(("Wallet Error: backup parts could not be sealed."));
				fail(u"BACKUP_ENCRYPT_FAILED"_q);
				return;
			}
			auto list = QVector<MTPbytes>();
			list.reserve(int(parts->size()));
			for (const auto &part : *parts) {
				list.push_back(MTP_bytes(part));
			}
			const auto sealed = MTP_vector<MTPbytes>(std::move(list));
			validatePhraseIdentity(words, [=](
					std::optional<PhraseIdentity> identity) {
				if (!identity
					|| (identity->anchor != record.publicKey)
					|| (identity->signing != proofKey)) {
					LOG(("Wallet Error: the revealed phrase does not derive "
						"the key its proof names."));
					fail(u"BACKUP_KEY_MISMATCH"_q);
					return;
				}
				send(sealed);
			});
		}, [=](const QString &error) {
			// revealLocally is the phrase flow's helper and refuses in its
			// own family; a vault cleared between the holder-DC answer and
			// this read is this flow's refusal, so it leaves in this flow's
			// token.
			fail((error == u"PHRASE_VAULT_LOCKED"_q)
				? u"BACKUP_VAULT_LOCKED"_q
				: error);
		});
	}).fail([=](const MTP::Error &error) {
		LOG(("Wallet Error: wallet.getBackupHolderDcs failed: %1"
			).arg(error.type()));
		fail(error.type());
	}).send();
}

QString Session::backupEnableRefusal(
		const QString &address,
		const QString &recordId,
		const QByteArray &proofKey,
		const QString &error) {
	const auto keyRefused = (error == u"WALLET_ROTATION_NOT_FOUND"_q)
		|| (error == u"WALLET_PROOF_INVALID"_q);
	if (keyRefused && (_address == address) && (_publicKey != proofKey)) {
		const auto now = custody().current(_address, _publicKey);
		if (!now || now->recordId != recordId) {
			return u"BACKUP_PHRASE_OUTDATED"_q;
		} else if (error == u"WALLET_ROTATION_NOT_FOUND"_q) {
			return u"BACKUP_KEY_UNCONFIRMED"_q;
		}
	}
	return (error == u"WALLET_PROOF_INVALID"_q)
		? u"BACKUP_NOT_VERIFIED"_q
		: error;
}

void Session::disableBackup(
		std::optional<Core::CloudPasswordResult> password,
		Fn<void()> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
	fail = LoggedFail(u"backup disable"_q, std::move(fail));
	if (custodyBusy() || custody().pendingRotation) {
		LOG(("Wallet Error: backup disable requested "
			"while another is in flight."));
		if (fail) {
			fail(u"BACKUP_BUSY"_q);
		}
		return;
	}
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize) {
		LOG(("Wallet Error: backup disable requested "
			"without a settled wallet key."));
		if (fail) {
			fail(u"BACKUP_STATE_UNKNOWN"_q);
		}
		return;
	}
	if (!currentRecord()) {
		LOG(("Wallet Error: backup disable requested without local custody."));
		if (fail) {
			fail(u"BACKUP_NO_CUSTODY"_q);
		}
		return;
	}
	retireCommentScopes();
	_backupChanging = true;
	done = [this, done = std::move(done)] {
		_backupChanging = false;
		if (done) {
			done();
		}
	};
	fail = [this, fail = std::move(fail)](const QString &error) {
		_backupChanging = false;
		if (fail) {
			fail(error);
		}
	};
	using Flag = MTPwallet_disableBackup::Flag;
	const auto checked = password && *password;
	_stateApi.request(MTPwallet_DisableBackup(
		MTP_flags(checked ? Flag::f_password : Flag(0)),
		checked ? password->result : MTP_inputCheckPasswordEmpty(),
		MTP_bytes(), // new_public_key
		MTPWalletOwnershipProof() // proof
	)).done([=, this](const MTPWalletState &result) {
		clearRotatedSinceBackup();
		applyState(result, false);
		done();
	}).fail([=](const MTP::Error &error) {
		LOG(("Wallet Error: wallet.disableBackup failed: %1"
			).arg(error.type()));
		fail(error.type());
	}).handleFloodErrors().send();
}

void Session::disableBackupWithProof(
		KeyAuthorization auth,
		BackupDisableApproval approved,
		Fn<void()> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
	fail = LoggedFail(u"backup disable"_q, std::move(fail));
	if (custodyBusy() || custody().pendingRotation) {
		LOG(("Wallet Error: backup disable requested "
			"while another is in flight."));
		if (fail) {
			fail(u"BACKUP_BUSY"_q);
		}
		return;
	}
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize) {
		LOG(("Wallet Error: backup disable requested "
			"without a settled wallet key."));
		if (fail) {
			fail(u"BACKUP_STATE_UNKNOWN"_q);
		}
		return;
	}
	const auto record = vaultKeyUnusable() ? nullptr : currentRecord();
	const auto proofKey = !record
		? QByteArray()
		: record->signingKey.isEmpty()
		? record->publicKey
		: record->signingKey;
	if (proofKey.size() != kCustodyPublicKeySize) {
		LOG(("Wallet Error: backup disable requested without local custody."));
		if (fail) {
			fail(u"BACKUP_NO_CUSTODY"_q);
		}
		return;
	}
	if (approved.networkGeneration != _networkGeneration
		|| approved.address != _address
		|| approved.recordId.isEmpty()
		|| record->recordId != approved.recordId) {
		LOG(("Wallet Error: backup disable approved "
			"for another wallet state."));
		if (fail) {
			fail(u"BACKUP_WALLET_CHANGED"_q);
		}
		return;
	}
	if (!ReadAuthorized(*this, auth)) {
		if (fail) {
			fail(u"BACKUP_VAULT_LOCKED"_q);
		}
		return;
	}
	const auto address = _address;
	const auto recordId = record->recordId;
	const auto generation = _networkGeneration;
	auto descriptor = DescriptorFromRecord(*record);
	retireCommentScopes();
	_backupChanging = true;
	done = [this, done = std::move(done)] {
		_backupChanging = false;
		if (done) {
			done();
		}
	};
	fail = [this, fail = std::move(fail)](const QString &error) {
		_backupChanging = false;
		if (fail) {
			fail(error);
		}
	};
	const auto current = [=, this] {
		const auto now = vaultKeyUnusable() ? nullptr : currentRecord();
		return (generation == _networkGeneration)
			&& (address == _address)
			&& now
			&& (now->recordId == recordId);
	};
	const auto proofReady = [=, this](OwnershipProof proof) {
		if (!current()) {
			fail(u"BACKUP_WALLET_CHANGED"_q);
			return;
		}
		using Flag = MTPwallet_disableBackup::Flag;
		// The proof is one-shot, so a negative or 500-class answer reaches
		// .fail() instead of the transport resending the identical body.
		// Every refusal is settled against the served state: a disable the
		// scanner already made, or one the server applied before its answer
		// was lost, is still the outcome the user asked for.
		_stateApi.request(MTPwallet_DisableBackup(
			MTP_flags(Flag::f_new_public_key | Flag::f_proof),
			MTP_inputCheckPasswordEmpty(), // password
			MTP_bytes(proofKey), // new_public_key
			MTP_walletOwnershipProof(
				MTP_int(proof.timestamp),
				MTP_bytes(bytes::make_span(proof.signature))) // proof
		)).done([=, this](const MTPWalletState &result) {
			clearRotatedSinceBackup();
			applyState(result, false);
			done();
		}).fail([=, this](const MTP::Error &error) {
			LOG(("Wallet Error: wallet.disableBackup with a proof failed: %1"
				).arg(error.type()));
			settleRefusedBackupChange(
				address,
				proofKey,
				false,
				error.type(),
				done,
				fail);
		}).handleAllErrors().send();
	};
	const auto proofFailed = [=](OwnershipProofError error) {
		fail(!current()
			? u"BACKUP_WALLET_CHANGED"_q
			: (error == OwnershipProofError::VaultLocked)
			? u"BACKUP_VAULT_LOCKED"_q
			: u"BACKUP_PROOF_FAILED"_q);
	};
	requestOwnershipProof(
		std::move(descriptor),
		proofKey,
		auth.grant,
		proofReady,
		proofFailed);
}

void Session::settleRefusedBackupChange(
		QString address,
		QByteArray proofKey,
		bool backupEnabled,
		QString error,
		Fn<void()> done,
		Fn<void(const QString &)> fail) {
	// WHY: only a served backup in the asked state under the proof key is
	// this action's outcome; the read bypasses requestState, where another
	// settle would cancel it and leave _backupChanging set until relaunch.
	const auto startedAt = crl::now();
	const auto generation = _networkGeneration;
	_stateApi.request(MTPwallet_GetState(
	)).done([=, this](const MTPWalletState &state, mtpRequestId requestId) {
		LOG(("Wallet Info: backup settle wallet.getState request=%1 "
			"elapsed_ms=%2; %3."
			).arg(requestId
			).arg(crl::now() - startedAt
			).arg(LogWalletState(state)));
		if (generation != _networkGeneration) {
			fail(error);
			return;
		}
		applyState(state, false);
		const auto settled = (state.type() == mtpc_walletState)
			&& (state.c_walletState().is_backup_enabled() == backupEnabled)
			&& (state.c_walletState().vpublic_key().v == proofKey)
			&& (_address == address);
		if (settled) {
			clearRotatedSinceBackup();
			done();
		} else {
			fail(error);
		}
	}).fail([=](const MTP::Error &refused, mtpRequestId requestId) {
		LOG(("Wallet Error: backup settle wallet.getState request=%1 "
			"elapsed_ms=%2 failed: %3"
			).arg(requestId).arg(crl::now() - startedAt).arg(refused.type()));
		fail(error);
	}).handleAllErrors().send();
}

} // namespace Wallet
