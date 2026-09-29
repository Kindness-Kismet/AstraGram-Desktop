#ifdef _DEBUG
#include "extras/debug/debug_server.h"

#include "extras/debug/debug_commands.h"
#include "extras/debug/crash_dump.h"

#include <QtNetwork/QHostAddress>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

namespace ExtrasDebug {
namespace {

// 固定端口便于外部脚本直接连接；只绑回环，不对外暴露。
constexpr auto kPort = 20100;

std::unique_ptr<QTcpServer> Server;

void reply(not_null<QTcpSocket*> socket, const Result &result) {
	auto line = (result.ok ? u"OK"_q : u"ERR"_q);
	if (!result.payload.isEmpty()) {
		line += ' ' + result.payload;
	}
	line += '\n';
	socket->write(line.toUtf8());
	socket->flush();
	// 一条连接执行一条指令，响应发送后再关闭连接。
	QMetaObject::invokeMethod(socket, [socket] {
		socket->disconnectFromHost();
	}, Qt::QueuedConnection);
}

void handleLine(not_null<QTcpSocket*> socket, const QString &line) {
	auto parts = QStringList();
	auto position = 0;
	while (position < line.size()) {
		if (line[position].isSpace()) {
			++position;
			continue;
		}
		const auto start = position;
		if (line[position] != '"') {
			while (position < line.size() && !line[position].isSpace()) {
				++position;
			}
			parts.push_back(line.mid(start, position - start));
			continue;
		}
		auto closed = false;
		auto escaped = false;
		while (++position < line.size()) {
			const auto ch = line[position];
			if (escaped) {
				escaped = false;
			} else if (ch == '\\') {
				escaped = true;
			} else if (ch == '"') {
				++position;
				closed = true;
				break;
			}
		}
		if (!closed || (position < line.size() && !line[position].isSpace())) {
			reply(socket, Result::Err(u"invalid quoted argument"_q));
			return;
		}
		// 引用参数采用 JSON 字符串，保留空值、换行与转义字符。
		auto error = QJsonParseError();
		const auto value = QJsonDocument::fromJson(
			('[' + line.mid(start, position - start) + ']').toUtf8(), &error);
		if (error.error != QJsonParseError::NoError) {
			reply(socket, Result::Err(u"invalid argument escape"_q));
			return;
		}
		parts.push_back(value.array().at(0).toString());
	}
	if (parts.isEmpty()) {
		reply(socket, Result::Err(u"expected a command"_q));
		return;
	}
	const auto command = parts.takeFirst();
	reply(socket, Execute(command, parts));
}

void setupConnection(not_null<QTcpSocket*> socket) {
	struct Input {
		QByteArray bytes;
		bool handled = false;
	};
	const auto input = std::make_shared<Input>();
	const auto consume = [=] {
		if (input->handled) {
			return;
		}
		input->bytes.append(socket->readAll());
		const auto end = input->bytes.indexOf('\n');
		if (end < 0) {
			return;
		}
		// 收齐整行后再解码，UTF-8 字符可能跨 TCP 分包。
		input->handled = true;
		handleLine(socket, QString::fromUtf8(input->bytes.constData(), end));
	};
	QObject::connect(socket, &QTcpSocket::readyRead, socket, consume);
	QObject::connect(socket, &QTcpSocket::disconnected,
		socket, &QObject::deleteLater);
	if (socket->bytesAvailable()) {
		consume();
	}
}

} // namespace

void StartServer() {
	if (Server) {
		return;
	}
	InstallCrashHandler();
	auto server = std::make_unique<QTcpServer>();
	if (!server->listen(QHostAddress::LocalHost, kPort)) {
		LOG(("Debug server: cannot listen on %1: %2"
			).arg(kPort
			).arg(server->errorString()));
		return;
	}
	const auto raw = server.get();
	QObject::connect(raw, &QTcpServer::newConnection, raw, [=] {
		while (const auto socket = raw->nextPendingConnection()) {
			setupConnection(socket);
		}
	});
	Server = std::move(server);
	// 静态持有的服务端在 QApplication 析构前释放。
	QObject::connect(
		qApp,
		&QCoreApplication::aboutToQuit,
		qApp,
		[] { Server = nullptr; },
		Qt::UniqueConnection);
	LOG(("Debug server: listening on 127.0.0.1:%1").arg(kPort));
}

void StopServer() {
	Server = nullptr;
}

int ServerPort() {
	return Server ? kPort : 0;
}

} // namespace ExtrasDebug
#endif // _DEBUG
