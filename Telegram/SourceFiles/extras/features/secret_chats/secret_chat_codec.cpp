#include "extras/features/secret_chats/secret_chat_codec.h"

#include "base/random.h"

#include <QtCore/QtEndian>

#include <bit>
#include <cmath>

namespace Extras::SecretChats::Codec {
namespace {

constexpr auto kLayer = quint32(0x1be31789);
constexpr auto kMessage = quint32(0x91cc4674);
constexpr auto kOldMessage = quint32(0x36b091de);
constexpr auto kService = quint32(0x73164160);
constexpr auto kDocument = quint32(0x6abd9782);
constexpr auto kOldDocument = quint32(0x7afe8ae2);
constexpr auto kVector = quint32(0x1cb5c415);
constexpr auto kFilename = quint32(0x15590068);
constexpr auto kMaxThumbBytes = 128 * 1024;
constexpr auto kMaxIds = 8192;

class Reader {
public:
	explicit Reader(const QByteArray &data) : _data(data) {
	}
	quint32 word() {
		if (!take(4)) {
			return 0;
		}
		return qFromLittleEndian<quint32>(_data.constData() + _offset - 4);
	}
	qint32 integer() {
		return std::bit_cast<qint32>(word());
	}
	qint64 longInteger() {
		if (!take(8)) {
			return 0;
		}
		return qFromLittleEndian<qint64>(_data.constData() + _offset - 8);
	}
	double real() {
		return std::bit_cast<double>(longInteger());
	}
	QByteArray bytes(int limit = kMaxPacketBytes) {
		if (!take(1)) {
			return {};
		}
		auto size = int(quint8(_data[_offset - 1]));
		auto header = 1;
		if (size == 254) {
			if (!take(3)) {
				return {};
			}
			const auto p = reinterpret_cast<const uchar*>(_data.constData() + _offset - 3);
			size = int(p[0]) | (int(p[1]) << 8) | (int(p[2]) << 16);
			header = 4;
			if (size < 254) {
				fail();
				return {};
			}
		}
		if (size == 255 && header == 1) {
			fail();
			return {};
		}
		const auto padding = (4 - ((size + header) % 4)) % 4;
		if (size > limit || !take(size + padding)) {
			fail();
			return {};
		}
		const auto start = _offset - size - padding;
		for (auto i = 0; i != padding; ++i) {
			if (_data[start + size + i] != '\0') {
				fail();
				return {};
			}
		}
		return _data.mid(start, size);
	}
	QString string() {
		const auto value = bytes();
		auto result = QString::fromUtf8(value);
		if (result.toUtf8() != value) {
			fail();
		}
		return result;
	}
	int count(int limit, int minimumBytes) {
		if (word() != kVector) {
			fail();
			return 0;
		}
		const auto result = integer();
		if (result < 0 || result > limit || result > remaining() / minimumBytes) {
			fail();
			return 0;
		}
		return result;
	}
	bool good() const {
		return _good;
	}
	bool done() const {
		return _good && !_data.isEmpty() && _offset == _data.size();
	}
	bool fail() {
		_good = false;
		return false;
	}

private:
	int remaining() const {
		return _data.size() - _offset;
	}
	bool take(int size) {
		if (!_good || size > remaining()) {
			return fail();
		}
		_offset += size;
		return true;
	}
	const QByteArray &_data;
	int _offset = 0;
	bool _good = true;
};

class Writer {
public:
	void word(quint32 value) {
		char bytes[4];
		qToLittleEndian(value, bytes);
		_data.append(bytes, 4);
	}
	void longInteger(qint64 value) {
		char bytes[8];
		qToLittleEndian(value, bytes);
		_data.append(bytes, 8);
	}
	void bytes(const QByteArray &value) {
		const auto start = _data.size();
		if (value.size() < 254) {
			_data.append(char(value.size()));
		} else {
			_data.append(char(254));
			_data.append(char(value.size() & 255));
			_data.append(char((value.size() >> 8) & 255));
			_data.append(char((value.size() >> 16) & 255));
		}
		_data.append(value);
		while ((_data.size() - start) % 4) {
			_data.append('\0');
		}
	}
	void string(const QString &value) {
		bytes(value.toUtf8());
	}
	QByteArray finish() {
		return std::move(_data);
	}
private:
	QByteArray _data;
};

bool dimensions(Reader &reader, int count) {
	for (auto i = 0; i != count; ++i) {
		if (reader.integer() < 0) {
			return reader.fail();
		}
	}
	return reader.good();
}

bool location(Reader &reader) {
	const auto type = reader.word();
	if (type == 0x53d69076) {
		reader.integer();
	} else if (type != 0x7c596b46) {
		return reader.fail();
	}
	reader.longInteger();
	reader.integer();
	reader.longInteger();
	return reader.good();
}

bool photoSize(Reader &reader) {
	const auto type = reader.word();
	reader.string();
	if (type == 0x0e17e23c) {
		return reader.good();
	}
	if ((type != 0x77bfb61b && type != 0xe9a734fa)
		|| !location(reader)
		|| !dimensions(reader, 2)) {
		return reader.fail();
	}
	if (type == 0x77bfb61b) {
		return dimensions(reader, 1);
	}
	reader.bytes(kMaxThumbBytes);
	return reader.good();
}

bool attributes(Reader &reader, Packet &packet) {
	const auto count = reader.count(1024, 4);
	for (auto i = 0; i != count && reader.good(); ++i) {
		const auto type = reader.word();
		if (type == kFilename) {
			packet.fileName = reader.string();
			continue;
		}
		packet.unsupported = true;
		switch (type) {
		case 0x6c37c15c:
			if (!dimensions(reader, 2)) return false;
			break;
		case 0x11b58939:
			break;
		case 0x3a556302: {
			reader.string();
			const auto set = reader.word();
			if (set == 0x861cc8a0) {
				reader.string();
			} else if (set != 0xffb62b95) {
				return reader.fail();
			}
		} break;
		case 0x5910cccb:
			if (packet.layer >= 66 || !dimensions(reader, 3)) return reader.fail();
			break;
		case 0x0ef02ce6:
			if (packet.layer < 66 || (reader.word() & ~quint32(1))
				|| !dimensions(reader, 3)) return reader.fail();
			break;
		case 0x9852f9c6: {
			const auto flags = reader.word();
			if ((flags & ~quint32(0x407)) || !dimensions(reader, 1)) return reader.fail();
			if (flags & 1) reader.string();
			if (flags & 2) reader.string();
			if (flags & 4) reader.bytes();
		} break;
		default:
			return reader.fail();
		}
	}
	return reader.good();
}

bool fileKeys(Reader &reader) {
	const auto key = reader.bytes(32);
	const auto iv = reader.bytes(32);
	return (key.size() == 32 && iv.size() == 32) || reader.fail();
}

bool coordinates(Reader &reader) {
	const auto latitude = reader.real();
	const auto longitude = reader.real();
	return (std::isfinite(latitude) && latitude >= -90 && latitude <= 90
		&& std::isfinite(longitude) && longitude >= -180 && longitude <= 180)
		|| reader.fail();
}

bool media(Reader &reader, Packet &packet) {
	const auto type = reader.word();
	if (type == 0x089f5c4a) {
		return reader.good();
	}
	if (type == kDocument || type == kOldDocument) {
		if ((type == kDocument) != (packet.layer >= 143)) {
			return reader.fail();
		}
		packet.document = true;
		packet.thumb = reader.bytes(kMaxThumbBytes);
		packet.thumbWidth = reader.integer();
		packet.thumbHeight = reader.integer();
		packet.mimeType = reader.string();
		packet.fileSize = (type == kDocument) ? reader.longInteger() : reader.integer();
		packet.fileKey = reader.bytes(32);
		packet.fileIv = reader.bytes(32);
		if (packet.thumbWidth < 0 || packet.thumbHeight < 0
			|| packet.fileSize <= 0
			|| packet.fileKey.size() != 32 || packet.fileIv.size() != 32
			|| !attributes(reader, packet)) {
			return reader.fail();
		}
		packet.caption = reader.string();
		packet.unsupported = packet.unsupported || packet.fileSize > kMaxFileBytes;
		return reader.good();
	}
	packet.unsupported = true;
	switch (type) {
	case 0xf1fa8d78:
		reader.bytes(kMaxThumbBytes);
		if (!dimensions(reader, 5) || !fileKeys(reader)) return reader.fail();
		reader.string();
		break;
	case 0x970c8c0e:
		reader.bytes(kMaxThumbBytes);
		if (!dimensions(reader, 3)) return false;
		reader.string();
		if (!dimensions(reader, 3) || !fileKeys(reader)) return reader.fail();
		reader.string();
		break;
	case 0x57e0a9cb:
		if (!dimensions(reader, 1)) return false;
		reader.string();
		if (!dimensions(reader, 1) || !fileKeys(reader)) return reader.fail();
		break;
	case 0x35480a59:
		return coordinates(reader);
	case 0x588a0a97:
		reader.string();
		reader.string();
		reader.string();
		reader.integer();
		break;
	case 0x8a0df56f:
		if (!coordinates(reader)) return false;
		for (auto i = 0; i != 4; ++i) reader.string();
		break;
	case 0xe50511d8:
		reader.string();
		break;
	case 0xfa95b0dd:
		reader.longInteger();
		reader.longInteger();
		reader.integer();
		reader.string();
		if (!dimensions(reader, 1) || !photoSize(reader)) return reader.fail();
		reader.integer();
		return attributes(reader, packet);
	default:
		return reader.fail();
	}
	return reader.good();
}

bool entities(Reader &reader, Packet &packet) {
	const auto count = reader.count(4096, 12);
	for (auto i = 0; i != count && reader.good(); ++i) {
		const auto type = reader.word();
		const auto offset = reader.integer();
		const auto length = reader.integer();
		if (offset < 0 || length < 0 || offset > packet.text.size()
			|| length > packet.text.size() - offset) {
			return reader.fail();
		}
		switch (type) {
		case 0xbb92ba95: case 0xfa04579d: case 0x6f635b0d: case 0x6cef8ac7:
		case 0x6ed02538: case 0x64e475c2: case 0xbd610bc9: case 0x826f8b60:
		case 0x28a20571:
			break;
		case 0x73924be0: case 0x76a6d327:
			reader.string();
			break;
		case 0x9c4e7e8b: case 0xbf0693d4: case 0x020df5d0:
			if (packet.layer < 101) return reader.fail();
			break;
		case 0x32ca960f:
			if (packet.layer < 144) return reader.fail();
			packet.unsupported = true;
			break;
		case 0xc8cf05f8:
			if (packet.layer < 144) return reader.fail();
			reader.longInteger();
			packet.unsupported = true;
			break;
		default:
			return reader.fail();
		}
	}
	return reader.good();
}

bool typing(quint32 type, int layer) {
	switch (type) {
	case 0x16bf744e: case 0xfd5ec8f5: case 0xa187d66f: case 0x92042ff7:
	case 0xd52f73f7: case 0xe6ac8a6f: case 0x990a3c1a: case 0x8faee98e:
	case 0x176f8ba1: case 0x628cbc6f:
		return true;
	case 0x88f27fbc: case 0xbb718624:
		return layer >= 73;
	default:
		return false;
	}
}

bool service(Reader &reader, Packet &packet) {
	const auto type = reader.word();
	switch (type) {
	case 0xf3048883:
		packet.action = Action::NotifyLayer;
		packet.value = reader.integer();
		if (packet.value <= 0) return reader.fail();
		break;
	case 0xa1733aec:
		packet.action = Action::SetTtl;
		packet.value = reader.integer();
		if (packet.value < 0) return reader.fail();
		break;
	case 0x0c4f40be: case 0x65614304: case 0x8ac1f475: {
		packet.action = type == 0x0c4f40be ? Action::Read
			: type == 0x65614304 ? Action::Delete : Action::Screenshot;
		const auto count = reader.count(kMaxIds, 8);
		packet.ids.reserve(count);
		for (auto i = 0; i != count && reader.good(); ++i) {
			packet.ids.push_back(reader.longInteger());
		}
	} break;
	case 0x6719e45c:
		packet.action = Action::Flush;
		break;
	case 0x511110b0:
		packet.action = Action::Resend;
		packet.value = reader.integer();
		packet.endSeq = reader.integer();
		if (packet.value < 0 || packet.endSeq < packet.value) return reader.fail();
		break;
	case 0xf3c9611b: case 0x6fe1735b:
		packet.action = type == 0xf3c9611b ? Action::RequestKey : Action::AcceptKey;
		packet.exchangeId = reader.longInteger();
		packet.publicValue = reader.bytes(256);
		if (packet.publicValue.isEmpty()) return reader.fail();
		if (packet.action == Action::AcceptKey) packet.keyFingerprint = reader.longInteger();
		break;
	case 0xdd05ec6b:
		packet.action = Action::AbortKey;
		packet.exchangeId = reader.longInteger();
		break;
	case 0xec2e0b9b:
		packet.action = Action::CommitKey;
		packet.exchangeId = reader.longInteger();
		packet.keyFingerprint = reader.longInteger();
		break;
	case 0xa82fdd63:
		packet.action = Action::Noop;
		break;
	case 0xccb27641:
		packet.action = Action::Typing;
		packet.value = std::bit_cast<qint32>(reader.word());
		if (!typing(std::bit_cast<quint32>(qint32(packet.value)), packet.layer)) return reader.fail();
		break;
	default:
		return reader.fail();
	}
	return reader.good();
}

bool writeService(Writer &writer, const Packet &packet) {
	switch (packet.action) {
	case Action::NotifyLayer:
		if (packet.value <= 0) return false;
		writer.word(0xf3048883);
		writer.word(packet.value);
		break;
	case Action::SetTtl:
		if (packet.value < 0) return false;
		writer.word(0xa1733aec);
		writer.word(packet.value);
		break;
	case Action::Read: case Action::Delete: case Action::Screenshot:
		if (packet.ids.size() > kMaxIds) return false;
		writer.word(packet.action == Action::Read ? 0x0c4f40be
			: packet.action == Action::Delete ? 0x65614304 : 0x8ac1f475);
		writer.word(kVector);
		writer.word(packet.ids.size());
		for (const auto id : packet.ids) writer.longInteger(id);
		break;
	case Action::Flush:
		writer.word(0x6719e45c);
		break;
	case Action::Resend:
		if (packet.value < 0 || packet.endSeq < packet.value) return false;
		writer.word(0x511110b0);
		writer.word(packet.value);
		writer.word(packet.endSeq);
		break;
	case Action::RequestKey: case Action::AcceptKey:
		if (packet.publicValue.isEmpty() || packet.publicValue.size() > 256) return false;
		writer.word(packet.action == Action::RequestKey ? 0xf3c9611b : 0x6fe1735b);
		writer.longInteger(packet.exchangeId);
		writer.bytes(packet.publicValue);
		if (packet.action == Action::AcceptKey) writer.longInteger(packet.keyFingerprint);
		break;
	case Action::CommitKey:
		writer.word(0xec2e0b9b);
		writer.longInteger(packet.exchangeId);
		writer.longInteger(packet.keyFingerprint);
		break;
	case Action::AbortKey:
		writer.word(0xdd05ec6b);
		writer.longInteger(packet.exchangeId);
		break;
	case Action::Noop:
		writer.word(0xa82fdd63);
		break;
	case Action::Typing: {
		const auto type = packet.value ? quint32(packet.value) : quint32(0x16bf744e);
		if (!typing(type, packet.layer)) return false;
		writer.word(0xccb27641);
		writer.word(type);
	} break;
	case Action::None:
		return false;
	}
	return true;
}

} // namespace

std::optional<QByteArray> encode(const Packet &packet) {
	if (packet.layer < 46 || packet.layer > 144 || packet.inSeq < 0
		|| packet.outSeq < 0 || !packet.randomId || packet.ttl < 0
		|| packet.unsupported || packet.text.size() > kMaxPacketBytes
		|| packet.caption.size() > kMaxPacketBytes
		|| packet.fileName.size() > kMaxPacketBytes
		|| packet.mimeType.size() > kMaxPacketBytes) {
		return std::nullopt;
	}
	auto writer = Writer();
	writer.word(kLayer);
	auto random = QByteArray(16, '\0');
	base::RandomFill(random.data(), random.size());
	writer.bytes(random);
	writer.word(packet.layer);
	writer.word(packet.inSeq);
	writer.word(packet.outSeq);
	if (packet.action != Action::None) {
		writer.word(kService);
		writer.longInteger(packet.randomId);
		if (!writeService(writer, packet)) return std::nullopt;
	} else {
		writer.word(packet.layer >= 73 ? kMessage : kOldMessage);
		writer.word(packet.document ? (1 << 9) : 0);
		writer.longInteger(packet.randomId);
		writer.word(packet.ttl);
		writer.string(packet.text);
		if (packet.document) {
			if (packet.fileSize <= 0 || packet.fileSize > kMaxFileBytes
				|| packet.fileKey.size() != 32 || packet.fileIv.size() != 32
				|| packet.thumb.size() > kMaxThumbBytes || packet.thumbWidth < 0
				|| packet.thumbHeight < 0) return std::nullopt;
			writer.word(packet.layer >= 143 ? kDocument : kOldDocument);
			writer.bytes(packet.thumb);
			writer.word(packet.thumbWidth);
			writer.word(packet.thumbHeight);
			writer.string(packet.mimeType);
			if (packet.layer >= 143) writer.longInteger(packet.fileSize);
			else writer.word(packet.fileSize);
			writer.bytes(packet.fileKey);
			writer.bytes(packet.fileIv);
			writer.word(kVector);
			writer.word(1);
			writer.word(kFilename);
			writer.string(packet.fileName);
			writer.string(packet.caption);
		}
	}
	auto result = writer.finish();
	return result.size() <= kMaxPacketBytes
		? std::optional<QByteArray>(std::move(result))
		: std::nullopt;
}

std::optional<Packet> decode(const QByteArray &bytes) {
	if (bytes.isEmpty() || bytes.size() > kMaxPacketBytes || bytes.size() % 4) {
		return std::nullopt;
	}
	auto reader = Reader(bytes);
	auto packet = Packet();
	if (reader.word() != kLayer || reader.bytes(1024).size() < 15) {
		return std::nullopt;
	}
	packet.layer = reader.integer();
	packet.inSeq = reader.integer();
	packet.outSeq = reader.integer();
	if (packet.layer < 46 || packet.layer > 144 || packet.inSeq < 0 || packet.outSeq < 0) {
		return std::nullopt;
	}
	const auto type = reader.word();
	if (type == kService) {
		packet.randomId = reader.longInteger();
		if (!service(reader, packet)) return std::nullopt;
	} else if (type == (packet.layer >= 73 ? kMessage : kOldMessage)) {
		const auto flags = reader.word();
		const auto allowed = quint32((1 << 9) | (1 << 7) | (1 << 11) | (1 << 3))
			| (packet.layer >= 73 ? quint32((1 << 1) | (1 << 5) | (1 << 17)) : 0);
		if (flags & ~allowed) return std::nullopt;
		packet.randomId = reader.longInteger();
		packet.ttl = reader.integer();
		packet.text = reader.string();
		if (packet.ttl < 0) return std::nullopt;
		if ((flags & (1 << 9)) && !media(reader, packet)) return std::nullopt;
		if ((flags & (1 << 7)) && !entities(reader, packet)) return std::nullopt;
		if (flags & (1 << 11)) reader.string();
		if (flags & (1 << 3)) reader.longInteger();
		if (flags & (1 << 17)) reader.longInteger();
	} else {
		return std::nullopt;
	}
	return packet.randomId && reader.done()
		? std::optional<Packet>(std::move(packet))
		: std::nullopt;
}

} // namespace Extras::SecretChats::Codec
