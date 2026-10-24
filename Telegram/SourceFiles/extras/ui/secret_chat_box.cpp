#include "extras/ui/secret_chat_box.h"

#include "extras/features/secret_chats/secret_chat_manager.h"
#include "extras/features/secret_chats/secret_chat_dialog.h"
#include "history/history.h"
#include "boxes/delete_messages_box.h"
#include "core/application.h"
#include "core/core_screenshot_protection.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "settings/settings_common.h"
#include "crl/crl_on_main.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/text/format_values.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtGui/QClipboard>
#include <QtWidgets/QApplication>
#include <atomic>

namespace Extras::SecretChats {
namespace {

struct Context {
	QPointer<Ui::RpWidget> _widget;
	QPointer<Ui::GenericBox> _box;
	base::weak_ptr<Window::SessionController> _controller;
	base::weak_ptr<Main::Session> _session;
	base::weak_ptr<Manager> _manager;
	std::shared_ptr<std::atomic<bool>> _closed;

	[[nodiscard]] bool valid() const {
		return _widget && !_closed->load()
			&& _controller && _session && _manager
			&& &_controller->session() == _session.get();
	}
};

[[nodiscard]] Context bindWidget(
		not_null<Ui::RpWidget*> widget,
		not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	auto result = Context{
		._widget = widget.get(),
		._controller = controller,
		._session = session,
		._manager = &session->secretChats(),
		._closed = std::make_shared<std::atomic<bool>>(false),
	};
	widget->lifetime().add([closed = result._closed] { closed->store(true); });
	return result;
}

[[nodiscard]] Context bindBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	auto result = bindWidget(box, controller);
	result._box = box.get();
	const auto close = [weakBox = result._box] {
		if (weakBox) {
			weakBox->closeBox();
		}
	};
	controller->lifetime().add(close);
	controller->session().lifetime().add(close);
	box->boxClosing() | rpl::on_next([closed = result._closed] {
		closed->store(true);
	}, box->lifetime());
	box->lifetime().add([closed = result._closed] { closed->store(true); });
	box->setWidth(st::boxWideWidth);
	return result;
}

[[nodiscard]] QString timerText(int ttl) {
	return ttl ? Ui::FormatMuteFor(ttl) : tr::extras_SecretChatTimerOff(tr::now);
}

[[nodiscard]] bool activeChat(const Context &context, int chatId) {
	const auto chat = context.valid() && context._manager->available()
		? context._manager->chat(chatId)
		: std::nullopt;
	return chat && chat->state == State::Active;
}

void reportError(const Context &context, const QString &error) {
	if (context.valid() && !error.isEmpty()) {
		context._controller->showToast(tr::extras_SecretChatFailed(tr::now));
	}
}

not_null<Ui::FlatLabel*> addLabel(
		not_null<Ui::VerticalLayout*> content,
		QString text,
		bool selectable = false) {
	const auto label = content->add(object_ptr<Ui::FlatLabel>(
		content, std::move(text), st::boxLabel), st::boxRowPadding);
	label->setSelectable(selectable);
	label->setBreakEverywhere(true);
	if (selectable) {
		label->setContextCopyText(tr::lng_context_copy_text(tr::now));
	} else {
		label->setContextMenuPolicy(Qt::NoContextMenu);
		label->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
	return label;
}

not_null<Ui::SettingsButton*> addAction(
		not_null<Ui::VerticalLayout*> content,
		rpl::producer<QString> text,
		Fn<void()> callback,
		QString name = {}) {
	const auto button = Settings::AddButtonWithIcon(
		content, std::move(text), st::settingsButtonNoIcon);
	button->addClickHandler(std::move(callback));
	button->setObjectName(std::move(name));
	return button;
}

void watchChat(const Context &context, int chatId) {
	context._manager->changed() | rpl::on_next([=] {
		if (!activeChat(context, chatId) && context._box) {
			context._box->closeBox();
		}
	}, context._box->lifetime());
}

void confirmHistoryAction(const Context &context, int chatId, bool justClear) {
	if (!context.valid()) {
		return;
	}
	const auto dialog = context._manager->dialog(chatId);
	if (dialog) {
		context._controller->show(Box<DeleteMessagesBox>(dialog->owningHistory()->peer, justClear));
	}
}

void verifyKey(const Context &context, int chatId) {
	if (!activeChat(context, chatId)) {
		return;
	}
	const auto chat = context._manager->chat(chatId);
	if (chat->verification.size() != 36) {
		context._controller->showToast(tr::extras_SecretChatFailed(tr::now));
		return;
	}
	auto lines = QStringList();
	const auto hex = QString::fromLatin1(chat->verification.toHex());
	for (auto offset = 0; offset != hex.size(); offset += 12) {
		lines.push_back(hex.mid(offset, 12));
	}
	const auto digest = lines.join('\n');
	context._controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto child = bindBox(box, context._controller.get());
		Core::App().screenshotProtection().addContentReason(
			context._controller->widget(), box->shownValue(), box->lifetime());
		watchChat(child, chatId);
		child._manager->changed() | rpl::on_next([=] {
			const auto current = child.valid() ? child._manager->chat(chatId) : std::nullopt;
			if (!current || current->verification != chat->verification) {
				box->closeBox();
			}
		}, box->lifetime());
		box->setTitle(tr::extras_SecretChatVerify());
		addLabel(box->verticalLayout(), tr::extras_SecretChatVerifyHelp(tr::now));
		addLabel(box->verticalLayout(), digest, true)->setObjectName(
			u"secret/keyDigest"_q);
		box->addButton(tr::lng_context_copy_text(), [=] {
			if (child.valid() && activeChat(child, chatId)) {
				QGuiApplication::clipboard()->setText(digest);
			}
		});
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}), Ui::LayerOption::KeepOther);
}

void chooseTtl(const Context &context, int chatId) {
	if (!activeChat(context, chatId)) {
		return;
	}
	context._controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto child = bindBox(box, context._controller.get());
		watchChat(child, chatId);
		box->setTitle(tr::extras_SecretChatTimer());
		for (const auto seconds : { 0, 5, 10, 15, 30, 60, 300, 3600, 86400, 604800 }) {
			addAction(box->verticalLayout(), rpl::single(timerText(seconds)), [=] {
				if (!child.valid() || !activeChat(context, chatId)) {
					return;
				}
				box->closeBox();
				context._manager->setTtl(chatId, seconds, [=](QString error) {
					reportError(context, error);
				});
			}, u"secret/ttl/"_q + QString::number(seconds));
		}
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}), Ui::LayerOption::KeepOther);
}

} // namespace

void verifyKey(not_null<Window::SessionController*> controller,
		not_null<Ui::RpWidget*> parent, int chatId) {
	verifyKey(bindWidget(parent, controller), chatId);
}

void fillMenu(not_null<Window::SessionController*> controller,
		not_null<Ui::RpWidget*> parent, int chatId,
		const Ui::Menu::MenuCallback &callback) {
	const auto context = bindWidget(parent, controller);
	callback(tr::extras_SecretChatVerify(tr::now), [=] { verifyKey(context, chatId); }, &st::menuIconLock);
	callback(tr::extras_SecretChatTimer(tr::now), [=] { chooseTtl(context, chatId); }, &st::menuIconTTL);
	callback(tr::extras_SecretChatClear(tr::now), [=] {
		confirmHistoryAction(context, chatId, true);
	}, &st::menuIconDelete);
	callback(tr::extras_SecretChatDestroy(tr::now), [=] {
		confirmHistoryAction(context, chatId, false);
	}, &st::menuIconDelete);
}

void confirmCreate(
		not_null<Window::SessionController*> controller,
		not_null<UserData*> user) {
	if (&user->session() != &controller->session()) {
		return;
	}
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto context = bindBox(box, controller);
		box->setTitle(tr::extras_SecretChatStart());
		box->setObjectName(u"secret/create"_q);
		addLabel(box->verticalLayout(), tr::extras_SecretChatConfirm(tr::now));
		addLabel(box->verticalLayout(), user->name());
		const auto busy = box->lifetime().make_state<rpl::variable<bool>>(false);
		const auto status = box->lifetime().make_state<rpl::variable<QString>>();
		box->addRow(object_ptr<Ui::FlatLabel>(box, status->value(), st::boxLabel));
		const auto start = box->addButton(tr::extras_SecretChatStart(), [=] {
			if (busy->current() || !context.valid()) {
				return;
			}
			if (!context._manager->available()) {
				box->showToast(tr::extras_SecretChatUnavailable(tr::now));
				return;
			}
			*busy = true;
			*status = tr::extras_SecretChatPending(tr::now);
			context._manager->create(user, [=](int id, QString error) {
				if (!context.valid()) {
					return;
				}
				*busy = false;
				if (!id || !error.isEmpty()) {
					*status = tr::extras_SecretChatFailed(tr::now);
					return;
				}
				box->closeBox();
				crl::on_main(context._controller.get(), [=] {
					showChat(context._controller.get(), id);
				});
			});
		});
		start->setObjectName(u"secret/create/confirm"_q);
		busy->value() | rpl::on_next([=](bool value) {
			start->setDisabled(value);
		}, box->lifetime());
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}), Ui::LayerOption::KeepOther);
}

}
