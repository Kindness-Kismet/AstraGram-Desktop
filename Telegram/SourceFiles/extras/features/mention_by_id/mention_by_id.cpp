#include "extras/features/mention_by_id/mention_by_id.h"

#include "base/event_filter.h"
#include "chat_helpers/compose/compose_show.h"
#include "chat_helpers/message_field.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/dialogs_key.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "mtproto/sender.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <QtCore/QEvent>
#include <QtGui/QTextDocument>
#include <QtWidgets/QMenu>
#include <QtWidgets/QTextEdit>

namespace ExtrasMentionById {
namespace {

[[nodiscard]] std::optional<UserId> parseUserId(QString text) {
	text = text.trimmed();
	if (text.isEmpty() || !std::ranges::all_of(text, [](QChar ch) {
		return ch >= QChar('0') && ch <= QChar('9');
	})) {
		return std::nullopt;
	}
	auto ok = false;
	const auto value = text.toULongLong(&ok);
	if (!ok || !value || value > PeerId::kChatTypeMask) {
		return std::nullopt;
	}
	return UserId(value);
}

[[nodiscard]] bool canMention(not_null<UserData*> user) {
	return !user->isInaccessible()
		&& user->isLoaded()
		&& (user->isSelf() || user->accessHash());
}

void mentionBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<Ui::InputField*> field) {
	struct State {
		UserData *user = nullptr;
		mtpRequestId requestId = 0;
		bool closed = false;
		rpl::variable<bool> resolved = false;
		rpl::variable<QString> status;
	};
	const auto session = &show->session();
	const auto weakSession = base::make_weak(session);
	const auto weakField = base::make_weak(field);
	const auto controller = show->resolveWindow();
	const auto weakController = base::make_weak(controller);
	const auto chat = controller
		? controller->activeChatCurrent()
		: Dialogs::Key();
	const auto revision = field->document()->revision();
	const auto cursor = field->textCursor();
	const auto range = Ui::InputFieldTextRange{
		cursor.selectionStart(),
		cursor.selectionEnd(),
	};
	const auto state = box->lifetime().make_state<State>();
	const auto api = box->lifetime().make_state<MTP::Sender>(&session->mtp());
	const auto valid = [=] {
		return !state->closed
			&& weakSession
			&& show->valid()
			&& weakField
			&& weakField->isVisible()
			&& !weakField->rawTextEdit()->isReadOnly()
			&& weakField->document()->revision() == revision
			&& (!controller || (weakController
				&& weakController->activeChatCurrent() == chat));
	};
	const auto cancel = [=] {
		api->request(base::take(state->requestId)).cancel();
	};
	box->boxClosing() | rpl::on_next([=] {
		state->closed = true;
		cancel();
	}, box->lifetime());
	session->account().sessionChanges() | rpl::on_next([=] {
		cancel();
		box->closeBox();
	}, box->lifetime());
	if (controller) {
		controller->activeChatChanges() | rpl::on_next([=] {
			box->closeBox();
		}, box->lifetime());
	}
	base::install_event_filter(box, field, [=](not_null<QEvent*> event) {
		if (event->type() == QEvent::Hide) {
			box->closeBox();
		}
		return base::EventFilterResult::Continue;
	});
	QObject::connect(field, &QObject::destroyed, box, [=] {
		box->closeBox();
	});

	box->setTitle(tr::extras_MentionById());
	const auto id = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		Ui::InputField::Mode::SingleLine,
		tr::extras_MentionByIdUserId()));
	id->setObjectName(u"extrasMentionByIdUserId"_q);
	id->setInputMethodHints(Qt::ImhDigitsOnly);
	const auto name = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		Ui::InputField::Mode::SingleLine,
		tr::extras_MentionByIdText()));
	name->setObjectName(u"extrasMentionByIdText"_q);
	state->status = tr::extras_MentionByIdHint(tr::now);
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		state->status.value(),
		st::boxLabel));

	const auto failed = [=](QString message) {
		state->requestId = 0;
		id->setEnabled(true);
		state->status = std::move(message);
		id->showError();
	};
	const auto resolved = [=](not_null<UserData*> user) {
		state->requestId = 0;
		id->setEnabled(true);
		if (!canMention(user)) {
			failed(tr::extras_MentionByIdUnknown(tr::now));
			return;
		}
		state->user = user;
		state->resolved = true;
		if (name->empty()) {
			name->setTextWithTags({ user->name() });
		}
		state->status = tr::extras_MentionByIdResolved(
			tr::now,
			lt_user,
			user->name() + u" ("_q
				+ QString::number(peerToUser(user->id).bare) + ')');
		name->setFocusFast();
	};
	const auto submit = [=] {
		if (!valid()) {
			box->closeBox();
			return;
		}
		if (state->requestId) {
			return;
		}
		if (state->resolved.current()) {
			const auto user = state->user;
			if (!canMention(user)) {
				failed(tr::extras_MentionByIdUnknown(tr::now));
				return;
			}
			const auto text = name->getLastText().trimmed();
			if (text.isEmpty()) {
				name->showError();
				return;
			}
			const auto tag = PrepareMentionTag(user);
			box->closeBox();
			if (const auto input = weakField.get()) {
				input->commitMarkdownTagEdit(range, tag, text);
				input->setFocusFast();
			}
			return;
		}
		const auto userId = parseUserId(id->getLastText());
		if (!userId) {
			failed(tr::extras_MentionByIdInvalid(tr::now));
			return;
		}
		const auto user = session->data().user(*userId);
		if (canMention(user)) {
			resolved(user);
			return;
		}
		// 只有已有消息上下文才允许解析不完整资料，不能猜测访问参数。
		if (user->isLoaded() || user->isInaccessible()
			|| !session->data().messageWithPeer(user->id)) {
			failed(tr::extras_MentionByIdUnknown(tr::now));
			return;
		}
		id->setEnabled(false);
		state->status = tr::extras_MentionByIdLoading(tr::now);
		state->requestId = api->request(MTPusers_GetUsers(
			MTP_vector<MTPInputUser>(1, user->inputUser())
		)).done([=](const MTPVector<MTPUser> &result) {
			state->requestId = 0;
			if (!valid()) {
				box->closeBox();
				return;
			}
			if (result.v.size() != 1 || result.v.front().match([&](
					const MTPDuser &data) {
				return UserId(data.vid()) != *userId;
			}, [](const MTPDuserEmpty &) {
				return true;
			})) {
				failed(tr::extras_MentionByIdUnknown(tr::now));
				return;
			}
			resolved(session->data().processUser(result.v.front()));
		}).fail([=] {
			state->requestId = 0;
			if (!valid()) {
				box->closeBox();
				return;
			}
			failed(tr::extras_MentionByIdFailed(tr::now));
		}).send();
	};
	id->changes() | rpl::on_next([=] {
		if (state->user && name->getLastText() == state->user->name()) {
			name->setTextWithTags({});
		}
		state->user = nullptr;
		state->resolved = false;
		state->status = tr::extras_MentionByIdHint(tr::now);
	}, box->lifetime());
	id->submits() | rpl::on_next(submit, box->lifetime());
	name->submits() | rpl::on_next(submit, box->lifetime());
	box->addButton(state->resolved.value() | rpl::map([](bool resolved) {
		return resolved
			? tr::extras_MentionByIdInsert(tr::now)
			: tr::extras_MentionByIdResolve(tr::now);
	}), submit);
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
	box->setFocusCallback([=] {
		id->setFocusFast();
	});
}

} // namespace

void install(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<Ui::InputField*> field) {
	field->addContextMenuHook([=](Ui::InputField::ContextMenuRequest request) {
		if (field->rawTextEdit()->isReadOnly()) {
			return;
		}
		request.menu->addSeparator();
		const auto action = request.menu->addAction(
			tr::extras_MentionById(tr::now));
		action->setObjectName(u"extrasMentionByIdAction"_q);
		QObject::connect(action, &QAction::triggered, field, [=] {
			show->showBox(Box(mentionBox, show, field));
		});
	});
}

} // namespace ExtrasMentionById
