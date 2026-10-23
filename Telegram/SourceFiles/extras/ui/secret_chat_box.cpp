#include "extras/ui/secret_chat_box.h"

#include "extras/features/secret_chats/secret_chat_manager.h"
#include "extras/features/secret_chats/secret_chat_files.h"
#include "base/openssl_help.h"
#include "base/timer.h"
#include "core/application.h"
#include "core/core_screenshot_protection.h"
#include "core/file_utilities.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/text/format_values.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "crl/crl_async.h"

#include <QtCore/QBuffer>
#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QLocale>
#include <QtCore/QMimeDatabase>
#include <QtCore/QSaveFile>
#include <QtGui/QClipboard>
#include <QtGui/QImageReader>
#include <QtWidgets/QApplication>
#include <algorithm>
#include <atomic>
#include <limits>

namespace Extras::SecretChats {
namespace {

struct Context {
	QPointer<Ui::GenericBox> _box;
	base::weak_ptr<Window::SessionController> _controller;
	base::weak_ptr<Main::Session> _session;
	base::weak_ptr<Manager> _manager;
	std::shared_ptr<std::atomic<bool>> _closed;

	[[nodiscard]] bool valid() const {
		return _box && !_closed->load()
			&& _controller && _session && _manager
			&& &_controller->session() == _session.get();
	}
};

struct Operation {
	std::atomic<bool> _cancelled = false;
};

void clearBytes(QByteArray &bytes) {
	OPENSSL_cleanse(bytes.data(), bytes.size());
	bytes.clear();
}

[[nodiscard]] Context bindBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	auto result = Context{
		._box = box.get(),
		._controller = controller,
		._session = session,
		._manager = &session->secretChats(),
		._closed = std::make_shared<std::atomic<bool>>(false),
	};
	const auto close = [weakBox = result._box] {
		if (weakBox) {
			weakBox->closeBox();
		}
	};
	controller->lifetime().add(close);
	session->lifetime().add(close);
	box->boxClosing() | rpl::on_next([closed = result._closed] {
		closed->store(true);
	}, box->lifetime());
	box->lifetime().add([closed = result._closed] { closed->store(true); });
	Core::App().screenshotProtection().addContentReason(
		box->shownValue(), box->lifetime());
	box->setWidth(st::boxWideWidth);
	return result;
}

[[nodiscard]] QString stateText(const Chat &chat) {
	if (!chat.error.isEmpty()) {
		return tr::extras_SecretChatFailed(tr::now);
	}
	switch (chat.state) {
	case State::Waiting:
	case State::Requested: return tr::extras_SecretChatPending(tr::now);
	case State::Active: return tr::extras_SecretChatReady(tr::now);
	case State::Discarded: return tr::extras_SecretChatEnded(tr::now);
	}
	Unexpected("Invalid secret chat state");
}

[[nodiscard]] QString deliveryText(Delivery delivery) {
	switch (delivery) {
	case Delivery::Pending: return tr::extras_SecretChatSending(tr::now);
	case Delivery::Sent: return tr::extras_SecretChatSent(tr::now);
	case Delivery::Failed: return tr::extras_SecretChatFailed(tr::now);
	case Delivery::Read: return tr::extras_SecretChatRead(tr::now);
	}
	Unexpected("Invalid secret chat delivery");
}

[[nodiscard]] bool sameMessageContent(const Message &a, const Message &b) {
	if (a.id != b.id || a.outgoing != b.outgoing || a.text != b.text
		|| a.ttl != b.ttl || a.unsupported != b.unsupported
		|| a.screenshot != b.screenshot || bool(a.document) != bool(b.document)
		|| (a.outgoing && (a.delivery == Delivery::Failed)
			!= (b.delivery == Delivery::Failed))) {
		return false;
	}
	return !a.document || (a.document->fileName == b.document->fileName
		&& a.document->mimeType == b.document->mimeType
		&& a.document->size == b.document->size);
}

[[nodiscard]] QString accountText(not_null<UserData*> user) {
	return user->name() + u" · "_q
		+ QString::number(peerToUser(user->id).bare);
}

[[nodiscard]] QString timerText(int ttl) {
	return ttl ? Ui::FormatTTL(ttl) : tr::extras_SecretChatTimerOff(tr::now);
}

[[nodiscard]] crl::time expiryDelay(qint64 expiresAt, qint64 now) {
	// Qt 单次计时只支持 32 位毫秒，长计时到点前只重新排期。
	return std::clamp(crl::time(expiresAt - now) * 1000,
		crl::time(1), crl::time(std::numeric_limits<int>::max()));
}

[[nodiscard]] std::optional<Message> findMessage(
		const Context &context,
		int chatId,
		qint64 messageId) {
	if (!context.valid()) {
		return std::nullopt;
	}
	for (const auto &message : context._manager->messages(chatId)) {
		if (message.id == messageId) {
			return message;
		}
	}
	return std::nullopt;
}

[[nodiscard]] bool activeChat(const Context &context, int chatId) {
	const auto chat = context.valid() && context._manager->available()
		? context._manager->chat(chatId)
		: std::nullopt;
	return chat && chat->state == State::Active;
}

void reportError(const Context &context, const QString &error) {
	if (context.valid() && !error.isEmpty()) {
		context._box->showToast(tr::extras_SecretChatFailed(tr::now));
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

[[nodiscard]] std::shared_ptr<Operation> watchOperation(
		const Context &context,
		int chatId,
		qint64 messageId = 0,
		bool exporting = false) {
	const auto operation = std::make_shared<Operation>();
	const auto weak = std::weak_ptr(operation);
	context._box->boxClosing() | rpl::on_next([weak] {
		if (const auto operation = weak.lock()) {
			operation->_cancelled.store(true);
		}
	}, context._box->lifetime());
	context._box->lifetime().add([weak] {
		if (const auto operation = weak.lock()) {
			operation->_cancelled.store(true);
		}
	});
	context._manager->changed() | rpl::on_next([=] {
		const auto operation = weak.lock();
		if (!operation) {
			return;
		}
		const auto message = messageId
			? findMessage(context, chatId, messageId)
			: std::nullopt;
		if (!activeChat(context, chatId)
			|| (messageId && (!message || (exporting && message->ttl)))) {
			operation->_cancelled.store(true);
		}
	}, context._box->lifetime());
	return operation;
}

void confirmAction(
		const Context &context,
		rpl::producer<QString> title,
		rpl::producer<QString> text,
		rpl::producer<QString> confirm,
		Fn<void()> action) {
	if (!context.valid()) {
		return;
	}
	context._controller->show(Box([
		context,
		title = std::move(title),
		text = std::move(text),
		confirm = std::move(confirm),
		action = std::move(action)
	](not_null<Ui::GenericBox*> box) mutable {
		const auto child = bindBox(box, context._controller.get());
		box->setTitle(std::move(title));
		box->addRow(object_ptr<Ui::FlatLabel>(box, std::move(text), st::boxLabel));
		box->addButton(std::move(confirm), [=] {
			if (!child.valid() || !context.valid()) {
				return;
			}
			box->closeBox();
			action();
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}), Ui::LayerOption::KeepOther);
}

void verifyKey(const Context &context, int chatId) {
	if (!activeChat(context, chatId)) {
		return;
	}
	const auto chat = context._manager->chat(chatId);
	if (chat->verification.size() != 36) {
		context._box->showToast(tr::extras_SecretChatFailed(tr::now));
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

void showOptions(const Context &context, int chatId) {
	if (!context.valid()) {
		return;
	}
	context._controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto child = bindBox(box, context._controller.get());
		box->setTitle(tr::extras_SecretChatOptions());
		const auto content = box->verticalLayout();
		addAction(content, tr::extras_SecretChatVerify(), [=] {
			verifyKey(child, chatId);
		}, u"secret/verify"_q);
		addAction(content, tr::extras_SecretChatTimer(), [=] {
			chooseTtl(child, chatId);
		}, u"secret/timer"_q);
		addAction(content, tr::extras_SecretChatClear(), [=] {
			confirmAction(child,
				tr::extras_SecretChatClear(), tr::extras_SecretChatClearConfirm(),
				tr::extras_SecretChatClear(), [=] {
					if (child.valid()) {
						child._manager->clear(chatId, [=](QString error) {
							reportError(child, error);
						});
					}
				});
		}, u"secret/clear"_q);
		addAction(content, tr::extras_SecretChatEnd(), [=] {
			confirmAction(child,
				tr::extras_SecretChatEnd(), tr::extras_SecretChatEndConfirm(),
				tr::extras_SecretChatEnd(), [=] {
					if (child.valid()) {
						child._manager->discard(chatId, false, [=](QString error) {
							reportError(child, error);
						});
					}
				});
		}, u"secret/end"_q);
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}), Ui::LayerOption::KeepOther);
}

void chooseAttachment(const Context &context, int chatId) {
	if (!activeChat(context, chatId)) {
		return;
	}
	const auto operation = watchOperation(context, chatId);
	FileDialog::GetOpenPath(context._box.data(),
		tr::extras_SecretChatAttachment(tr::now), FileDialog::AllFilesFilter(),
		[=](FileDialog::OpenResult &&result) {
			if (!context.valid() || operation->_cancelled.load() || result.paths.isEmpty()) {
				return;
			}
			const auto path = result.paths.front();
			crl::async([=] {
				auto bytes = QByteArray();
				auto file = QFile(path);
				auto ok = !operation->_cancelled.load() && file.open(QIODevice::ReadOnly);
				auto tooLarge = ok && file.size() > kMaxDocumentBytes;
				if (ok && !tooLarge) {
					bytes = file.read(kMaxDocumentBytes + 1);
					tooLarge = bytes.size() > kMaxDocumentBytes;
					ok = file.error() == QFileDevice::NoError && !bytes.isEmpty();
				}
				const auto name = QFileInfo(path).fileName();
				const auto mime = QMimeDatabase().mimeTypeForFile(
					path, QMimeDatabase::MatchExtension).name();
				crl::on_main([
					=,
					bytes = std::move(bytes)
				]() mutable {
					if (!context.valid() || operation->_cancelled.load()
						|| !activeChat(context, chatId)) {
						clearBytes(bytes);
						return;
					}
					if (!ok || tooLarge) {
						clearBytes(bytes);
						context._box->showToast(tooLarge
							? tr::extras_SecretChatFileLimit(tr::now)
							: tr::extras_SecretChatFailed(tr::now));
						return;
					}
					context._manager->sendDocument(chatId, name, mime,
						std::move(bytes), {}, [=](qint64, QString error) {
							reportError(context, error);
						});
				});
			});
		});
}

void saveAttachment(const Context &context, int chatId, qint64 messageId) {
	const auto message = findMessage(context, chatId, messageId);
	if (!activeChat(context, chatId) || !message || !message->document || message->ttl) {
		return;
	}
	const auto operation = watchOperation(context, chatId, messageId, true);
	FileDialog::GetWritePath(context._box.data(),
		tr::extras_SecretChatSaveAttachment(tr::now), FileDialog::AllFilesFilter(),
		QFileInfo(message->document->fileName).fileName(),
		[=](QString &&path) {
			const auto current = findMessage(context, chatId, messageId);
			if (!activeChat(context, chatId) || !current || current->ttl
				|| operation->_cancelled.load() || path.isEmpty()) {
				return;
			}
			context._manager->downloadDocument(chatId, messageId, [
				=,
				path = std::move(path)
			](QByteArray bytes, QString error) mutable {
				const auto current = findMessage(context, chatId, messageId);
				if (!activeChat(context, chatId) || !current || current->ttl
					|| operation->_cancelled.load()) {
					clearBytes(bytes);
					return;
				}
				if (!error.isEmpty()) {
					clearBytes(bytes);
					reportError(context, error);
					return;
				}
				crl::async([
					=,
					bytes = std::move(bytes)
				]() mutable {
					const auto clear = gsl::finally([&] { clearBytes(bytes); });
					if (operation->_cancelled.load()) {
						return;
					}
					// 临时输出只用于用户选定路径的原子保存，取消时不提交。
					auto file = QSaveFile(path);
					auto ok = file.open(QIODevice::WriteOnly)
						&& file.write(bytes) == bytes.size();
					if (operation->_cancelled.load()) {
						file.cancelWriting();
						return;
					}
					ok = ok && file.commit();
					crl::on_main([=] {
						if (context.valid() && !operation->_cancelled.load() && !ok) {
							context._box->showToast(tr::extras_SecretChatFailed(tr::now));
						}
					});
				});
			});
		});
}

void viewAttachment(const Context &context, int chatId, qint64 messageId) {
	const auto message = findMessage(context, chatId, messageId);
	if (!activeChat(context, chatId) || !message || !message->document) {
		return;
	}
	const auto document = *message->document;
	const auto ttl = message->ttl;
	context._controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto child = bindBox(box, context._controller.get());
		const auto operation = watchOperation(child, chatId, messageId);
		const auto expiry = box->lifetime().make_state<base::Timer>();
		const auto close = [=] {
			if (!child.valid()) {
				return;
			}
			operation->_cancelled.store(true);
			// 先清空内容，关闭动画不能继续展示已失效的明文。
			box->verticalLayout()->clear();
			box->closeBox();
		};
		const auto scheduleExpiry = [=] {
			if (!child.valid()) {
				return;
			}
			expiry->cancel();
			const auto current = findMessage(child, chatId, messageId);
			if (operation->_cancelled.load() || !activeChat(child, chatId) || !current) {
				close();
				return;
			}
			if (current->expiresAt) {
				expiry->callOnce(expiryDelay(current->expiresAt,
					child._manager->currentTime()), Qt::PreciseTimer);
			}
		};
		expiry->setCallback(scheduleExpiry);
		box->setTitle(tr::extras_SecretChatViewAttachment());
		addLabel(box->verticalLayout(), document.fileName);
		addLabel(box->verticalLayout(), Ui::FormatSizeText(document.size));
		const auto loading = addLabel(box->verticalLayout(),
			tr::extras_SecretChatLoadingAttachment(tr::now));
		if (ttl) {
			addLabel(box->verticalLayout(), tr::extras_SecretChatProtectedAttachment(tr::now));
		} else {
			box->addButton(tr::extras_SecretChatSaveAttachment(), [=] {
				saveAttachment(child, chatId, messageId);
			});
		}
		child._manager->changed() | rpl::on_next(scheduleExpiry, box->lifetime());
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
		scheduleExpiry();
		if (!child.valid() || operation->_cancelled.load()) {
			return;
		}
		child._manager->downloadDocument(chatId, messageId, [=](QByteArray bytes, QString error) mutable {
			if (!activeChat(child, chatId) || !findMessage(child, chatId, messageId)
				|| operation->_cancelled.load()) {
				clearBytes(bytes);
				return;
			}
			if (!error.isEmpty()) {
				clearBytes(bytes);
				loading->setText(tr::extras_SecretChatFailed(tr::now));
				return;
			}
			crl::async([=, bytes = std::move(bytes)]() mutable {
				const auto clear = gsl::finally([&] { clearBytes(bytes); });
				if (operation->_cancelled.load()) {
					return;
				}
				auto buffer = QBuffer(&bytes);
				buffer.open(QIODevice::ReadOnly);
				auto reader = QImageReader(&buffer);
				reader.setAutoTransform(true);
				const auto size = reader.size();
				if (size.isValid()) {
					reader.setScaledSize(size.scaled(640, 480, Qt::KeepAspectRatio));
				}
				auto image = reader.read();
				crl::on_main([=, image = std::move(image)]() mutable {
					if (!activeChat(child, chatId) || !findMessage(child, chatId, messageId)
						|| operation->_cancelled.load()) {
						return;
					}
					loading->setText(QString());
					if (image.isNull()) {
						return;
					}
					const auto preview = box->addRow(object_ptr<Ui::RpWidget>(box));
					preview->resize(0, image.height());
					preview->setObjectName(u"secret/attachmentPreview"_q);
					preview->paintRequest() | rpl::on_next([=] {
						auto painter = QPainter(preview);
						const auto shown = image.size().scaled(
							preview->width(), image.height(), Qt::KeepAspectRatio);
						painter.drawImage(QRect(
							(preview->width() - shown.width()) / 2, 0,
							shown.width(), shown.height()), image);
					}, preview->lifetime());
				});
			});
		});
	}), Ui::LayerOption::KeepOther);
}

class ChatBox final {
public:
	ChatBox(Context context, int chatId)
	: _context(std::move(context))
	, _chatId(chatId)
	, _expiryTimer([=] {
		if (!_context.valid() || !_nextExpiry) {
			return;
		}
		if (_nextExpiry > _context._manager->currentTime()) {
			scheduleExpiry();
			return;
		}
		refresh();
	}) {
	}

	~ChatBox() {
		_context._closed->store(true);
	}

	void setup() {
		const auto box = _context._box.data();
		box->setObjectName(u"secret/chat/"_q + QString::number(_chatId));
		box->setTitle(tr::extras_SecretChatsTitle());
		box->setAdditionalTitle(rpl::single(accountText(_context._session->user())));
		box->setMaxHeight(700);
		const auto header = box->setPinnedToTopContent(object_ptr<Ui::VerticalLayout>(box));
		Settings::AddButtonWithLabel(header,
			_name.value(), rpl::single(QString()), st::settingsButton,
			{ .icon = &st::menuIconLock })->addClickHandler([=] {
				verifyKey(_context, _chatId);
			});
		const auto status = addLabel(header, QString());
		status->setObjectName(u"secret/status"_q);
		_status.value() | rpl::on_next([=](QString text) {
			status->setText(std::move(text));
		}, box->lifetime());
		_messages = box->addRow(object_ptr<Ui::VerticalLayout>(box), style::margins());
		const auto composer = box->setPinnedToBottomContent(object_ptr<Ui::VerticalLayout>(box));
		_attachment = addAction(composer, tr::extras_SecretChatAttachment(), [=] {
			chooseAttachment(_context, _chatId);
		}, u"secret/attach"_q).get();
		_field = composer->add(object_ptr<Ui::InputField>(
			composer, st::defaultInputField, Ui::InputField::Mode::MultiLine,
			tr::lng_message_ph()), st::boxRowPadding);
		_field->setObjectName(u"secret/compose"_q);
		_field->setMaxLength(32768);
		_field->setMaxHeight(100);
		_field->setSubmitSettings(Ui::InputField::SubmitSettings::Enter);
		_read = addAction(composer, tr::lng_context_mark_read(), [=] {
			readVisible();
		}, u"secret/read"_q).get();
		_send = box->addButton(tr::lng_send_button(), [=] { send(); });
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
		box->addLeftButton(tr::extras_SecretChatOptions(), [=] {
			showOptions(_context, _chatId);
		});
		_field->submits() | rpl::on_next([=] { send(); }, box->lifetime());
		_field->changes() | rpl::on_next([=] { updateComposer(); }, box->lifetime());
		_context._manager->changed() | rpl::on_next([=] { refresh(); }, box->lifetime());
		box->setShowFinishedCallback([=] {
			if (_latest) {
				box->scrollToWidget(_latest.data());
			}
			_field->setFocusFast();
		});
		refresh();
	}

private:
	struct RenderedMessage {
		Message message;
		QPointer<Ui::FlatLabel> details;
		QString detailsText;
	};

	void scheduleExpiry() {
		_expiryTimer.cancel();
		if (!_nextExpiry || !_context.valid()) {
			return;
		}
		_expiryTimer.callOnce(expiryDelay(_nextExpiry,
			_context._manager->currentTime()), Qt::PreciseTimer);
	}

	void updateComposer() {
		const auto active = activeChat(_context, _chatId);
		_field->setEnabled(active && !_sending);
		_send->setDisabled(!active || _sending || _field->getLastText().trimmed().isEmpty());
		_attachment->setDisabled(!active);
		_read->setDisabled(!active);
	}

	void refresh() {
		if (!_context.valid()) {
			_context._box->closeBox();
			return;
		}
		const auto chat = _context._manager->chat(_chatId);
		if (!chat) {
			_context._box->closeBox();
			return;
		}
		const auto user = _context._session->data().user(chat->userId);
		_name = accountText(user);
		_status = stateText(*chat) + u" · "_q + timerText(chat->ttl);
		const auto messages = _context._manager->messages(_chatId);
		const auto detailsText = [&](const Message &message) {
			const auto name = message.outgoing
				? _context._session->user()->name() : user->name();
			const auto date = QLocale().toString(
				QDateTime::fromSecsSinceEpoch(message.date), QLocale::ShortFormat);
			return name + u" · "_q + date + u" · "_q + deliveryText(message.delivery);
		};
		_nextExpiry = 0;
		for (const auto &message : messages) {
			if (message.expiresAt && (!_nextExpiry || message.expiresAt < _nextExpiry)) {
				_nextExpiry = message.expiresAt;
			}
		}
		scheduleExpiry();
		updateComposer();
		// 提示状态变化保留正文控件，避免清掉普通文字选区。
		if (_rendered && messages.size() == _renderedMessages.size()
			&& std::equal(messages.cbegin(), messages.cend(), _renderedMessages.cbegin(),
				[](const Message &message, const RenderedMessage &rendered) {
					return sameMessageContent(message, rendered.message);
				})) {
			for (auto i = 0; i != messages.size(); ++i) {
				auto &rendered = _renderedMessages[i];
				const auto text = detailsText(messages[i]);
				if (rendered.detailsText != text) {
					rendered.details->setText(text);
					rendered.detailsText = text;
				}
				rendered.message = messages[i];
			}
			return;
		}
		const auto wasAtBottom = !_rendered
			|| _context._box->scrollTop() + _context._box->scrollHeight()
				>= _context._box->verticalLayout()->height() - 8;
		_renderedMessages.clear();
		_messages->clear();
		_latest = nullptr;
		for (const auto &message : messages) {
			const auto messageId = message.id;
			const auto text = detailsText(message);
			_renderedMessages.push_back({ message, addLabel(_messages, text).get(), text });
			if (message.screenshot) {
				_latest = addLabel(_messages,
					tr::extras_SecretChatScreenshotNotice(tr::now)).get();
			}
			if (!message.text.isEmpty() || message.unsupported) {
				const auto text = message.unsupported
					? tr::extras_SecretChatUnsupported(tr::now) : message.text;
				_latest = addLabel(_messages, text, !message.ttl && !message.unsupported).get();
				_latest->setObjectName(u"secret/message/"_q
					+ QString::number(message.id) + u"/text"_q);
			}
			if (message.document) {
				_latest = addAction(_messages,
					rpl::single(tr::extras_SecretChatViewAttachment(tr::now)
						+ u" · "_q + message.document->fileName
						+ u" · "_q + Ui::FormatSizeText(message.document->size)),
					[=] { viewAttachment(_context, _chatId, messageId); },
					u"secret/message/"_q + QString::number(message.id) + u"/view"_q).get();
			}
			if (message.delivery == Delivery::Failed && message.outgoing) {
				_latest = addAction(_messages, tr::extras_SecretChatRetry(), [=] {
					if (activeChat(_context, _chatId)) {
						_context._manager->retry(_chatId, messageId, [context = _context](QString error) {
							reportError(context, error);
						});
					}
				}, u"secret/message/"_q + QString::number(message.id) + u"/retry"_q).get();
			}
			_latest = addAction(_messages, tr::lng_context_delete_msg(), [=] {
				confirmAction(_context, tr::lng_context_delete_msg(),
					tr::extras_SecretChatDeleteConfirm(), tr::lng_box_delete(), [=] {
						if (_context.valid()) {
							_context._manager->deleteMessages(_chatId, { messageId },
								[context = _context](QString error) { reportError(context, error); });
						}
					});
			}, u"secret/message/"_q + QString::number(message.id) + u"/delete"_q).get();
		}
		_rendered = true;
		if (wasAtBottom && _latest) {
			crl::on_main(_context._box, [context = _context, latest = _latest] {
				if (context.valid() && latest) {
					context._box->scrollToWidget(latest.data());
				}
			});
		}
	}

	void send() {
		if (_sending || !activeChat(_context, _chatId)) {
			return;
		}
		const auto text = _field->getLastText();
		if (text.trimmed().isEmpty()) {
			return;
		}
		_sending = true;
		updateComposer();
		const auto context = _context;
		_context._manager->send(_chatId, text, [=](qint64 id, QString error) {
			if (!context.valid()) {
				return;
			}
			_sending = false;
			if (id && _field->getLastText() == text) {
				_field->setTextWithTags({}, Ui::InputField::HistoryAction::Clear);
			}
			updateComposer();
			reportError(_context, error);
		});
	}

	void readVisible() {
		if (!activeChat(_context, _chatId) || !_context._box->isVisible()
			|| QApplication::applicationState() != Qt::ApplicationActive
			|| !_context._box->window()->isActiveWindow()
			|| _context._box->scrollTop() + _context._box->scrollHeight()
				< _context._box->verticalLayout()->height() - 8) {
			return;
		}
		auto date = 0;
		for (const auto &message : _context._manager->messages(_chatId)) {
			if (!message.outgoing) {
				date = std::max(date, message.date);
			}
		}
		if (date) {
			_context._manager->read(_chatId, date,
				[context = _context](QString error) { reportError(context, error); });
		}
	}

	const Context _context;
	const int _chatId;
	base::Timer _expiryTimer;
	qint64 _nextExpiry = 0;
	rpl::variable<QString> _name;
	rpl::variable<QString> _status;
	Ui::VerticalLayout *_messages = nullptr;
	Ui::InputField *_field = nullptr;
	Ui::SettingsButton *_attachment = nullptr;
	Ui::SettingsButton *_read = nullptr;
	QPointer<Ui::RoundButton> _send;
	QPointer<Ui::RpWidget> _latest;
	QVector<RenderedMessage> _renderedMessages;
	bool _sending = false;
	bool _rendered = false;
};

}

void showChats(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto context = bindBox(box, controller);
		box->setTitle(tr::extras_SecretChatsTitle());
		box->setObjectName(u"secret/chats"_q);
		addLabel(box->verticalLayout(), tr::extras_SecretChatsDescription(tr::now));
		const auto rows = box->addRow(object_ptr<Ui::VerticalLayout>(box), style::margins());
		const auto refresh = [=] {
			if (!context.valid()) {
				box->closeBox();
				return;
			}
			rows->clear();
			if (!context._manager->available()) {
				addLabel(rows, tr::extras_SecretChatUnavailable(tr::now));
				return;
			}
			const auto chats = context._manager->chats();
			if (chats.isEmpty()) {
				addLabel(rows, tr::extras_SecretChatsEmpty(tr::now));
				return;
			}
			for (const auto &chat : chats) {
				const auto user = context._session->data().user(chat.userId);
				const auto button = Settings::AddButtonWithLabel(rows,
					rpl::single(accountText(user)), rpl::single(stateText(chat)),
					st::settingsButton, { .icon = &st::menuIconLock });
				button->setObjectName(u"secret/open/"_q + QString::number(chat.id));
				button->addClickHandler([=] {
					if (context.valid()) {
						showChat(context._controller.get(), chat.id);
					}
				});
			}
		};
		context._manager->changed() | rpl::on_next(refresh, box->lifetime());
		refresh();
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}), Ui::LayerOption::KeepOther);
}

void showChat(not_null<Window::SessionController*> controller, int chatId) {
	if (!controller->session().secretChats().chat(chatId)) {
		return;
	}
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto state = box->lifetime().make_state<ChatBox>(
			bindBox(box, controller), chatId);
		state->setup();
	}), Ui::LayerOption::KeepOther);
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
		addLabel(box->verticalLayout(), tr::extras_SecretChatAccount(tr::now)
			+ u"\n"_q + accountText(context._session->user()));
		addLabel(box->verticalLayout(), tr::extras_SecretChatContact(tr::now)
			+ u"\n"_q + accountText(user));
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
				showChat(context._controller.get(), id);
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
