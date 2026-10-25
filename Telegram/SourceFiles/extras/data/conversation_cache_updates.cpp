#include "extras/data/conversation_cache.h"

#include "data/data_messages.h"
#include "data/data_message_reaction_id.h"
#include "data/data_todo_list.h"

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

MTPMessageMedia applyTodoAction(
		const MTPDmessageMediaToDo &before,
		const MTPMessageAction &action,
		const MTPPeer &actor,
		const MTPint &date) {
	const auto &todo = before.vtodo().data();
	auto tasks = todo.vlist();
	auto completions = field(before.vcompletions());
	if (action.type() == mtpc_messageActionTodoCompletions) {
		const auto &update = action.c_messageActionTodoCompletions();
		const auto erase = [&](int id) {
			completions.v.erase(ranges::remove_if(completions.v, [=](const MTPTodoCompletion &value) {
				return value.data().vid().v == id;
			}), completions.v.end());
		};
		for (const auto &id : update.vcompleted().v) {
			erase(id.v);
			if (ranges::none_of(tasks.v, [=](const MTPTodoItem &task) {
				return task.data().vid().v == id.v;
			})) continue;
			completions.v.push_back(MTP_todoCompletion(id, actor, date));
		}
		for (const auto &id : update.vincompleted().v) erase(id.v);
	} else {
		for (const auto &task : action.c_messageActionTodoAppendTasks().vlist().v) {
			if (tasks.v.size() >= TodoListData::kMaxOptions) break;
			if (ranges::any_of(tasks.v, [&](const MTPTodoItem &existing) {
				return existing.data().vid().v == task.data().vid().v;
			})) continue;
			tasks.v.push_back(task);
		}
	}
	using Flag = MTPDmessageMediaToDo::Flag;
	const auto flags = completions.v.isEmpty()
		? before.vflags().v & ~Flag::f_completions
		: before.vflags().v | Flag::f_completions;
	return MTP_messageMediaToDo(MTP_flags(flags),
		MTP_todoList(todo.vflags(), todo.vtitle(), tasks), completions);
}

std::optional<MTPMessageMedia> applyPollAnswerAction(
		const MTPDmessageMediaPoll &before,
		const MTPMessageAction &action) {
	const auto &poll = before.vpoll().data();
	const auto append = action.type() == mtpc_messageActionPollAppendAnswer;
	const auto answer = append
		? action.c_messageActionPollAppendAnswer().vanswer()
		: action.c_messageActionPollDeleteAnswer().vanswer();
	if (answer.type() != mtpc_pollAnswer) return std::nullopt;
	const auto option = answer.c_pollAnswer().voption().v;
	auto answers = poll.vanswers();
	const auto existing = ranges::find_if(answers.v, [&](const MTPPollAnswer &entry) {
		return entry.type() == mtpc_pollAnswer && entry.c_pollAnswer().voption().v == option;
	});
	if (append) {
		if (existing == answers.v.end()) answers.v.push_back(answer);
	} else if (existing != answers.v.end()) {
		answers.v.erase(existing);
	}
	const auto updated = MTP_poll(poll.vid(), poll.vflags(), poll.vquestion(), answers,
		field(poll.vclose_period()), field(poll.vclose_date()), field(poll.vcountries_iso2()), poll.vhash());
	return MTP_messageMediaPoll(before.vflags(),
		updated, before.vresults(), field(before.vattached_media()));
}

} // namespace

void ConversationCache::updateCachedMessages(
		PeerId peer,
		Fn<bool(MTPMessage &)> update) {
	if (_preloading && (!peer || _preloading->key.peer == peer)) {
		cancelPreload();
		_preloadTimer.callOnce(250);
	}
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

void ConversationCache::extendedMediaUpdated(
		FullMsgId id,
		const QVector<MTPMessageExtendedMedia> &media) {
	updateCachedMessages(id.peer, [=](MTPMessage &message) {
		if (message.type() != mtpc_message || IdFromMessage(message) != id.msg) return false;
		const auto &data = message.c_message();
		const auto before = data.vmedia();
		if (!before) return false;
		const auto merge = [](const MTPMessageExtendedMedia &previous,
				const MTPMessageExtendedMedia &incoming) {
			// 已解锁内容不被后续预览覆盖，文件仍沿原有下载限制处理。
			return previous.type() == mtpc_messageExtendedMedia
				&& incoming.type() == mtpc_messageExtendedMediaPreview ? previous : incoming;
		};
		if (before->type() == mtpc_messageMediaPaidMedia) {
			const auto &paid = before->c_messageMediaPaidMedia();
			auto updated = media;
			for (auto i = 0; i < std::min(updated.size(), paid.vextended_media().v.size()); ++i) {
				updated[i] = merge(paid.vextended_media().v[i], updated[i]);
			}
			message = replaceMessageParts(data, MTP_messageMediaPaidMedia(
				paid.vstars_amount(), MTP_vector<MTPMessageExtendedMedia>(updated)), nullptr);
			return true;
		}
		if (before->type() != mtpc_messageMediaInvoice || media.size() != 1) return false;
		const auto &invoice = before->c_messageMediaInvoice();
		const auto prior = invoice.vextended_media();
		const auto updated = prior ? merge(*prior, media.front()) : media.front();
		message = replaceMessageParts(data, MTP_messageMediaInvoice(
			MTP_flags(invoice.vflags().v | MTPDmessageMediaInvoice::Flag::f_extended_media),
			invoice.vtitle(), invoice.vdescription(), field(invoice.vphoto()),
			field(invoice.vreceipt_msg_id()), invoice.vcurrency(), invoice.vtotal_amount(),
			invoice.vstart_param(), updated), nullptr);
		return true;
	});
}

void ConversationCache::serviceMessageReceived(const MTPDmessageService &service) {
	const auto reply = service.vreply_to();
	if (!reply || reply->type() != mtpc_messageReplyHeader) return;
	const auto target = reply->c_messageReplyHeader().vreply_to_msg_id();
	if (!target) return;
	const auto action = service.vaction();
	const auto type = action.type();
	if (type != mtpc_messageActionTodoCompletions && type != mtpc_messageActionTodoAppendTasks
		&& type != mtpc_messageActionPollAppendAnswer && type != mtpc_messageActionPollDeleteAnswer) return;
	const auto id = target->v;
	const auto actor = field(service.vfrom_id(), service.vpeer_id());
	const auto date = service.vdate();
	updateCachedMessages(peerFromMTP(service.vpeer_id()), [=](MTPMessage &message) {
		if (message.type() != mtpc_message || IdFromMessage(message) != MsgId(id)) return false;
		const auto &data = message.c_message();
		const auto media = data.vmedia();
		if (!media) return false;
		if (media->type() == mtpc_messageMediaToDo) {
			if (type != mtpc_messageActionTodoCompletions
				&& type != mtpc_messageActionTodoAppendTasks) return false;
			message = replaceMessageParts(data,
				applyTodoAction(media->c_messageMediaToDo(), action, actor, date), nullptr);
			return true;
		}
		if (media->type() != mtpc_messageMediaPoll
			|| (type != mtpc_messageActionPollAppendAnswer && type != mtpc_messageActionPollDeleteAnswer)) return false;
		const auto updated = applyPollAnswerAction(media->c_messageMediaPoll(), action);
		if (!updated) return false;
		message = replaceMessageParts(data, *updated, nullptr);
		return true;
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
