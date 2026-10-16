/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/connection_box.h"

#include "base/call_delayed.h"
#include "base/qt/qt_key_modifiers.h"
#include "base/qthelp_regex.h"
#include "base/qthelp_url.h"
#include "base/weak_ptr.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/local_url_handlers.h"
#include "extras/ui/controls/proxy_type_field.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "mtproto/facade.h"
#include "mtproto/mtproto_config.h"
#include "mtproto/proxy_check.h"
#include "mtproto/web_proxy/web_proxy_transport.h"
#include "qr/qr_generate.h"
#include "settings/settings_common.h"
#include "storage/localstorage.h"
#include "ui/basic_click_handlers.h"
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/dialog_layout.h"
#include "ui/boxes/peer_qr_box.h"
#include "ui/effects/animations.h"
#include "ui/effects/radial_animation.h"
#include "ui/painter.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/number_input.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/table_layout.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "ui/ui_utility.h"
#include "boxes/abstract_box.h" // Ui::show().
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_connection_box.h"
#include "styles/style_info.h"
#include "styles/style_intro.h"
#include "styles/style_layers.h"
#include "styles/style_media_player.h" // mediaPlayerMenuCheck
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>
#include <QtWidgets/QTextEdit>

namespace {

constexpr auto kSaveSettingsDelayedTimeout = crl::time(1000);

using ProxyData = MTP::ProxyData;

struct TopButtonMenuState {
	~TopButtonMenuState() {
		closing = true;
	}

	bool closing = false;
	base::unique_qptr<Ui::PopupMenu> menu;
};

[[nodiscard]] int ClosestProxyRotationTimeout(int value) {
	return *ranges::min_element(
		Core::SettingsProxy::kProxyRotationTimeouts,
		ranges::less(),
		[&](int timeout) { return std::abs(timeout - value); });
}

[[nodiscard]] QString ProxyRotationTimeoutText(int seconds) {
	return tr::lng_proxy_auto_switch_timeout(tr::now, lt_count, seconds);
}

// 总开关下方的状态：未开启、跟随系统，或当前使用的代理地址。
[[nodiscard]] QString ProxyUseStatusText(
		const Core::SettingsProxy &settings) {
	switch (settings.settings()) {
	case ProxyData::Settings::Disabled:
		return tr::extras_ProxyUseOffAbout(tr::now);
	case ProxyData::Settings::System:
		return tr::extras_ProxyUseSystemAbout(tr::now);
	case ProxyData::Settings::Enabled: {
		const auto proxy = settings.selected();
		return tr::extras_ProxyUseCustomAbout(
			tr::now,
			lt_server,
			(proxy.type == ProxyData::Type::Web)
				? proxy.webAddress()
				: proxy.host);
	}
	}
	Unexpected("Proxy settings value in ProxyUseStatusText.");
}

[[nodiscard]] int ProxyRowHeight() {
	return st::proxyRowPadding.top()
		+ st::semiboldFont->height
		+ st::proxyRowSkip
		+ st::normalFont->height
		+ st::proxyRowPadding.bottom();
}

[[nodiscard]] std::vector<QString> ExtractLinkCandidates(const QString &input) {
	auto urls = std::vector<QString>();
	static const auto urlRegex = QRegularExpression(
		R"((?:https?:\/\/[^\s]+|tg:\/\/[^\s]+|(?:www\.)?(?:t\.me|telegram\.me|telegram\.dog)\/[^\s]+))",
		QRegularExpression::CaseInsensitiveOption);

	auto it = urlRegex.globalMatch(input);
	while (it.hasNext()) {
		urls.push_back(it.next().captured(0));
	}

	return urls;
}

[[nodiscard]] bool ProxyDataIsShareable(const ProxyData &proxy) {
	using Type = ProxyData::Type;
	return (proxy.type == Type::Socks5)
		|| (proxy.type == Type::Mtproto)
		|| (proxy.type == Type::Web);
}

[[nodiscard]] QString ProxyDataToQueryPath(const ProxyData &proxy) {
	using Type = ProxyData::Type;
	const auto path = [&] {
		switch (proxy.type) {
		case Type::Socks5: return u"socks"_q;
		case Type::Mtproto: return u"proxy"_q;
		case Type::Web: return u"webproxy"_q;
		case Type::None:
		case Type::Http: return QString();
		}
		Unexpected("Proxy type in ProxyDataToQueryPath.");
	}();
	if (path.isEmpty()) {
		return QString();
	}
	const auto server = (proxy.type == Type::Web)
		? qthelp::url_encode(proxy.webAddress())
		: proxy.host;
	return path
		+ "?server=" + server
		+ ((proxy.type != Type::Web)
			? "&port=" + QString::number(proxy.port) : "")
		+ ((proxy.type == Type::Socks5 && !proxy.user.isEmpty())
			? "&user=" + qthelp::url_encode(proxy.user) : "")
		+ ((proxy.type == Type::Socks5 && !proxy.password.isEmpty())
			? "&pass=" + qthelp::url_encode(proxy.password) : "")
		+ (((proxy.type == Type::Mtproto || proxy.type == Type::Web)
				&& !proxy.password.isEmpty())
			? "&secret=" + ((proxy.type == Type::Web)
				? MTP::EncodeWebProxyLinkSecret(proxy)
				: proxy.password) : "");
}

[[nodiscard]] QString ProxyDataToLocalLink(const ProxyData &proxy) {
	const auto queryPath = ProxyDataToQueryPath(proxy);
	return queryPath.isEmpty() ? QString() : (u"tg://"_q + queryPath);
}

[[nodiscard]] QString ProxyDataToPublicLink(
		not_null<Main::Account*> account,
		const ProxyData &proxy) {
	const auto queryPath = ProxyDataToQueryPath(proxy);
	if (queryPath.isEmpty()) {
		return QString();
	}
	if (const auto session = account->maybeSession()) {
		return session->createInternalLinkFull(queryPath);
	}
	auto domain = MTP::ConfigFields(
		account->mtp().environment()
	).internalLinksDomain;
	if (domain.endsWith('/') && queryPath.startsWith('/')) {
		domain.chop(1);
	} else if (!domain.endsWith('/') && !queryPath.startsWith('/')) {
		domain += '/';
	}
	return domain + queryPath;
}

[[nodiscard]] QColor ProxyQrActiveColor() {
	return QColor(0x40, 0xA7, 0xE3);
}

[[nodiscard]] QImage ProxyQr(const Qr::Data &data, int pixel, int max = 0) {
	Expects(data.size > 0);

	const auto available = max
		? std::max(max - 2 * st::introQrBackgroundSkip, 1)
		: 0;
	if (available > 0 && data.size * pixel > available) {
		pixel = std::max(available / data.size, 1);
	}
	return Qr::Generate(
		data,
		pixel * style::DevicePixelRatio(),
		Qt::black,
		Qt::white);
}

[[nodiscard]] QImage ProxyQrLogo() {
	const auto size = QSize(st::introQrCenterSize, st::introQrCenterSize);
	auto result = QImage(
		size * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	result.setDevicePixelRatio(style::DevicePixelRatio());
	{
		auto p = QPainter(&result);
		auto hq = PainterHighQualityEnabler(p);
		p.setBrush(ProxyQrActiveColor());
		p.setPen(Qt::NoPen);
		p.drawEllipse(QRect(QPoint(), size));
		st::introQrPlane.paintInCenter(p, QRect(QPoint(), size));
	}
	return result;
}

[[nodiscard]] QImage ProxyQrTile(const QString &link, int max = 0) {
	const auto data = Qr::Encode(link, Qr::Redundancy::Quartile);
	const auto qr = ProxyQr(
		data,
		st::introQrPixel,
		max);
	const auto qrSize = qr.width() / style::DevicePixelRatio();
	const auto skip = st::introQrBackgroundSkip;
	const auto size = QSize(qrSize + 2 * skip, qrSize + 2 * skip);
	auto result = QImage(
		size * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	result.setDevicePixelRatio(style::DevicePixelRatio());
	{
		auto p = QPainter(&result);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		p.drawRoundedRect(
			QRect(QPoint(), size),
			st::introQrBackgroundRadius,
			st::introQrBackgroundRadius);
		p.drawImage(QRect(skip, skip, qrSize, qrSize), qr);
		const auto logo = ProxyQrLogo();
		p.drawImage(
			QRect(
				(size.width() - st::introQrCenterSize) / 2,
				(size.height() - st::introQrCenterSize) / 2,
				st::introQrCenterSize,
				st::introQrCenterSize),
			logo);
	}
	return result;
}

[[nodiscard]] QImage ProxyQrForShare(const QString &link) {
	return ProxyQrTile(
		link,
		st::boxWidth - st::boxRowPadding.left() - st::boxRowPadding.right());
}

void ShowProxyQrBox(std::shared_ptr<Ui::Show> show, const QString &link) {
	show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_proxy_edit_share_qr_box_title());
		box->addButton(tr::lng_about_done(), [=] { box->closeBox(); });

		const auto copyCallback = [=, image = ProxyQrForShare(link)] {
			QGuiApplication::clipboard()->setImage(image);
			show->showToast({
				.text = { tr::lng_group_invite_qr_copied(tr::now) },
				.iconLottie = u"toast/copy"_q,
				.iconLottieSize = st::toastLottieIconSize,
			});
		};

		const auto qr = ProxyQrTile(
			link,
			st::boxWidth
				- st::boxRowPadding.left()
				- st::boxRowPadding.right());
		const auto size = qr.width() / style::DevicePixelRatio();
		const auto height = st::inviteLinkQrSkip * 2 + size;
		const auto container = box->addRow(
			object_ptr<Ui::BoxContentDivider>(box, height),
			st::inviteLinkQrMargin);
		const auto button = Ui::CreateChild<Ui::AbstractButton>(container);
		button->resize(size, size);
		button->paintRequest(
		) | rpl::on_next([=] {
			QPainter(button).drawImage(QRect(0, 0, size, size), qr);
		}, button->lifetime());
		container->widthValue(
		) | rpl::on_next([=](int width) {
			button->move((width - size) / 2, st::inviteLinkQrSkip);
		}, button->lifetime());
		button->setClickedCallback(copyCallback);

		box->addLeftButton(tr::lng_group_invite_context_copy(), copyCallback);
	}));
}

void ShareProxy(
		std::shared_ptr<Ui::Show> show,
		not_null<Main::Account*> account,
		const ProxyData &proxy,
		bool qr) {
	if (!ProxyDataIsShareable(proxy)) {
		return;
	}
	const auto qrLink = ProxyDataToLocalLink(proxy);
	if (qrLink.isEmpty()) {
		return;
	}
	if (qr) {
		if (account->sessionExists()) {
			show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
				Ui::FillPeerQrBox(
					box,
					nullptr,
					qrLink,
					rpl::single(QString()));
				box->setTitle(tr::lng_proxy_edit_share_qr_box_title());
			}));
		} else {
			ShowProxyQrBox(show, qrLink);
		}
		return;
	}
	const auto internal = base::IsCtrlPressed()
		|| base::IsAltPressed()
		|| base::IsShiftPressed();
	const auto shareLink = internal
		? qrLink
		: ProxyDataToPublicLink(account, proxy);
	if (shareLink.isEmpty()) {
		return;
	}
	TextUtilities::SetClipboardText(TextForMimeData::Simple(shareLink));
	show->showToast({
		.text = { tr::lng_username_copied(tr::now) },
		.iconLottie = u"toast/voip_invite"_q,
		.iconLottieSize = st::toastLottieIconSize,
	});
}

[[nodiscard]] ProxyData ProxyDataFromFields(
		ProxyData::Type type,
		const QMap<QString, QString> &fields) {
	auto proxy = ProxyData();
	proxy.type = type;
	const auto web = (type == ProxyData::Type::Web);
	const auto server = fields.value(u"server"_q);
	proxy.port = web ? 443 : fields.value(u"port"_q).toUInt();
	if (web) {
		proxy.setWebAddress(
			server.isEmpty() ? fields.value(u"host"_q) : server);
	} else {
		proxy.host = server;
	}
	if (type == ProxyData::Type::Socks5) {
		proxy.user = fields.value(u"user"_q);
		proxy.password = fields.value(u"pass"_q);
	} else if (type == ProxyData::Type::Mtproto || web) {
		proxy.password = fields.value(u"secret"_q);
		proxy.password.replace('+', '-').replace('/', '_');
		if (web) {
			proxy.password = MTP::DecodeWebProxyLinkSecret(
				proxy.password,
				!proxy.webBasePath().isEmpty());
		}
	}
	return proxy;
};

[[nodiscard]] ProxyData ProxyDataFromLocalUrl(const QString &local) {
	const auto protocol = u"tg://"_q;
	const auto proxyString = u"proxy"_q;
	const auto socksString = u"socks"_q;
	const auto webproxyString = u"webproxy"_q;
	if (!local.startsWith(protocol + proxyString, Qt::CaseInsensitive)
		&& !local.startsWith(protocol + socksString, Qt::CaseInsensitive)
		&& !local.startsWith(protocol + webproxyString, Qt::CaseInsensitive)) {
		return ProxyData();
	}
	const auto command = base::StringViewMid(local, protocol.size(), 8192);
	using namespace qthelp;
	const auto options = RegExOption::CaseInsensitive;
	for (const auto &[expression, _] : Core::LocalUrlHandlers()) {
		const auto midExpression = base::StringViewMid(expression, 1);
		const auto isSocks = midExpression.startsWith(socksString);
		const auto isWeb = midExpression.startsWith(webproxyString);
		if (!midExpression.startsWith(proxyString) && !isSocks && !isWeb) {
			continue;
		}
		const auto match = regex_match(expression, command, options);
		if (!match) {
			continue;
		}
		const auto type = isWeb
			? ProxyData::Type::Web
			: isSocks
			? ProxyData::Type::Socks5
			: ProxyData::Type::Mtproto;
		auto fields = url_parse_params(
			match->captured(1),
			qthelp::UrlParamNameTransform::ToLower);
		return ProxyDataFromFields(type, fields);
	}
	return ProxyData();
}

[[nodiscard]] ProxyData ProxyDataFromClipboard() {
	const auto candidates = ExtractLinkCandidates(
		QGuiApplication::clipboard()->text());
	if (candidates.size() != 1) {
		return ProxyData();
	}
	const auto trimmed = candidates.front().trimmed();
	const auto converted = Core::TryConvertUrlToLocal(trimmed);
	const auto local = converted.isEmpty() ? trimmed : converted;
	const auto proxy = ProxyDataFromLocalUrl(local);
	return proxy.valid() ? proxy : ProxyData();
}

void AddProxyFromClipboard(
		not_null<ProxiesBoxController*> controller,
		std::shared_ptr<Ui::Show> show) {
	const auto proxyString = u"proxy"_q;
	const auto socksString = u"socks"_q;
	const auto webproxyString = u"webproxy"_q;
	const auto protocol = u"tg://"_q;

	const auto maybeUrls = ExtractLinkCandidates(
		QGuiApplication::clipboard()->text());
	const auto isSingle = maybeUrls.size() == 1;

	enum class Result {
		Success,
		Failed,
		Unsupported,
		IncorrectSecret,
		Invalid,
	};

	const auto proceedUrl = [=](const QString &local) {
		const auto isProxyLink
			= local.startsWith(protocol + proxyString, Qt::CaseInsensitive)
			|| local.startsWith(protocol + socksString, Qt::CaseInsensitive)
			|| local.startsWith(
				protocol + webproxyString,
				Qt::CaseInsensitive);
		if (!isProxyLink) {
			return Result::Failed;
		}
		const auto proxy = ProxyDataFromLocalUrl(local);
		if (proxy.type == ProxyData::Type::None) {
			return Result::Success;
		} else if (!proxy) {
			const auto status = proxy.status();
			return (status == ProxyData::Status::Unsupported)
				? Result::Unsupported
				: (status == ProxyData::Status::IncorrectSecret)
				? Result::IncorrectSecret
				: Result::Invalid;
		}
		const auto contains = controller->contains(proxy);
		const auto toast = (contains
			? tr::lng_proxy_add_from_clipboard_existing_toast
			: tr::lng_proxy_add_from_clipboard_good_toast)(tr::now);
		if (isSingle) {
			show->showToast(toast);
		}
		if (!contains) {
			controller->addNewItem(proxy);
		}
		return Result::Success;
	};

	auto success = Result::Failed;
	for (const auto &maybeUrl : maybeUrls) {
		const auto trimmed = maybeUrl.trimmed();
		const auto local = Core::TryConvertUrlToLocal(trimmed);
		const auto check = local.isEmpty() ? trimmed : local;
		const auto result = proceedUrl(check);
		if (success != Result::Success) {
			success = result;
		}
	}

	if (success != Result::Success) {
		if (success == Result::Failed) {
			show->showToast(
				tr::lng_proxy_add_from_clipboard_failed_toast(tr::now));
		} else {
			show->showBox(Ui::MakeInformBox(
				((success == Result::IncorrectSecret)
					? tr::lng_proxy_incorrect_secret(tr::now, tr::rich)
					: (success == Result::Unsupported)
					? tr::lng_proxy_unsupported(tr::now, tr::rich)
					: tr::lng_proxy_invalid(tr::now, tr::rich))));
		}
	}
}

class HostInput : public Ui::MaskedInputField {
public:
	HostInput(
		QWidget *parent,
		const style::InputField &st,
		rpl::producer<QString> placeholder,
		const QString &val);

protected:
	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;
};

HostInput::HostInput(
	QWidget *parent,
	const style::InputField &st,
	rpl::producer<QString> placeholder,
	const QString &val)
: MaskedInputField(parent, st, std::move(placeholder), val) {
	setInputMethodHints(Qt::ImhUrlCharactersOnly
		| Qt::ImhNoAutoUppercase
		| Qt::ImhNoPredictiveText);
}

void HostInput::correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
	QString newText;
	int newCursor = nowCursor;
	newText.reserve(now.size());
	for (auto i = 0, l = int(now.size()); i < l; ++i) {
		if (now[i] == ',') {
			newText.append('.');
		} else {
			newText.append(now[i]);
		}
	}
	setCorrectedText(now, nowCursor, newText, newCursor);
}

class Base64UrlInput : public Ui::MaskedInputField {
public:
	Base64UrlInput(
		QWidget *parent,
		const style::InputField &st,
		rpl::producer<QString> placeholder,
		const QString &val);

protected:
	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;

};

Base64UrlInput::Base64UrlInput(
	QWidget *parent,
	const style::InputField &st,
	rpl::producer<QString> placeholder,
	const QString &val)
: MaskedInputField(parent, st, std::move(placeholder), val) {
	setInputMethodHints(Qt::ImhLatinOnly
		| Qt::ImhNoAutoUppercase
		| Qt::ImhNoPredictiveText
		| Qt::ImhSensitiveData);
	static const auto RegExp = QRegularExpression("^[a-zA-Z0-9_\\-]+$");
	if (!RegExp.match(val).hasMatch()) {
		setText(QString());
	}
}

void Base64UrlInput::correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
	QString newText;
	newText.reserve(now.size());
	auto newPos = nowCursor;
	for (auto i = 0, l = int(now.size()); i < l; ++i) {
		const auto ch = now[i];
		if ((ch >= '0' && ch <= '9')
			|| (ch >= 'a' && ch <= 'z')
			|| (ch >= 'A' && ch <= 'Z')
			|| (ch == '-')
			|| (ch == '_')) {
			newText.append(ch);
		} else if (i < nowCursor) {
			--newPos;
		}
	}
	setCorrectedText(now, nowCursor, newText, newPos);
}

class ProxyRow : public Ui::RippleButton {
public:
	using View = ProxiesBoxController::ItemView;
	using State = ProxiesBoxController::ItemState;

	ProxyRow(QWidget *parent, View &&view);

	void updateFields(View &&view);

	rpl::producer<> deleteClicks() const;
	rpl::producer<> restoreClicks() const;
	rpl::producer<> editClicks() const;
	rpl::producer<> shareClicks() const;
	rpl::producer<> showQrClicks() const;
	rpl::producer<> openBrowserClicks() const;

protected:
	int resizeGetHeight(int newWidth) override;

	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(
		AbstractButton::State was,
		StateChangeSource source) override;

private:
	void setupControls(View &&view);
	int countAvailableWidth() const;
	void radialAnimationCallback();
	void paintCheck(Painter &p);
	void showMenu();

	View _view;

	Ui::Text::String _title;
	object_ptr<Ui::IconButton> _menuToggle;
	rpl::event_stream<> _deleteClicks;
	rpl::event_stream<> _restoreClicks;
	rpl::event_stream<> _editClicks;
	rpl::event_stream<> _shareClicks;
	rpl::event_stream<> _showQrClicks;
	rpl::event_stream<> _openBrowserClicks;
	base::unique_qptr<Ui::DropdownMenu> _menu;

	bool _set = false;
	Ui::Animations::Simple _toggled;
	Ui::Animations::Simple _setAnimation;
	std::unique_ptr<Ui::InfiniteRadialAnimation> _progress;
	std::unique_ptr<Ui::InfiniteRadialAnimation> _checking;

	int _skipLeft = 0;
	int _skipRight = 0;

};

// 列表首行的「系统代理」：与代理行同高，单选圈同列。
class SystemProxyRow final : public Ui::RippleButton {
public:
	SystemProxyRow(QWidget *parent, bool checked);

	void setChecked(bool checked);

protected:
	int resizeGetHeight(int newWidth) override;

	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;

private:
	Ui::RadioView _radio;

};

class ProxiesBox : public Ui::BoxContent {
public:
	using View = ProxiesBoxController::ItemView;

	ProxiesBox(
		QWidget*,
		not_null<ProxiesBoxController*> controller,
		Core::SettingsProxy &settings,
		const QString &highlightId = QString());

protected:
	void prepare() override;
	void showFinished() override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	void setupContent();
	void setupTopButton();
	void setupUseProxy(not_null<Ui::VerticalLayout*> container);
	void setupRotation(not_null<Ui::VerticalLayout*> container);
	void showRotationTimeoutMenu();
	void enableProxy();
	void addNewProxy();
	void applyView(View &&view);
	void setupButtons(int id, not_null<ProxyRow*> button);
	void refreshProxyForCalls();
	void refreshProxyRotation();
	void refreshOptionsSkip();
	void refreshPasteFromClipboard();

	not_null<ProxiesBoxController*> _controller;
	Core::SettingsProxy &_settings;
	rpl::variable<ProxyData::Settings> _mode;
	// 打开总开关时恢复的模式，关闭总开关不会覆盖它。
	ProxyData::Settings _lastUsedMode = ProxyData::Settings::System;
	rpl::variable<bool> _listShareable;
	rpl::variable<int> _rotationTimeout;
	QPointer<Settings::Button> _tryIPv6;
	QPointer<Ui::SlideWrap<Settings::Button>> _proxyForCalls;
	QPointer<Ui::SlideWrap<Settings::Button>> _proxyRotation;
	QPointer<Ui::SlideWrap<Ui::VerticalLayout>> _proxyRotationOptions;
	QPointer<Ui::SlideWrap<Settings::Button>> _pasteFromClipboard;
	QPointer<Ui::SlideWrap<Ui::FixedHeightWidget>> _optionsSkip;
	object_ptr<Ui::VerticalLayout> _initialWrap;
	QPointer<Ui::VerticalLayout> _wrap;
	int _currentProxySupportsCallsId = 0;

	base::flat_map<int, base::unique_qptr<ProxyRow>> _rows;

	base::unique_qptr<Ui::PopupMenu> _rotationTimeoutMenu;
	QPointer<Ui::RpWidget> _topButton;
	QPointer<Ui::RpWidget> _useProxyButton;
	QPointer<Ui::RpWidget> _addProxyButton;
	QString _highlightId;

};

class ProxyBox final : public Ui::BoxContent {
public:
	ProxyBox(
		QWidget*,
		const ProxyData &data,
		Fn<void(ProxyData)> callback,
		Fn<void(ProxyData)> shareCallback);

private:
	using Type = ProxyData::Type;

	void prepare() override;
	void resizeEvent(QResizeEvent *e) override {
		Ui::BoxContent::resizeEvent(e);
		_content->resizeToWidth(width());
	}
	void setInnerFocus() override {
		if (_type->current() == Type::Web) {
			_webHost->setFocusFast();
		} else {
			_host->setFocusFast();
		}
	}

	void refreshButtons();
	ProxyData collectData();
	void save();
	void share();
	void fillFromClipboard();
	void setupControls(const ProxyData &data);
	void setupTypes();
	void setupSocketAddress(const ProxyData &data);
	void setupWebAddress(const ProxyData &data);
	void setupCredentials(const ProxyData &data);
	void setupMtprotoCredentials(const ProxyData &data);

	void addLabel(
		not_null<Ui::VerticalLayout*> parent,
		const QString &text) const;

	const bool _allowShare = false;
	int _formHeight = 0;
	bool _changingType = false;
	Fn<void(ProxyData)> _callback;
	Fn<void(ProxyData)> _shareCallback;

	object_ptr<Ui::VerticalLayout> _content;

	std::shared_ptr<Ui::RadioenumGroup<Type>> _type;

	QPointer<HostInput> _host;
	QPointer<Ui::NumberInput> _port;
	QPointer<Ui::InputField> _user;
	QPointer<Ui::PasswordInput> _password;
	QPointer<Base64UrlInput> _secret;
	QPointer<Ui::InputField> _webHost;

	QPointer<Ui::SlideWrap<Ui::VerticalLayout>> _socketAddress;
	QPointer<Ui::SlideWrap<Ui::VerticalLayout>> _webAddress;
	QPointer<Ui::SlideWrap<Ui::VerticalLayout>> _credentials;
	QPointer<Ui::SlideWrap<Ui::VerticalLayout>> _mtprotoCredentials;

};

ProxyRow::ProxyRow(QWidget *parent, View &&view)
: RippleButton(parent, st::proxyRowRipple)
, _menuToggle(this, st::topBarMenuToggle) {
	setupControls(std::move(view));
}

rpl::producer<> ProxyRow::deleteClicks() const {
	return _deleteClicks.events();
}

rpl::producer<> ProxyRow::restoreClicks() const {
	return _restoreClicks.events();
}

rpl::producer<> ProxyRow::editClicks() const {
	return _editClicks.events();
}

rpl::producer<> ProxyRow::shareClicks() const {
	return _shareClicks.events();
}

rpl::producer<> ProxyRow::showQrClicks() const {
	return _showQrClicks.events();
}

rpl::producer<> ProxyRow::openBrowserClicks() const {
	return _openBrowserClicks.events();
}

void ProxyRow::setupControls(View &&view) {
	updateFields(std::move(view));
	_toggled.stop();
	_setAnimation.stop();

	_menuToggle->addClickHandler([=] { showMenu(); });
}

int ProxyRow::countAvailableWidth() const {
	return width() - _skipLeft - _skipRight;
}

void ProxyRow::updateFields(View &&view) {
	if (_view.selected != view.selected) {
		_toggled.start(
			[=] { update(); },
			view.selected ? 0. : 1.,
			view.selected ? 1. : 0.,
			st::defaultRadio.duration);
	}
	_view = std::move(view);
	// 端口用链接色绘制，与主机名区分；类型移到状态行。
	auto title = TextWithEntities().append(_view.host);
	if (!_view.web) {
		title.append(tr::link(u":"_q + QString::number(_view.port), QString()));
	}
	_title.setMarkedText(
		st::proxyRowTitleStyle,
		title,
		Ui::ItemTextDefaultOptions());

	const auto state = _view.state;
	if (state == State::Connecting) {
		if (!_progress) {
			_progress = std::make_unique<Ui::InfiniteRadialAnimation>(
				[=] { radialAnimationCallback(); },
				st::proxyCheckingAnimation);
		}
		_progress->start();
	} else if (_progress) {
		_progress->stop();
	}
	if (state == State::Checking) {
		if (!_checking) {
			_checking = std::make_unique<Ui::InfiniteRadialAnimation>(
				[=] { radialAnimationCallback(); },
				st::proxyCheckingAnimation);
			_checking->start();
		}
	} else {
		_checking = nullptr;
	}
	const auto set = (state == State::Connecting || state == State::Online);
	if (_set != set) {
		_set = set;
		_setAnimation.start(
			[=] { update(); },
			_set ? 0. : 1.,
			_set ? 1. : 0.,
			st::defaultRadio.duration);
	}

	setPointerCursor(!_view.deleted);

	update();
}

void ProxyRow::radialAnimationCallback() {
	if (!anim::Disabled()) {
		update();
	}
}

int ProxyRow::resizeGetHeight(int newWidth) {
	const auto result = ProxyRowHeight();
	auto right = st::proxyRowPadding.right();
	_menuToggle->moveToRight(
		right,
		(result - _menuToggle->height()) / 2,
		newWidth);
	right += _menuToggle->width();
	_skipRight = right;
	_skipLeft = st::proxyRowPadding.left()
		+ st::proxyRowCheckSkip;
	return result;
}

void ProxyRow::onStateChanged(
		AbstractButton::State was,
		StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	update();
}

void ProxyRow::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (!_view.deleted) {
		if (isOver()) {
			p.fillRect(e->rect(), st::windowBgOver);
		}
		paintRipple(p, 0, 0);
	}

	const auto left = _skipLeft;
	const auto availableWidth = countAvailableWidth();
	auto top = st::proxyRowPadding.top();

	if (_view.deleted) {
		p.setOpacity(st::stickersRowDisabledOpacity);
	}

	paintCheck(p);

	p.setPen(st::proxyRowTitleFg);
	p.setFont(st::semiboldFont);
	p.setTextPalette(st::proxyRowTitlePalette);
	_title.drawLeftElided(p, left, top, availableWidth, width());
	top += st::semiboldFont->height + st::proxyRowSkip;

	const auto statusFg = [&] {
		switch (_view.state) {
		case State::Online:
			return st::proxyRowStatusFgOnline;
		case State::Unavailable:
			return st::proxyRowStatusFgOffline;
		case State::WaitingForBrowser:
			return st::proxyRowStatusFg;
		case State::Available:
			return st::proxyRowStatusFgAvailable;
		default:
			return st::proxyRowStatusFg;
		}
	}();
	const auto state = [&] {
		switch (_view.state) {
		case State::Available:
			return tr::lng_proxy_available(
				tr::now,
				lt_ping,
				QString::number(_view.ping));
		case State::Checking:
			return tr::lng_proxy_checking(tr::now);
		case State::NotTested:
			return tr::lng_proxy_not_tested(tr::now);
		case State::Connecting:
			return tr::lng_proxy_connecting(tr::now);
		case State::Online:
			return tr::lng_proxy_online(tr::now);
		case State::Unavailable:
			return tr::lng_proxy_unavailable(tr::now);
		case State::WaitingForBrowser:
			return tr::lng_proxy_web_waiting(tr::now);
		}
		Unexpected("State in ProxyRow::paintEvent.");
	}();
	const auto status = QString(
		_view.type + u" "_q + QChar(0x00B7) + u" "_q + state);
	const auto &color = _view.deleted ? st::proxyRowStatusFg : statusFg;

	// 检测中画转圈，其余时候在同一位置画状态圆点，切换时文字不跳动。
	const auto marker = QPoint(
		left + st::proxyCheckingPosition.x(),
		top + st::proxyCheckingPosition.y());
	const auto markerSize = st::proxyCheckingAnimation.size;
	if (_checking) {
		_checking->draw(p, marker, width());
	} else {
		const auto dot = st::proxyRowStatusDotSize;
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(color->b);
		p.drawEllipse(style::rtlrect(
			QRectF(
				marker.x() + (markerSize.width() - dot) / 2.,
				marker.y() + (markerSize.height() - dot) / 2.,
				dot,
				dot),
			width()));
	}
	const auto statusLeft = marker.x()
		+ markerSize.width()
		+ st::proxyCheckingSkip;
	p.setPen(color);
	p.setFont(st::normalFont);
	p.drawTextLeft(statusLeft, top, width(), status);
}

void ProxyRow::paintCheck(Painter &p) {
	const auto loading = _progress
		? _progress->computeState()
		: Ui::RadialState{ 0., 0, arc::kFullLength };
	const auto toggled = _toggled.value(_view.selected ? 1. : 0.)
		* (1. - loading.shown);
	const auto &st = st::defaultRadio;
	const auto set = _setAnimation.value(_set ? 1. : 0.);

	PainterHighQualityEnabler hq(p);

	const auto outer = style::rtlrect(
		QRectF(
			st::proxyRowPadding.left(),
			(height() - st.diameter) / 2,
			st.diameter,
			st.diameter),
		width());
	// 与标准单选圈同形；连接中在外圈上叠加进度弧。
	Ui::RadioView::PaintOuter(
		p,
		outer,
		st.untoggledFg->c,
		st.toggledFg->c,
		toggled * set);
	if (_progress && loading.shown > 0) {
		const auto half = st.thickness / 2.;
		const auto ring = outer.marginsRemoved({ half, half, half, half });
		auto pen = st.toggledFg->p;
		pen.setWidth(st.thickness);
		pen.setCapStyle(Qt::RoundCap);
		if (anim::Disabled()) {
			anim::DrawStaticLoading(p, ring, st.thickness, pen);
		} else {
			p.setPen(pen);
			p.setBrush(Qt::NoBrush);
			p.drawArc(ring, loading.arcFrom, loading.arcLength);
		}
	}
	if (toggled > 0 && (!_progress || !anim::Disabled())) {
		const auto skip = (st.diameter / 2.) * (1. - toggled)
			+ (st.skip / 10.) * toggled;
		p.setPen(Qt::NoPen);
		p.setBrush(anim::brush(st.untoggledFg, st.toggledFg, toggled * set));
		p.drawEllipse(outer.marginsRemoved({ skip, skip, skip, skip }));
	}
}

void ProxyRow::showMenu() {
	if (_menu) {
		return;
	}
	_menu = base::make_unique_q<Ui::DropdownMenu>(
		window(),
		st::dropdownMenuWithIcons);
	const auto weak = _menu.get();
	_menu->setHiddenCallback([=] {
		weak->deleteLater();
		if (_menu == weak) {
			_menuToggle->setForceRippled(false);
		}
	});
	_menu->setShowStartCallback([=] {
		if (_menu == weak) {
			_menuToggle->setForceRippled(true);
		}
	});
	_menu->setHideStartCallback([=] {
		if (_menu == weak) {
			_menuToggle->setForceRippled(false);
		}
	});
	_menuToggle->installEventFilter(_menu);
	const auto addAction = [&](
			const QString &text,
			Fn<void()> callback,
			const style::icon *icon) {
		return _menu->addAction(text, std::move(callback), icon);
	};
	addAction(tr::lng_proxy_menu_edit(tr::now), [=] {
		_editClicks.fire({});
	}, &st::menuIconEdit);
	if (_view.canOpenBrowser) {
		addAction(tr::lng_proxy_web_open(tr::now), [=] {
			_openBrowserClicks.fire({});
		}, &st::menuIconLink);
	}
	if (_view.supportsShare) {
		addAction(tr::lng_proxy_edit_share(tr::now), [=] {
			_shareClicks.fire({});
		}, &st::menuIconShare);
		addAction(tr::lng_group_invite_context_qr(tr::now), [=] {
			_showQrClicks.fire({});
		}, &st::menuIconQrCode);
	}
	if (_view.deleted) {
		addAction(tr::lng_proxy_menu_restore(tr::now), [=] {
			_restoreClicks.fire({});
		}, &st::menuIconRestore);
	} else {
		addAction(tr::lng_proxy_menu_delete(tr::now), [=] {
			_deleteClicks.fire({});
		}, &st::menuIconDelete);
	}
	const auto parentTopLeft = window()->mapToGlobal(QPoint());
	const auto buttonTopLeft = _menuToggle->mapToGlobal(QPoint());
	const auto parent = QRect(parentTopLeft, window()->size());
	const auto button = QRect(buttonTopLeft, _menuToggle->size());
	const auto bottom = button.y()
		+ st::proxyDropdownDownPosition.y()
		+ _menu->height()
		- parent.y();
	const auto top = button.y()
		+ st::proxyDropdownUpPosition.y()
		- _menu->height()
		- parent.y();
	if (bottom > parent.height() && top >= 0) {
		const auto left = button.x()
			+ button.width()
			+ st::proxyDropdownUpPosition.x()
			- _menu->width()
			- parent.x();
		_menu->move(left, top);
		_menu->showAnimated(Ui::PanelAnimation::Origin::BottomRight);
	} else {
		const auto left = button.x()
			+ button.width()
			+ st::proxyDropdownDownPosition.x()
			- _menu->width()
			- parent.x();
		_menu->move(left, bottom - _menu->height());
		_menu->showAnimated(Ui::PanelAnimation::Origin::TopRight);
	}
}

SystemProxyRow::SystemProxyRow(QWidget *parent, bool checked)
: RippleButton(parent, st::proxyRowRipple)
, _radio(st::defaultRadio, checked, [=] { update(); }) {
}

void SystemProxyRow::setChecked(bool checked) {
	_radio.setChecked(checked, anim::type::normal);
}

int SystemProxyRow::resizeGetHeight(int newWidth) {
	return ProxyRowHeight();
}

void SystemProxyRow::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	update();
}

void SystemProxyRow::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	if (isOver()) {
		p.fillRect(e->rect(), st::windowBgOver);
	}
	paintRipple(p, 0, 0);

	const auto &padding = st::proxyRowPadding;
	_radio.paint(
		p,
		padding.left(),
		(height() - _radio.getSize().height()) / 2,
		width());

	const auto left = padding.left() + st::proxyRowCheckSkip;
	const auto available = width() - left - padding.right();
	auto top = padding.top();
	p.setPen(st::proxyRowTitleFg);
	p.setFont(st::semiboldFont);
	p.drawTextLeft(
		left,
		top,
		width(),
		st::semiboldFont->elided(tr::extras_ProxySystem(tr::now), available));
	top += st::semiboldFont->height + st::proxyRowSkip;

	p.setPen(st::proxyRowStatusFg);
	p.setFont(st::normalFont);
	p.drawTextLeft(
		left,
		top,
		width(),
		st::normalFont->elided(
			tr::extras_ProxySystemAbout(tr::now),
			available));
}

ProxiesBox::ProxiesBox(
	QWidget*,
	not_null<ProxiesBoxController*> controller,
	Core::SettingsProxy &settings,
	const QString &highlightId)
: _controller(controller)
, _settings(settings)
, _mode(_controller->proxySettingsValue())
, _listShareable(_controller->listShareableChanges())
, _rotationTimeout(
	ClosestProxyRotationTimeout(settings.proxyRotationTimeout()))
, _initialWrap(this)
, _highlightId(highlightId) {
	const auto current = _mode.current();
	_lastUsedMode = (current != ProxyData::Settings::Disabled)
		? current
		: _settings.selected()
		? ProxyData::Settings::Enabled
		: ProxyData::Settings::System;
	_mode.changes(
	) | rpl::filter([](ProxyData::Settings mode) {
		return (mode != ProxyData::Settings::Disabled);
	}) | rpl::on_next([=](ProxyData::Settings mode) {
		_lastUsedMode = mode;
	}, lifetime());

	_controller->views(
	) | rpl::on_next([=](View &&view) {
		applyView(std::move(view));
	}, lifetime());
}

void ProxiesBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Copy
		|| (e->key() == Qt::Key_C && e->modifiers() == Qt::ControlModifier)) {
		_controller->shareItems();
	} else if (e->key() == Qt::Key_Paste
		|| (e->key() == Qt::Key_V && e->modifiers() == Qt::ControlModifier)) {
		AddProxyFromClipboard(_controller, uiShow());
	} else {
		BoxContent::keyPressEvent(e);
	}
}

void ProxiesBox::prepare() {
	setTitle(tr::lng_proxy_settings());

	addButton(tr::lng_close(), [=] { closeBox(); });

	setupTopButton();
	setupContent();
}

void ProxiesBox::showFinished() {
	if (_highlightId == u"proxy/add-proxy"_q) {
		// 总开关关闭时添加行不可见，改为提示打开总开关。
		const auto target = (_mode.current() == ProxyData::Settings::Disabled)
			? _useProxyButton
			: _addProxyButton;
		if (target) {
			_highlightId = QString();
			Settings::HighlightWidget(target);
		}
	} else if (_highlightId == u"proxy/share-list"_q) {
		if (_topButton) {
			_highlightId = QString();
			Settings::HighlightWidget(_topButton, { .rippleShape = true });
		}
	}
}

void ProxiesBox::setupTopButton() {
	const auto top = addTopButton(st::infoTopBarMenu);
	_topButton = top;
	const auto state = top->lifetime().make_state<TopButtonMenuState>();

	top->setClickedCallback([=] {
		state->menu = base::make_unique_q<Ui::PopupMenu>(
			top,
			st::popupMenuWithIcons);
		const auto raw = state->menu.get();
		const auto addAction = Ui::Menu::CreateAddActionCallback(raw);
		addAction({
			.text = tr::lng_proxy_add_from_clipboard(tr::now),
			.handler = [=] { AddProxyFromClipboard(_controller, uiShow()); },
			.icon = &st::menuIconImportTheme,
		});
		if (_listShareable.current()) {
			addAction({
				.text = tr::lng_proxy_edit_share_list_button(tr::now),
				.handler = [=] { _controller->shareItems(); },
				.icon = &st::menuIconShare,
			});
		}
		if (!_rows.empty()) {
			addAction({ .isSeparator = true });
			addAction({
				.text = tr::lng_group_invite_context_delete_all(tr::now),
				.handler = [=] { _controller->deleteItems(); },
				.icon = &st::menuIconDeleteAttention,
				.isAttention = true,
			});
		}
		raw->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
		top->setForceRippled(true);
		raw->setDestroyedCallback([=] {
			if (!state->closing) {
				top->setForceRippled(false);
			}
		});
		raw->popup(
			top->mapToGlobal(
				QPoint(top->width(), top->height() - st::lineWidth * 3)));
		return true;
	});
}

void ProxiesBox::setupUseProxy(not_null<Ui::VerticalLayout*> container) {
	const auto &st = st::proxyUseButton;
	const auto button = Settings::AddButtonWithIcon(
		container,
		tr::extras_ProxyUse(),
		st);
	_useProxyButton = button;
	button->toggleOn(_mode.value() | rpl::map([](ProxyData::Settings mode) {
		return (mode != ProxyData::Settings::Disabled);
	}));
	button->toggledChanges(
	) | rpl::on_next([=](bool enabled) {
		if (enabled == (_mode.current() != ProxyData::Settings::Disabled)) {
			return;
		}
		if (enabled) {
			enableProxy();
		} else {
			_controller->setProxySettings(ProxyData::Settings::Disabled);
		}
	}, button->lifetime());

	const auto status = Ui::CreateChild<Ui::FlatLabel>(
		button.get(),
		rpl::single(rpl::empty) | rpl::then(
			_settings.connectionTypeChanges()
		) | rpl::map([=] {
			return ProxyUseStatusText(_settings);
		}),
		st::proxyUseStatus);
	status->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto toggleWidth = Ui::ToggleView(st.toggle, false).getSize().width();
	button->widthValue(
	) | rpl::on_next([=](int width) {
		status->resizeToWidth(width
			- st.padding.left()
			- st.padding.right()
			- st.toggleSkip
			- toggleWidth);
		status->moveToLeft(
			st.padding.left(),
			st.padding.top() + st.height + st::proxyUseStatusSkip);
	}, status->lifetime());
}

void ProxiesBox::enableProxy() {
	// 上次用的是自定义代理但列表已空时，退回系统代理。
	const auto mode = (_lastUsedMode == ProxyData::Settings::Enabled
		&& _settings.list().empty())
		? ProxyData::Settings::System
		: _lastUsedMode;
	_controller->setProxySettings(mode);
}

void ProxiesBox::setupRotation(not_null<Ui::VerticalLayout*> container) {
	const auto interval = Settings::AddButtonWithLabel(
		container,
		tr::extras_ProxyAutoSwitchInterval(),
		_rotationTimeout.value() | rpl::map(ProxyRotationTimeoutText),
		Ui::DialogButtonStyle(container, st::settingsButtonNoIcon));
	interval->setClickedCallback([=] { showRotationTimeoutMenu(); });
	Ui::AddDialogText(container, tr::lng_proxy_auto_switch_about());
}

void ProxiesBox::showRotationTimeoutMenu() {
	_rotationTimeoutMenu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	const auto current = _rotationTimeout.current();
	for (const auto seconds : Core::SettingsProxy::kProxyRotationTimeouts) {
		const auto check = (seconds == current)
			? &st::mediaPlayerMenuCheck
			: nullptr;
		_rotationTimeoutMenu->addAction(
			ProxyRotationTimeoutText(seconds),
			[=] {
				_rotationTimeout = seconds;
				_controller->setProxyRotationTimeout(seconds);
			},
			check,
			check);
	}
	_rotationTimeoutMenu->popup(QCursor::pos());
}

void ProxiesBox::setupContent() {
	const auto inner = setInnerWidget(
		object_ptr<Ui::VerticalLayout>(this),
		st::boxScroll);

	setupUseProxy(inner);

	// 打开总开关后的线路列表：系统代理与自定义代理同列单选。
	const auto listWrap = inner->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			inner,
			object_ptr<Ui::VerticalLayout>(inner)));
	const auto list = listWrap->entity();
	Ui::AddSubsectionTitle(list, tr::extras_ProxyServers());

	const auto system = list->add(object_ptr<SystemProxyRow>(
		list,
		(_mode.current() == ProxyData::Settings::System)));
	system->setClickedCallback([=] {
		_controller->setProxySettings(ProxyData::Settings::System);
	});
	_mode.changes(
	) | rpl::on_next([=](ProxyData::Settings mode) {
		system->setChecked(mode == ProxyData::Settings::System);
	}, system->lifetime());

	_wrap = list->add(std::move(_initialWrap));

	const auto add = Settings::AddButtonWithIcon(
		list,
		tr::lng_proxy_add(),
		st::proxyListAction,
		{ &st::proxyListAddIcon, Settings::IconType::Simple });
	add->setClickedCallback([=] { addNewProxy(); });
	_addProxyButton = add;

	_pasteFromClipboard = list->add(
		object_ptr<Ui::SlideWrap<Settings::Button>>(
			list,
			Settings::CreateButtonWithIcon(
				list,
				tr::extras_ProxyPasteLink(),
				st::proxyListAction,
				{ &st::proxyListPasteIcon, Settings::IconType::Simple })));
	_pasteFromClipboard->entity()->setClickedCallback([=] {
		AddProxyFromClipboard(_controller, uiShow());
	});

	// 通话与轮换至少显示一项时，才与上方列表隔开。
	_optionsSkip = list->add(
		object_ptr<Ui::SlideWrap<Ui::FixedHeightWidget>>(
			list,
			object_ptr<Ui::FixedHeightWidget>(list, st::boxMediumSkip)));

	_proxyForCalls = list->add(
		object_ptr<Ui::SlideWrap<Settings::Button>>(
			list,
			Settings::CreateButtonWithIcon(
				list,
				tr::lng_proxy_use_for_calls(),
				Ui::DialogButtonStyle(list, st::settingsButtonNoIcon))));
	_proxyForCalls->entity()->toggleOn(
		rpl::single(_settings.useProxyForCalls()));

	_proxyRotation = list->add(
		object_ptr<Ui::SlideWrap<Settings::Button>>(
			list,
			Settings::CreateButtonWithIcon(
				list,
				tr::lng_proxy_auto_switch(),
				Ui::DialogButtonStyle(list, st::settingsButtonNoIcon))));
	_proxyRotation->entity()->toggleOn(
		rpl::single(_settings.proxyRotationEnabled()));

	_proxyRotationOptions = list->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			list,
			object_ptr<Ui::VerticalLayout>(list)));
	setupRotation(_proxyRotationOptions->entity());

	listWrap->toggleOn(_mode.value() | rpl::map([](ProxyData::Settings mode) {
		return (mode != ProxyData::Settings::Disabled);
	}));

	// IPv6 对直连同样有效，所以不随总开关隐藏。
	Ui::AddSkip(inner, st::boxMediumSkip);
	_tryIPv6 = Settings::AddButtonWithIcon(
		inner,
		tr::lng_connection_try_ipv6(),
		Ui::DialogButtonStyle(inner, st::settingsButtonNoIcon));
	_tryIPv6->toggleOn(rpl::single(_settings.tryIPv6()));
	Ui::AddDialogText(inner, tr::lng_proxy_about());

	_tryIPv6->toggledChanges(
	) | rpl::on_next([=](bool checked) {
		_controller->setTryIPv6(checked);
	}, _tryIPv6->lifetime());
	_proxyForCalls->entity()->toggledChanges(
	) | rpl::on_next([=](bool checked) {
		_controller->setProxyForCalls(checked);
	}, _proxyForCalls->lifetime());
	_proxyRotation->entity()->toggledChanges(
	) | rpl::on_next([=](bool checked) {
		_controller->setProxyRotationEnabled(checked);
		refreshProxyRotation();
	}, _proxyRotation->lifetime());
	_mode.changes(
	) | rpl::on_next([=] {
		refreshProxyForCalls();
		refreshProxyRotation();
	}, inner->lifetime());

	refreshProxyForCalls();
	refreshProxyRotation();
	refreshPasteFromClipboard();
	listWrap->finishAnimating();
	_pasteFromClipboard->finishAnimating();
	_optionsSkip->finishAnimating();
	_proxyForCalls->finishAnimating();
	_proxyRotation->finishAnimating();
	_proxyRotationOptions->finishAnimating();

	inner->resizeToWidth(st::boxWideWidth);

	inner->heightValue(
	) | rpl::map([=](int height) {
		return std::min(height, st::boxMaxListHeight);
	}) | rpl::distinct_until_changed(
	) | rpl::on_next([=](int height) {
		setDimensions(st::boxWideWidth, height, true);
	}, inner->lifetime());
}

void ProxiesBox::refreshProxyForCalls() {
	if (!_proxyForCalls) {
		return;
	}
	_proxyForCalls->toggle(
		(_mode.current() == ProxyData::Settings::Enabled
			&& _currentProxySupportsCallsId != 0),
		anim::type::normal);
	refreshOptionsSkip();
}

void ProxiesBox::refreshProxyRotation() {
	if (!_proxyRotation || !_proxyRotationOptions) {
		return;
	}
	const auto visible = (_mode.current() == ProxyData::Settings::Enabled)
		&& _settings.selected()
		&& (_settings.list().size() > 1);
	_proxyRotation->toggle(visible, anim::type::normal);
	_proxyRotationOptions->toggle(
		visible && _proxyRotation->entity()->toggled(),
		anim::type::normal);
	refreshOptionsSkip();
}

void ProxiesBox::refreshOptionsSkip() {
	if (!_optionsSkip || !_proxyForCalls || !_proxyRotation) {
		return;
	}
	_optionsSkip->toggle(
		_proxyForCalls->toggled() || _proxyRotation->toggled(),
		anim::type::normal);
}

void ProxiesBox::refreshPasteFromClipboard() {
	if (!_pasteFromClipboard) {
		return;
	}
	_pasteFromClipboard->toggle(_rows.empty(), anim::type::normal);
}

void ProxiesBox::addNewProxy() {
	getDelegate()->show(_controller->addNewItemBox());
}

void ProxiesBox::applyView(View &&view) {
	if (view.selected) {
		_currentProxySupportsCallsId = view.supportsCalls ? view.id : 0;
	} else if (view.id == _currentProxySupportsCallsId) {
		_currentProxySupportsCallsId = 0;
	}
	refreshProxyForCalls();

	const auto id = view.id;
	const auto i = _rows.find(id);
	if (i == _rows.end()) {
		const auto wrap = _wrap
			? _wrap.data()
			: _initialWrap.data();
		const auto &[i, ok] = _rows.emplace(id, nullptr);
		i->second.reset(wrap->insert(
			0,
			object_ptr<ProxyRow>(
				wrap,
				std::move(view))));
		setupButtons(id, i->second.get());
		wrap->resizeToWidth(width());
	} else if (view.host.isEmpty()) {
		_rows.erase(i);
	} else {
		i->second->updateFields(std::move(view));
	}
	refreshProxyRotation();
	refreshPasteFromClipboard();
}

void ProxiesBox::setupButtons(int id, not_null<ProxyRow*> button) {
	button->deleteClicks(
	) | rpl::on_next([=] {
		_controller->deleteItem(id);
	}, button->lifetime());

	button->restoreClicks(
	) | rpl::on_next([=] {
		_controller->restoreItem(id);
	}, button->lifetime());

	button->editClicks(
	) | rpl::on_next([=] {
		getDelegate()->show(_controller->editItemBox(id));
	}, button->lifetime());

	rpl::merge(
		button->shareClicks() | rpl::map_to(false),
		button->showQrClicks() | rpl::map_to(true)
	) | rpl::on_next([=](bool qr) {
		_controller->shareItem(id, qr);
	}, button->lifetime());

	button->openBrowserClicks(
	) | rpl::on_next([=] {
		_controller->openBrowser(id);
	}, button->lifetime());

	button->clicks(
	) | rpl::on_next([=] {
		_controller->applyItem(id);
	}, button->lifetime());
}

ProxyBox::ProxyBox(
	QWidget*,
	const ProxyData &data,
	Fn<void(ProxyData)> callback,
	Fn<void(ProxyData)> shareCallback)
: _allowShare(data.type != Type::None)
, _callback(std::move(callback))
, _shareCallback(std::move(shareCallback))
, _content(this) {
	setupControls(data);
}

void ProxyBox::prepare() {
	setTitle(_allowShare ? tr::lng_proxy_edit() : tr::lng_proxy_add());
	setProperty("responsiveBoxWidth", true);

	connect(_host.data(), &HostInput::changed, [=] {
		Ui::PostponeCall(_host, [=] {
			const auto host = _host->getLastText().trimmed();
			static const auto mask = QRegularExpression(
				u"^\\d+\\.\\d+\\.\\d+\\.\\d+:(\\d*)$"_q);
			const auto match = mask.match(host);
			if (_host->cursorPosition() == host.size()
				&& match.hasMatch()) {
				const auto port = match.captured(1);
				_port->setText(port);
				_port->setCursorPosition(port.size());
				_port->setFocus();
				_host->setText(host.mid(0, host.size() - port.size() - 1));
			}
		});
	});
	_port.data()->events(
	) | rpl::on_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::KeyPress
			&& (static_cast<QKeyEvent*>(e.get())->key() == Qt::Key_Backspace)
			&& _port->cursorPosition() == 0) {
			_host->setCursorPosition(_host->getLastText().size());
			_host->setFocus();
		}
	}, _port->lifetime());

	const auto submit = [=] {
		if (_webHost->hasFocus()
			&& !_webHost->getLastText().trimmed().isEmpty()) {
			_secret->setFocus();
		} else if (_host->hasFocus()
			&& !_host->getLastText().trimmed().isEmpty()) {
			_port->setFocus();
		} else if (_port->hasFocus()
			&& !_port->getLastText().trimmed().isEmpty()) {
			if (_type->current() == Type::Mtproto) {
				_secret->setFocus();
			} else {
				_user->setFocus();
			}
		} else if (_user->hasFocus()) {
			_password->setFocus();
		} else {
			save();
		}
	};
	connect(_host.data(), &Ui::MaskedInputField::submitted, submit);
	connect(_port.data(), &Ui::MaskedInputField::submitted, submit);
	_user->submits(
	) | rpl::on_next(submit, _user->lifetime());
	_webHost->submits(
	) | rpl::on_next(submit, _webHost->lifetime());
	connect(_password.data(), &Ui::MaskedInputField::submitted, submit);
	connect(_secret.data(), &Ui::MaskedInputField::submitted, submit);

	refreshButtons();
	_content->heightValue() | rpl::on_next([=](int height) {
		if (!_changingType) {
			setDimensions(st::proxyEditWidth, std::max(height, _formHeight), true);
		}
	}, lifetime());
}

void ProxyBox::refreshButtons() {
	clearButtons();
	const auto close = addCloseButton(st::boxTitleClose, [=] {
		closeBox();
	});
	close->setAccessibleName(tr::lng_close(tr::now));
	addButton(tr::lng_settings_save(), [=] { save(); });
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	const auto type = _type->current();
	if (_allowShare
		&& (type == Type::Socks5
			|| type == Type::Mtproto
			|| type == Type::Web)) {
		addLeftButton(tr::lng_proxy_share(), [=] { share(); });
	}
}

void ProxyBox::save() {
	if (const auto data = collectData()) {
		_callback(data);
		closeBox();
	}
}

void ProxyBox::share() {
	if (const auto data = collectData()) {
		_shareCallback(data);
	}
}

ProxyData ProxyBox::collectData() {
	auto result = ProxyData();
	result.type = _type->current();
	const auto web = (result.type == Type::Web);
	result.host = web
		? QString()
		: _host->getLastText().trimmed();
	result.port = web
		? 443
		: _port->getLastText().trimmed().toInt();
	result.user = (result.type == Type::Mtproto || web)
		? QString()
		: _user->getLastText();
	result.password = (result.type == Type::Mtproto || web)
		? _secret->getLastText()
		: _password->getLastText();
	if (web) {
		result.setWebAddress(_webHost->getLastText());
	}
	if (result.host.isEmpty()) {
		if (web) {
			_webHost->showError();
		} else {
			_host->showError();
		}
	} else if (!web && !result.port) {
		_port->showError();
	} else if ((result.type == Type::Http || result.type == Type::Socks5)
		&& !result.password.isEmpty() && result.user.isEmpty()) {
		_user->showError();
	} else if ((result.type == Type::Mtproto || web) && !result.valid()) {
		_secret->showError();
	} else if (!result) {
		if (web) {
			_webHost->showError();
		} else {
			_host->showError();
		}
	} else {
		return result;
	}
	return ProxyData();
}

void ProxyBox::fillFromClipboard() {
	const auto data = ProxyDataFromClipboard();
	if (!data) {
		uiShow()->showToast(
			tr::lng_proxy_add_from_clipboard_failed_toast(tr::now));
		return;
	}
	_type->setValue(data.type);
	if (data.type == Type::Web) {
		_webHost->setText(data.webAddress());
	} else {
		_host->setText(data.host);
		_port->setText(QString::number(data.port));
	}
	if (data.type == Type::Mtproto || data.type == Type::Web) {
		_secret->setText(data.password);
	} else {
		_user->setText(data.user);
		_password->setText(data.password);
	}
}

void ProxyBox::setupTypes() {
	addLabel(_content, tr::extras_ProxyType(tr::now));
	const auto field = _content->add(
		object_ptr<ExtrasUi::ProxyTypeField>(
			_content,
			_type->value(),
			[=](Type type) { _type->setValue(type); }),
		st::proxyEditInputPadding);
	field->setObjectName(u"proxy-type"_q);
}

void ProxyBox::setupSocketAddress(const ProxyData &data) {
	_socketAddress = _content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			_content,
			object_ptr<Ui::VerticalLayout>(_content)));
	const auto content = _socketAddress->entity();
	const auto labels = content->add(object_ptr<Ui::FixedHeightWidget>(
		content, st::proxyEditTitle.style.font->height), st::proxyEditTitlePadding);
	const auto hostLabel = Ui::CreateChild<Ui::FlatLabel>(
		labels, tr::lng_connection_host_ph(), st::proxyEditTitle);
	const auto portLabel = Ui::CreateChild<Ui::FlatLabel>(
		labels, tr::lng_connection_port_ph(), st::proxyEditTitle);
	labels->widthValue() | rpl::on_next([=](int width) {
		hostLabel->moveToLeft(0, 0);
		const auto portWidth = style::ConvertScale(width < style::ConvertScale(400) ? 84 : 96);
		portLabel->moveToLeft(width - portWidth, 0);
	}, labels->lifetime());
	const auto address = content->add(
		object_ptr<Ui::FixedHeightWidget>(
			content,
			st::connectionHostInputField.heightMin),
		st::proxyEditInputPadding);
	_host = Ui::CreateChild<HostInput>(
		address,
		st::connectionHostInputField,
		rpl::single(u"proxy.example.com"_q),
		data.host);
	_port = Ui::CreateChild<Ui::NumberInput>(
		address,
		st::connectionPortInputField,
		rpl::single(u"443"_q),
		data.port ? QString::number(data.port) : QString(),
		65535);
	address->widthValue(
	) | rpl::on_next([=](int width) {
		_port->resize(style::ConvertScale(width < style::ConvertScale(400) ? 84 : 96), _port->height());
		_port->moveToRight(0, 0);
		_host->resize(
			width - _port->width() - st::proxyEditSkip,
			_host->height());
		_host->moveToLeft(0, 0);
	}, address->lifetime());
}

void ProxyBox::setupWebAddress(const ProxyData &data) {
	_webAddress = _content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			_content,
			object_ptr<Ui::VerticalLayout>(_content)));
	const auto content = _webAddress->entity();
	addLabel(content, tr::lng_proxy_web_server_label(tr::now));
	_webHost = content->add(
		object_ptr<Ui::InputField>(
			content,
			st::connectionUserInputField,
			tr::lng_proxy_web_host_ph(),
			(data.type == Type::Web) ? data.webAddress() : QString()),
		st::proxyEditInputPadding);
	_webHost->setInputMethodHints(Qt::ImhUrlCharactersOnly
		| Qt::ImhNoAutoUppercase
		| Qt::ImhNoPredictiveText);
}

void ProxyBox::setupCredentials(const ProxyData &data) {
	_credentials = _content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			_content,
			object_ptr<Ui::VerticalLayout>(_content)));
	const auto credentials = _credentials->entity();
	addLabel(credentials, tr::extras_ProxyUsernameOptional(tr::now));
	_user = credentials->add(
		object_ptr<Ui::InputField>(
			credentials,
			st::connectionUserInputField,
			tr::extras_ProxyNoAuthentication(),
			data.user),
		st::proxyEditInputPadding);
	_user->setInputMethodHints(Qt::ImhNoAutoUppercase
		| Qt::ImhNoPredictiveText);

	addLabel(credentials, tr::extras_ProxyPasswordOptional(tr::now));
	auto passwordWrap = object_ptr<Ui::RpWidget>(credentials);
	_password = Ui::CreateChild<Ui::PasswordInput>(
		passwordWrap.data(),
		st::connectionPasswordInputField,
		rpl::single(QString(8, QChar(0x2022))),
		(data.type == Type::Mtproto || data.type == Type::Web)
			? QString()
			: data.password);
	_password->move(0, 0);
	_password->heightValue(
	) | rpl::on_next([=, wrap = passwordWrap.data()](int height) {
		wrap->resize(wrap->width(), height);
	}, _password->lifetime());
	passwordWrap->widthValue(
	) | rpl::on_next([=](int width) {
		_password->resize(width, _password->height());
	}, _password->lifetime());
	credentials->add(std::move(passwordWrap), st::proxyEditInputPadding);
}

void ProxyBox::setupMtprotoCredentials(const ProxyData &data) {
	_mtprotoCredentials = _content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			_content,
			object_ptr<Ui::VerticalLayout>(_content)));
	const auto mtproto = _mtprotoCredentials->entity();
	addLabel(mtproto, tr::lng_proxy_credentials(tr::now));

	auto secretWrap = object_ptr<Ui::RpWidget>(mtproto);
	_secret = Ui::CreateChild<Base64UrlInput>(
		secretWrap.data(),
		st::connectionUserInputField,
		tr::lng_connection_proxy_secret_ph(),
		(data.type == Type::Mtproto || data.type == Type::Web)
			? data.password
			: QString());
	_secret->move(0, 0);
	_secret->heightValue(
	) | rpl::on_next([=, wrap = secretWrap.data()](int height) {
		wrap->resize(wrap->width(), height);
	}, _secret->lifetime());
	secretWrap->widthValue(
	) | rpl::on_next([=](int width) {
		_secret->resize(width, _secret->height());
	}, _secret->lifetime());
	mtproto->add(std::move(secretWrap), st::proxyEditInputPadding);

	// 赞助频道提示只对需要密钥的 MTProto 与 Web 代理出现，随密钥区一起显示。
	auto warningText = _type->value(
	) | rpl::map([](Type type) {
		return (type == Type::Web)
			? tr::lng_proxy_web_warning(tr::now)
			: tr::lng_proxy_sponsor_warning(tr::now);
	});
	const auto warning = mtproto->add(
		object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>(
			mtproto,
			object_ptr<Ui::FlatLabel>(
				mtproto,
				std::move(warningText),
				st::proxyEditWarningLabel),
			st::proxyEditWarningPadding),
		st::proxyEditWarningMargin);
	warning->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(warning);
		{
			auto hq = PainterHighQualityEnabler(p);
			const auto radius = st::proxyFormInput.borderRadius;
			p.setPen(Qt::NoPen);
			p.setBrush(st::proxyFormBg);
			p.drawRoundedRect(warning->rect(), radius, radius);
		}
		st::proxyEditWarningIcon.paint(
			p,
			st::proxyEditWarningIconPosition,
			warning->width());
	}, warning->lifetime());
}

void ProxyBox::setupControls(const ProxyData &data) {
	_type = std::make_shared<Ui::RadioenumGroup<Type>>(
		(data.type == Type::None
			? Type::Socks5
			: data.type));
	_content.create(this);
	_content->resizeToWidth(st::proxyEditWidth);
	_content->moveToLeft(0, 0);

	setupTypes();
	setupSocketAddress(data);
	setupWebAddress(data);
	setupCredentials(data);
	setupMtprotoCredentials(data);
	_host->setObjectName(u"proxy-host"_q);
	_port->setObjectName(u"proxy-port"_q);
	_user->setObjectName(u"proxy-username"_q);
	_password->setObjectName(u"proxy-password"_q);
	_secret->setObjectName(u"proxy-secret"_q);
	_webHost->setObjectName(u"proxy-web-host"_q);
	Ui::AddSkip(_content, style::ConvertScale(12));
	// 只在添加时提供自动填写，编辑已有代理不覆盖原有内容。
	if (!_allowShare) {
		const auto paste = Settings::AddButtonWithIcon(
			_content,
			tr::extras_ProxyPasteAutofill(),
			st::proxyEditPasteButton,
			{ &st::proxyListPasteIcon, Settings::IconType::Simple });
		paste->setObjectName(u"proxy-paste"_q);
		paste->setClickedCallback([=] { fillFromClipboard(); });
	}

	const auto handleType = [=](Type type) {
		_changingType = true;
		const auto web = (type == Type::Web);
		const auto credentialsShown
			= (type == Type::Http || type == Type::Socks5);
		const auto mtprotoShown = (type == Type::Mtproto || web);
		_socketAddress->toggle(!web, anim::type::instant);
		_webAddress->toggle(web, anim::type::instant);
		_credentials->toggle(credentialsShown, anim::type::instant);
		_mtprotoCredentials->toggle(mtprotoShown, anim::type::instant);
		const auto credentialsPolicy = credentialsShown
			? Qt::StrongFocus
			: Qt::NoFocus;
		_user->rawTextEdit()->setFocusPolicy(credentialsPolicy);
		_password->setFocusPolicy(credentialsPolicy);
		_secret->setFocusPolicy(
			mtprotoShown ? Qt::StrongFocus : Qt::NoFocus);
		_webHost->setFocusPolicy(web ? Qt::StrongFocus : Qt::NoFocus);
		_changingType = false;
	};
	_type->setChangedCallback([=](Type type) {
		handleType(type);
		setDimensions(st::proxyEditWidth, std::max(_formHeight, _content->height()), true);
		refreshButtons();
	});
	// 各协议共用最大表单高度，切换时保留弹窗位置。
	for (const auto type : { Type::Socks5, Type::Http, Type::Mtproto, Type::Web }) {
		handleType(type);
		_content->resizeToWidth(st::proxyEditWidth);
		accumulate_max(_formHeight, _content->height());
	}
	handleType(_type->current());
}

void ProxyBox::addLabel(
		not_null<Ui::VerticalLayout*> parent,
		const QString &text) const {
	parent->add(
		object_ptr<Ui::FlatLabel>(
			parent,
			text,
			st::proxyEditTitle),
		st::proxyEditTitlePadding);
}

using Connection = MTP::details::AbstractConnection;
using Checker = MTP::ProxyCheckConnection;

} // namespace

ProxiesBoxController::ProxiesBoxController(not_null<Main::Account*> account)
: _account(account)
, _settings(Core::App().settings().proxy())
, _saveTimer([] { Local::writeSettings(); }) {
	_list = ranges::views::all(
		_settings.list()
	) | ranges::views::transform([&](const ProxyData &proxy) {
		return Item{ ++_idCounter, proxy };
	}) | ranges::to_vector;

	_settings.connectionTypeChanges(
	) | rpl::on_next([=] {
		_proxySettingsChanges.fire_copy(_settings.settings());
		const auto i = findByProxy(_settings.selected());
		if (i != end(_list)) {
			updateView(*i);
		}
	}, _lifetime);

	MTP::WebProxy::Transport::StateChanges(
	) | rpl::on_next([=](const MTP::WebProxy::Transport::StateChange &change) {
		const auto i = findByProxy(change.proxy);
		if (i != end(_list)) {
			updateView(*i);
		}
	}, _lifetime);

	for (auto &item : _list) {
		refreshChecker(item);
	}
}

ProxyData ProxiesBoxController::ProxyFromLink(const QString &link) {
	const auto trimmed = link.trimmed();
	const auto converted = Core::TryConvertUrlToLocal(trimmed);
	return ProxyDataFromLocalUrl(converted.isEmpty() ? trimmed : converted);
}

void ProxiesBoxController::ShowApplyConfirmation(
		Window::SessionController *controller,
		Type type,
		const QMap<QString, QString> &fields) {
	const auto proxy = ProxyDataFromFields(type, fields);
	if (!proxy) {
		const auto status = proxy.status();
		auto box = Ui::MakeInformBox(
			((status == ProxyData::Status::Unsupported)
				? tr::lng_proxy_unsupported(tr::now, tr::rich)
				: (status == ProxyData::Status::IncorrectSecret)
				? tr::lng_proxy_incorrect_secret(tr::now, tr::rich)
				: tr::lng_proxy_invalid(tr::now, tr::rich)));
		if (controller) {
			controller->uiShow()->showBox(std::move(box));
		} else {
			Ui::show(std::move(box));
		}
		return;
	}
	static const auto UrlStartRegExp = QRegularExpression(
		"^https://",
		QRegularExpression::CaseInsensitiveOption);
	static const auto UrlEndRegExp = QRegularExpression("/$");
	const auto displayed = "https://"
		+ ((type == Type::Web) ? proxy.webAddress() : proxy.host)
		+ "/";
	const auto parsed = QUrl::fromUserInput(displayed);
	const auto displayUrl = !UrlClickHandler::IsSuspicious(displayed)
		? displayed
		: parsed.isValid()
		? QString::fromUtf8(parsed.toEncoded())
		: UrlClickHandler::ShowEncoded(displayed);
	const auto displayServer = QString(
		displayUrl
	).replace(
		UrlStartRegExp,
		QString()
	).replace(UrlEndRegExp, QString());
	const auto box = [=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_proxy_box_table_title());
		box->setStyle(st::proxyApplyBox);
		box->addCloseButton(st::boxTitleClose, [=] {
			box->closeBox();
		});

		if (ProxyDataIsShareable(proxy) && type != Type::Web) {
			const auto account = controller
				? &controller->session().account()
				: &Core::App().activeAccount();
			const auto top = box->addTopButton(st::boxTitleMenu);
			const auto state = top->lifetime().make_state<
				TopButtonMenuState>();
			top->setClickedCallback([=] {
				state->menu = base::make_unique_q<Ui::PopupMenu>(
					top,
					st::popupMenuWithIcons);
				const auto raw = state->menu.get();
				const auto addAction = Ui::Menu::CreateAddActionCallback(raw);
				addAction({
					.text = tr::lng_proxy_edit_share(tr::now),
					.handler = [=] {
						ShareProxy(box->uiShow(), account, proxy, false);
					},
					.icon = &st::menuIconShare,
				});
				addAction({
					.text = tr::lng_group_invite_context_qr(tr::now),
					.handler = [=] {
						ShareProxy(box->uiShow(), account, proxy, true);
					},
					.icon = &st::menuIconQrCode,
				});
				raw->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
				top->setForceRippled(true);
				raw->setDestroyedCallback([=] {
					if (!state->closing) {
						top->setForceRippled(false);
					}
				});
				raw->popup(top->mapToGlobal(QPoint(
					top->width(),
					top->height() - st::lineWidth * 3)));
				return true;
			});
		}

		const auto table = box->addRow(
			object_ptr<Ui::TableLayout>(
				box,
				st::proxyApplyBoxTable),
			st::proxyApplyBoxTableMargin);
		const auto addRow = [&](
				rpl::producer<QString> label,
				object_ptr<Ui::RpWidget> value) {
			table->addRow(
				object_ptr<Ui::FlatLabel>(
					table,
					std::move(label),
					table->st().defaultLabel),
				std::move(value),
				st::proxyApplyBoxTableLabelMargin,
				st::proxyApplyBoxTableValueMargin);
		};
		const auto add = [&](
				const QString &value,
				rpl::producer<QString> label) {
			if (!value.isEmpty()) {
				constexpr auto kOneLineCount = 20;
				const auto oneLine = value.length() <= kOneLineCount;
				auto widget = object_ptr<Ui::FlatLabel>(
					table,
					rpl::single(Ui::Text::Wrapped(
						{ value },
						EntityType::Code,
						{})),
					(oneLine
						? table->st().defaultValue
						: st::proxyApplyBoxValueMultiline),
					st::defaultPopupMenu);
				addRow(std::move(label), std::move(widget));
			}
		};
		if (!displayServer.isEmpty()) {
			add(displayServer, tr::lng_proxy_box_server());
		}
		if (type != Type::Web) {
			add(QString::number(proxy.port), tr::lng_proxy_box_port());
		}
		if (type == Type::Socks5) {
			add(proxy.user, tr::lng_proxy_box_username());
			add(proxy.password, tr::lng_proxy_box_password());
		} else if (type == Type::Mtproto || type == Type::Web) {
			add(proxy.password, tr::lng_proxy_box_secret());
		}

		if (type != Type::Web) {
			struct ProxyCheckStatusState {
				Checker v4;
				Checker v6;
				rpl::variable<TextWithEntities> statusValue;
				bool finished = false;
			};
			const auto state
				= box->lifetime().make_state<ProxyCheckStatusState>();
			state->statusValue = Ui::Text::Link(
				tr::lng_proxy_box_check_status(tr::now));
			const auto weak = base::make_weak(box);
			auto statusWidget = object_ptr<Ui::FlatLabel>(
				table,
				state->statusValue.value(),
				table->st().defaultValue,
				st::defaultPopupMenu);
			const auto statusLabel = statusWidget.data();
			addRow(tr::lng_proxy_box_status(), std::move(statusWidget));
			const auto setUnavailable = [=] {
				state->statusValue = TextWithEntities{
					tr::lng_proxy_box_table_unavailable(tr::now),
				};
				statusLabel->setTextColorOverride(
					st::proxyRowStatusFgOffline->c);
			};
			const auto runCheck = [=] {
				if (!weak) {
					return;
				}
				const auto account = controller
					? &controller->session().account()
					: &Core::App().activeAccount();
				state->finished = false;
				state->statusValue = TextWithEntities{
					tr::lng_proxy_box_table_checking(tr::now),
				};
				statusLabel->setTextColorOverride(st::proxyRowStatusFg->c);
				MTP::StartProxyCheck(
					&account->mtp(),
					proxy,
					Core::App().settings().proxy().tryIPv6(),
					state->v4,
					state->v6,
					[=](Connection *raw, int ping) {
						if (!weak || state->finished) {
							return;
						}
						MTP::DropProxyChecker(state->v4, state->v6, raw);
						state->finished = true;
						MTP::ResetProxyCheckers(state->v4, state->v6);
						state->statusValue = TextWithEntities{
							tr::lng_proxy_box_table_available(
								tr::now,
								lt_ping,
								QString::number(ping)),
						};
						statusLabel->setTextColorOverride(
							st::proxyRowStatusFgAvailable->c);
					},
					[=](Connection *raw) {
						if (!weak || state->finished) {
							return;
						}
						MTP::DropProxyChecker(state->v4, state->v6, raw);
						if (!MTP::HasProxyCheckers(state->v4, state->v6)) {
							state->finished = true;
							setUnavailable();
						}
					});
				if (!MTP::HasProxyCheckers(state->v4, state->v6)) {
					state->finished = true;
					setUnavailable();
				}
			};
			statusLabel->setClickHandlerFilter([=](const auto &...) {
				auto &proxy = Core::App().settings().proxy();
				if (proxy.checkIpWarningShown()) {
					runCheck();
				} else {
					box->uiShow()->showBox(Ui::MakeConfirmBox({
						.text = tr::lng_proxy_check_ip_warning(),
						.confirmed = [=](Fn<void()> close) {
							auto &proxy = Core::App().settings().proxy();
							proxy.setCheckIpWarningShown(true);
							Local::writeSettings();
							close();
							runCheck();
						},
						.confirmText = tr::lng_proxy_check_ip_proceed(),
						.title = tr::lng_proxy_check_ip_warning_title(),
					}));
				}
				return false;
			});
		}

		if (type == Type::Mtproto || type == Type::Web) {
			table->addRow(
				object_ptr<Ui::FlatLabel>(
					table,
					(type == Type::Web)
						? tr::lng_proxy_web_warning()
						: tr::lng_proxy_sponsor_warning(),
					st::proxyApplyBoxSponsorLabel),
				object_ptr<Ui::RpWidget>(nullptr),
				st::proxyApplyBoxSponsorMargin,
				st::proxyApplyBoxSponsorMargin);
		}

		const auto enableButton = box->addButton(
			tr::lng_proxy_box_table_button(),
			[=] {
				auto &settings = Core::App().settings().proxy();
				if (settings.indexInList(proxy) < 0) {
					settings.addToList(proxy);
				}
				Core::App().setCurrentProxy(
					proxy,
					ProxyData::Settings::Enabled);
				Local::writeSettings();
				box->closeBox();
			});
		enableButton->setFullRadius(true);
		box->events() | rpl::on_next([=](not_null<QEvent*> e) {
			if ((e->type() != QEvent::KeyPress) || !enableButton) {
				return;
			}
			const auto k = static_cast<QKeyEvent*>(e.get());
			if (k->key() == Qt::Key_Enter || k->key() == Qt::Key_Return) {
				enableButton->clicked({}, Qt::LeftButton);
			}
		}, box->lifetime());
	};
	if (controller) {
		controller->uiShow()->showBox(Box(box));
	} else {
		Ui::show(Box(box));
	}
}

auto ProxiesBoxController::proxySettingsValue() const
-> rpl::producer<ProxyData::Settings> {
	return _proxySettingsChanges.events_starting_with_copy(
		_settings.settings()
	) | rpl::distinct_until_changed();
}

void ProxiesBoxController::refreshChecker(Item &item) {
	if (item.data.type == Type::Web) {
		MTP::ResetProxyCheckers(item.checker, item.checkerv6);
		item.state = ItemState::NotTested;
		item.ping = 0;
		updateView(item);
		return;
	}
	item.state = ItemState::Checking;
	const auto id = item.id;
	MTP::StartProxyCheck(
		&_account->mtp(),
		item.data,
		Core::App().settings().proxy().tryIPv6(),
		item.checker,
		item.checkerv6,
		[=](Connection *raw, int pingTime) {
			const auto item = ranges::find(
				_list,
				id,
				[](const Item &item) { return item.id; });
			if (item == end(_list)) {
				return;
			}
			MTP::DropProxyChecker(item->checker, item->checkerv6, raw);
			MTP::ResetProxyCheckers(item->checker, item->checkerv6);
			if (item->state == ItemState::Checking) {
				item->state = ItemState::Available;
				item->ping = pingTime;
				updateView(*item);
			}
		},
		[=](Connection *raw) {
			const auto item = ranges::find(
				_list,
				id,
				[](const Item &item) { return item.id; });
			if (item == end(_list)) {
				return;
			}
			MTP::DropProxyChecker(item->checker, item->checkerv6, raw);
			if (!MTP::HasProxyCheckers(item->checker, item->checkerv6)
				&& item->state == ItemState::Checking) {
				item->state = ItemState::Unavailable;
				updateView(*item);
			}
		});
	if (!MTP::HasProxyCheckers(item.checker, item.checkerv6)) {
		item.state = ItemState::Unavailable;
	}
}

object_ptr<Ui::BoxContent> ProxiesBoxController::CreateOwningBox(
		not_null<Main::Account*> account,
		const QString &highlightId) {
	auto controller = std::make_unique<ProxiesBoxController>(account);
	auto box = controller->create(highlightId);
	Ui::AttachAsChild(box, std::move(controller));
	return box;
}

object_ptr<Ui::BoxContent> ProxiesBoxController::create(
		const QString &highlightId) {
	auto result = Box<ProxiesBox>(this, _settings, highlightId);
	_show = result->uiShow();
	for (const auto &item : _list) {
		updateView(item);
	}
	return result;
}

auto ProxiesBoxController::findById(int id) -> std::vector<Item>::iterator {
	const auto result = ranges::find(
		_list,
		id,
		[](const Item &item) { return item.id; });
	Assert(result != end(_list));
	return result;
}

auto ProxiesBoxController::findByProxy(const ProxyData &proxy)
->std::vector<Item>::iterator {
	return ranges::find(
		_list,
		proxy,
		[](const Item &item) { return item.data; });
}

void ProxiesBoxController::deleteItem(int id) {
	setDeleted(id, true);
}

void ProxiesBoxController::deleteItems() {
	for (const auto &item : _list) {
		setDeleted(item.id, true);
	}
}

void ProxiesBoxController::restoreItem(int id) {
	setDeleted(id, false);
}

void ProxiesBoxController::shareItem(int id, bool qr) {
	share(findById(id)->data, qr);
}

void ProxiesBoxController::shareItems() {
	auto result = QString();
	for (const auto &item : _list) {
		if (!item.deleted && ProxyDataIsShareable(item.data)) {
			const auto link = ProxyDataToPublicLink(_account, item.data);
			if (!link.isEmpty()) {
				result += (result.isEmpty() ? QString() : u"\n\n"_q) + link;
			}
		}
	}
	if (result.isEmpty()) {
		return;
	}
	QGuiApplication::clipboard()->setText(result);
	_show->showToast({
		.text = { tr::lng_proxy_edit_share_list_toast(tr::now) },
		.iconLottie = u"toast/copy"_q,
		.iconLottieSize = st::toastLottieIconSize,
	});
}

void ProxiesBoxController::applyItem(int id) {
	auto item = findById(id);
	if (_settings.isEnabled() && (_settings.selected() == item->data)) {
		return;
	} else if (item->deleted) {
		return;
	}

	auto j = findByProxy(_settings.selected());

	Core::App().setCurrentProxy(
		item->data,
		ProxyData::Settings::Enabled);
	saveDelayed();

	if (j != end(_list)) {
		updateView(*j);
	}
	updateView(*item);
}

void ProxiesBoxController::openBrowser(int id) {
	const auto item = findById(id);
	if (_settings.isEnabled()
		&& _settings.selected() == item->data
		&& item->data.type == Type::Web) {
		MTP::WebProxy::Transport::OpenBrowser(item->data);
	}
}

void ProxiesBoxController::setDeleted(int id, bool deleted) {
	auto item = findById(id);
	if (item->deleted == deleted) {
		return;
	}
	item->deleted = deleted;

	if (deleted) {
		const auto removed = _settings.removeFromList(item->data);
		Assert(removed);

		if (item->data == _settings.selected()) {
			_lastSelectedProxy = _settings.selected();
			_settings.setSelected(MTP::ProxyData());
			if (_settings.isEnabled()) {
				_lastSelectedProxyUsed = true;
				Core::App().setCurrentProxy(
					ProxyData(),
					ProxyData::Settings::System);
				saveDelayed();
			} else {
				_lastSelectedProxyUsed = false;
			}
		}
	} else {
		if (_settings.indexInList(item->data) < 0) {
			const auto &proxies = _settings.list();
			auto insertBefore = item + 1;
			while (insertBefore != end(_list) && insertBefore->deleted) {
				++insertBefore;
			}
			const auto foundIndex = (insertBefore == end(_list))
				? int(proxies.size())
				: _settings.indexInList(insertBefore->data);
			const auto insertIndex = (foundIndex >= 0)
				? foundIndex
				: int(proxies.size());
			_settings.insertToList(insertIndex, item->data);
		}

		if (!_settings.selected() && _lastSelectedProxy == item->data) {
			Assert(!_settings.isEnabled());

			if (base::take(_lastSelectedProxyUsed)) {
				Core::App().setCurrentProxy(
					base::take(_lastSelectedProxy),
					ProxyData::Settings::Enabled);
			} else {
				_settings.setSelected(base::take(_lastSelectedProxy));
			}
		}
	}
	saveDelayed();
	updateView(*item);
}

object_ptr<Ui::BoxContent> ProxiesBoxController::editItemBox(int id) {
	return Box<ProxyBox>(findById(id)->data, [=](const ProxyData &result) {
		auto i = findById(id);
		auto j = ranges::find(
			_list,
			result,
			[](const Item &item) { return item.data; });
		if (j != end(_list) && j != i) {
			replaceItemWith(i, j);
		} else {
			replaceItemValue(i, result);
		}
	}, [=](const ProxyData &proxy) {
		share(proxy);
	});
}

void ProxiesBoxController::replaceItemWith(
		std::vector<Item>::iterator which,
		std::vector<Item>::iterator with) {
	// Read before the erase below: _list is a vector, so erasing `which`
	// shifts everything after it and leaves `with` naming the wrong item.
	const auto withId = with->id;
	const auto withDeleted = with->deleted;

	if (which->deleted) {
		// A deleted item is not in the settings list at all, so it has to
		// go back in before it can be removed - the same restore-first
		// order replaceItemValue() uses right below.
		restoreItem(which->id);
	}
	const auto removed = _settings.removeFromList(which->data);
	Assert(removed);

	_views.fire({ which->id });
	_list.erase(which);

	if (withDeleted) {
		restoreItem(withId);
	}
	applyItem(withId);
	saveDelayed();
}

void ProxiesBoxController::replaceItemValue(
		std::vector<Item>::iterator which,
		const ProxyData &proxy) {
	if (which->deleted) {
		restoreItem(which->id);
	}

	const auto replaced = _settings.replaceInList(which->data, proxy);
	Assert(replaced);
	which->data = proxy;
	refreshChecker(*which);

	applyItem(which->id);
	saveDelayed();
}

object_ptr<Ui::BoxContent> ProxiesBoxController::addNewItemBox() {
	const auto fromClipboard = ProxyDataFromClipboard();
	return Box<ProxyBox>(fromClipboard, [=](const ProxyData &result) {
		auto j = ranges::find(
			_list,
			result,
			[](const Item &item) { return item.data; });
		if (j != end(_list)) {
			if (j->deleted) {
				restoreItem(j->id);
			}
			applyItem(j->id);
		} else {
			addNewItem(result);
		}
	}, [=](const ProxyData &proxy) {
		share(proxy);
	});
}

bool ProxiesBoxController::contains(const ProxyData &proxy) const {
	const auto j = ranges::find(
		_list,
		proxy,
		[](const Item &item) { return item.data; });
	return (j != end(_list));
}

void ProxiesBoxController::addNewItem(const ProxyData &proxy) {
	_settings.addToList(proxy);

	_list.push_back({ ++_idCounter, proxy });
	refreshChecker(_list.back());
	applyItem(_list.back().id);
}

bool ProxiesBoxController::setProxySettings(ProxyData::Settings value) {
	if (_settings.settings() == value) {
		return true;
	} else if (value == ProxyData::Settings::Enabled) {
		if (_settings.list().empty()) {
			return false;
		} else if (!_settings.selected()) {
			_settings.setSelected(_settings.list().back());
			auto j = findByProxy(_settings.selected());
			if (j != end(_list)) {
				updateView(*j);
			}
		}
	}
	Core::App().setCurrentProxy(_settings.selected(), value);
	saveDelayed();
	return true;
}

void ProxiesBoxController::setProxyForCalls(bool enabled) {
	if (_settings.useProxyForCalls() == enabled) {
		return;
	}
	_settings.setUseProxyForCalls(enabled);
	if (_settings.isEnabled() && _settings.selected().supportsCalls()) {
		_settings.connectionTypeChangesNotify();
	}
	saveDelayed();
}

void ProxiesBoxController::setProxyRotationEnabled(bool enabled) {
	if (_settings.proxyRotationEnabled() == enabled) {
		return;
	}
	_settings.setProxyRotationEnabled(enabled);
	saveDelayed();
}

void ProxiesBoxController::setProxyRotationTimeout(int value) {
	if (_settings.proxyRotationTimeout() == value) {
		return;
	}
	_settings.setProxyRotationTimeout(value);
	saveDelayed();
}

void ProxiesBoxController::setTryIPv6(bool enabled) {
	if (Core::App().settings().proxy().tryIPv6() == enabled) {
		return;
	}
	Core::App().settings().proxy().setTryIPv6(enabled);
	_account->mtp().restart();
	_settings.connectionTypeChangesNotify();
	saveDelayed();
}

void ProxiesBoxController::saveDelayed() {
	Core::App().proxyRotationSettingsChanged();
	_saveTimer.callOnce(kSaveSettingsDelayedTimeout);
}

auto ProxiesBoxController::views() const -> rpl::producer<ItemView> {
	return _views.events();
}

rpl::producer<bool> ProxiesBoxController::listShareableChanges() const {
	return _views.events_starting_with(ItemView()) | rpl::map([=] {
		for (const auto &item : _list) {
			if (!item.deleted && ProxyDataIsShareable(item.data)) {
				return true;
			}
		}
		return false;
	});
}

void ProxiesBoxController::updateView(const Item &item) {
	const auto selected = (_settings.selected() == item.data);
	const auto deleted = item.deleted;
	const auto type = [&] {
		switch (item.data.type) {
		case Type::Http: return u"HTTP"_q;
		case Type::Socks5: return u"SOCKS5"_q;
		case Type::Mtproto: return u"MTProto"_q;
		case Type::Web: return u"Web"_q;
		}
		Unexpected("Proxy type in ProxiesBoxController::updateView.");
	}();
	const auto state = [&] {
		if (!selected || !_settings.isEnabled()) {
			return item.state;
		} else if (item.data.type == Type::Web) {
			switch (MTP::WebProxy::Transport::CurrentState(item.data)) {
			case MTP::WebProxy::Transport::State::WaitingForBrowser:
				return ItemState::WaitingForBrowser;
			case MTP::WebProxy::Transport::State::Failed:
				return ItemState::Unavailable;
			case MTP::WebProxy::Transport::State::Connected:
				return ItemState::Online;
			case MTP::WebProxy::Transport::State::Idle:
				return ItemState::NotTested;
			case MTP::WebProxy::Transport::State::Connecting:
				return ItemState::Connecting;
			}
			Unexpected("Web proxy transport state.");
		} else if (_account->mtp().dcstate() == MTP::ConnectedState) {
			return ItemState::Online;
		}
		return ItemState::Connecting;
	}();
	const auto supportsShare = ProxyDataIsShareable(item.data);
	const auto supportsCalls = item.data.supportsCalls();
	_views.fire({
		item.id,
		type,
		((item.data.type == Type::Web)
			? item.data.webAddress()
			: item.data.host),
		item.data.port,
		item.ping,
		!deleted && selected && _settings.isEnabled(),
		deleted,
		!deleted && supportsShare,
		supportsCalls,
		item.data.type == Type::Web,
		!deleted
			&& selected
			&& _settings.isEnabled()
			&& item.data.type == Type::Web
			&& (state == ItemState::WaitingForBrowser
				|| state == ItemState::Unavailable),
		state,
	});
}

void ProxiesBoxController::share(const ProxyData &proxy, bool qr) {
	ShareProxy(_show, _account, proxy, qr);
}

void ProxiesBoxController::Show(
		not_null<Window::SessionController*> controller,
		const QString &highlightId) {
	controller->show(
		CreateOwningBox(&controller->session().account(), highlightId));
}

ProxiesBoxController::~ProxiesBoxController() {
	if (_saveTimer.isActive()) {
		base::call_delayed(
			kSaveSettingsDelayedTimeout,
			QCoreApplication::instance(),
			[] { Local::writeSettings(); });
	}
}
