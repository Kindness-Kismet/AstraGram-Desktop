#pragma once

#include "base/weak_ptr.h"
#include "data/data_msg_id.h"
#include "mtproto/sender.h"
#include "ui/text/text_entity.h"

class HistoryItem;

namespace Main { class Session; }
namespace Iv { struct RichPage; }
namespace Ui { class PopupMenu; class TranslateProvider; }
namespace Window { class SessionController; }

namespace Extras::Translator {

class MessageTranslationManager final : public base::has_weak_ptr {
public:
	explicit MessageTranslationManager(not_null<Main::Session*> session);
	~MessageTranslationManager();

	void translate(not_null<HistoryItem*> item, Fn<void(QString)> reportError);
	void showOriginal(not_null<HistoryItem*> item);

private:
	struct Pending {
		uint64 token = 0;
		mtpRequestId requestId = 0;
		TextWithEntities original;
		std::shared_ptr<const Iv::RichPage> page;
		Fn<void(QString)> reportError;
	};

	void finish(
		FullMsgId id,
		uint64 token,
		TextWithEntities text,
		std::shared_ptr<const Iv::RichPage> page,
		QString error);
	void resetProvider();

	const not_null<Main::Session*> _session;
	std::unique_ptr<Ui::TranslateProvider> _provider;
	MTP::Sender _api;
	base::flat_map<FullMsgId, Pending> _pending;
	base::flat_set<FullMsgId> _manual;
	uint64 _nextToken = 0;
	rpl::lifetime _lifetime;
};

void addMessageTranslationActions(
	not_null<Ui::PopupMenu*> menu,
	not_null<HistoryItem*> item,
	not_null<Window::SessionController*> controller);

} // namespace Extras::Translator
