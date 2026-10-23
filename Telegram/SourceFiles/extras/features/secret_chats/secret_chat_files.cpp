#include "extras/features/secret_chats/secret_chat_files.h"

#include "base/random.h"
#include "crl/crl_async.h"
#include "crl/crl_on_main.h"
#include "mtproto/mtproto_dc_options.h"

#include <QtCore/QCryptographicHash>

namespace Extras::SecretChats {
namespace {

constexpr auto kPartSize = 512 * 1024;
constexpr auto kBigFileThreshold = 10 * 1024 * 1024;

} // namespace

struct FileTransfer::Upload {
	quint64 transferId = 0;
	mtpRequestId requestId = 0;
	bool finished = false;
	QByteArray bytes;
	QString checksum;
	quint64 id = 0;
	int fingerprint = 0;
	int part = 0;
	int parts = 0;
	bool big = false;
	Fn<void(std::optional<MTPInputEncryptedFile>, QString)> done;
};

struct FileTransfer::Download {
	quint64 transferId = 0;
	mtpRequestId requestId = 0;
	bool finished = false;
	MTPEncryptedFile file;
	QByteArray bytes;
	qint64 size = 0;
	Fn<void(QByteArray, QString)> done;
};

FileTransfer::FileTransfer(not_null<MTP::Instance*> instance)
: MTP::Sender(instance) {
}

FileTransfer::~FileTransfer() = default;

quint64 FileTransfer::upload(
		QByteArray ciphertext,
		qint32 fingerprint,
		Fn<void(std::optional<MTPInputEncryptedFile>, QString)> done) {
	if (ciphertext.isEmpty()
		|| ciphertext.size() > kMaxDocumentBytes + 16
		|| ciphertext.size() % 16) {
		done(std::nullopt, u"invalid encrypted document size"_q);
		return 0;
	}
	const auto operation = std::make_shared<Upload>();
	operation->bytes = std::move(ciphertext);
	operation->fingerprint = fingerprint;
	operation->done = std::move(done);
	operation->parts = (operation->bytes.size() + kPartSize - 1) / kPartSize;
	operation->big = operation->bytes.size() > kBigFileThreshold;
	operation->transferId = _nextId++;
	_uploads.emplace(operation->transferId, operation);
	do {
		operation->id = base::RandomValue<quint64>();
	} while (!operation->id);
	const auto weak = base::make_weak(this);
	const auto bytes = operation->bytes;
	const auto big = operation->big;
	crl::async([weak, operation, bytes, big] {
		const auto checksum = big ? QString() : QString::fromLatin1(
			QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toHex());
		crl::on_main([weak, operation, checksum] {
			if (!weak || operation->finished) {
				return;
			}
			operation->checksum = checksum;
			weak->uploadPart(operation);
		});
	});
	return operation->transferId;
}

void FileTransfer::uploadPart(std::shared_ptr<Upload> operation) {
	if (operation->part == operation->parts) {
		const auto input = operation->big
			? MTP_inputEncryptedFileBigUploaded(
				MTP_long(operation->id), MTP_int(operation->parts),
				MTP_int(operation->fingerprint))
			: MTP_inputEncryptedFileUploaded(
				MTP_long(operation->id), MTP_int(operation->parts),
				MTP_string(operation->checksum), MTP_int(operation->fingerprint));
		completeUpload(operation, input, QString());
		return;
	}
	const auto part = operation->bytes.mid(operation->part * kPartSize, kPartSize);
	const auto done = [=](const MTPBool &result) {
		operation->requestId = 0;
		if (result.type() != mtpc_boolTrue) {
			completeUpload(operation, std::nullopt, u"encrypted upload part rejected"_q);
			return;
		}
		++operation->part;
		uploadPart(operation);
	};
	const auto fail = [=](const MTP::Error &error) {
		operation->requestId = 0;
		completeUpload(operation, std::nullopt, error.type());
	};
	if (operation->big) {
		operation->requestId = request(MTPupload_SaveBigFilePart(
			MTP_long(operation->id), MTP_int(operation->part),
			MTP_int(operation->parts), MTP_bytes(part)
		)).done(done).fail(fail).send();
		return;
	}
	operation->requestId = request(MTPupload_SaveFilePart(
		MTP_long(operation->id), MTP_int(operation->part), MTP_bytes(part)
	)).done(done).fail(fail).send();
}

quint64 FileTransfer::download(
		const MTPEncryptedFile &file,
		Fn<void(QByteArray, QString)> done) {
	if (file.type() != mtpc_encryptedFile) {
		done(QByteArray(), u"encrypted file reference is empty"_q);
		return 0;
	}
	const auto &data = file.c_encryptedFile();
	const auto size = data.vsize().v;
	if (size <= 0 || size > kMaxDocumentBytes + 16 || size % 16
		|| data.vdc_id().v <= 0) {
		done(QByteArray(), u"invalid encrypted file reference"_q);
		return 0;
	}
	const auto operation = std::make_shared<Download>();
	operation->file = file;
	operation->size = size;
	operation->bytes.reserve(size);
	operation->done = std::move(done);
	operation->transferId = _nextId++;
	_downloads.emplace(operation->transferId, operation);
	downloadPart(operation);
	return operation->transferId;
}

void FileTransfer::downloadPart(std::shared_ptr<Download> operation) {
	if (operation->bytes.size() == operation->size) {
		completeDownload(operation, std::move(operation->bytes), QString());
		return;
	}
	const auto &data = operation->file.c_encryptedFile();
	operation->requestId = request(MTPupload_GetFile(
		MTP_flags(MTPupload_GetFile::Flag::f_precise),
		MTP_inputEncryptedFileLocation(data.vid(), data.vaccess_hash()),
		MTP_long(operation->bytes.size()), MTP_int(kPartSize)
	)).toDC(MTP::downloadDcId(data.vdc_id().v, 0)
	).done([=](const MTPupload_File &result) {
		operation->requestId = 0;
		if (result.type() != mtpc_upload_file) {
			completeDownload(operation, QByteArray(), u"unexpected encrypted download redirect"_q);
			return;
		}
		const auto &part = result.c_upload_file().vbytes().v;
		const auto left = operation->size - operation->bytes.size();
		if (part.isEmpty() || part.size() > left || part.size() > kPartSize) {
			completeDownload(operation, QByteArray(), u"invalid encrypted download part"_q);
			return;
		}
		operation->bytes.append(part);
		downloadPart(operation);
	}).fail([=](const MTP::Error &error) {
		operation->requestId = 0;
		completeDownload(operation, QByteArray(), error.type());
	}).send();
}

void FileTransfer::completeUpload(
		std::shared_ptr<Upload> operation,
		std::optional<MTPInputEncryptedFile> file,
		QString error) {
	operation->finished = true;
	_uploads.erase(operation->transferId);
	operation->bytes.clear();
	operation->done(std::move(file), std::move(error));
}

void FileTransfer::completeDownload(
		std::shared_ptr<Download> operation,
		QByteArray ciphertext,
		QString error) {
	operation->finished = true;
	_downloads.erase(operation->transferId);
	operation->bytes.clear();
	operation->done(std::move(ciphertext), std::move(error));
}

void FileTransfer::cancel(quint64 transferId) {
	if (const auto i = _uploads.find(transferId); i != _uploads.end()) {
		const auto operation = i->second;
		if (operation->requestId) {
			request(operation->requestId).cancel();
		}
		completeUpload(operation, std::nullopt, u"encrypted transfer cancelled"_q);
		return;
	}
	if (const auto i = _downloads.find(transferId); i != _downloads.end()) {
		const auto operation = i->second;
		if (operation->requestId) {
			request(operation->requestId).cancel();
		}
		completeDownload(operation, QByteArray(), u"encrypted transfer cancelled"_q);
	}
}

} // namespace Extras::SecretChats
