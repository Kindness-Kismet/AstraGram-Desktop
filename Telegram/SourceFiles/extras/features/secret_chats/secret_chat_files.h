#pragma once

#include "base/weak_ptr.h"
#include "mtproto/sender.h"

#include <optional>
#include <map>

namespace Extras::SecretChats {

inline constexpr auto kMaxDocumentBytes = 32 * 1024 * 1024;

// 传输接口仅接收和返回密文，明文不进入普通媒体缓存。
class FileTransfer final : public MTP::Sender, public base::has_weak_ptr {
public:

	explicit FileTransfer(not_null<MTP::Instance*> instance);
	~FileTransfer();

	quint64 upload(
		QByteArray ciphertext,
		qint32 fingerprint,
		Fn<void(std::optional<MTPInputEncryptedFile>, QString)> done,
		Fn<void(qint64)> progress = {});
	void setUploadPaused(quint64 transferId, bool paused);
	quint64 download(
		const MTPEncryptedFile &file,
		Fn<void(QByteArray, QString)> done,
		Fn<void(qint64)> progress = {});
	void cancel(quint64 transferId);

private:
	struct Upload;
	struct Download;
	void uploadPart(std::shared_ptr<Upload> operation);
	void downloadPart(std::shared_ptr<Download> operation);
	void completeUpload(
		std::shared_ptr<Upload> operation,
		std::optional<MTPInputEncryptedFile> file,
		QString error);
	void completeDownload(
		std::shared_ptr<Download> operation,
		QByteArray ciphertext,
		QString error);

	quint64 _nextId = 1;
	std::map<quint64, std::shared_ptr<Upload>> _uploads;
	std::map<quint64, std::shared_ptr<Download>> _downloads;

};

} // namespace Extras::SecretChats
