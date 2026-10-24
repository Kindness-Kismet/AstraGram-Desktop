#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "core/application.h"
#include "crl/crl_async.h"
#include "crl/crl_on_main.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "extras/debug/debug_login.h"
#include "extras/features/secret_chats/secret_chat_files.h"
#include "extras/features/secret_chats/secret_chat_manager.h"
#include "extras/ui/secret_chat_box.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"

#include <QtCore/QCryptographicHash>

namespace ExtrasDebug::Commands {
namespace {

using Json = nlohmann::json;

// 真实私密操作必须给出账号编号，不依赖当前选中的账号。
Main::Session *specifiedSession(const QString &value) {
	auto ok = false;
	const auto id = value.toULongLong(&ok);
	if (!ok || !id) {
		return nullptr;
	}
	for (const auto &entry : Core::App().domain().accounts()) {
		const auto session = entry.account->sessionExists()
			? &entry.account->session() : nullptr;
		if (session && session->userId().bare == id
			&& !isSimulationSession(session)) {
			return session;
		}
	}
	return nullptr;
}

int specifiedChat(const QString &value) {
	auto ok = false;
	const auto id = value.toInt(&ok);
	return ok && id > 0 ? id : 0;
}

uint64 sessionJob(not_null<Main::Session*> session, const char *kind) {
	const auto id = beginJob(kind);
	session->lifetime().add([id] {
		finishJob(id, false, "session closed");
	});
	return id;
}

QString digest(const QByteArray &bytes) {
	return QString::fromLatin1(QCryptographicHash::hash(
		bytes, QCryptographicHash::Sha256).toHex());
}

QString stateName(Extras::SecretChats::State state) {
	using State = Extras::SecretChats::State;
	switch (state) {
	case State::Waiting: return u"waiting"_q;
	case State::Requested: return u"requested"_q;
	case State::Active: return u"active"_q;
	case State::Discarded: return u"discarded"_q;
	}
	Unexpected("Unknown secret chat state.");
}

Result list(const QStringList &args) {
	if (args.size() != 1) {
		return Result::Err(u"usage: secret.list <accountUserId>"_q);
	}
	const auto session = specifiedSession(args[0]);
	if (!session) {
		return Result::Err(u"specified account is not logged in"_q);
	}
	auto chats = Json::array();
	for (const auto &chat : session->secretChats().chats()) {
		const auto user = session->data().userLoaded(chat.userId);
		chats.push_back({
			{ "chatId", chat.id }, { "userId", chat.userId.bare },
			{ "name", user ? user->name().toStdString() : std::string() },
			{ "state", stateName(chat.state).toStdString() },
			{ "creator", chat.creator }, { "ttl", chat.ttl },
			{ "layer", chat.layer },
			{ "verification", chat.verification.toHex().toStdString() },
			{ "error", chat.error.toStdString() },
		});
	}
	return Result::Ok(Compact(Json{
		{ "accountUserId", session->userId().bare },
		{ "accountName", session->user()->name().toStdString() },
		{ "available", session->secretChats().available() },
		{ "baseline", session->secretChats().hasBaseline() },
		{ "qts", session->secretChats().committedQts() },
		{ "currentTime", session->secretChats().currentTime() },
		{ "error", session->secretChats().error().toStdString() },
		{ "chats", std::move(chats) },
	}));
}

Result messages(const QStringList &args) {
	if (args.size() != 2) {
		return Result::Err(u"usage: secret.messages <accountUserId> <chatId>"_q);
	}
	const auto session = specifiedSession(args[0]);
	const auto chatId = specifiedChat(args[1]);
	if (!session || !session->secretChats().chat(chatId)) {
		return Result::Err(u"unknown chat in the specified account"_q);
	}
	auto result = Json::array();
	for (const auto &message : session->secretChats().messages(chatId)) {
		auto entry = Json{
			{ "id", message.id }, { "date", message.date },
			{ "outgoing", message.outgoing },
			{ "replyTo", message.replyTo },
			{ "delivery", int(message.delivery) },
			{ "textBytes", message.text.toUtf8().size() },
			{ "textDigest", digest(message.text.toUtf8()).toStdString() },
			{ "ttl", message.ttl }, { "expiresAt", message.expiresAt },
			{ "unsupported", message.unsupported },
		};
		if (message.document) {
			entry["fileName"] = message.document->fileName.toStdString();
			entry["mimeType"] = message.document->mimeType.toStdString();
			entry["fileSize"] = message.document->size;
			entry["uploadOffset"] = message.uploadOffset
				? Json(*message.uploadOffset) : Json(nullptr);
		}
		result.push_back(std::move(entry));
	}
	return Result::Ok(Compact(result));
}

Result create(const QStringList &args) {
	if (args.size() != 2) {
		return Result::Err(u"usage: secret.create <accountUserId> <targetUserId>"_q);
	}
	const auto session = specifiedSession(args[0]);
	auto ok = false;
	const auto targetId = args[1].toULongLong(&ok);
	const auto target = session && ok
		? session->data().userLoaded(UserId(targetId)) : nullptr;
	if (!target || target->isSelf() || target->isBot() || target->isInaccessible()) {
		return Result::Err(u"target user is not available in the specified account"_q);
	}
	const auto job = sessionJob(session, "secret-create");
	session->secretChats().create(target, [=](int chatId, QString error) {
		if (!error.isEmpty()) {
			finishJob(job, false, error.toStdString());
			return;
		}
		finishJob(job, true, Json{
			{ "accountUserId", session->userId().bare },
			{ "targetUserId", targetId }, { "chatId", chatId },
			{ "status", "created; inspect secret.list for handshake completion" },
		});
	});
	return jobStarted(job);
}

Result send(const QStringList &args) {
	if (args.size() < 3) {
		return Result::Err(u"usage: secret.send <accountUserId> <chatId> <text>"_q);
	}
	const auto session = specifiedSession(args[0]);
	const auto chatId = specifiedChat(args[1]);
	if (!session || !session->secretChats().chat(chatId)) {
		return Result::Err(u"unknown chat in the specified account"_q);
	}
	const auto job = sessionJob(session, "secret-send");
	session->secretChats().send(chatId, args.mid(2).join(u' '), [=](
			qint64 id, QString error) {
		finishJob(job, error.isEmpty(), error.isEmpty()
			? Json{ { "messageId", id }, { "status", "queued" } }
			: Json(error.toStdString()));
	});
	return jobStarted(job);
}

Result reply(const QStringList &args) {
	if (args.size() < 4) {
		return Result::Err(u"usage: secret.reply <accountUserId> <chatId> <messageId> <text>"_q);
	}
	const auto session = specifiedSession(args[0]);
	const auto chatId = specifiedChat(args[1]);
	auto ok = false;
	const auto target = args[2].toLongLong(&ok);
	if (!session || !ok || !target || !session->secretChats().chat(chatId)) {
		return Result::Err(u"unknown reply target in the specified account"_q);
	}
	const auto job = sessionJob(session, "secret-reply");
	session->secretChats().sendReply(chatId, args.mid(3).join(u' '), target,
		[=](qint64 id, QString error) {
			finishJob(job, error.isEmpty(), error.isEmpty()
				? Json{ { "messageId", id }, { "replyTo", target }, { "status", "queued" } }
				: Json(error.toStdString()));
		});
	return jobStarted(job);
}

Result draft(const QStringList &args) {
	if (args.size() < 2) {
		return Result::Err(u"usage: secret.draft <accountUserId> <chatId> [<replyTo> <text>]"_q);
	}
	const auto session = specifiedSession(args[0]);
	const auto chatId = specifiedChat(args[1]);
	if (!session || !session->secretChats().chat(chatId)) {
		return Result::Err(u"unknown chat in the specified account"_q);
	}
	if (args.size() == 2) {
		const auto value = session->secretChats().draft(chatId);
		return Result::Ok(Compact(Json{
			{ "replyTo", value.replyTo }, { "textBytes", value.text.toUtf8().size() },
			{ "textDigest", digest(value.text.toUtf8()).toStdString() },
		}));
	}
	auto ok = false;
	const auto target = args[2].toLongLong(&ok);
	if (!ok) return Result::Err(u"invalid secret draft reply target"_q);
	const auto job = sessionJob(session, "secret-draft");
	session->secretChats().setDraft(chatId,
		Extras::SecretChats::Draft{ args.mid(3).join(u' '), target }, [=](QString error) {
			finishJob(job, error.isEmpty(), error.isEmpty()
				? Json{ { "status", "committed" } } : Json(error.toStdString()));
		});
	return jobStarted(job);
}

Result sample(const QStringList &args) {
	if (args.size() < 3 || args.size() > 4) {
		return Result::Err(u"usage: secret.sample <accountUserId> <chatId> <bytes> [replyTo]"_q);
	}
	const auto session = specifiedSession(args[0]);
	const auto chatId = specifiedChat(args[1]);
	auto ok = false;
	const auto size = args[2].toInt(&ok);
	if (!session || !session->secretChats().chat(chatId)
		|| !ok || size < 1 || size > Extras::SecretChats::kMaxDocumentBytes) {
		return Result::Err(u"expected a valid account, chat and sample size up to 32 MiB"_q);
	}
	const auto replyTo = args.size() == 4 ? args[3].toLongLong(&ok) : 0;
	if (!ok) return Result::Err(u"invalid secret document reply target"_q);
	const auto job = sessionJob(session, "secret-sample");
	const auto weak = base::make_weak(session);
	// 样本只在内存构造，指令不接受真实文件路径。
	crl::async([=] {
		auto bytes = QByteArray(size, Qt::Uninitialized);
		for (auto i = 0; i != size; ++i) {
			bytes[i] = char(((i & 255) * 73 + 17) & 255);
		}
		const auto hash = digest(bytes).toStdString();
		crl::on_main([=, bytes = std::move(bytes)]() mutable {
			if (!weak) {
				return;
			}
			weak->secretChats().sendDocumentReply(chatId,
				u"加密测试-"_q + QString::number(size) + u".bin"_q,
				u"application/octet-stream"_q, std::move(bytes), QString(), replyTo,
				[=](qint64 id, QString error) {
					finishJob(job, error.isEmpty(), error.isEmpty()
						? Json{ { "messageId", id }, { "bytes", size },
							{ "replyTo", replyTo },
							{ "sha256", hash }, { "status", "queued" } }
						: Json(error.toStdString()));
				});
		});
	});
	return jobStarted(job);
}

Result download(const QStringList &args) {
	if (args.size() != 3) {
		return Result::Err(u"usage: secret.download <accountUserId> <chatId> <messageId>"_q);
	}
	const auto session = specifiedSession(args[0]);
	const auto chatId = specifiedChat(args[1]);
	auto ok = false;
	const auto id = args[2].toLongLong(&ok);
	if (!session || !ok || !session->secretChats().chat(chatId)) {
		return Result::Err(u"unknown message in the specified account"_q);
	}
	const auto job = sessionJob(session, "secret-download");
	session->secretChats().downloadDocument(chatId, id, [=](
			QByteArray bytes, QString error) {
		if (!error.isEmpty()) {
			finishJob(job, false, error.toStdString());
			return;
		}
		crl::async([job, bytes = std::move(bytes)] {
			const auto hash = digest(bytes).toStdString();
			const auto size = bytes.size();
			crl::on_main([=] {
				finishJob(job, true, Json{ { "bytes", size }, { "sha256", hash } });
			});
		});
	});
	return jobStarted(job);
}

Result readContents(const QStringList &args) {
	if (args.size() < 3) {
		return Result::Err(u"usage: secret.read-contents <accountUserId> <chatId> <messageId>..."_q);
	}
	const auto session = specifiedSession(args[0]);
	const auto chatId = specifiedChat(args[1]);
	if (!session || !session->secretChats().chat(chatId)) {
		return Result::Err(u"unknown chat in the specified account"_q);
	}
	auto ids = QVector<qint64>();
	for (const auto &value : args.mid(2)) {
		auto ok = false;
		const auto id = value.toLongLong(&ok);
		if (!ok || !id) return Result::Err(u"invalid secret content message id"_q);
		ids.push_back(id);
	}
	const auto job = sessionJob(session, "secret-read-contents");
	session->secretChats().readContents(chatId, std::move(ids), [=](QString error) {
		finishJob(job, error.isEmpty(), error.isEmpty()
			? Json{ { "status", "committed" } } : Json(error.toStdString()));
	});
	return jobStarted(job);
}

Result open(const QStringList &args) {
	if (args.size() != 2) {
		return Result::Err(u"usage: secret.open <accountUserId> <chatId>"_q);
	}
	const auto session = specifiedSession(args[0]);
	const auto chatId = specifiedChat(args[1]);
	const auto controller = session ? session->tryResolveWindow() : nullptr;
	if (!controller || !session->secretChats().chat(chatId)) {
		return Result::Err(u"activate the specified account and choose an existing chat"_q);
	}
	Extras::SecretChats::showChat(controller, chatId);
	return Result::Ok();
}

Result action(const QStringList &args, const QString &kind) {
	const auto needsValue = kind == u"ttl"_q || kind == u"delete"_q
		|| kind == u"retry"_q;
	if (args.size() != (needsValue ? 3 : 2)) {
		return Result::Err(u"expected accountUserId, chatId and action value where required"_q);
	}
	const auto session = specifiedSession(args[0]);
	const auto chatId = specifiedChat(args[1]);
	if (!session || !session->secretChats().chat(chatId)) {
		return Result::Err(u"unknown chat in the specified account"_q);
	}
	auto value = qint64(0);
	if (needsValue) {
		auto ok = false;
		value = args[2].toLongLong(&ok);
		if (!ok || (kind == u"ttl"_q && (value < 0 || value > 31536000))) {
			return Result::Err(u"invalid secret action value"_q);
		}
	}
	const auto job = sessionJob(session, "secret-action");
	const auto done = [=](QString error) {
		finishJob(job, error.isEmpty(), error.isEmpty()
			? Json{ { "status", "committed" } } : Json(error.toStdString()));
	};
	auto &manager = session->secretChats();
	if (kind == u"ttl"_q) manager.setTtl(chatId, int(value), done);
	else if (kind == u"delete"_q) manager.deleteMessages(chatId, { value }, done);
	else if (kind == u"retry"_q) manager.retry(chatId, value, done);
	else if (kind == u"clear"_q) manager.clear(chatId, done);
	else if (kind == u"end"_q) manager.discard(chatId, false, done);
	else if (kind == u"destroy"_q) manager.discard(chatId, true, done);
	else {
		auto date = 0;
		for (const auto &message : manager.messages(chatId)) {
			if (!message.outgoing) date = std::max(date, message.date);
		}
		manager.read(chatId, date, done);
	}
	return jobStarted(job);
}

Result read(const QStringList &args) { return action(args, u"read"_q); }

Result pauseUpload(const QStringList &args) {
	if (args.size() != 4 || (args[3] != u"true"_q && args[3] != u"false"_q)) {
		return Result::Err(u"usage: secret.pause-upload <accountUserId> <chatId> <messageId> <true|false>"_q);
	}
	const auto session = specifiedSession(args[0]);
	const auto chatId = specifiedChat(args[1]);
	const auto messageId = args[2].toLongLong();
	if (!session || !ranges::any_of(session->secretChats().messages(chatId), [=](const auto &message) {
		return message.id == messageId && message.uploadOffset.has_value();
	})) {
		return Result::Err(u"secret document is not waiting for upload"_q);
	}
	session->secretChats().setDocumentUploadPaused(chatId, messageId, args[3] == u"true"_q);
	return Result::Ok(Compact(Json{ { "paused", args[3] == u"true"_q } }));
}
Result ttl(const QStringList &args) { return action(args, u"ttl"_q); }
Result erase(const QStringList &args) { return action(args, u"delete"_q); }
Result retry(const QStringList &args) { return action(args, u"retry"_q); }
Result clear(const QStringList &args) { return action(args, u"clear"_q); }
Result end(const QStringList &args) { return action(args, u"end"_q); }
Result destroy(const QStringList &args) { return action(args, u"destroy"_q); }

} // namespace

const HandlerMap &secretChatHandlers() {
	static const auto result = HandlerMap{
		{ u"secret.list"_q, &list }, { u"secret.messages"_q, &messages },
		{ u"secret.create"_q, &create }, { u"secret.open"_q, &open },
		{ u"secret.send"_q, &send }, { u"secret.sample"_q, &sample },
		{ u"secret.reply"_q, &reply }, { u"secret.draft"_q, &draft },
		{ u"secret.download"_q, &download }, { u"secret.read"_q, &read },
		{ u"secret.pause-upload"_q, &pauseUpload },
		{ u"secret.read-contents"_q, &readContents },
		{ u"secret.ttl"_q, &ttl }, { u"secret.delete"_q, &erase },
		{ u"secret.retry"_q, &retry }, { u"secret.clear"_q, &clear },
		{ u"secret.end"_q, &end },
		{ u"secret.destroy"_q, &destroy },
	};
	return result;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
