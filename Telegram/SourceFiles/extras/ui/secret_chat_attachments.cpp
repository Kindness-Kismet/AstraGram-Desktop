#include "extras/ui/secret_chat_attachments.h"

#include "extras/features/secret_chats/secret_chat_dialog.h"
#include "extras/features/secret_chats/secret_chat_files.h"
#include "api/api_common.h"
#include "boxes/send_files_box.h"
#include "core/application.h"
#include "core/core_screenshot_protection.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "crl/crl_async.h"
#include "crl/crl_on_main.h"
#include "history/history.h"
#include "history/view/controls/history_view_compose_controls.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "menu/menu_send.h"
#include "storage/storage_media_prepare.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QBuffer>
#include <QtCore/QMimeDatabase>

namespace Extras::SecretChats {
namespace {

bool confirmAttachments(not_null<Window::SessionController*> controller,
		not_null<Ui::RpWidget*> parent, int chatId,
		not_null<HistoryView::ComposeControls*> compose,
		Ui::PreparedList list, const QString &insertTextOnCancel = {}) {
	const auto manager = base::make_weak(&controller->session().secretChats());
	const auto dialog = manager->dialog(chatId);
	const auto chat = manager->chat(chatId);
	if (!dialog || !chat || chat->state != State::Active) {
		return false;
	}
	if (list.error != Ui::PreparedList::Error::None) {
		controller->showToast(tr::extras_SecretChatFailed(tr::now));
		return false;
	}
	list.overrideSendImagesAsPhotos = false;
	const auto originalReply = compose->replyingToMessage();
	auto box = Box<SendFilesBox>(SendFilesBoxDescriptor{
		.show = controller->uiShow(),
		.list = std::move(list),
		.caption = compose->getTextWithAppliedMarkdown(),
		.toPeer = dialog->owningHistory()->peer,
		.limits = SendFilesAllow::Files | SendFilesAllow::Texts
			| SendFilesAllow::EmojiWithoutPremium,
		.check = [=](const Ui::PreparedFile &file, bool compress, bool silent) {
			const auto image = std::get_if<Ui::PreparedFileInformation::Image>(&file.information->media);
			const auto valid = !compress && !file.archive
				&& (file.size > 0 || !file.content.isEmpty() || image)
				&& file.size <= kMaxDocumentBytes && file.content.size() <= kMaxDocumentBytes;
			if (!valid && !silent) {
				controller->showToast(file.size > kMaxDocumentBytes
					? tr::extras_SecretChatFileLimit(tr::now)
					: tr::extras_SecretChatFailed(tr::now));
			}
			return valid;
		},
		.sendType = Api::SendType::Normal,
		.sendMenuDetails = [] { return SendMenu::Details(); },
		.confirmed = crl::guard(parent, [=](std::shared_ptr<Ui::PreparedBundle> bundle,
				Api::SendOptions, FullReplyTo replyTo) {
			if (!manager) {
				return;
			}
			const auto dialog = manager->dialog(chatId);
			if (!dialog) {
				return;
			}
			const auto randomId = dialog->randomId(replyTo.messageId.msg);
			if (compose->replyingToMessage().messageId == originalReply.messageId) {
				compose->cancelReplyMessage();
			}
			struct Batch {
				int pending = 0;
				bool failed = false;
				QStringList captions;
			};
			const auto batch = std::make_shared<Batch>();
			for (const auto &group : bundle->groups) {
				batch->pending += group.list.files.size();
			}
			const auto report = crl::guard(parent, [=](QString error, QString caption) {
				if (!error.isEmpty()) {
					batch->failed = true;
					if (!caption.isEmpty()) { batch->captions.push_back(std::move(caption)); }
				}
				if (--batch->pending || !batch->failed) {
					return;
				}
				controller->showToast(tr::extras_SecretChatFailed(tr::now));
				if (!compose->fieldLastText().isEmpty() || compose->replyingToMessage()) {
					return;
				}
				compose->setText({ batch->captions.join('\n') });
				if (manager && manager->dialog(chatId)
					&& manager->dialog(chatId)->randomId(replyTo.messageId.msg)) {
					compose->replyToMessage(replyTo);
				}
			});
			crl::async([=] {
				for (auto &group : bundle->groups) {
					for (auto &file : group.list.files) {
						auto name = file.displayName.isEmpty()
							? QFileInfo(file.path).fileName() : file.displayName;
						const auto mime = file.information->filemime;
						if (name.isEmpty()) {
							name = u"attachment."_q + QMimeDatabase().mimeTypeForName(mime).preferredSuffix();
						}
						const auto image = std::get_if<Ui::PreparedFileInformation::Image>(&file.information->media);
						const auto pixels = image ? image->data : QImage();
						const auto caption = file.caption.text;
						const auto path = file.path;
						auto bytes = std::move(file.content);
						if (bytes.isEmpty() && path.isEmpty() && !pixels.isNull()) {
							auto output = QBuffer(&bytes);
							output.open(QIODevice::WriteOnly);
							pixels.save(&output, "PNG");
						} else if (bytes.isEmpty()) {
							auto input = QFile(path);
							if (input.open(QIODevice::ReadOnly)) {
								bytes = input.read(kMaxDocumentBytes + 1);
							}
						}
						crl::on_main([=, bytes = std::move(bytes)]() mutable {
							if (!manager) {
								return;
							}
							manager->sendDocumentReply(chatId, name, mime, std::move(bytes),
								caption, randomId, [=](qint64, QString error) { report(error, caption); });
						});
					}
				}
			});
		}),
		.cancelled = compose->restoreTextCallback(insertTextOnCancel),
		.replyTo = compose->replyingToMessage(),
		.cancelled2 = crl::guard(parent, [=](const TextWithTags &text) { compose->setText(text); }),
	});
	box->takeTextWithTagsRequests() | rpl::on_next([=](TextWithTags text) {
		compose->setText(std::move(text));
	}, box->lifetime());
	Core::App().screenshotProtection().addContentReason(
		controller->widget(), box->shownValue(), box->lifetime());
	controller->show(std::move(box));
	return true;
}

} // namespace

void chooseAttachment(not_null<Window::SessionController*> controller,
		not_null<Ui::RpWidget*> parent, int chatId,
		not_null<HistoryView::ComposeControls*> compose) {
	FileDialog::GetOpenPaths(parent.get(), tr::lng_choose_files(tr::now),
		FileDialog::AllFilesFilter(), crl::guard(parent, [=](FileDialog::OpenResult &&result) {
			if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
				return;
			}
			if (!result.remoteContent.isEmpty()) {
				auto list = Ui::PreparedList();
				auto file = Ui::PreparedFile(QString());
				file.content = std::move(result.remoteContent);
				file.size = file.content.size();
				Storage::PrepareDetails(file, st::sendMediaPreviewSize, 0);
				list.files.push_back(std::move(file));
				confirmAttachments(controller, parent, chatId, compose, std::move(list));
				return;
			}
			auto list = Storage::PreparedFileFromFilesDialog(std::move(result),
				[](const Ui::PreparedList &) { return true; },
				[=](tr::phrase<> text) { controller->showToast(text(tr::now)); },
				st::sendMediaPreviewSize, false);
			if (list) {
				confirmAttachments(controller, parent, chatId, compose, std::move(*list));
			}
		}), nullptr);
}

bool insertAttachments(not_null<Window::SessionController*> controller,
		not_null<Ui::RpWidget*> parent, int chatId,
		not_null<HistoryView::ComposeControls*> compose,
		not_null<const QMimeData*> data) {
	const auto urls = Core::ReadMimeUrls(data);
	if (!urls.empty()) {
		auto list = Storage::PrepareMediaList(urls, st::sendMediaPreviewSize, false);
		if (list.error != Ui::PreparedList::Error::NonLocalUrl) {
			return confirmAttachments(controller, parent, chatId, compose,
				std::move(list), Core::ReadMimeText(data));
		}
	}
	if (auto image = Core::ReadMimeImage(data)) {
		return confirmAttachments(controller, parent, chatId, compose,
			Storage::PrepareMediaFromImage(std::move(image.image), std::move(image.content),
				st::sendMediaPreviewSize), Core::ReadMimeText(data));
	}
	return false;
}

} // namespace Extras::SecretChats
