#include "extras/data/conversation_cache.h"

#include "data/data_messages.h"
#include "data/data_message_reaction_id.h"

namespace Extras {
namespace {

template <typename Type>
Type field(tl::conditional<Type> value, Type otherwise = Type()) {
	return value ? *value : otherwise;
}

MTPMessage replaceMessageParts(
		const MTPDmessage &data,
		const MTPMessageMedia &media,
		const MTPMessageReactions *reactions) {
	using Flag = MTPDmessage::Flag;
	const auto flags = reactions
		? data.vflags().v | Flag::f_reactions
		: data.vflags().v;
	return MTP_message(MTP_flags(flags), data.vid(),
		field(data.vfrom_id()), field(data.vfrom_boosts_applied()),
		field(data.vfrom_rank()), data.vpeer_id(),
		field(data.vsaved_peer_id()), field(data.vfwd_from()),
		field(data.vvia_bot_id()), field(data.vvia_business_bot_id()),
		field(data.vguestchat_via_from()), field(data.vreply_to()),
		data.vdate(), data.vmessage(), media, field(data.vreply_markup()),
		field(data.ventities()), field(data.vviews()),
		field(data.vforwards()), field(data.vreplies()),
		field(data.vedit_date()), field(data.vpost_author()),
		field(data.vgrouped_id()),
		reactions ? *reactions : field(data.vreactions()),
		field(data.vrestriction_reason()), field(data.vttl_period()),
		field(data.vquick_reply_shortcut_id()), field(data.veffect()),
		field(data.vfactcheck()), field(data.vreport_delivery_until_date()),
		field(data.vpaid_message_stars()), field(data.vsuggested_post()),
		field(data.vschedule_repeat_period()),
		field(data.vsummary_from_language()), field(data.vrich_message()));
}

MTPPollResults mergePollResults(
		const MTPPollResults &previous,
		const MTPPollResults &incoming) {
	const auto &old = previous.data();
	const auto &data = incoming.data();
	using Flag = MTPDpollResults::Flag;
	auto flags = data.vflags().v;
	if (old.vresults()) flags |= Flag::f_results;
	if (old.vtotal_voters()) flags |= Flag::f_total_voters;
	if (old.vrecent_voters()) flags |= Flag::f_recent_voters;
	if (old.vsolution()) flags |= Flag::f_solution;
	if (old.vsolution_media()) flags |= Flag::f_solution_media;
	if (data.is_min() && old.is_can_view_stats()) flags |= Flag::f_can_view_stats;
	flags &= ~Flag::f_min;
	const auto total = field(data.vtotal_voters(), field(old.vtotal_voters()));
	auto answers = field(old.vresults());
	using AnswerFlag = MTPDpollAnswerVoters::Flag;
	if (const auto incoming = data.vresults()) {
		for (const auto &answer : incoming->v) {
			const auto &value = answer.data();
			const auto prior = ranges::find_if(answers.v, [&](const MTPPollAnswerVoters &entry) {
				return entry.data().voption().v == value.voption().v;
			});
			if (prior == answers.v.end()) {
				answers.v.push_back(answer);
				continue;
			}
			const auto &before = prior->data();
			auto answerFlags = value.vflags().v;
			if (data.is_min()) {
				answerFlags = (answerFlags & ~AnswerFlag::f_chosen)
					| (before.vflags().v & AnswerFlag::f_chosen);
			}
			answerFlags |= before.vflags().v & AnswerFlag::f_correct;
			if (before.vvoters()) answerFlags |= AnswerFlag::f_voters;
			*prior = MTP_pollAnswerVoters(MTP_flags(answerFlags), value.voption(),
				field(value.vvoters(), field(before.vvoters())),
				field(value.vrecent_voters(), field(before.vrecent_voters())));
		}
	} else if (!data.is_min() && !total.v) {
		for (auto &answer : answers.v) {
			const auto &value = answer.data();
			answer = MTP_pollAnswerVoters(MTP_flags(value.vflags().v & ~AnswerFlag::f_chosen),
				value.voption(), field(value.vvoters()), field(value.vrecent_voters()));
		}
	}
	return MTP_pollResults(MTP_flags(flags), answers,
		total,
		field(data.vrecent_voters(), field(old.vrecent_voters())),
		field(data.vsolution(), field(old.vsolution())),
		field(data.vsolution_entities(), field(old.vsolution_entities())),
		field(data.vsolution_media(), field(old.vsolution_media())));
}

MTPMessageReactions mergeReactions(
		const MTPMessageReactions *previous,
		const MTPMessageReactions &incoming) {
	const auto &data = incoming.data();
	if (!data.is_min() || !previous) return incoming;
	const auto &old = previous->data();
	auto counts = data.vresults();
	using CountFlag = MTPDreactionCount::Flag;
	for (auto &count : counts.v) {
		const auto &value = count.data();
		for (const auto &before : old.vresults().v) {
			const auto &prior = before.data();
			if (Data::ReactionFromMTP(prior.vreaction())
				!= Data::ReactionFromMTP(value.vreaction())) continue;
			const auto chosen = prior.vchosen_order();
			count = MTP_reactionCount(MTP_flags(chosen
				? value.vflags().v | CountFlag::f_chosen_order
				: value.vflags().v & ~CountFlag::f_chosen_order),
				field(chosen), value.vreaction(), value.vcount());
			break;
		}
	}
	using Flag = MTPDmessageReactions::Flag;
	const auto flags = (data.vflags().v & ~Flag::f_min)
		| (old.vflags().v & (Flag::f_can_see_list | Flag::f_reactions_as_tags));
	// 部分更新沿用本人选择，只更新服务器提供的反应数量。
	return MTP_messageReactions(MTP_flags(flags), counts,
		field(data.vrecent_reactions()), field(data.vtop_reactors()));
}

} // namespace

void ConversationCache::updateCachedMessages(
		PeerId peer,
		Fn<bool(MTPMessage &)> update) {
	if (!_enabled || _restoring) {
		return;
	}
	withIndex([=] {
		auto keys = base::flat_set<HistoryCacheKey>();
		if (peer) {
			for (const auto key : _historyKeys) {
				if (key.peer == peer) keys.emplace(key);
			}
		}
		for (const auto &[key, entry] : _histories) {
			if (!peer || key.peer == peer) keys.emplace(key);
		}
		for (const auto key : keys) {
			// 正在加载的历史按回调顺序合并更新，避免旧快照覆盖新状态。
			loadHistory(key, [=](HistoryEntry &entry) {
				for (auto &[id, message] : entry.messages) {
					if (update(message)) entry.dirty = true;
				}
				if (entry.dirty) scheduleSave();
			});
		}
		for (auto &[id, message] : _lastMessages) {
			if ((!peer || id == peer) && update(message)) scheduleSave();
		}
	});
}

void ConversationCache::reactionsUpdated(
		FullMsgId id,
		const MTPMessageReactions &reactions) {
	updateCachedMessages(id.peer, [=](MTPMessage &message) {
		if (IdFromMessage(message) != id.msg) return false;
		if (message.type() == mtpc_message) {
			const auto &data = message.c_message();
			const auto merged = mergeReactions(data.vreactions(), reactions);
			message = replaceMessageParts(data, field(data.vmedia()), &merged);
			return true;
		}
		if (message.type() == mtpc_messageService) {
			const auto &data = message.c_messageService();
			const auto merged = mergeReactions(data.vreactions(), reactions);
			message = MTP_messageService(
				MTP_flags(data.vflags().v | MTPDmessageService::Flag::f_reactions),
				data.vid(), field(data.vfrom_id()), data.vpeer_id(),
				field(data.vsaved_peer_id()), field(data.vreply_to()),
				data.vdate(), data.vaction(), merged, field(data.vttl_period()));
			return true;
		}
		return false;
	});
}

void ConversationCache::pollUpdated(const MTPDupdateMessagePoll &update) {
	const auto pollId = update.vpoll_id().v;
	const auto poll = field(update.vpoll());
	const auto hasPoll = bool(update.vpoll());
	const auto results = update.vresults();
	updateCachedMessages({}, [=](MTPMessage &message) {
		if (message.type() != mtpc_message) return false;
		const auto &data = message.c_message();
		const auto media = data.vmedia();
		if (!media || media->type() != mtpc_messageMediaPoll) return false;
		const auto &before = media->c_messageMediaPoll();
		if (before.vpoll().data().vid().v != pollId) return false;
		const auto updated = MTP_messageMediaPoll(before.vflags(),
			hasPoll ? poll : before.vpoll(), mergePollResults(before.vresults(), results),
			field(before.vattached_media()));
		message = replaceMessageParts(data, updated, nullptr);
		return true;
	});
}

void ConversationCache::webPageUpdated(const MTPWebPage &page) {
	if (page.type() == mtpc_webPageNotModified) return;
	const auto pageId = page.match([](const MTPDwebPageNotModified &) {
		return uint64();
	}, [](const auto &data) { return data.vid().v; });
	updateCachedMessages({}, [=](MTPMessage &message) {
		if (message.type() != mtpc_message) return false;
		const auto &data = message.c_message();
		const auto media = data.vmedia();
		if (!media || media->type() != mtpc_messageMediaWebPage) return false;
		const auto &before = media->c_messageMediaWebPage();
		const auto beforeId = before.vwebpage().match([](const MTPDwebPageNotModified &) {
			return uint64();
		}, [](const auto &data) {
			return data.vid().v;
		});
		if (beforeId != pageId) return false;
		message = replaceMessageParts(data,
			MTP_messageMediaWebPage(before.vflags(), page), nullptr);
		return true;
	});
}

} // namespace Extras
