/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/transfer_gift_password.h"
#include "apiwrap.h"
#include "api/api_cloud_password.h"
#include "base/call_delayed.h"
#include "base/unixtime.h"
#include "boxes/passcode_box.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/layers/box_content.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "window/window_session_controller.h"

namespace GiftTransfer {
namespace {

constexpr auto kTmpPasswordPeriod = 10 * TimeId(60);
constexpr auto kPasswordStateTimeout = 10 * crl::time(1000);

class GiftPasswordFlow final
	: public std::enable_shared_from_this<GiftPasswordFlow> {
public:
	GiftPasswordFlow(
		not_null<Window::SessionController*> window,
		not_null<Ui::BoxContent*> confirmation,
		GiftPasswordStep step);

	void start();

private:
	[[nodiscard]] bool canPrompt() const;
	void confirmationClosing();
	void waitForState();
	void stateReady(std::optional<Core::CloudPasswordState> state);
	void stateTimeout();
	void showPasswordBox(const Core::CloudPasswordState &state);
	bool showPrePasswordBox(const QString &type);
	void showBox(object_ptr<Ui::BoxContent> box);
	void boxClosing(not_null<Ui::BoxContent*> box);
	void requestTmpPassword(
		const Core::CloudPasswordResult &check,
		base::weak_qptr<PasscodeBox> box);
	void tmpPasswordReady(const MTPaccount_TmpPassword &result);
	void tmpPasswordFailed(
		const MTP::Error &error,
		base::weak_qptr<PasscodeBox> box);
	void sendGift(const QByteArray &tmpPassword, bool stored);
	void giftSent();
	void giftFailed(
		const MTP::Error &error,
		const QByteArray &tmpPassword,
		bool stored);
	void fail(const MTP::Error &error);

	const base::weak_ptr<Main::Session> _session;
	const std::shared_ptr<Ui::Show> _show;
	const base::weak_qptr<Ui::BoxContent> _confirmation;
	const GiftPasswordStep _step;
	base::weak_qptr<Ui::BoxContent> _box;
	bool _waiting = false;
	bool _issuing = false;
	bool _sending = false;
	bool _handedOff = false;
	bool _abandoned = false;
	bool _done = false;
	rpl::lifetime _lifetime;

};

// 礼物请求只携带临时密码，密码错误表示临时密码被服务器拒绝。
[[nodiscard]] bool TmpPasswordRefused(const QString &type) {
	const auto refusals = std::array{
		u"TMP_PASSWORD_INVALID"_q,
		u"PASSWORD_HASH_INVALID"_q,
		u"SRP_PASSWORD_CHANGED"_q,
	};
	return ranges::contains(refusals, type);
}

GiftPasswordFlow::GiftPasswordFlow(
	not_null<Window::SessionController*> window,
	not_null<Ui::BoxContent*> confirmation,
	GiftPasswordStep step)
: _session(base::make_weak(&window->session()))
, _show(window->uiShow())
, _confirmation(confirmation)
, _step(std::move(step)) {
}

void GiftPasswordFlow::start() {
	const auto session = _session.get();
	const auto confirmation = _confirmation.get();
	if (!session || !confirmation) {
		return;
	}
	confirmation->boxClosing() | rpl::on_next([=] {
		confirmationClosing();
	}, _lifetime);
	const auto stored = session->validTmpPassword();
	if (stored.isEmpty()) {
		waitForState();
	} else {
		sendGift(stored, true);
	}
}

bool GiftPasswordFlow::canPrompt() const {
	return !_abandoned
		&& (_handedOff ? (_box != nullptr) : (_confirmation != nullptr));
}

void GiftPasswordFlow::confirmationClosing() {
	if (!_handedOff) {
		_abandoned = true;
	}
}

void GiftPasswordFlow::waitForState() {
	const auto session = _session.get();
	if (!session) {
		return;
	}
	_waiting = true;
	base::call_delayed(
		kPasswordStateTimeout,
		_confirmation.get(),
		[self = shared_from_this()] { self->stateTimeout(); });
	session->api().cloudPassword().reload([weak = weak_from_this()](
			std::optional<Core::CloudPasswordState> state) {
		if (const auto strong = weak.lock()) {
			strong->stateReady(std::move(state));
		}
	});
}

void GiftPasswordFlow::stateReady(
		std::optional<Core::CloudPasswordState> state) {
	if (!base::take(_waiting) || !canPrompt()) {
		return;
	}
	if (!state) {
		fail(MTP::Error::Local(
			u"PASSWORD_STATE_FAILED"_q,
			u"account.getPassword failed"_q));
	} else if (!state->hasPassword) {
		showPrePasswordBox(u"PASSWORD_MISSING"_q);
	} else {
		showPasswordBox(*state);
	}
}

void GiftPasswordFlow::stateTimeout() {
	if (base::take(_waiting) && canPrompt()) {
		fail(MTP::Error::Local(
			u"PASSWORD_STATE_TIMEOUT"_q,
			u"account.getPassword timed out"_q));
	}
}

void GiftPasswordFlow::showPasswordBox(
		const Core::CloudPasswordState &state) {
	const auto session = _session.get();
	if (!session) {
		return;
	}
	auto fields = PasscodeBox::CloudFields::From(state);
	fields.customTitle = tr::lng_gift_transfer_password_title();
	fields.customDescription
		= tr::lng_gift_transfer_password_description(tr::now);
	fields.customSubmitButton = tr::lng_passcode_submit();
	fields.customCheckCallback = [self = shared_from_this()](
			const Core::CloudPasswordResult &check,
			base::weak_qptr<PasscodeBox> box) {
		self->requestTmpPassword(check, box);
	};
	showBox(Box<PasscodeBox>(session, fields));
}

bool GiftPasswordFlow::showPrePasswordBox(const QString &type) {
	const auto session = _session.get();
	if (!session) {
		return false;
	}
	auto box = PrePasswordErrorBox(
		type,
		session,
		tr::lng_gift_transfer_password_about(tr::now, tr::marked));
	if (!box) {
		return false;
	}
	showBox(std::move(box));
	return true;
}

void GiftPasswordFlow::showBox(object_ptr<Ui::BoxContent> box) {
	const auto raw = box.data();
	const auto previous = std::exchange(_box, raw);
	_handedOff = true;
	raw->boxClosing() | rpl::on_next([self = shared_from_this(), raw] {
		self->boxClosing(raw);
	}, raw->lifetime());
	if (const auto strong = previous.get()) {
		strong->closeBox();
	}
	_show->show(std::move(box));
	if (_step.boxShown) {
		_step.boxShown();
	}
}

void GiftPasswordFlow::boxClosing(not_null<Ui::BoxContent*> box) {
	if (_box.get() != box.get()) {
		return;
	}
	_box = nullptr;
	if (_done || _sending) {
		return;
	}
	_abandoned = true;
	if (_step.dismissed) {
		_step.dismissed();
	}
}

void GiftPasswordFlow::requestTmpPassword(
		const Core::CloudPasswordResult &check,
		base::weak_qptr<PasscodeBox> box) {
	const auto session = _session.get();
	if (!session || !canPrompt() || _issuing || _sending || _done) {
		return;
	}
	_issuing = true;
	session->api().request(MTPaccount_GetTmpPassword(
		check.result,
		MTP_int(kTmpPasswordPeriod)
	)).done([self = shared_from_this()](
			const MTPaccount_TmpPassword &result) {
		self->tmpPasswordReady(result);
	}).fail([self = shared_from_this(), box](const MTP::Error &error) {
		self->tmpPasswordFailed(error, box);
	}).handleFloodErrors().send();
}

void GiftPasswordFlow::tmpPasswordReady(
		const MTPaccount_TmpPassword &result) {
	_issuing = false;
	const auto session = _session.get();
	if (!session || !canPrompt()) {
		return;
	}
	const auto &data = result.data();
	const auto tmpPassword = data.vtmp_password().v;
	if (tmpPassword.isEmpty()) {
		fail(MTP::Error::Local(
			u"TMP_PASSWORD_EMPTY"_q,
			u"account.getTmpPassword returned no bytes"_q));
		return;
	}
	session->setTmpPassword(tmpPassword, data.vvalid_until().v);
	sendGift(tmpPassword, false);
}

void GiftPasswordFlow::tmpPasswordFailed(
		const MTP::Error &error,
		base::weak_qptr<PasscodeBox> box) {
	_issuing = false;
	if (!canPrompt()) {
		return;
	}
	const auto &type = error.type();
	if (showPrePasswordBox(type)) {
		return;
	}
	const auto strong = box.get();
	if (!strong || !strong->handleCustomCheckError(type)) {
		fail(error);
	}
}

void GiftPasswordFlow::sendGift(const QByteArray &tmpPassword, bool stored) {
	Expects(!tmpPassword.isEmpty());

	_sending = true;
	const auto self = shared_from_this();
	_step.send(
		tmpPassword,
		[=] { self->giftSent(); },
		[=](const MTP::Error &error) {
			self->giftFailed(error, tmpPassword, stored);
		});
}

void GiftPasswordFlow::giftSent() {
	_sending = false;
	_done = true;
	_step.succeeded();
	if (const auto strong = _box.get()) {
		strong->closeBox();
	}
}

void GiftPasswordFlow::giftFailed(
		const MTP::Error &error,
		const QByteArray &tmpPassword,
		bool stored) {
	_sending = false;
	const auto &type = error.type();
	const auto refused = TmpPasswordRefused(type);
	const auto session = _session.get();
	if (refused && session) {
		session->dropTmpPassword(tmpPassword);
	}
	if (_abandoned) {
		return;
	}
	if (refused && stored && canPrompt()) {
		waitForState();
	} else if (!canPrompt() || !showPrePasswordBox(type)) {
		fail(error);
	}
}

void GiftPasswordFlow::fail(const MTP::Error &error) {
	if (_show->valid()) {
		_step.failed(error);
	}
}

} // namespace

void RequestWithGiftPassword(
		not_null<Window::SessionController*> window,
		not_null<Ui::BoxContent*> confirmation,
		GiftPasswordStep step) {
	const auto flow = std::make_shared<GiftPasswordFlow>(
		window,
		confirmation,
		std::move(step));
	flow->start();
}

} // namespace GiftTransfer
