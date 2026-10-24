#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"
#include "extras/debug/debug_login.h"
#include "extras/debug/dialogs_preview.h"
#include "extras/debug/simulation_media.h"
#include "extras/debug/simulation_search.h"
#include "extras/debug/simulation_profile.h"
#include "extras/debug/simulation_music.h"
#include "extras/debug/simulation_scenarios.h"
#include "base/unixtime.h"
#include "boxes/stickers_box.h"
#include "data/data_chat_filters.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/stickers/data_stickers.h"
#include "dialogs/dialogs_widget.h"
#include "lang/lang_keys.h"
#include "lang/lang_instance.h"
#include "main/main_session.h"
#include "settings.h"
#include "settings/sections/settings_local_storage.h"
#include "extras/features/secret_chats/secret_chat_manager.h"
#include "extras/ui/secret_chat_box.h"
#include "data/data_user.h"
#include <QtCore/QElapsedTimer>
#include <QtWidgets/QApplication>
#include "wallet/wallet_panel.h"
#include "wallet/wallet_session.h"
#include "ui/emoji_config.h"
#include "ui/widgets/separate_panel.h"
#include "window/window_session_controller.h"

namespace ExtrasDebug::Commands {
using json = nlohmann::json;
namespace {

constexpr auto kWalletBalance = int64(123456789000);
constexpr auto kStickerSetId = uint64(9800000000);
constexpr auto kEmojiSetId = uint64(9800000100);
constexpr auto kEmojiSetCount = 3;

Result simulationSecret(const QStringList &args) {
	auto ok = false;
	const auto count = args.empty() ? 120 : args.front().toInt(&ok);
	if (args.size() > 1 || (!args.empty() && !ok) || count < 1 || count > 2000) {
		return Result::Err(u"usage: simulation.secret [1..2000]"_q);
	}
	const auto session = ActiveSession();
	if (!cDebugProfile() || !session || !isSimulationSession(session)) {
		return Result::Err(u"an isolated simulation profile is required"_q);
	}
	const auto peer = simulationPeer(session, u"private"_q);
	const auto controller = session->tryResolveWindow();
	if (!peer || !peer->isUser() || !controller) {
		return Result::Err(u"simulation peer or window not found"_q);
	}
	const auto chatId = session->secretChats().seedForDebug(peer->id.to<UserId>(), count);
	Extras::SecretChats::showChat(controller, chatId);
	return Result::Ok(Compact(json{ { "chatId", chatId }, { "messages", count } }));
}

Result simulationCacheChats(const QStringList &args) {
	auto ok = false;
	const auto count = args.size() == 1 ? args.front().toInt(&ok) : 0;
	if (!ok || count < 0 || count > 10000) {
		return Result::Err(u"usage: simulation.cache-chats <0..10000>"_q);
	}
	const auto session = ActiveSession();
	if (!cDebugProfile() || !session || !isSimulationSession(session)) {
		return Result::Err(u"an isolated simulation profile is required"_q);
	}
	for (const auto widget : QApplication::allWidgets()) {
		const auto page = dynamic_cast<Settings::LocalStorage*>(widget);
		if (!page || !page->isVisible()) {
			continue;
		}
		auto timer = QElapsedTimer();
		timer.start();
		page->setDebugChats(count);
		return Result::Ok(Compact(json{
			{ "chats", count }, { "elapsedUs", timer.nsecsElapsed() / 1000 },
			{ "widgets", page->findChildren<QWidget*>().size() },
		}));
	}
	return Result::Err(u"open the cache settings page first"_q);
}

Result simulationChatFolders(const QStringList &args) {
	if (args.size() != 1
		|| (args.front() != u"single"_q
			&& args.front() != u"none"_q
			&& args.front() != u"pending"_q)) {
		return Result::Err(u"usage: simulation.chat-folders <single|none|pending>"_q);
	}
	const auto session = ActiveSession();
	if (!cDebugProfile() || !session || !isSimulationSession(session)) {
		return Result::Err(u"an isolated simulation profile is required"_q);
	}
	const auto controller = session->tryResolveWindow();
	if (!controller) {
		return Result::Err(u"no session window"_q);
	}
	auto &filters = session->data().chatsFilters();
	if (args.front() == u"pending"_q) {
		filters.beginLoadForDebug();
		controller->setActiveChatsFilter(0);
		return Result::Ok(u"waiting for chat folders"_q);
	}
	auto result = QVector<MTPDialogFilter>{ MTP_dialogFilterDefault() };
	if (args.front() == u"single"_q) {
		const auto &list = filters.list();
		const auto first = ranges::find_if(list, [](const Data::ChatFilter &filter) {
			return filter.id() != 0;
		});
		if (first == end(list)) {
			return Result::Err(u"restart simulation to restore chat folders"_q);
		}
		result.push_back(first->tl());
	}
	// 从全部对话开始加载分组，覆盖没有标签栏参与初始化的情况。
	controller->setActiveChatsFilter(0);
	filters.setPreloaded(result, filters.tagsEnabled());
	return Result::Ok(Compact(json{
		{ "loaded", filters.loaded() },
		{ "count", filters.list().size() },
		{ "activeId", controller->activeChatsFilterCurrent() },
		{ "defaultId", filters.defaultId() },
	}));
}

Result simulationLanguage(const QStringList &args) {
	if (args.size() != 1 || (args.front() != u"en"_q && args.front() != u"zh-hans"_q)) {
		return Result::Err(u"usage: simulation.language <en|zh-hans>"_q);
	}
	const auto session = ActiveSession();
	if (!cDebugProfile() || !session || !isSimulationSession(session)) {
		return Result::Err(u"an isolated simulation profile is required"_q);
	}
	auto &instance = Lang::GetInstance();
	instance.switchToId({ .id = args.front() });
	// 通过正式语言更新通知加载内置翻译，不请求云端语言包。
	const auto difference = MTP_langPackDifference(
		MTP_string(args.front()),
		MTP_int(0),
		MTP_int(0),
		MTP_vector<MTPLangPackString>(QVector<MTPLangPackString>()));
	instance.applyDifference(Lang::Pack::Current, difference.data());
	return Result::Ok(Compact(json{
		{ "language", instance.id().toStdString() },
		{ "emoji", tr::extras_SelectorTabEmoji(tr::now).toStdString() },
		{ "stickers", tr::extras_SelectorTabStickers(tr::now).toStdString() },
		{ "gifs", tr::extras_SelectorTabGifs(tr::now).toStdString() },
	}));
}

bool seedStickerSets(not_null<Main::Session*> session) {
	auto &stickers = session->data().stickers();
	auto changed = false;
	const auto add = [&](int index, const QString &title,
			Data::StickersSetFlags flags, Data::StickersSetsOrder &order) {
		const auto id = kStickerSetId + index;
		auto &sets = stickers.setsRef();
		auto it = sets.find(id);
		if (it == sets.end()) {
			const auto media = simulationMedia(
				session, SimulationMedia::Sticker, 2800000 + index, id);
			const auto document = session->data().processDocument(
				*media.c_messageMediaDocument().vdocument());
			auto set = std::make_unique<Data::StickersSet>(
				&session->data(), id, 0, 0, title,
				u"simulation_stickers_%1"_q.arg(index), 1, flags,
				(flags & Data::StickersSetFlag::Installed) ? base::unixtime::now() : 0);
			set->stickers.push_back(document.get());
			set->emoji[Ui::Emoji::Find(u"🙂"_q)] = set->stickers;
			it = sets.emplace(id, std::move(set)).first;
			changed = true;
		}
		if ((it->second->flags & flags) != flags) {
			it->second->flags |= flags;
			changed = true;
		}
		if (!order.contains(id)) {
			order.push_back(id);
			changed = true;
		}
	};
	// 样本不带未读标记，切页不触发自动已读，徽标计数由指令单独控制。
	add(0, u"模拟已安装贴纸"_q, Data::StickersSetFlag::Installed,
		stickers.setsOrderRef());
	add(1, u"模拟热门贴纸"_q, Data::StickersSetFlag::Featured,
		stickers.featuredSetsOrderRef());
	add(2, u"模拟归档贴纸"_q, Data::StickersSetFlag::Archived,
		stickers.archivedSetsOrderRef());
	// 本地样本已完整加载，打开面板时不再请求服务端空列表覆盖它们。
	stickers.setLastUpdate(crl::now());
	stickers.setLastFeaturedUpdate(crl::now());
	return changed;
}

Result simulationStickers(const QStringList &args) {
	if (args.isEmpty() || args.size() > 2
		|| (args.size() == 2 && args[1] != u"open"_q)) {
		return Result::Err(u"usage: simulation.stickers <0..9999> [open]"_q);
	}
	auto valid = false;
	const auto count = args.front().toInt(&valid);
	if (!valid || count < 0 || count > 9999) {
		return Result::Err(u"sticker count must be between 0 and 9999"_q);
	}
	const auto session = ActiveSession();
	if (!cDebugProfile() || !session || !isSimulationSession(session)) {
		return Result::Err(u"an isolated simulation profile is required"_q);
	}
	const auto open = (args.size() == 2);
	const auto controller = session->tryResolveWindow();
	if (open && !controller) {
		return Result::Err(u"no session window"_q);
	}
	auto &stickers = session->data().stickers();
	const auto seeded = seedStickerSets(session);
	stickers.setFeaturedSetsUnreadCount(count);
	if (seeded) {
		stickers.notifyUpdated(Data::StickersType::Stickers);
	}
	if (open) {
		auto box = Box<StickersBox>(
			controller->uiShow(), StickersBox::Section::Installed);
		box->setObjectName(u"simulation.stickers"_q);
		controller->show(std::move(box));
	}
	return Result::Ok(Compact(json{
		{ "count", stickers.featuredSetsUnreadCount() },
		{ "seeded", seeded },
		{ "opened", open },
		{ "installedSets", stickers.setsOrder().size() },
		{ "featuredSets", stickers.featuredSetsOrder().size() },
		{ "archivedSets", stickers.archivedSetsOrder().size() },
	}));
}

Result simulationEmojiPacks(const QStringList &args) {
	if (args.size() != 1 || (args.front() != u"on"_q && args.front() != u"off"_q)) {
		return Result::Err(u"usage: simulation.emoji-packs <on|off>"_q);
	}
	const auto session = ActiveSession();
	if (!cDebugProfile() || !session || !isSimulationSession(session)) {
		return Result::Err(u"an isolated simulation profile is required"_q);
	}
	const auto enabled = (args.front() == u"on"_q);
	auto &stickers = session->data().stickers();
	auto &sets = stickers.setsRef();
	auto &order = stickers.emojiSetsOrderRef();
	// 界面刷新前保留移除的样本，避免现有图标引用已销毁的表情包。
	auto removed = std::vector<std::unique_ptr<Data::StickersSet>>();
	auto changed = false;
	for (auto index = 0; index != kEmojiSetCount; ++index) {
		const auto id = kEmojiSetId + index;
		auto it = sets.find(id);
		if (!enabled) {
			changed = (order.removeAll(id) > 0) || changed;
			if (it != sets.end()) {
				removed.push_back(std::move(it->second));
				sets.erase(it);
				changed = true;
			}
			continue;
		}
		if (it == sets.end()) {
			const auto media = simulationMedia(
				session, SimulationMedia::Emoji, 2800100 + index, id);
			const auto document = session->data().processDocument(
				*media.c_messageMediaDocument().vdocument());
			auto set = std::make_unique<Data::StickersSet>(
				&session->data(), id, 0, 0,
				u"模拟表情包 %1"_q.arg(index + 1),
				u"simulation_emoji_%1"_q.arg(index + 1), 1,
				Data::StickersSetFlag::Emoji | Data::StickersSetFlag::Installed,
				base::unixtime::now());
			set->stickers.push_back(document.get());
			it = sets.emplace(id, std::move(set)).first;
			changed = true;
		}
		it->second->flags |= Data::StickersSetFlag::Installed;
		if (!order.contains(id)) {
			order.push_back(id);
			changed = true;
		}
	}
	stickers.setLastEmojiUpdate(crl::now());
	if (changed) {
		stickers.notifyUpdated(Data::StickersType::Emoji);
	}
	return Result::Ok(Compact(json{
		{ "enabled", enabled },
		{ "changed", changed },
		{ "emojiSets", order.size() },
	}));
}

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
		{ u"simulation.secret"_q, &simulationSecret },
		{ u"simulation.profile"_q, &seedProfilePreview },
		{ u"simulation.music"_q, &simulateMusic },
		{ u"simulation.cache-chats"_q, &simulationCacheChats },
		{ u"simulation.chat-folders"_q, &simulationChatFolders },
		{ u"simulation.language"_q, &simulationLanguage },
		{ u"simulation.search"_q, &seedSearchPreview },
		{ u"simulation.stickers"_q, &simulationStickers },
		{ u"simulation.emoji-packs"_q, &simulationEmojiPacks },
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
