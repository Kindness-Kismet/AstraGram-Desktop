#pragma once

#include "mtproto/mtproto_response.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class BoxContent;
} // namespace Ui

struct GiftPasswordStep {
	Fn<void(
		const QByteArray &tmpPassword,
		Fn<void()> done,
		Fn<void(const MTP::Error &)> fail)> send;
	Fn<void()> boxShown;
	Fn<void()> succeeded;
	Fn<void(const MTP::Error &)> failed;
	Fn<void()> dismissed;
	bool closeOnFail = false;
};

void RequestWithGiftPassword(
		not_null<Window::SessionController*> window,
		not_null<Ui::BoxContent*> confirmation,
		GiftPasswordStep step);
