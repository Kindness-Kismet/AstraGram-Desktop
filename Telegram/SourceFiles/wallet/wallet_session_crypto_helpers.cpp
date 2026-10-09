#include "wallet/wallet_session_internal.h"

namespace Wallet {
using namespace SessionDetails;

namespace SessionDetails {

[[nodiscard]] uint64 RotationValidUntil(uint64 seconds) {
	return uint64(base::unixtime::now()) + seconds;
}

[[nodiscard]] engine::PrepareKeyRotationRequest RotationRequest(
		uint64 seconds) {
	return engine::PrepareKeyRotationRequest{
		.valid_until = RotationValidUntil(seconds),
		.message_kind = engine::KeyRotationMessageKind::kExternal,
	};
}

[[nodiscard]] std::vector<QString> SplitWords(const QString &phrase) {
	const auto list = phrase.split(QChar(' '), Qt::SkipEmptyParts);
	return std::vector<QString>(list.begin(), list.end());
}

[[nodiscard]] QString NormalizeWord(const QString &word) {
	return word.trimmed().toLower();
}

[[nodiscard]] std::optional<QByteArray> RotationMnemonicKey(
		const QStringList &words) {
	try {
		const auto key = engine::rotation_mnemonic_public_key(
			words.join(QChar(' ')).toStdString());
		if (int(key.size()) != kCustodyPublicKeySize) {
			return std::nullopt;
		}
		return QByteArray(
			reinterpret_cast<const char*>(key.data()),
			key.size());
	} catch (...) {
		return std::nullopt;
	}
}

// The engine exports no signing-key function, only
// rotation_mnemonic_public_key, which returns derive_half_key(anchor).
// derive_rotation_keys derives both halves of a Rotation mnemonic with that
// same derive_half_key, independently per half; a 12-word phrase's signing
// half is its anchor half; and each half of a 24-word phrase is an
// independently checksummed BIP-39 phrase the engine accepts as a 12-word
// Rotation mnemonic of its own. So the anchor of words 13-24 taken alone is
// the 24-word phrase's signing key byte for byte, computed entirely inside
// the engine, with no cryptography in Telegram. Runs on the engine worker.
[[nodiscard]] std::optional<PhraseIdentity> DerivePhraseIdentity(
		const QStringList &normalized) {
	const auto count = normalized.size();
	if (count != 12 && count != 24) {
		LOG(("Wallet Error: phrase validation invalid_word_count=%1.")
			.arg(count));
		return std::nullopt;
	}
	const auto anchor = RotationMnemonicKey(normalized);
	if (!anchor) {
		LOG(("Wallet Error: phrase validation anchor_derivation_failed "
			"word_count=%1.").arg(count));
		return std::nullopt;
	}
	const auto signing = (count == 24)
		? RotationMnemonicKey(normalized.mid(12))
		: anchor;
	if (!signing) {
		LOG(("Wallet Error: phrase validation signing_derivation_failed "
			"word_count=%1.").arg(count));
		return std::nullopt;
	}
	return PhraseIdentity{ .anchor = *anchor, .signing = *signing };
}

// WHY: ton_connect_account() is the engine's only secret-free address
// derivation, refusing a descriptor whose anchor derives another address;
// the ref copies the engine's own form, so only the address can differ.
[[nodiscard]] bool AnchorDerivesOtherAddress(
		const std::shared_ptr<engine::WalletLifecycle> &lifecycle,
		const QByteArray &anchor,
		const QString &address) {
	const auto recordId = std::string("phrase-check");
	try {
		lifecycle->ton_connect_account(engine::WalletDescriptor{
			.record_id = recordId,
			.address = address.toStdString(),
			.public_key = std::vector<uint8_t>(
				anchor.constData(),
				anchor.constData() + anchor.size()),
			.network = engine::Network::kMainnet,
			.secret_ref = engine::ProtectedSecretRef{
				.value = "wallet:" + recordId + ":mnemonic",
			},
		});
	} catch (const engine::wallet_lifecycle_error::InvalidRecordId &) {
		return true;
	} catch (...) {
	}
	return false;
}

[[nodiscard]] const std::vector<QString> &Wordlist() {
	static const auto result = [] {
		auto list = std::vector<QString>();
		try {
			const auto words = engine::mnemonic_wordlist();
			list.reserve(words.size());
			for (const auto &word : words) {
				list.push_back(QString::fromStdString(word));
			}
			std::sort(list.begin(), list.end());
		} catch (...) {
			LOG(("Wallet Error: cannot read the engine wordlist."));
			list.clear();
		}
		return list;
	}();
	return result;
}

[[nodiscard]] QString LifecycleErrorName(const EngineError &error) {
	if (!error.underlying) {
		return u"unknown"_q;
	}
	try {
		std::rethrow_exception(error.underlying);
	} catch (const engine::wallet_lifecycle_error::InvalidRecordId &) {
		return u"InvalidRecordId"_q;
	} catch (const engine::wallet_lifecycle_error::InvalidRecoveryPhrase &) {
		return u"InvalidRecoveryPhrase"_q;
	} catch (const engine::wallet_lifecycle_error::AddressDerivationFailed &) {
		return u"AddressDerivationFailed"_q;
	} catch (const engine::wallet_lifecycle_error::TonConnectSigningFailed &) {
		return u"TonConnectSigningFailed"_q;
	} catch (const engine::wallet_lifecycle_error::SecretWalletMismatch &) {
		return u"SecretWalletMismatch"_q;
	} catch (const engine::wallet_lifecycle_error::ProtectedSecretHost &) {
		return u"ProtectedSecretHost"_q;
	} catch (const engine::wallet_lifecycle_error::InvalidTonConnectSessionInput &) {
		return u"InvalidTonConnectSessionInput"_q;
	} catch (...) {
	}
	return u"unknown"_q;
}

[[nodiscard]] bool IsVaultLocked(const EngineError &error) {
	if (!error.underlying) {
		return false;
	}
	try {
		std::rethrow_exception(error.underlying);
	} catch (const engine::wallet_lifecycle_error::ProtectedSecretHost &e) {
		return (e.kind
			== engine::ProtectedSecretHostErrorKind::kAuthenticationFailed);
	} catch (const engine::protected_secret_host_error::Failed &e) {
		return (e.kind
			== engine::ProtectedSecretHostErrorKind::kAuthenticationFailed);
	} catch (...) {
	}
	return false;
}

[[nodiscard]] QString LifecycleErrorDetails(const EngineError &error) {
	auto hostKind = -1;
	if (error.underlying) {
		try {
			std::rethrow_exception(error.underlying);
		} catch (const engine::wallet_lifecycle_error::ProtectedSecretHost &e) {
			hostKind = int(e.kind);
		} catch (const engine::protected_secret_host_error::Failed &e) {
			hostKind = int(e.kind);
		} catch (...) {
		}
	}
	return u"%1 secret_read=%2 protected_host_kind=%3"_q
		.arg(LifecycleErrorName(error))
		.arg(int(ProtectedSecretFailure(error)))
		.arg(hostKind);
}

[[nodiscard]] bool ReadAuthorized(
		Session &session,
		const KeyAuthorization &auth) {
	return auth.grant
		&& auth.grant->valid()
		&& session.vault().unlocked();
}

[[nodiscard]] bool ValidCommentPayloadSize(int size) {
	return size >= 64 && size <= 1024 && ((size - 48) % 16 == 0);
}

[[nodiscard]] QByteArray ServerCommentBody(const QByteArray &payload) {
	if (!ValidCommentPayloadSize(payload.size())) {
		return QByteArray();
	}
	const auto cells = 1 + (payload.size() - 35 + 126) / 127;
	const auto total = payload.size() + 4 + 3 * cells - 1;
	const auto width = (total > 255) ? 2 : 1;
	auto result = QByteArray::fromHex("b5ee9c7201");
	result.reserve(10 + width + total);
	result.append(char(width));
	result.append(char(cells));
	result.append(char(1));
	result.append(char(0));
	if (width == 2) {
		result.append(char(total >> 8));
	}
	result.append(char(total));
	result.append(char(0));
	for (auto cell = 0, offset = 0; cell != cells; ++cell) {
		const auto first = (cell == 0);
		const auto last = (cell + 1 == cells);
		const auto count = first
			? 35
			: std::min(127, int(payload.size()) - offset);
		result.append(char(last ? 0 : 1));
		result.append(char(2 * (count + (first ? 4 : 0))));
		if (first) {
			result.append(QByteArray::fromHex("2167da4b"));
		}
		result.append(payload.constData() + offset, count);
		offset += count;
		if (!last) {
			result.append(char(cell + 1));
		}
	}
	return result.toBase64();
}

// WHY: a fee counts a body's bits and cells, never what they hold, and an
// encrypted comment is 32 bytes of key mix, 16 of message key and the text
// padded by 16 to 31 bytes up to a multiple of 16: no key prices it.
[[nodiscard]] QByteArray EncryptedCommentFeeBody(const QString &text) {
	const auto bytes = int(text.toUtf8().size());
	return ServerCommentBody(QByteArray(48 + ((bytes + 31) & ~15), char(0)));
}

[[nodiscard]] bool ValidEncryptedCommentBody(const QByteArray &encoded) {
	constexpr auto kMaxCells = 1025;
	constexpr auto kMaxCellBytes = 1028 + 2 * kMaxCells + 2 * (kMaxCells - 1);
	constexpr auto kMaxBytes = 16 + kMaxCellBytes;
	if (encoded.isEmpty()
		|| encoded.size() > 4 * ((kMaxBytes + 2) / 3)
		|| encoded.size() % 4) {
		return false;
	}
	const auto decoded = QByteArray::fromBase64Encoding(
		encoded,
		QByteArray::AbortOnBase64DecodingErrors);
	if (!decoded || decoded.decoded.toBase64() != encoded) {
		return false;
	}
	const auto &body = decoded.decoded;
	auto offset = 0;
	const auto read = [&](int width) {
		if (offset + width > body.size()) {
			return -1;
		}
		auto value = 0;
		for (auto i = 0; i != width; ++i) {
			value = (value << 8) | uchar(body[offset++]);
		}
		return value;
	};
	if (read(2) != 0xb5ee || read(2) != 0x9c72) {
		return false;
	}
	const auto refs = read(1);
	const auto offsets = read(1);
	if ((refs != 1 && refs != 2) || (offsets != 1 && offsets != 2)) {
		return false;
	}
	const auto cells = read(refs);
	if (cells < 1 || cells > kMaxCells
		|| refs != ((cells > 255) ? 2 : 1)
		|| read(refs) != 1
		|| read(refs) != 0) {
		return false;
	}
	const auto total = read(offsets);
	if (total < 0 || total > kMaxCellBytes
		|| offsets != ((total > 255) ? 2 : 1)
		|| read(refs) != 0
		|| total != body.size() - offset) {
		return false;
	}
	auto payload = 0;
	for (auto cell = 0; cell != cells; ++cell) {
		const auto last = (cell + 1 == cells);
		if (read(1) != (last ? 0 : 1)) {
			return false;
		}
		const auto bits = read(1);
		const auto count = bits / 2;
		if (bits < 0 || bits % 2
			|| count < ((cell == 0) ? 4 : 1)
			|| count > body.size() - offset) {
			return false;
		}
		if (cell == 0) {
			if (read(2) != 0x2167 || read(2) != 0xda4b) {
				return false;
			}
			offset += count - 4;
			payload += count - 4;
		} else {
			offset += count;
			payload += count;
		}
		if (!last && read(refs) != cell + 1) {
			return false;
		}
	}
	return offset == body.size() && ValidCommentPayloadSize(payload);
}

[[nodiscard]] QByteArray EncryptedCommentBody(const TransferItem &item) {
	using Source = TransferItem::Source;
	using Format = TransferItem::EncryptedFormat;
	if (item.source == Source::Server
		&& item.encryptedFormat == Format::ServerPayload) {
		return ServerCommentBody(item.encryptedPayload);
	} else if (item.source == Source::Engine
		&& item.encryptedFormat == Format::EngineBodyBoc) {
		return item.encryptedPayload;
	}
	return QByteArray();
}

[[nodiscard]] KeyAuthorization TrackCommentInstallation(
		KeyAuthorization auth,
		const std::shared_ptr<KeyAuthorization> &installed) {
	installed->grant = auth.grant;
	if (const auto install = auth.install) {
		auth.install = [=](CustodyInstallRequest request) {
			request.ready = [=, ready = std::move(request.ready)](
					CustodyInstall result) {
				installed->grant = result.grant;
				ready(std::move(result));
			};
			install(std::move(request));
		};
	}
	return auth;
}

[[nodiscard]] DecryptedComment DecryptCommentBody(
		const std::shared_ptr<engine::WalletClient> &client,
		const engine::DecryptCommentRequest &request) {
	using Error = CommentDecryptError;
	auto watch = SecretReadWatch();
	try {
		auto text = client->decrypt_comment(request);
		const auto wipe = gsl::finally([&] {
			OPENSSL_cleanse(text.data(), text.size());
		});
		return { .text = SecureBytes(bytes::make_span(text)) };
	} catch (const engine::wallet_client_error::EncryptedCommentUnavailable &error) {
		LOG(("Wallet Error: comment decryption failed: %1"
			).arg(QString::fromUtf8(error.what())));
		// The engine reports a read the host refused and a decryption with
		// the wrong key alike; only the watch tells which one this was.
		const auto secret = watch.failure();
		return { .error = (secret != SecretReadFailure::None)
			? Error::KeyUnreadable
			: Error::DecryptionFailed,
			.secret = secret };
	} catch (const engine::wallet_client_error::LocalSigningUnavailable &) {
		return { .error = Error::Unavailable, .secret = watch.failure() };
	} catch (const engine::wallet_client_error::InvalidProtectedSecret &) {
		// The secret this device stored is broken, which is a statement
		// about the key, not about what is available right now.
		return {
			.error = Error::KeyUnreadable,
			.secret = (watch.failure() != SecretReadFailure::None)
				? watch.failure()
				: SecretReadFailure::Unreadable,
		};
	} catch (const engine::wallet_client_error::SendAlreadyInProgress &) {
		return { .error = Error::Busy };
	} catch (const engine::wallet_client_error::StateUnavailable &) {
		return { .error = Error::Cancelled };
	} catch (...) {
		return { .error = Error::Failed };
	}
}

[[nodiscard]] QString ClientErrorName(std::exception_ptr error) {
	if (!error) {
		return u"unknown"_q;
	}
	try {
		std::rethrow_exception(error);
	} catch (const engine::wallet_client_error::WalletIdentityMismatch &) {
		return u"WalletIdentityMismatch"_q;
	} catch (const engine::wallet_client_error::InvalidWalletPublicKey &) {
		return u"InvalidWalletPublicKey"_q;
	} catch (const engine::wallet_client_error
			::InvalidLocalSecretReference &) {
		return u"InvalidLocalSecretReference"_q;
	} catch (const engine::wallet_client_error::InvalidProviderBaseUrl &) {
		return u"InvalidProviderBaseUrl"_q;
	} catch (...) {
	}
	return u"unknown"_q;
}

[[nodiscard]] engine::WalletClientConfig ClientConfigFromRecord(
		const CustodyRecord &record) {
	return engine::WalletClientConfig{
		.record_id = record.recordId.toStdString(),
		.address = record.address.toStdString(),
		.public_key = std::vector<uint8_t>(
			record.publicKey.constData(),
			record.publicKey.constData() + record.publicKey.size()),
		.local_secret_ref = engine::ProtectedSecretRef{
			.value = record.secretRef.toStdString(),
		},
		.network = engine::Network(record.network),
		.send_validity_seconds = kClientSendValiditySeconds,
		.resolution_margin_seconds = kClientResolutionMarginSeconds,
		.providers = engine::ProviderConfig{
			.toncenter_base_url = "https://toncenter.com",
			.dns_root_address = std::nullopt,
			.request_timeout_ms = kClientRequestTimeoutMs,
		},
	};
}

// A public-key-only client for the served wallet. The engine reads state
// and emulates transfers from the address and key alone, with a placeholder
// signature, and answers send() with LocalSigningUnavailable. Its record id
// names no custody record, so nothing it could journal reads back as a
// signing record's, and the session never binds _clientRecordId to it.
[[nodiscard]] engine::WalletClientConfig ClientConfigForPreview(
		const TransferWalletIdentity &identity) {
	return engine::WalletClientConfig{
		.record_id = kPreviewClientRecordId,
		.address = FormatFriendly(identity.address, false).toStdString(),
		.public_key = std::vector<uint8_t>(
			identity.publicKey.constData(),
			identity.publicKey.constData() + identity.publicKey.size()),
		.local_secret_ref = std::nullopt,
		.network = engine::Network::kMainnet,
		.send_validity_seconds = kClientSendValiditySeconds,
		.resolution_margin_seconds = kClientResolutionMarginSeconds,
		.providers = engine::ProviderConfig{
			.toncenter_base_url = "https://toncenter.com",
			.dns_root_address = std::nullopt,
			.request_timeout_ms = kClientRequestTimeoutMs,
		},
	};
}

[[nodiscard]] std::optional<std::vector<int>> ParseHolderDcs(
		const MTPDwallet_secretPhraseParts &data) {
	const auto &list = data.vdcs().v;
	if (data.vtoken().v.isEmpty() || list.isEmpty()) {
		LOG(("Wallet Error: invalid backup holders token_empty=%1 count=%2."
			).arg(data.vtoken().v.isEmpty()).arg(list.size()));
		return std::nullopt;
	}
	auto result = std::vector<int>();
	result.reserve(list.size());
	for (const auto &dc : list) {
		if (dc.v <= 0
			|| dc.v >= MTP::kDcShift
			|| ranges::contains(result, dc.v)) {
			LOG(("Wallet Error: invalid backup holder index=%1 dc=%2 "
				"duplicate=%3 count=%4."
				).arg(result.size()
				).arg(dc.v
				).arg(ranges::contains(result, dc.v)
				).arg(list.size()));
			return std::nullopt;
		}
		result.push_back(dc.v);
	}
	return result;
}

[[nodiscard]] std::optional<std::vector<QByteArray>> ParseBackupHolderKeys(
		const QVector<MTPwallet_HolderDc> &list) {
	if (list.size() < 2) {
		return std::nullopt;
	}
	auto dcs = std::vector<int>();
	auto result = std::vector<QByteArray>();
	dcs.reserve(list.size());
	result.reserve(list.size());
	for (const auto &holder : list) {
		const auto &data = holder.data();
		const auto dc = data.vdc().v;
		const auto &key = data.vpublic_key().v;
		if (dc <= 0
			|| ranges::contains(dcs, dc)
			|| key.size() != PhraseShares::kPublicKeySize) {
			return std::nullopt;
		}
		dcs.push_back(dc);
		result.push_back(key);
	}
	return result;
}

[[nodiscard]] std::optional<std::vector<QByteArray>> SealBackupParts(
		const std::vector<QByteArray> &holderKeys,
		const std::vector<QString> &words) {
	const auto seed = PhraseShares::SeedFromWords(words);
	if (seed.isEmpty()) {
		return std::nullopt;
	}
	const auto shares = PhraseShares::SplitSeed(seed, int(holderKeys.size()));
	auto result = std::vector<QByteArray>();
	result.reserve(holderKeys.size());
	for (auto i = 0, count = int(holderKeys.size()); i != count; ++i) {
		auto part = PhraseShares::EncryptShare(holderKeys[i], shares[i]);
		if (!part) {
			return std::nullopt;
		}
		result.push_back(std::move(*part));
	}
	return result;
}

} // namespace SessionDetails

} // namespace Wallet
