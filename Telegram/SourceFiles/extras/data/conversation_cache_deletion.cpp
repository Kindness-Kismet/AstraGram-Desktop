#include "extras/data/conversation_cache.h"

#include "data/data_messages.h"
#include "main/main_session.h"

namespace Extras {
namespace {

PeerId messageSender(const MTPMessage &message) {
	return message.match([](const MTPDmessageEmpty &) {
		return PeerId();
	}, [](const auto &data) {
		const auto from = data.vfrom_id();
		return peerFromMTP(from ? *from : data.vpeer_id());
	});
}

struct ParticipantMessages {
	base::flat_set<MsgId> ids;
	int pending = 1;
	bool cancelled = false;
};

} // namespace

void ConversationCache::collectMessagesFromParticipant(
		PeerId peer,
		PeerId from,
		Fn<void(QVector<MTPint>)> done) {
	Expects(peer != PeerId());
	Expects(from != PeerId());

	if (!_enabled || _restoring) {
		done({});
		return;
	}
	const auto state = std::make_shared<ParticipantMessages>();
	const auto requestId = ++_deletionRequestId;
	_deletionCallbacks.emplace(requestId, [=] {
		auto result = QVector<MTPint>();
		if (!state->cancelled && _enabled) {
			result.reserve(state->ids.size());
			for (const auto id : state->ids) {
				result.push_back(MTP_int(id.bare));
			}
		}
		done(std::move(result));
	});
	withIndex([=] {
		if (!_deletionCallbacks.contains(requestId)) {
			return;
		}
		const auto complete = [=] {
			if (auto callback = _deletionCallbacks.take(requestId)) {
				(*callback)();
			}
		};
		if (!_enabled || _restoring) {
			state->cancelled = true;
			complete();
			return;
		}
		const auto collect = [=](const MTPMessage &message) {
			const auto id = IdFromMessage(message);
			if (IsServerMsgId(id) && messageSender(message) == from) {
				state->ids.emplace(id);
			}
		};
		if (const auto last = _lastMessages.find(peer); last != end(_lastMessages)) {
			collect(last->second);
		}
		auto keys = base::flat_set<HistoryCacheKey>();
		for (const auto key : _historyKeys) {
			if (key.peer == peer) {
				keys.emplace(key);
			}
		}
		for (const auto &[key, entry] : _histories) {
			if (key.peer == peer) {
				keys.emplace(key);
			}
		}
		const auto oneDone = [=] {
			if (!--state->pending) {
				complete();
			}
		};
		for (const auto key : keys) {
			++state->pending;
			loadHistory(key, [=](HistoryEntry &entry) {
				for (const auto &[id, message] : entry.messages) {
					collect(message);
				}
				crl::on_main(_session, oneDone);
			}, [=] {
				state->cancelled = true;
				oneDone();
			});
		}
		oneDone();
	});
}

} // namespace Extras
