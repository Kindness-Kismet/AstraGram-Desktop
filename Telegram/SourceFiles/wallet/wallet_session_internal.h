#pragma once

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_session.h"

#include "apiwrap.h"
#include "base/call_delayed.h"
#include "base/platform/base_platform_info.h"
#include "base/openssl_help.h"
#include "base/random.h"
#include "base/unixtime.h"
#include "core/version.h"
#include "data/components/recent_money_recipients.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "gram/api/gram_api_account.h"
#include "gram/api/gram_api_emulate.h"
#include "gram/gram_boc.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mtproto/mtproto_response.h"
#include "storage/storage_domain.h"
#include "tde2e/tde2e_api.h"
#include "ui/widgets/separate_panel.h"
#include "wallet/wallet_engine.h"
#include "wallet/wallet_key_protection.h"
#include "wallet/wallet_onramp.h"
#include "wallet/wallet_phrase_shares.h"
#include "wallet/wallet_rates.h"
#include "wallet/wallet_ton_connect.h"
#include "wallet/wallet_ton_connect_emulation.h"
#include "wallet/wallet_transfer_messages.h"
#include "wallet/wallet_unlock.h"
#include "wallet/wallet_user_addresses.h"
#include "wallet/wallet_vault.h"

#include "wallet_engine.hpp"

#include <QtCore/QUuid>

#include <atomic>
#include <cmath>
#include <deque>
#include <limits>

namespace Wallet {

struct ShareFetch {
	TdE2E::TemporaryKeyPair keys;
	std::vector<QByteArray> shares;
	std::vector<mtpRequestId> requests;
	std::vector<MTP::ShiftedDcId> sessions;
	std::vector<int> dcs;
	Fn<void(const QString &)> fail;
	mtpRequestId exportRequestId = 0;
	crl::time startedAt = 0;
	int pending = 0;
};

struct CommentScope::State {
	const Session *session = nullptr;
	TransferItem target;
	QByteArray body;
	QString sender;
	std::shared_ptr<VaultRuntime> vault;
	std::optional<CustodyRecord> record;
	Fn<void()> cancelPending;
	int generation = 0;
	quint32 epoch = 0;
	std::atomic<bool> cancelled = false;
};

struct Session::HistoryRequest {
	std::optional<TransferWalletIdentity> identity;
	uint64 identityRevision = 0;
	int generation = 0;
	// The cursor this flight spent. The answer is compared against it to
	// catch a server that hands back the offset it was given, so the value
	// has to survive the round trip with the request that sent it and not
	// be re-read from _historyNextOffset, which a landing page may already
	// have moved.
	QString offset;
	mtpRequestId id = 0;
	std::vector<Fn<void()>> done;
};

struct Session::CollectiblesRequest {
	std::optional<TransferWalletIdentity> identity;
	uint64 identityRevision = 0;
	int generation = 0;
	QString offset;
	bool more = false;
	mtpRequestId id = 0;
};

struct Session::SubmittedTransfer {
	std::string operationId;
	TransferWalletIdentity identity;
	std::weak_ptr<wallet_engine::WalletClient> client;
	std::unique_ptr<TransferItem> fallback;
	std::unique_ptr<TransferItem> item;
	QString canonicalId;
	QString collectible;
	QByteArray confirmedHash;
	std::optional<TransferReceipt> receipt;
	std::optional<wallet_engine::SendPhase> terminal;
	int generation = 0;
	int lookupAttempts = 0;
	bool lookupStopped = false;
	bool paired = false;
	bool leaving = false;
};

struct Session::SubmittedLookup {
	std::string operationId;
	TransferWalletIdentity identity;
	QByteArray messageHash;
	int generation = 0;
	mtpRequestId id = 0;
};

struct Session::PreparedRotation {
	std::vector<QString> words;
	QByteArray newPublicKey;
	std::string signedBoc;
	uint32 seqno = 0;
	uint64 validUntil = 0;
	int64 quotedFeeNano = 0;
};

struct PreparedSend {
	SendArgs args;
	TransferWalletIdentity identity;
	GaslessTerms terms;
	std::shared_ptr<const wallet_engine::SendIntent> intent;
	std::shared_ptr<const wallet_engine::NftTransferIntent> nft;
	std::string operationId;
	int64 feeNano = 0;
	uint64 owner = 0;
	uint64 revision = 0;
	int generation = 0;
	std::optional<quint32> privateEpoch;
	std::shared_ptr<wallet_engine::WalletClient> client;
	bool tonConnect = false;
};

struct Session::PreviewRequest {
	std::optional<TransferWalletIdentity> identity;
	GaslessTerms terms;
	uint64 owner = 0;
	uint64 revision = 0;
	int generation = 0;
	std::optional<quint32> privateEpoch;
	std::shared_ptr<wallet_engine::WalletClient> client;
	KeyAuthorization auth;
	SendArgs args;
	std::string operationId;
	Fn<void(FeeResult)> done;
	std::shared_ptr<const wallet_engine::SendRequest> tonConnect;
	bool feeOnly = false;
};

struct Session::PreviewState : base::has_weak_ptr {
	struct Flight {
		enum class Stage {
			Encrypting,
			Previewing,
		};

		uint64 id = 0;
		PreviewRequest request;
		std::shared_ptr<const wallet_engine::SendIntent> intent;
		std::shared_ptr<const wallet_engine::NftTransferIntent> nft;
		FeeResult result;
		Stage stage = Stage::Encrypting;
		bool finished = false;
		bool cancelIssued = false;
		bool cancelFinished = false;
	};

	base::flat_map<uint64, uint64> owners;
	std::deque<PreviewRequest> queue;
	std::optional<Flight> active;
	uint64 lastOwner = 0;
	uint64 lastFlight = 0;
	bool dispatching = false;
};

namespace SessionDetails {

namespace engine = wallet_engine;

struct ResetClientCompletion {
	~ResetClientCompletion();

	Fn<void()> done;
};

constexpr auto kPollInterval = 5 * crl::time(1000);

constexpr auto kCollectiblesPollInterval = 60 * crl::time(1000);

// The server reads the balance from toncenter and caches it for about
// thirty seconds, so two reads inside one such window return the same
// answer. This floor is twice that window: whatever jitter the poll tick,
// a stream hint and a pushed update add between two requests, the later
// one can never land inside the cache window the earlier one filled.
constexpr auto kStateRefreshInterval = 60 * crl::time(1000);

constexpr auto kShareFetchTimeout = 60 * crl::time(1000);

// A wallet.getState that fails for anything but WALLET_UNAVAILABLE leaves the
// presence at Unknown, which is also what "the first request has not answered
// yet" reads as, so the cold-open gate would otherwise stay closed on an
// unlabelled indicator for as long as the server keeps failing. After this
// many consecutive failed attempts the lane stops claiming to be loading and
// settles to a stated face; the 60-second floor keeps retrying underneath and
// one applied state clears the latch again.
constexpr auto kStateFailuresBeforeStated = 2;

// The largest limit wallet.getTransactions documents.
constexpr auto kTransactionsPerPage = 50;

// How many wallet.getTransactions pages in a row may answer with nothing
// the feed can show before the next one is refused. At the documented page
// limit that is 1000 transactions per chain. Every `more` request spends
// from it, whether the reader scrolled for it or the hidden-page
// continuation volunteered it, so the two pagers share one bound, and only
// a page carrying a visible row refills it. A head page that shows nothing
// must not: a transfer push asks for one, so refilling there would restart
// the whole chain per push on exactly the wallets this feature exists for.
// What re-arms the bound instead is a key change or a presence transition
// (both go through clearHistory()), closing the panel, coming back to the
// transactions tab, a changed threshold, a scroll the reader moved down,
// and - because a feed with nothing to show has nothing to scroll either -
// pollTick(), once the history staleness floor has passed with the walk
// quiet. So nothing the server sends restarts the chain by itself, the
// walk is paced by this client's clock, and no eligible row is walled off.
constexpr auto kMaxHiddenPagesInRow = 20;

constexpr auto kForcedCollectiblesInterval = 10 * crl::time(1000);

constexpr auto kCollectibleTransferAttachedNanos = int64(50'000'000);

constexpr auto kCollectibleTransferForwardNanos = int64(1);

constexpr auto kCollectibleFollowUpRefreshes = 6;

// The largest limit wallet.getNfts accepts.
constexpr auto kCollectiblesPerPage = 20;

constexpr auto kStreamResyncInterval = 30 * crl::time(1000);

constexpr auto kClientSendValiditySeconds = uint64(300);

constexpr auto kClientResolutionMarginSeconds = uint64(60);

constexpr auto kClientRequestTimeoutMs = uint64(15000);

// Clock error only: unixtime leads the server by under 3 s, plus rounding.
constexpr auto kSendingMatchSkew = TimeId(5);

constexpr auto kPreviewClientRecordId = "public-key-only";

constexpr auto kDecryptBusyRetries = 5;

constexpr auto kDecryptBusyRetryDelay = crl::time(500);

constexpr auto kCommentRecipientRetries = 3;

constexpr auto kCommentRecipientRetryDelay = crl::time(1000);

constexpr auto kGaslessRefreshInterval = crl::time(60 * 1000);

constexpr auto kGaslessRefreshAhead = crl::time(10 * 1000);

constexpr auto kGaslessRetryInterval = crl::time(15 * 1000);

// The largest individual data field wallet.sendTransfer allows, inclusive.
constexpr auto kTransferDataMaxBytes = 16 * 1024;

constexpr auto kTonConnectOperationIdMaxBytes = 256;

// The lane follows a submitted message for as long as the engine can
// still see the message accepted (validity plus the resolution
// margin), one attempt per tick.
constexpr auto kSubmittedLookupAttempts = int(
	(kClientSendValiditySeconds + kClientResolutionMarginSeconds)
	* 1000
	/ uint64(kPollInterval));

// The throw-away rotation a quote emulates leaves the device with random
// bytes where its signature was, so the contract can never execute what the
// emulator was shown: the emulation request asks for signature checks to be
// skipped, so the fee it reports is still the fee of the real rotation. The
// expiration is the shortest window that comfortably outlives one emulation
// round trip (the Wallet::Api deadline plus queueing); the fresh prepare
// keeps the engine's own send validity.
constexpr auto kRotationQuoteValiditySeconds = uint64(120);

constexpr auto kOwnershipProofSignatureSize = 64;

constexpr auto kTonConnectChallengeAnswerSize = 32;

[[nodiscard]] int64 MinNanosFromConfig(
		float64 configured,
		int64 fallback);

[[nodiscard]] int64 GaslessMinNanos(not_null<Main::Session*> session);

[[nodiscard]] GaslessInfo GaslessInfoFromServer(
		const MTPDupdateWalletGaslessInfo &data);

[[nodiscard]] std::optional<int64> DecimalInt64(const std::string &value);

[[nodiscard]] std::optional<uint64> DecimalUint64(
		const std::string &value);

void FinishHistoryWaiters(std::vector<Fn<void()>> callbacks);

[[nodiscard]] bool SameHistory(
		const std::vector<TransferItem> &was,
		const std::vector<TransferItem> &now);

[[nodiscard]] base::flat_set<QString> HistoryNamedIds(
		const std::vector<TransferItem> &list);

struct MergedHead {
	std::vector<TransferItem> list;
	int retained = 0;
	bool namedLast = false;
};

[[nodiscard]] MergedHead MergedHeadHistory(
		const std::vector<TransferItem> &was,
		std::vector<TransferItem> &&head);

[[nodiscard]] std::vector<TransferItem> UnheldHistory(
		const std::vector<TransferItem> &was,
		std::vector<TransferItem> &&page);

[[nodiscard]] std::vector<TransferItem> ArrivedCollectibles(
		const std::vector<TransferItem> &was,
		const std::vector<TransferItem> &head);

[[nodiscard]] std::vector<Gram::NftItem> UnheldCollectibles(
		const std::vector<Gram::NftItem> &was,
		std::vector<Gram::NftItem> &&page);

[[nodiscard]] bool SameCollectibles(
		const std::vector<Gram::NftItem> &was,
		const std::vector<Gram::NftItem> &now);

[[nodiscard]] std::string NewRecordId();

[[nodiscard]] CustodyRecord RecordFromDescriptor(
		const engine::WalletDescriptor &descriptor);

[[nodiscard]] auto EngineKey(const QByteArray &key)
-> std::optional<std::vector<uint8_t>>;

[[nodiscard]] std::vector<uint8_t> EngineBytes(const QByteArray &bytes);

[[nodiscard]] QByteArray BytesFromEngine(const std::vector<uint8_t> &bytes);

[[nodiscard]] engine::WalletDescriptor DescriptorFromRecord(
		const CustodyRecord &record);

[[nodiscard]] bool RecordParked(
		const CustodyRecord &record,
		const QString &canonicalAddress,
		const QByteArray &servedKey);

[[nodiscard]] QString CheckableParkedAddress(
		const CustodyRecord &record,
		const QString &servedAddress);

[[nodiscard]] QString LogKey(const QByteArray &key);

[[nodiscard]] QString AwaitingKeyRefusal(
		const CustodyStore &store,
		const PhraseIdentity &identity,
		const QString &outdated,
		const QString &changing);

[[nodiscard]] QString LogWalletState(const MTPWalletState &state);

[[nodiscard]] QString SendErrorName(SendError error);

[[nodiscard]] Fn<void(const QString &)> LoggedFail(
		const QString &stage,
		Fn<void(const QString &)> fail);

[[nodiscard]] Fn<void(FeeResult)> LoggedFeeDone(Fn<void(FeeResult)> done);

struct Restored {
	engine::WalletDescriptor descriptor;
	std::vector<QString> words;
};

struct ThrowawayRotation {
	QString signedBoc;
};

[[nodiscard]] uint64 RotationValidUntil(uint64 seconds);

[[nodiscard]] engine::PrepareKeyRotationRequest RotationRequest(
		uint64 seconds);

[[nodiscard]] std::vector<QString> SplitWords(const QString &phrase);

[[nodiscard]] QString NormalizeWord(const QString &word);

[[nodiscard]] std::optional<QByteArray> RotationMnemonicKey(
		const QStringList &words);

[[nodiscard]] std::optional<PhraseIdentity> DerivePhraseIdentity(
		const QStringList &normalized);

[[nodiscard]] bool AnchorDerivesOtherAddress(
		const std::shared_ptr<engine::WalletLifecycle> &lifecycle,
		const QByteArray &anchor,
		const QString &address);

[[nodiscard]] const std::vector<QString> &Wordlist();

[[nodiscard]] QString LifecycleErrorName(const EngineError &error);

[[nodiscard]] bool IsVaultLocked(const EngineError &error);

[[nodiscard]] QString LifecycleErrorDetails(const EngineError &error);

[[nodiscard]] bool ReadAuthorized(
		Session &session,
		const KeyAuthorization &auth);

[[nodiscard]] bool ValidCommentPayloadSize(int size);

[[nodiscard]] QByteArray ServerCommentBody(const QByteArray &payload);

[[nodiscard]] QByteArray EncryptedCommentFeeBody(const QString &text);

[[nodiscard]] bool ValidEncryptedCommentBody(const QByteArray &encoded);

[[nodiscard]] QByteArray EncryptedCommentBody(const TransferItem &item);

[[nodiscard]] KeyAuthorization TrackCommentInstallation(
		KeyAuthorization auth,
		const std::shared_ptr<KeyAuthorization> &installed);

struct DecryptedComment {
	SecureBytes text;
	CommentDecryptError error = CommentDecryptError::None;
	SecretReadFailure secret = SecretReadFailure::None;
};

[[nodiscard]] DecryptedComment DecryptCommentBody(
		const std::shared_ptr<engine::WalletClient> &client,
		const engine::DecryptCommentRequest &request);

[[nodiscard]] QString ClientErrorName(std::exception_ptr error);

[[nodiscard]] engine::WalletClientConfig ClientConfigFromRecord(
		const CustodyRecord &record);

[[nodiscard]] engine::WalletClientConfig ClientConfigForPreview(
		const TransferWalletIdentity &identity);

[[nodiscard]] std::optional<std::vector<int>> ParseHolderDcs(
		const MTPDwallet_secretPhraseParts &data);

[[nodiscard]] std::optional<std::vector<QByteArray>> ParseBackupHolderKeys(
		const QVector<MTPwallet_HolderDc> &list);

[[nodiscard]] std::optional<std::vector<QByteArray>> SealBackupParts(
		const std::vector<QByteArray> &holderKeys,
		const std::vector<QString> &words);

void FinishShareFetch(
		MTP::Sender &api,
		base::Timer &deadline,
		const std::shared_ptr<ShareFetch> &state);

void FailShareFetch(
		MTP::Sender &api,
		base::Timer &deadline,
		const std::shared_ptr<ShareFetch> &state,
		const QString &error);

[[nodiscard]] bool OpenSharePart(
		const std::shared_ptr<ShareFetch> &state,
		int index,
		const QByteArray &data);

[[nodiscard]] QString TransferTerminalCode(TransferTerminal terminal);

[[nodiscard]] SendError SendErrorFrom(const EngineError &error);

[[nodiscard]] bool IsInsufficientForFees(const EngineError &error);

[[nodiscard]] bool IsSubmissionUnknown(const EngineError &error);

[[nodiscard]] std::optional<SendError> DefiniteTransferRefusal(
		const MTP::Error &error);

[[nodiscard]] MTPInputUser TransferRecipientInput(
		not_null<Main::Session*> session,
		UserId id);

[[nodiscard]] QString RotationErrorToken(const EngineError &error);

[[nodiscard]] engine::SendIntent IntentFromArgs(
		const SendArgs &args,
		engine::SendMessageBody body);

[[nodiscard]] engine::NftTransferIntent CollectibleTransferIntent(
		const SendArgs &args);

[[nodiscard]] std::optional<TonConnectTransfer> TonConnectTransferFromEngine(
		const engine::SendRequest &request);

[[nodiscard]] std::vector<TonConnectSignDataField> DecodedCellFields(
		engine::TonConnectDerivedSession &session,
		const std::string &schema,
		const std::string &cell);

[[nodiscard]] TonConnectSignData TonConnectSignDataFromEngine(
		engine::TonConnectDerivedSession &session,
		const engine::TonConnectSignDataRequest &request);

[[nodiscard]] int TonConnectProtocolCode(
		engine::TonConnectRpcErrorCode code);

[[nodiscard]] TonConnectAppRequest TonConnectAppRequestFromEngine(
		engine::TonConnectDerivedSession &session,
		engine::TonConnectDerivedRequest derived);

[[nodiscard]] bool TerminalSendPhase(engine::SendPhase phase);

[[nodiscard]] TransferTerminal StoredTransferTerminal(
		engine::SendPhase phase);

[[nodiscard]] std::optional<engine::SendPhase> RestoredTransferTerminal(
		TransferTerminal terminal);

[[nodiscard]] std::optional<TimeId> OldestHistoryDate(
		const std::vector<TransferItem> &history);

[[nodiscard]] bool StaleSubmittedRecord(
		const SubmittedTransferRecord &record,
		TimeId now);

[[nodiscard]] bool FailedTransferTerminal(TransferTerminal terminal);

[[nodiscard]] engine::SendPhase PairedSendPhase(
		engine::SendPhase phase,
		bool paired);

[[nodiscard]] TonConnectSendResult TonConnectSendOutcome(
		const engine::SendResult &result,
		const std::string &operationId,
		bool rpcStarted,
		const QByteArray &normal);

[[nodiscard]] SubmittedTransferProjection StoredTransferProjection(
		const TransferItem &item);

void KeepSubmittedRecipient(
		TransferItem &item,
		const SubmittedTransferRecord &record);

[[nodiscard]] std::optional<Gram::NftWebDocument> WebDocumentFromServer(
		const tl::conditional<MTPWebDocument> &document);

[[nodiscard]] std::optional<Gram::NftItem> CollectibleFromServer(
		const MTPwallet_NftItem &item);

void SetDirectedAmount(
		TransferItem &item,
		int64 nanograms,
		bool incoming);

[[nodiscard]] std::optional<TransferItem> HistoryItemFromEngine(
		const engine::ActivityItem &item,
		const std::optional<TransferWalletIdentity> &identity);

[[nodiscard]] TransferItem HistoryItemFromServer(
		const MTPWalletTransaction &item,
		const std::optional<TransferWalletIdentity> &identity);

[[nodiscard]] std::optional<TransferReceipt> ReceiptFromServer(
		const MTPDupdateSentWalletTransaction &data);

[[nodiscard]] const MTPDupdateSentWalletTransaction *SentUpdateFromServer(
		const MTPUpdates &updates);

} // namespace SessionDetails

QByteArray TransactionHashFromServer(const QString &value);

bool EncryptedCommentPending(const TransferItem &item);

bool EncryptedCommentUnusable(const TransferItem &item);

bool EncryptedCommentRevealable(const TransferItem &item);

QByteArray DecodeServerEncryptedComment(const QString &encoded);

std::vector<TransferItem> HistoryFromServer(
		const QVector<MTPWalletTransaction> &list,
		std::optional<TransferWalletIdentity> identity);

std::vector<Gram::NftItem> CollectiblesFromServer(
		const QVector<MTPwallet_NftItem> &list);

bool IsWordlistWord(const QString &word);

std::vector<QString> WordlistSuggestions(
		const QString &prefix,
		int limit);

PhraseMatch DetectPhraseMatch(const std::vector<QString> &words);

WalletLoss WalletLossOnLogout(not_null<Main::Account*> account);

QString WalletLossWarning(WalletLoss loss);























TransferItem ItemFromPending(const PendingSendInfo &pending);

int SendCommentBytes(const QString &text);

bool SendCommentFits(const QString &text);

int64 CollectibleTransferAttachedNanos();

int64 TransferMinNanosFromConfig(float64 configured);

int64 TransferMinNanos(not_null<Main::Session*> session);

bool TransferMagnitudeBelowMinimum(int64 amountNano, int64 minNanos);

bool TransferAmountBelowMinimum(int64 amountNano, int64 minNanos);

bool HistoryTransferHidden(const TransferItem &item, int64 minNanos);



} // namespace Wallet
