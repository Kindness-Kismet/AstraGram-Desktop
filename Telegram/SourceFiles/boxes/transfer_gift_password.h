#pragma once

#include "boxes/transfer_gift_box.h"
#include "mtproto/mtproto_response.h"

namespace Ui {
class BoxContent;
} // namespace Ui

namespace GiftTransfer {

struct GiftPasswordStep {
	Fn<void(
		const QByteArray &tmpPassword,
		Fn<void()> done,
		Fn<void(const MTP::Error &)> fail)> send;
	Fn<void()> boxShown;
	Fn<void()> succeeded;
	Fn<void(const MTP::Error &)> failed;
	Fn<void()> dismissed;
};

void RequestWithGiftPassword(
		not_null<Window::SessionController*> window,
		not_null<Ui::BoxContent*> confirmation,
		GiftPasswordStep step);

} // namespace GiftTransfer
