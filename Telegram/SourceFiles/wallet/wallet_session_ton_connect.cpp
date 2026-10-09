#include "wallet/wallet_session_internal.h"

namespace Wallet {
using namespace SessionDetails;

bool Session::tonConnectProofDomainAllowed(const QString &domain) const {
	return TonConnectProofDomainAllowed(domain, _tonConnectOwnershipDomain);
}

void Session::deriveTonConnectSession(
		KeyAuthorization auth,
		const QString &dappClientId,
		const QByteArray &nonce,
		Fn<void(TonConnectKey)> done,
		Fn<void(TonConnectKeyError)> fail) {
	const auto record = (tonConnectAccess() == TonConnectAccess::Allowed)
		? currentRecord()
		: nullptr;
	if (!record) {
		fail(TonConnectKeyError::Blocked);
		return;
	} else if (!ReadAuthorized(*this, auth)) {
		fail(TonConnectKeyError::Locked);
		return;
	}
	struct Derived {
		std::shared_ptr<engine::TonConnectDerivedSession> session;
		std::string clientId;
		std::vector<uint8_t> signingKey;
	};
	const auto address = _address;
	const auto served = _publicKey;
	const auto generation = _networkGeneration;
	const auto recordId = record->recordId;
	const auto lifecycle = _engine->lifecycle();
	auto request = engine::TonConnectDerivedSessionRequest{
		.descriptor = DescriptorFromRecord(*record),
		.dapp_client_id = dappClientId.toLower().toStdString(),
		.nonce = EngineBytes(nonce),
	};
	_engine->runLocal([lifecycle, request = std::move(request)] {
		auto session = lifecycle->derive_ton_connect_session(request);
		auto clientId = session->public_key_hex();
		auto signingKey = session->signing_public_key();
		return Derived{
			.session = std::move(session),
			.clientId = std::move(clientId),
			.signingKey = std::move(signingKey),
		};
	}, [=, this, grant = auth.grant](Derived derived) {
		const auto now = currentRecord();
		auto signingKey = BytesFromEngine(derived.signingKey);
		if (generation != _networkGeneration
			|| address != _address
			|| served != _publicKey
			|| !now
			|| now->recordId != recordId
			|| signingKey != served) {
			LOG(("Wallet Error: TON Connect key derived "
				"for another wallet state."));
			fail(TonConnectKeyError::Blocked);
			return;
		}
		done(TonConnectKey{
			.session = std::move(derived.session),
			.clientId = QString::fromStdString(derived.clientId),
			.signingKey = std::move(signingKey),
		});
	}, [=, this, grant = auth.grant](EngineError error) {
		LOG(("Wallet Error: TON Connect key derivation failed: %1"
			).arg(LifecycleErrorName(error)));
		noteSecretReadFailure(ProtectedSecretFailure(error), recordId);
		fail(IsVaultLocked(error)
			? TonConnectKeyError::Locked
			: TonConnectKeyError::Failed);
	});
}

void Session::prepareTonConnectEvent(
		KeyAuthorization auth,
		TonConnectKey key,
		TonConnectEventRequest request,
		Fn<void(TonConnectReply)> done,
		Fn<void(TonConnectKeyError)> fail) {
	if (request.proofPayload
		&& !tonConnectProofDomainAllowed(request.proofDomain)) {
		LOG(("Wallet Error: TON Connect proof refused for a reserved "
			"domain."));
		fail(TonConnectKeyError::Failed);
		return;
	}
	const auto record = (tonConnectAccess() == TonConnectAccess::Allowed)
		? currentRecord()
		: nullptr;
	if (!record
		|| request.address != _address
		|| key.signingKey != _publicKey) {
		fail(TonConnectKeyError::Blocked);
		return;
	} else if (request.proofPayload && !ReadAuthorized(*this, auth)) {
		fail(TonConnectKeyError::Locked);
		return;
	}
	const auto timestamp = base::unixtime::now();
	if (!key
		|| request.eventId > uint64(std::numeric_limits<int64>::max())
		|| (request.proofPayload && timestamp <= 0)) {
		LOG(("Wallet Error: TON Connect event requested "
			"with unusable input."));
		fail(TonConnectKeyError::Failed);
		return;
	}
	struct Prepared {
		TonConnectReply reply;
		QString address;
		QByteArray publicKey;
	};
	const auto address = _address;
	const auto served = _publicKey;
	const auto generation = _networkGeneration;
	const auto recordId = record->recordId;
	const auto lifecycle = _engine->lifecycle();
	const auto session = key.session;
	const auto eventId = request.eventId;
	const auto payload = request.proofPayload
		? std::make_optional(request.proofPayload->toStdString())
		: std::nullopt;
	auto descriptor = DescriptorFromRecord(*record);
	auto domain = request.proofDomain.toStdString();
	auto challenge = EngineBytes(request.challenge);
	auto signingKey = EngineBytes(key.signingKey);
	auto device = engine::TonConnectDevice{
		.platform = Platform::IsWindows()
			? engine::TonConnectDevicePlatform::kWindows
			: Platform::IsMac()
			? engine::TonConnectDevicePlatform::kMac
			: engine::TonConnectDevicePlatform::kLinux,
		.app_name = "telegram",
		.app_version = AppVersionStr,
	};
	_engine->runLocal([
		=,
		descriptor = std::move(descriptor),
		domain = std::move(domain),
		challenge = std::move(challenge),
		signingKey = std::move(signingKey),
		device = std::move(device)
	] {
		auto account = lifecycle->ton_connect_account(descriptor);
		auto proof = std::optional<engine::TonConnectProofReply>();
		if (payload) {
			auto signature = lifecycle->sign_ton_connect_proof({
				.descriptor = descriptor,
				.domain = domain,
				.timestamp = uint64_t(timestamp),
				.payload = *payload,
			});
			account.public_key = std::move(signature.public_key);
			proof = engine::TonConnectProofReply{
				.timestamp = uint64_t(timestamp),
				.domain = domain,
				.payload = *payload,
				.signature = std::move(signature.signature),
			};
		} else {
			account.public_key = signingKey;
		}
		const auto answer = session->open_challenge(challenge);
		const auto body = session->encrypt_connect_event(
			eventId,
			account,
			std::move(proof),
			device);
		return Prepared{
			.reply = {
				.challengeAnswer = BytesFromEngine(answer),
				.body = BytesFromEngine(body),
			},
			.address = QString::fromStdString(account.address),
			.publicKey = BytesFromEngine(account.public_key),
		};
	}, [=, this, grant = auth.grant](Prepared prepared) {
		const auto now = currentRecord();
		if (generation != _networkGeneration
			|| address != _address
			|| served != _publicKey
			|| !now
			|| now->recordId != recordId) {
			LOG(("Wallet Error: TON Connect event prepared "
				"for another wallet state."));
			fail(TonConnectKeyError::Blocked);
			return;
		}
		const auto answerSize = prepared.reply.challengeAnswer.size();
		if (CanonicalAddress(prepared.address) != _address
			|| prepared.publicKey != _publicKey
			|| answerSize != kTonConnectChallengeAnswerSize) {
			LOG(("Wallet Error: TON Connect event prepared "
				"with an unexpected account."));
			fail(TonConnectKeyError::Failed);
			return;
		}
		done(std::move(prepared.reply));
	}, [=, this, grant = auth.grant](EngineError error) {
		LOG(("Wallet Error: TON Connect event preparation failed: %1"
			).arg(LifecycleErrorName(error)));
		noteSecretReadFailure(ProtectedSecretFailure(error), recordId);
		fail(IsVaultLocked(error)
			? TonConnectKeyError::Locked
			: TonConnectKeyError::Failed);
	});
}

void Session::prepareTonConnectError(
		TonConnectKey key,
		QByteArray challenge,
		uint64 eventId,
		int code,
		Fn<void(TonConnectReply)> done,
		Fn<void()> fail) {
	if (!key || eventId > uint64(std::numeric_limits<int64>::max())) {
		LOG(("Wallet Error: TON Connect rejection requested "
			"with unusable input."));
		fail();
		return;
	}
	using Code = engine::TonConnectConnectErrorCode;
	struct Reason {
		Code code = Code::kUserDeclined;
		std::string message;
	};
	constexpr auto kManifestNotFound = 2;
	constexpr auto kManifestContent = 3;
	auto reason = (code == kManifestNotFound)
		? Reason{ Code::kManifestNotFound, "Manifest not found" }
		: (code == kManifestContent)
		? Reason{ Code::kManifestContent, "Manifest content error" }
		: Reason{ Code::kUserDeclined, "User declined the connection" };
	_engine->runLocal([
		=,
		session = key.session,
		challenge = EngineBytes(challenge),
		reason = std::move(reason)
	] {
		const auto answer = session->open_challenge(challenge);
		const auto body = session->encrypt_connect_error(
			eventId,
			reason.code,
			reason.message);
		return TonConnectReply{
			.challengeAnswer = BytesFromEngine(answer),
			.body = BytesFromEngine(body),
		};
	}, [=](TonConnectReply reply) {
		if (reply.challengeAnswer.size() != kTonConnectChallengeAnswerSize) {
			LOG(("Wallet Error: TON Connect rejection prepared "
				"with an unexpected answer."));
			fail();
			return;
		}
		done(std::move(reply));
	}, [=](EngineError error) {
		LOG(("Wallet Error: TON Connect rejection preparation failed: %1"
			).arg(LifecycleErrorName(error)));
		fail();
	});
}

void Session::prepareTonConnectDisconnect(
		TonConnectKey key,
		uint64 eventId,
		Fn<void(QByteArray)> done,
		Fn<void()> fail) {
	if (!key || eventId > uint64(std::numeric_limits<int64>::max())) {
		LOG(("Wallet Error: TON Connect disconnect requested "
			"with unusable input."));
		fail();
		return;
	}
	_engine->runLocal([session = key.session, eventId] {
		return BytesFromEngine(session->encrypt_disconnect_event(eventId));
	}, [=](QByteArray body) {
		if (body.isEmpty()) {
			LOG(("Wallet Error: TON Connect disconnect prepared empty."));
			fail();
			return;
		}
		done(std::move(body));
	}, [=](EngineError error) {
		LOG(("Wallet Error: TON Connect disconnect preparation failed: %1"
			).arg(LifecycleErrorName(error)));
		fail();
	});
}

void Session::decryptTonConnectRequest(
		TonConnectKey key,
		QByteArray body,
		Fn<void(TonConnectAppRequest)> done,
		Fn<void()> fail) {
	const auto now = base::unixtime::now();
	if (!key || now <= 0) {
		LOG(("Wallet Error: TON Connect request decryption requested "
			"with unusable input."));
		fail();
		return;
	}
	_engine->runLocal([
		session = key.session,
		body = EngineBytes(body),
		now = uint64(now)
	] {
		auto derived = session->decrypt_request(body, now);
		return TonConnectAppRequestFromEngine(*session, std::move(derived));
	}, std::move(done), [=](EngineError error) {
		LOG(("Wallet Error: TON Connect request could not be decrypted: %1"
			).arg(error.message));
		fail();
	});
}

void Session::answerTonConnectChallenge(
		TonConnectKey key,
		QByteArray challenge,
		Fn<void(QByteArray)> done,
		Fn<void()> fail) {
	if (!key) {
		LOG(("Wallet Error: TON Connect challenge answer requested "
			"with unusable input."));
		fail();
		return;
	}
	_engine->runLocal([
		session = key.session,
		challenge = EngineBytes(challenge)
	] {
		return BytesFromEngine(session->open_challenge(challenge));
	}, [=](QByteArray answer) {
		if (answer.size() != kTonConnectChallengeAnswerSize) {
			LOG(("Wallet Error: TON Connect challenge answered "
				"with an unexpected size: %1.").arg(answer.size()));
			fail();
			return;
		}
		done(std::move(answer));
	}, [=](EngineError error) {
		LOG(("Wallet Error: TON Connect challenge could not be answered: %1"
			).arg(error.message));
		fail();
	});
}

void Session::encryptTonConnectResponse(
		TonConnectKey key,
		QString requestId,
		TonConnectResponse response,
		Fn<void(QByteArray)> done,
		Fn<void()> fail) {
	if (!key) {
		LOG(("Wallet Error: TON Connect response requested "
			"with unusable input."));
		fail();
		return;
	}
	using Code = engine::TonConnectRpcErrorCode;
	struct Reason {
		Code code = Code::kUnknown;
		std::string message;
	};
	auto reason = (response.error == TonConnectError::BadRequest)
		? Reason{ Code::kBadRequest, "Bad request" }
		: (response.error == TonConnectError::UserDeclined)
		? Reason{ Code::kUserDeclined, "User declined the transaction" }
		: (response.error == TonConnectError::UnknownApp)
		? Reason{ Code::kUnknownApp, "Unknown app" }
		: (response.error == TonConnectError::MethodNotSupported)
		? Reason{ Code::kMethodNotSupported, "Method not supported" }
		: Reason{ Code::kUnknown, "Transaction was not sent" };
	_engine->runLocal([
		session = key.session,
		id = requestId.toStdString(),
		boc = response.signedBoc.toStdString(),
		reason = std::move(reason),
		disconnected = response.disconnected
	] {
		return BytesFromEngine(disconnected
			? session->encrypt_disconnect_success(id)
			: boc.empty()
			? session->encrypt_error(id, reason.code, reason.message)
			: session->encrypt_send_success(id, boc));
	}, [=](QByteArray body) {
		if (body.isEmpty()) {
			LOG(("Wallet Error: TON Connect response encrypted "
				"to an empty body."));
			fail();
			return;
		}
		done(std::move(body));
	}, [=](EngineError error) {
		LOG(("Wallet Error: TON Connect response could not be encrypted: %1"
			).arg(error.message));
		fail();
	});
}

void Session::signTonConnectData(
		KeyAuthorization auth,
		TonConnectKey key,
		QString requestId,
		std::shared_ptr<const TonConnectSignData> data,
		QString domain,
		Fn<void(QByteArray)> done,
		Fn<void(TonConnectKeyError)> fail) {
	const auto record = (tonConnectAccess() == TonConnectAccess::Allowed)
		? currentRecord()
		: nullptr;
	if (!record || !key || key.signingKey != _publicKey) {
		fail(TonConnectKeyError::Blocked);
		return;
	} else if (!ReadAuthorized(*this, auth)) {
		fail(TonConnectKeyError::Locked);
		return;
	}
	const auto timestamp = base::unixtime::now();
	if (!data
		|| !data->request
		|| domain.isEmpty()
		|| !TonConnectRequestIdValid(requestId)
		|| timestamp <= 0) {
		LOG(("Wallet Error: TON Connect data signing requested "
			"with unusable input."));
		fail(TonConnectKeyError::Failed);
		return;
	}
	struct Signed {
		QByteArray body;
		QByteArray publicKey;
	};
	const auto address = _address;
	const auto served = _publicKey;
	const auto generation = _networkGeneration;
	const auto recordId = record->recordId;
	const auto lifecycle = _engine->lifecycle();
	const auto session = key.session;
	auto request = engine::TonConnectSignDataSignRequest{
		.descriptor = DescriptorFromRecord(*record),
		.request = *data->request,
		.domain = domain.toStdString(),
		.timestamp = uint64_t(timestamp),
	};
	_engine->runLocal([
		=,
		request = std::move(request),
		id = requestId.toStdString()
	] {
		const auto result = lifecycle->sign_ton_connect_data(request);
		return Signed{
			.body = BytesFromEngine(
				session->encrypt_sign_data_success(id, result)),
			.publicKey = BytesFromEngine(result.public_key),
		};
	}, [=, this, grant = auth.grant](Signed result) {
		const auto now = currentRecord();
		if (generation != _networkGeneration
			|| address != _address
			|| served != _publicKey
			|| !now
			|| now->recordId != recordId) {
			LOG(("Wallet Error: TON Connect data signed "
				"for another wallet state."));
			fail(TonConnectKeyError::Blocked);
			return;
		} else if (result.publicKey != served || result.body.isEmpty()) {
			LOG(("Wallet Error: TON Connect data signed "
				"with an unexpected key."));
			fail(TonConnectKeyError::Failed);
			return;
		}
		done(std::move(result.body));
	}, [=, this, grant = auth.grant](EngineError error) {
		LOG(("Wallet Error: TON Connect data signing failed: %1"
			).arg(LifecycleErrorName(error)));
		noteSecretReadFailure(ProtectedSecretFailure(error), recordId);
		fail(IsVaultLocked(error)
			? TonConnectKeyError::Locked
			: TonConnectKeyError::Failed);
	});
}

} // namespace Wallet
