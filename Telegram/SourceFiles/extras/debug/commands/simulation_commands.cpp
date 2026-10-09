#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"
#include "extras/debug/debug_login.h"
#include "extras/debug/dialogs_preview.h"
#include "extras/debug/simulation_scenarios.h"
#include "base/unixtime.h"
#include "dialogs/dialogs_widget.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings.h"
#include "wallet/wallet_panel.h"
#include "wallet/wallet_session.h"
#include "ui/widgets/separate_panel.h"
#include "window/window_session_controller.h"

namespace ExtrasDebug::Commands {
using json = nlohmann::json;
namespace {

constexpr auto kWalletBalance = int64(123456789000);

void applyWalletLists(Wallet::Session &wallet, bool withCollectibles) {
	const auto address = u"0:"_q + QString(64, QChar('0'));
	const auto peerAddress = u"0:"_q + QString(64, QChar('2'));
	const auto transaction = MTP_walletTransaction(
		MTP_flags(MTPDwalletTransaction::Flag::f_incoming
			| MTPDwalletTransaction::Flag::f_comment),
		MTP_string(QString(64, QChar('1'))),
		MTP_long(12500000000LL),
		MTP_long(0),
		MTP_int(base::unixtime::now()),
		MTP_walletTransactionPeerAddress(
			MTP_flags(0), MTP_string(peerAddress), MTPstring()),
		MTP_string(u"模拟转账记录"_q),
		MTPstring(),
		MTPwallet_NftItem());
	const auto transactions = MTP_wallet_transactions(
		MTP_flags(0),
		MTP_long(kWalletBalance),
		MTP_vector<MTPWalletTransaction>({ transaction }),
		MTPstring(),
		MTP_vector<MTPChat>(),
		MTP_vector<MTPUser>());
	auto items = QVector<MTPwallet_NftItem>();
	if (withCollectibles) {
		items.push_back(MTP_wallet_nftItem(
			MTP_flags(MTPDwallet_nftItem::Flag::f_name),
			MTPstring(),
			MTP_string(u"0:"_q + QString(64, QChar('3'))),
			MTP_string(address),
			MTP_string("1"),
			MTP_string(u"模拟藏品"_q),
			MTPstring(),
			MTPWebDocument(),
			MTPWebDocument(),
			MTPWebDocument(),
			MTPWebDocument(),
			MTP_vector<MTPwallet_NftAttribute>(),
			MTPDataJSON()));
	}
	wallet.debugApplyLists(transactions, MTP_wallet_nftItems(
		MTP_flags(0), MTP_vector<MTPwallet_NftItem>(items), MTPstring()));
}

} // namespace

const HandlerMap &simulationHandlers() {
	static const auto result = HandlerMap{
		{ u"simulation.wallet"_q, [](const QStringList &args) {
			const auto mode = args.isEmpty() ? u"keep"_q : args.front();
			if (args.size() > 1 || (mode != u"keep"_q && mode != u"missing"_q
				&& mode != u"ready"_q && mode != u"collectibles"_q
				&& mode != u"close"_q)) {
				return Result::Err(u"usage: simulation.wallet [keep|missing|ready|collectibles|close]"_q);
			}
			const auto session = ActiveSession();
			if (!cDebugProfile() || !session || !isSimulationSession(session)) {
				return Result::Err(u"an isolated simulation profile is required"_q);
			}
			if (mode == u"close"_q) {
				Wallet::CloseWallet(session);
				return Result::Ok();
			}
			auto &wallet = session->wallet();
			wallet.ensureLoaded();
			if (mode != u"keep"_q) {
				const auto state = (mode == u"missing"_q)
					? MTPWalletState(MTP_walletStateEmpty(MTP_flags(0)))
					: MTPWalletState(MTP_walletState(
						MTP_flags(0),
						MTP_string(u"0:"_q + QString(64, QChar('0'))),
						MTP_bytes(QByteArray(32, char(1))),
						MTP_long(kWalletBalance)));
				wallet.applyUpdate(MTP_updateWalletState(state).c_updateWalletState());
				if (mode != u"missing"_q) {
					applyWalletLists(wallet, mode == u"collectibles"_q);
				}
			}
			Wallet::ShowWallet(session)->setObjectName(u"simulation.wallet"_q);
			return Result::Ok(Compact(json{
				{ "mode", mode.toStdString() },
				{ "historyCount", wallet.history().size() },
				{ "collectibleCount", wallet.collectibles().size() },
				{ "listsGated", wallet.listsGated() },
			}));
		} },
		{ u"simulation.trigger"_q, [](const QStringList &args) {
			if (args.size() > 1) {
				return Result::Err(u"usage: simulation.trigger [list|key]"_q);
			}
			if (args.size() == 1 && args.front() == u"delete-countdown"_q) {
				return triggerSimulationCountdown({});
			}
			const auto entries = dialogsPreviewEntries();
			if (args.empty() || args.front() == u"list"_q) {
				auto result = json::array();
				for (const auto &entry : entries) {
					result.push_back({ { "key", entry.key },
						{ "name", entry.title.toStdString() } });
				}
				result.push_back({ { "key", "delete-countdown" },
					{ "name", tr::extras_SimulationCountdown(tr::now).toStdString() } });
				return Result::Ok(Compact(result));
			}
			const auto session = ActiveSession();
			if (!session || !isSimulationSession(session)) {
				return Result::Err(u"simulation mode is required"_q);
			}
			const auto controller = session->tryResolveWindow();
			const auto dialogs = controller ? dialogsPreviewWidget(controller) : nullptr;
			if (!dialogs) {
				return Result::Err(u"no chat list widget"_q);
			}
			for (const auto &entry : entries) {
				if (args.front() == QLatin1String(entry.key)) {
					dialogs->setListPreview(entry.id);
					return Result::Ok(Compact({ { "key", entry.key } }));
				}
			}
			return Result::Err(u"unknown scene, use simulation.trigger list"_q);
		} },
		{ u"simulation.clear"_q, [](const QStringList &args) {
			const auto session = ActiveSession();
			if (!args.empty() || !session || !isSimulationSession(session)) {
				return Result::Err(u"simulation.clear requires simulation mode and no arguments"_q);
			}
			const auto window = session->tryResolveWindow();
			const auto dialogs = window ? dialogsPreviewWidget(window) : nullptr;
			if (!dialogs) {
				return Result::Err(u"no chat list widget"_q);
			}
			dialogs->setListPreview(DialogsPreview::None);
			return Result::Ok();
		} },
		{ u"simulation.list"_q, &listSimulationScenes },
		{ u"simulation.open"_q, &openSimulationScene },
	};
	return result;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
