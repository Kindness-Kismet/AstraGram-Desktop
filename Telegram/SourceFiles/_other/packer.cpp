/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "packer.h"

#include "core/update_verify.h"

#include <QtCore/QDateTime>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

using std::cout;

namespace {

using Core::Updates::Channel;

std::optional<Core::Updates::Channel> V2Channel;
quint32 V2Counter = 0;
QString V2KeysLoc;
QString V2LocalKeyFile;
QString V2LocalKeyId;
QString V2SigningInputFile;
QString V2UnsignedFile;
std::vector<std::pair<QString, QString>> V2EmbedSignatures;
Core::Updates::Target V2Target;

struct BIODeleter {
	void operator()(BIO *value) {
		BIO_free(value);
	}
};

inline auto makeBIO(const void *buf, int len) {
	return std::unique_ptr<BIO, BIODeleter>{
		BIO_new_mem_buf(buf, len),
	};
}

void AppendLeU32(QByteArray &to, quint32 value) {
	const char bytes[4] = {
		char(value & 0xFF),
		char((value >> 8) & 0xFF),
		char((value >> 16) & 0xFF),
		char((value >> 24) & 0xFF),
	};
	to.append(bytes, 4);
}

void AppendLeU64(QByteArray &to, quint64 value) {
	AppendLeU32(to, quint32(value & 0xFFFFFFFFULL));
	AppendLeU32(to, quint32(value >> 32));
}

[[nodiscard]] QByteArray ReadFile(const QString &path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		cout << "Can't read '" << path.toUtf8().constData() << "'..\n";
		return QByteArray();
	}
	return file.readAll();
}

// 文件名后缀与发布通道保持一致。
[[nodiscard]] QString V2NameSuffix(Channel channel, quint32 counter) {
	switch (channel) {
	case Channel::Stable: return QString();
	case Channel::Beta: return QString("-beta");
	case Channel::CanaryPublic: return QString("-canary-%1").arg(counter);
	case Channel::CanaryPrivate:
		return QString("-canary-%1-private").arg(counter);
	}
	return QString();
}

[[nodiscard]] QString V2FileName(
		Channel channel,
		quint32 base,
		quint32 counter) {
	return QString("td-update-%1-%2-%3%4"
	).arg(QString::fromLatin1(Core::Updates::OsName(V2Target.os))
	).arg(QString::fromLatin1(Core::Updates::ArchName(V2Target.arch))
	).arg(base
	).arg(V2NameSuffix(channel, counter));
}

struct V2Keys {
	QByteArray rootPublicKeyPem;
	Core::Updates::Manifest manifest;
};

// 嵌入前验证根签名，确保打包使用已授权的发布密钥。
[[nodiscard]] std::optional<V2Keys> LoadV2Keys() {
	const auto rootPem = ReadFile(V2KeysLoc + "/root-public.pem");
	const auto manifest = ReadFile(V2KeysLoc + "/manifest.min.json");
	const auto signature = ReadFile(V2KeysLoc + "/manifest.sig");
	if (rootPem.isEmpty() || manifest.isEmpty() || signature.isEmpty()) {
		return std::nullopt;
	}
	auto error = QString();
	auto parsed = Core::Updates::ParseVerifiedManifest(
		manifest,
		signature,
		rootPem,
		&error);
	if (!parsed) {
		cout << "Manifest verification failed: "
			<< error.toUtf8().constData() << "\n";
		return std::nullopt;
	}
	return V2Keys{ rootPem, std::move(*parsed) };
}

// 客户端不按 manifest 到期时间拒绝更新；临近过期的发布密钥由打包阶段提示。
void ReportExpiry(const Core::Updates::Manifest &manifest, Channel channel) {
	constexpr auto kWarnDays = 90;
	const auto now = QDateTime::currentSecsSinceEpoch();
	const auto report = [&](const QByteArray &what, qint64 expires) {
		if (!expires) {
			return;
		}
		const auto days = (expires - now) / (24 * 60 * 60);
		if (days < 0) {
			cout << "WARNING: " << what.constData() << " expired "
				<< -days << " days ago!\n";
		} else if (days < kWarnDays) {
			cout << "WARNING: " << what.constData() << " expires in "
				<< days << " days!\n";
		} else {
			cout << what.constData() << " expires in " << days << " days.\n";
		}
	};
	report("The manifest", manifest.expires);
	const auto i = manifest.channels.find(Core::Updates::ChannelName(channel));
	if (i == manifest.channels.end()) {
		return;
	}
	for (const auto &group : i->second) {
		for (const auto &id : group) {
			for (const auto &key : manifest.keys) {
				if (key.id == id) {
					report("Key '" + id + "'", key.expires);
				}
			}
		}
	}
}

[[nodiscard]] QByteArray BuildV2Envelope(
		Channel channel,
		Core::Updates::Target target,
		quint64 version,
		qint64 created,
		const QByteArray &manifest,
		const QByteArray &manifestSignature,
		const std::vector<std::pair<QByteArray, QByteArray>> &signatures,
		const QByteArray &payload) {
	auto result = QByteArray();
	result.append(Core::Updates::kEnvelopeMagic, 4);
	AppendLeU32(result, Core::Updates::kEnvelopeFormat);
	result.append(char(uchar(channel)));
	result.append(char(uchar(target.os)));
	result.append(char(uchar(target.arch)));
	AppendLeU64(result, version);
	AppendLeU64(result, quint64(created));
	AppendLeU32(result, quint32(manifest.size()));
	result.append(manifest);
	AppendLeU32(result, quint32(manifestSignature.size()));
	result.append(manifestSignature);
	AppendLeU32(result, quint32(signatures.size()));
	for (const auto &[keyId, signature] : signatures) {
		AppendLeU32(result, quint32(keyId.size()));
		result.append(keyId);

		// ES256 签名使用原始 r||s 格式，客户端在验证时转换为 DER。
		AppendLeU32(result, quint32(signature.size()));
		result.append(signature);
	}
	AppendLeU32(result, quint32(payload.size()));
	result.append(payload);
	return result;
}

[[nodiscard]] QByteArray SignEd25519(
		const QByteArray &privateKeyPem,
		const QByteArray &message) {
	const auto bio = makeBIO(privateKeyPem.constData(), privateKeyPem.size());
	const auto key = PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr);
	if (!key) {
		cout << "Could not read the Ed25519 private key!\n";
		return QByteArray();
	}
	if (EVP_PKEY_id(key) != EVP_PKEY_ED25519) {
		cout << "The private key is not an Ed25519 key!\n";
		EVP_PKEY_free(key);
		return QByteArray();
	}
	auto result = QByteArray(64, Qt::Uninitialized);
	auto length = size_t(result.size());
	const auto context = EVP_MD_CTX_new();
	const auto success = context
		&& (EVP_DigestSignInit(context, nullptr, nullptr, nullptr, key) == 1)
		&& (EVP_DigestSign(
			context,
			reinterpret_cast<uchar*>(result.data()),
			&length,
			reinterpret_cast<const uchar*>(message.constData()),
			size_t(message.size())) == 1)
		&& (length == 64);
	EVP_MD_CTX_free(context);
	EVP_PKEY_free(key);
	if (!success) {
		cout << "Ed25519 signing failed!\n";
		return QByteArray();
	}
	return result;
}

// 成品使用正式客户端验证路径回验，通过后才能写出。
[[nodiscard]] bool VerifyPackedV2(
		const QByteArray &fileBytes,
		const V2Keys &keys,
		Channel channel,
		Core::Updates::Target target) {
	auto error = QString();
	const auto verified = Core::Updates::VerifyUpdate(
		fileBytes,
		channel,
		true,
		target,
		0,
		keys.manifest,
		keys.rootPublicKeyPem,
		QDateTime::currentSecsSinceEpoch(),
		&error);
	if (!verified) {
		cout << "Packed update verification failed: "
			<< error.toUtf8().constData() << "\n";
		return false;
	}
	return true;
}

[[nodiscard]] bool WriteWholeFile(
		const QString &path,
		const QByteArray &content) {
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)
		|| file.write(content) != content.size()) {
		cout << "Can't write '" << path.toUtf8().constData() << "'..\n";
		return false;
	}
	return true;
}

int WriteV2Update(const QByteArray &payload, quint32 baseVersion) {
	const auto keys = LoadV2Keys();
	if (!keys) {
		return -1;
	}
	const auto channel = *V2Channel;
	ReportExpiry(keys->manifest, channel);
	const auto version = Core::Updates::MakeUpdateVersion(
		baseVersion,
		V2Counter);
	const auto name = V2FileName(channel, baseVersion, V2Counter);

	auto signatures = std::vector<std::pair<QByteArray, QByteArray>>();
	const auto unsigned_ = BuildV2Envelope(
		channel,
		V2Target,
		version,
		QDateTime::currentSecsSinceEpoch(),
		keys->manifest.bytes,
		keys->manifest.signature,
		signatures,
		payload);

	auto error = QString();
	const auto envelope = Core::Updates::ParseEnvelope(unsigned_, &error);
	if (!envelope) {
		cout << "Built envelope did not parse back: "
			<< error.toUtf8().constData() << "\n";
		return -1;
	}
	const auto signingInput = Core::Updates::SigningInput(*envelope);
	if (signingInput.isEmpty()) {
		cout << "Could not build the signing input!\n";
		return -1;
	}

	if (!V2SigningInputFile.isEmpty()) {
		if (!WriteWholeFile(V2SigningInputFile, signingInput)
			|| !WriteWholeFile(name + ".unsigned", unsigned_)) {
			return -1;
		}
		cout << "Unsigned update '" << name.toUtf8().constData()
			<< ".unsigned' and signing input written, waiting for external signatures..\n";
		return 0;
	}

	const auto keyPem = ReadFile(V2LocalKeyFile);
	if (keyPem.isEmpty()) {
		return -1;
	}
	const auto signature = SignEd25519(keyPem, signingInput);
	if (signature.isEmpty()) {
		return -1;
	}
	signatures.push_back({ V2LocalKeyId.toUtf8(), signature });

	const auto result = BuildV2Envelope(
		channel,
		V2Target,
		version,
		envelope->created,
		keys->manifest.bytes,
		keys->manifest.signature,
		signatures,
		payload);
	if (!VerifyPackedV2(result, *keys, channel, V2Target)
		|| !WriteWholeFile(name, result)) {
		return -1;
	}
	cout << "Update file '" << name.toUtf8().constData()
		<< "' written successfully!\n";
	return 0;
}

int EmbedV2Signatures() {
	if (V2KeysLoc.isEmpty()) {
		cout << "The -keys-loc param is required to embed signatures!\n";
		return -1;
	} else if (!V2UnsignedFile.endsWith(".unsigned")) {
		cout << "The -unsigned param must point to an .unsigned file!\n";
		return -1;
	}
	const auto keys = LoadV2Keys();
	if (!keys) {
		return -1;
	}
	const auto bytes = ReadFile(V2UnsignedFile);
	if (bytes.isEmpty()) {
		return -1;
	}
	auto error = QString();
	const auto envelope = Core::Updates::ParseEnvelope(bytes, &error);
	if (!envelope) {
		cout << "Could not parse the unsigned update: "
			<< error.toUtf8().constData() << "\n";
		return -1;
	} else if (!envelope->signatures.empty()) {
		cout << "The unsigned update already has signatures!\n";
		return -1;
	}
	if (*V2Channel != envelope->channel) {
		cout << "The -channel param does not match the unsigned update!\n";
		return -1;
	}
	ReportExpiry(keys->manifest, envelope->channel);

	auto signatures = std::vector<std::pair<QByteArray, QByteArray>>();
	for (const auto &[keyId, file] : V2EmbedSignatures) {
		const auto signature = ReadFile(file);
		if (signature.isEmpty()) {
			return -1;
		}
		signatures.push_back({ keyId.toUtf8(), signature });
	}

	// 支持外部签名与本地 Ed25519 签名共同满足 manifest 的授权组。
	if (!V2LocalKeyFile.isEmpty()) {
		if (V2LocalKeyId.isEmpty()) {
			cout << "The -local-key param requires -local-key-id!\n";
			return -1;
		}
		const auto keyPem = ReadFile(V2LocalKeyFile);
		if (keyPem.isEmpty()) {
			return -1;
		}
		const auto signingInput = Core::Updates::SigningInput(*envelope);
		if (signingInput.isEmpty()) {
			cout << "Could not build the signing input!\n";
			return -1;
		}
		const auto signature = SignEd25519(keyPem, signingInput);
		if (signature.isEmpty()) {
			return -1;
		}
		signatures.push_back({ V2LocalKeyId.toUtf8(), signature });
	}
	if (signatures.empty()) {
		cout << "No signatures to embed, pass -embed-signatures or -local-key!\n";
		return -1;
	}

	const auto result = BuildV2Envelope(
		envelope->channel,
		envelope->target,
		envelope->version,
		envelope->created,
		envelope->manifest,
		envelope->manifestSignature,
		signatures,
		envelope->payload);
	const auto name = V2UnsignedFile.left(
		V2UnsignedFile.size() - int(strlen(".unsigned")));
	if (!VerifyPackedV2(result, *keys, envelope->channel, envelope->target)
		|| !WriteWholeFile(name, result)) {
		return -1;
	}
	cout << "Update file '" << name.toUtf8().constData()
		<< "' written successfully!\n";
	return 0;
}

bool ParseSignatureArguments(int &index, int argc, char *argv[]) {
	const auto count = V2EmbedSignatures.size();
	while (index + 1 < argc && argv[index + 1][0] != '-') {
		const auto entry = QString::fromLocal8Bit(argv[++index]);
		const auto colon = entry.indexOf(':');
		if (colon <= 0 || colon == entry.size() - 1) {
			cout << "Bad -embed-signatures entry, expected id:sigfile!\n";
			return false;
		}
		V2EmbedSignatures.push_back({
			entry.left(colon),
			entry.mid(colon + 1),
		});
	}
	if (V2EmbedSignatures.size() == count) {
		cout << "No -embed-signatures entries passed!\n";
		return false;
	}
	return true;
}

} // namespace

int main(int argc, char *argv[]) {
	QString remove;
	QString target;
	QString architecture;
	int version = 0;
	QFileInfoList files;
	const auto textOptions = std::map<QString, QString*>{
		{ "-target", &target },
		{ "-arch", &architecture },
		{ "-keys-loc", &V2KeysLoc },
		{ "-local-key", &V2LocalKeyFile },
		{ "-local-key-id", &V2LocalKeyId },
		{ "-emit-signing-input", &V2SigningInputFile },
		{ "-unsigned", &V2UnsignedFile },
	};
	for (auto i = 1; i < argc; ++i) {
		const auto option = QString::fromLocal8Bit(argv[i]);
		if (option == "-embed-signatures") {
			if (!ParseSignatureArguments(i, argc, argv)) {
				return -1;
			}
			continue;
		}
		if (i + 1 >= argc) {
			cout << "Missing value for " << option.toUtf8().constData() << "!\n";
			return -1;
		}
		const auto value = QString::fromLocal8Bit(argv[++i]);
		if (const auto entry = textOptions.find(option); entry != textOptions.end()) {
			*entry->second = value;
			continue;
		}
		if (option == "-path") {
			const auto info = QFileInfo(value);
			files.push_back(info);
			if (remove.isEmpty()) {
				remove = info.canonicalPath() + "/";
			}
			continue;
		}
		if (option == "-version") {
			version = value.toInt();
			continue;
		}
		if (option == "-channel") {
			V2Channel = Core::Updates::ChannelFromName(value.toLatin1());
			if (!V2Channel) {
				cout << "Bad -channel param value!\n";
				return -1;
			}
			continue;
		}
		if (option == "-counter") {
			auto valid = false;
			V2Counter = value.toUInt(&valid);
			if (!valid) {
				cout << "Bad -counter param value!\n";
				return -1;
			}
			continue;
		}
		cout << "Unknown parameter: " << option.toUtf8().constData() << "\n";
		return -1;
	}
	if (!V2Channel || V2KeysLoc.isEmpty()) {
		cout << "The -channel and -keys-loc params are required!\n";
		return -1;
	}
	if (V2LocalKeyFile.isEmpty() != V2LocalKeyId.isEmpty()) {
		cout << "The -local-key and -local-key-id params must be used together!\n";
		return -1;
	}
	if (!V2UnsignedFile.isEmpty()) {
		if (!V2SigningInputFile.isEmpty()) {
			cout << "The -unsigned and -emit-signing-input params are mutually exclusive!\n";
			return -1;
		}
		return EmbedV2Signatures();
	}
	const auto canary = (*V2Channel == Channel::CanaryPublic)
		|| (*V2Channel == Channel::CanaryPrivate);
	if (canary != (V2Counter > 0)) {
		cout << "Canary channels require a positive -counter, others require none!\n";
		return -1;
	}
	if (!V2EmbedSignatures.empty()) {
		cout << "The -embed-signatures param requires -unsigned!\n";
		return -1;
	}
	if (V2SigningInputFile.isEmpty() == V2LocalKeyFile.isEmpty()) {
		cout << "Use either -emit-signing-input or -local-key with -local-key-id!\n";
		return -1;
	}
	if (files.isEmpty() || remove.isEmpty() || version <= 1016 || version > 999999999) {
		cout << "Usage: Packer -path {file|dir} [-path ...] -version {AppUpdateVersion}\n";
		cout << "  -channel {stable|beta|canary-public|canary-private} -keys-loc {dir}\n";
		cout << "  -local-key {pem} -local-key-id {id} for Ed25519 signing, or\n";
		cout << "  -emit-signing-input {file}, then -unsigned {file} -embed-signatures {id}:{sigfile} ...\n";
		cout << "  Canary channels require -counter {n}. Windows requires -target {win64|winarm}.\n";
		cout << "  macOS requires -arch {x86_64|arm64}. Linux uses its native architecture.\n";
		return -1;
	}

	using Core::Updates::Os;
	using Core::Updates::Arch;
#ifdef Q_OS_WIN
	if ((target != "win64" && target != "winarm") || !architecture.isEmpty()) {
		cout << "Windows requires -target win64 or winarm, without -arch!\n";
		return -1;
	}
	V2Target = { Os::Windows, target == "winarm" ? Arch::Arm : Arch::X64 };
#else
	if (!target.isEmpty()) {
		cout << "The -target param is only supported on Windows!\n";
		return -1;
	}
#ifdef Q_OS_MACOS
	if (architecture != "x86_64" && architecture != "arm64") {
		cout << "macOS requires -arch x86_64 or arm64!\n";
		return -1;
	}
	V2Target = { Os::Mac, architecture == "arm64" ? Arch::Arm : Arch::X64 };
#else
#ifdef Q_PROCESSOR_ARM_64
	V2Target = { Os::Linux, Arch::Arm };
	const auto nativeArch = QString("arm64");
#else
	V2Target = { Os::Linux, Arch::X64 };
	const auto nativeArch = QString("x86_64");
#endif
	if (!architecture.isEmpty() && architecture != nativeArch) {
		cout << "The -arch param must match the native Linux Packer architecture!\n";
		return -1;
	}
#endif
#endif

	bool hasDirs = true;
	while (hasDirs) {
		hasDirs = false;
		for (QFileInfoList::iterator i = files.begin(); i != files.end(); ++i) {
			QFileInfo info(*i);
			if (info.isDir()) {
				hasDirs = true;
				files.erase(i);
				QDir d = QDir(info.absoluteFilePath());
				files.append(d.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoSymLinks | QDir::NoDotAndDotDot));
				break;
			} else if (!info.isReadable()) {
				cout << "Can't read: " << info.absoluteFilePath().toUtf8().constData() << "\n";
				return -1;
			} else if (info.isHidden()) {
				hasDirs = true;
				files.erase(i);
				break;
			}
		}
	}
	for (QFileInfoList::iterator i = files.begin(); i != files.end(); ++i) {
		QFileInfo info(*i);
		if (!info.canonicalFilePath().startsWith(remove)) {
			cout << "Can't find '" << remove.toUtf8().constData() << "' in file '" << info.canonicalFilePath().toUtf8().constData() << "' :(\n";
			return -1;
		}
	}

	QByteArray result;
	{
		QBuffer buffer(&result);
		buffer.open(QIODevice::WriteOnly);
		QDataStream stream(&buffer);
		stream.setVersion(QDataStream::Qt_5_1);

		stream << quint32(version);

		stream << quint32(files.size());
		cout << "Found " << files.size() << " file" << (files.size() == 1 ? "" : "s") << "..\n";
		for (QFileInfoList::iterator i = files.begin(); i != files.end(); ++i) {
			QFileInfo info(*i);
			QString fullName = info.canonicalFilePath();
			QString name = fullName.mid(remove.length());
			cout << name.toUtf8().constData() << " (" << info.size() << ")\n";

			QFile f(fullName);
			if (!f.open(QIODevice::ReadOnly)) {
				cout << "Can't open '" << fullName.toUtf8().constData() << "' for read..\n";
				return -1;
			}
			QByteArray inner = f.readAll();
			stream << name << quint32(inner.size()) << inner;
#ifndef Q_OS_WIN
			stream << (QFileInfo(fullName).isExecutable() ? true : false);
#endif
		}
		if (stream.status() != QDataStream::Ok) {
			cout << "Stream status is bad: " << stream.status() << "\n";
			return -1;
		}
	}

	if (result.size() <= 0 || result.size() > 1024 * 1024 * 1024) {
		cout << "Uncompressed payload size must be within 1..1073741824 bytes!\n";
		return -1;
	}
	const auto resultSize = qint32(result.size());
	cout << "Compression start, size: " << resultSize << "\n";

	QByteArray compressed, resultCheck;
#if defined Q_OS_WIN && !defined PACKER_USE_PACKAGED
	constexpr auto hPropsLen = LZMA_PROPS_SIZE;
	constexpr auto hOriginalSizeLen = sizeof(qint32);
	constexpr auto hSize = hPropsLen + hOriginalSizeLen;
	compressed.resize(hSize + resultSize + 1024 * 1024);

	size_t compressedLen = compressed.size() - hSize;
	size_t outPropsSize = LZMA_PROPS_SIZE;
	uchar *_dest = (uchar*)(compressed.data() + hSize);
	size_t *_destLen = &compressedLen;
	const uchar *_src = (const uchar*)(result.constData());
	size_t _srcLen = result.size();
	uchar *_outProps = (uchar*)(compressed.data());
	int res = LzmaCompress(_dest, _destLen, _src, _srcLen, _outProps, &outPropsSize, 9, 64 * 1024 * 1024, 4, 0, 2, 273, 2);
	if (res != SZ_OK) {
		cout << "Error in compression: " << res << "\n";
		return -1;
	}
	compressed.resize(int(hSize + compressedLen));
	memcpy(compressed.data() + hPropsLen, &resultSize, hOriginalSizeLen);

	cout << "Compressed to size: " << compressedLen << "\n";

	cout << "Checking uncompressed..\n";

	qint32 resultCheckLen;
	memcpy(&resultCheckLen, compressed.constData() + hPropsLen, hOriginalSizeLen);
	if (resultCheckLen <= 0 || resultCheckLen > 1024 * 1024 * 1024) {
		cout << "Bad result len: " << resultCheckLen << "\n";
		return -1;
	}
	resultCheck.resize(resultCheckLen);

	size_t resultLen = resultCheck.size();
	SizeT srcLen = compressedLen;
	int uncompressRes = LzmaUncompress((uchar*)resultCheck.data(), &resultLen, (const uchar*)(compressed.constData() + hSize), &srcLen, (const uchar*)(compressed.constData()), LZMA_PROPS_SIZE);
	if (uncompressRes != SZ_OK) {
		cout << "Uncompress failed: " << uncompressRes << "\n";
		return -1;
	}
	if (resultLen != size_t(result.size())) {
		cout << "Uncompress bad size: " << resultLen << ", was: " << result.size() << "\n";
		return -1;
	}
#else
	constexpr auto hOriginalSizeLen = sizeof(qint32);
	constexpr auto hSize = hOriginalSizeLen;
	compressed.resize(hSize + resultSize + 1024 * 1024);

	size_t compressedLen = compressed.size() - hSize;

	lzma_stream stream = LZMA_STREAM_INIT;

	int preset = 9 | LZMA_PRESET_EXTREME;
	lzma_ret ret = lzma_easy_encoder(&stream, preset, LZMA_CHECK_CRC64);
	if (ret != LZMA_OK) {
		const char *msg;
		switch (ret) {
			case LZMA_MEM_ERROR: msg = "Memory allocation failed"; break;
			case LZMA_OPTIONS_ERROR: msg = "Specified preset is not supported"; break;
			case LZMA_UNSUPPORTED_CHECK: msg = "Specified integrity check is not supported"; break;
			default: msg = "Unknown error, possibly a bug"; break;
		}
		cout << "Error initializing the encoder: " << msg << " (error code " << ret << ")\n";
		return -1;
	}

	stream.avail_in = resultSize;
	stream.next_in = (uint8_t*)result.constData();
	stream.avail_out = compressedLen;
	stream.next_out = (uint8_t*)(compressed.data() + hSize);

	lzma_ret res = lzma_code(&stream, LZMA_FINISH);
	compressedLen -= stream.avail_out;
	lzma_end(&stream);
	if (res != LZMA_OK && res != LZMA_STREAM_END) {
		const char *msg;
		switch (res) {
			case LZMA_MEM_ERROR: msg = "Memory allocation failed"; break;
			case LZMA_DATA_ERROR: msg = "File size limits exceeded"; break;
			default: msg = "Unknown error, possibly a bug"; break;
		}
		cout << "Error in compression: " << msg << " (error code " << res << ")\n";
		return -1;
	}

	compressed.resize(int(hSize + compressedLen));
	memcpy(compressed.data(), &resultSize, hOriginalSizeLen);

	cout << "Compressed to size: " << compressedLen << "\n";

	cout << "Checking uncompressed..\n";

	qint32 resultCheckLen;
	memcpy(&resultCheckLen, compressed.constData(), hOriginalSizeLen);
	if (resultCheckLen <= 0 || resultCheckLen > 1024 * 1024 * 1024) {
		cout << "Bad result len: " << resultCheckLen << "\n";
		return -1;
	}
	resultCheck.resize(resultCheckLen);

	size_t resultLen = resultCheck.size();

	stream = LZMA_STREAM_INIT;

	ret = lzma_stream_decoder(&stream, UINT64_MAX, LZMA_CONCATENATED);
	if (ret != LZMA_OK) {
		const char *msg;
		switch (ret) {
			case LZMA_MEM_ERROR: msg = "Memory allocation failed"; break;
			case LZMA_OPTIONS_ERROR: msg = "Specified preset is not supported"; break;
			case LZMA_UNSUPPORTED_CHECK: msg = "Specified integrity check is not supported"; break;
			default: msg = "Unknown error, possibly a bug"; break;
		}
		cout << "Error initializing the decoder: " << msg << " (error code " << ret << ")\n";
		return -1;
	}

	stream.avail_in = compressedLen;
	stream.next_in = (uint8_t*)(compressed.constData() + hSize);
	stream.avail_out = resultLen;
	stream.next_out = (uint8_t*)resultCheck.data();

	res = lzma_code(&stream, LZMA_FINISH);
	const auto availIn = stream.avail_in;
	const auto availOut = stream.avail_out;
	lzma_end(&stream);
	if (availIn) {
		cout << "Error in decompression, " << availIn << " bytes left in _in of " << compressedLen << " whole.\n";
		return -1;
	} else if (availOut) {
		cout << "Error in decompression, " << availOut << " bytes free left in _out of " << resultLen << " whole.\n";
		return -1;
	}
	if (res != LZMA_OK && res != LZMA_STREAM_END) {
		const char *msg;
		switch (res) {
			case LZMA_MEM_ERROR: msg = "Memory allocation failed"; break;
			case LZMA_FORMAT_ERROR: msg = "The input data is not in the .xz format"; break;
			case LZMA_OPTIONS_ERROR: msg = "Unsupported compression options"; break;
			case LZMA_DATA_ERROR: msg = "Compressed file is corrupt"; break;
			case LZMA_BUF_ERROR: msg = "Compressed data is truncated or otherwise corrupt"; break;
			default: msg = "Unknown error, possibly a bug"; break;
		}
		cout << "Error in decompression: " << msg << " (error code " << res << ")\n";
		return -1;
	}
#endif
	if (memcmp(result.constData(), resultCheck.constData(), resultLen)) {
		cout << "Data differ :(\n";
		return -1;
	}
	result = resultCheck = QByteArray();

	return WriteV2Update(compressed, quint32(version));
}
