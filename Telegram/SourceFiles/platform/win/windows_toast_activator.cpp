/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/windows_toast_activator.h"
#include "platform/win/windows_app_user_model_id.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#pragma warning(push)
// class has virtual functions, but destructor is not virtual
#pragma warning(disable:4265)
#pragma warning(disable:5104)
#include <wrl/module.h>
#pragma warning(pop)

namespace {

rpl::event_stream<> GlobalToastActivations;
std::vector<ToastActivation> PendingToastActivations;
bool ToastActivatorRegistered = false;

} // namespace

QString ToastActivation::String(LPCWSTR value) {
	const auto length = value ? int(wcsnlen(value, 16384)) : 0;
	auto result = value
		? QString::fromWCharArray(value, length)
		: QString();
	if (result.indexOf(QChar('\n')) < 0) {
		result.replace(QChar('\r'), QChar('\n'));
	}
	return result;
}

HRESULT ToastActivator::Activate(
		_In_ LPCWSTR appUserModelId,
		_In_ LPCWSTR invokedArgs,
		_In_reads_(dataCount) const NOTIFICATION_USER_INPUT_DATA *data,
		ULONG dataCount) {
	if (!appUserModelId
		|| Platform::AppUserModelId::Id() != appUserModelId) {
		return E_INVALIDARG;
	}
	const auto string = &ToastActivation::String;
	auto input = std::vector<ToastActivation::UserInput>();
	input.reserve(dataCount);
	for (auto i = 0; i != dataCount; ++i) {
		input.push_back({
			.key = string(data[i].Key),
			.value = string(data[i].Value),
		});
	}
	auto activation = ToastActivation{
		.args = string(invokedArgs),
		.input = std::move(input),
	};
	crl::on_main([activation = std::move(activation)]() mutable {
		QueueToastActivation(std::move(activation));
	});
	return S_OK;
}

HRESULT ToastActivator::QueryInterface(
		REFIID riid,
		void **ppObj) {
	if (riid == IID_IUnknown
		|| riid == IID_INotificationActivationCallback) {
		*ppObj = static_cast<INotificationActivationCallback*>(this);
		AddRef();
		return S_OK;
	}

	*ppObj = NULL;
	return E_NOINTERFACE;
}

ULONG ToastActivator::AddRef() {
	return InterlockedIncrement(&_ref);
}

ULONG ToastActivator::Release() {
	long ref = 0;
	ref = InterlockedDecrement(&_ref);
	if (!ref) {
		delete this;
	}
	return ref;
}

bool RegisterToastActivator() {
	if (ToastActivatorRegistered) {
		return true;
	}
	const auto result = Microsoft::WRL::Module<Microsoft::WRL::OutOfProc>
		::GetModule().RegisterObjects();
	if (FAILED(result)) {
		LOG(("Notifications Error: COM registration failed (%1).")
			.arg(uint32(result), 0, 16));
		return false;
	}
	ToastActivatorRegistered = true;
	return true;
}

bool UnregisterToastActivator() {
	if (!ToastActivatorRegistered) {
		return true;
	}
	const auto result = Microsoft::WRL::Module<Microsoft::WRL::OutOfProc>
		::GetModule().UnregisterObjects();
	if (FAILED(result)) {
		LOG(("Notifications Error: COM revocation failed (%1).")
			.arg(uint32(result), 0, 16));
		return false;
	}
	ToastActivatorRegistered = false;
	return true;
}

void QueueToastActivation(ToastActivation activation) {
	PendingToastActivations.push_back(std::move(activation));
	GlobalToastActivations.fire({});
}

void ClearToastActivations() {
	PendingToastActivations.clear();
}

void ProcessToastActivations(Fn<bool(const ToastActivation&)> process) {
	for (auto &activation : base::take(PendingToastActivations)) {
		if (!process(activation)) {
			PendingToastActivations.push_back(std::move(activation));
		}
	}
}

rpl::producer<> ToastActivations() {
	return GlobalToastActivations.events();
}

QByteArray SerializeToastActivation(const ToastActivation &activation) {
	auto input = QJsonObject();
	for (const auto &entry : activation.input) {
		input.insert(entry.key, entry.value);
	}
	return QJsonDocument(QJsonObject{
		{ u"args"_q, activation.args },
		{ u"input"_q, input },
	}).toJson(QJsonDocument::Compact).toBase64();
}

std::optional<ToastActivation> ParseToastActivation(const QByteArray &data) {
	const auto document = QJsonDocument::fromJson(QByteArray::fromBase64(data));
	const auto object = document.object();
	const auto args = object.value(u"args"_q);
	const auto input = object.value(u"input"_q);
	if (!args.isString() || !input.isObject()) {
		return std::nullopt;
	}
	auto result = ToastActivation{ .args = args.toString() };
	const auto entries = input.toObject();
	for (auto i = entries.begin(); i != entries.end(); ++i) {
		if (!i.value().isString()) {
			return std::nullopt;
		}
		result.input.push_back({ i.key(), i.value().toString() });
	}
	return result;
}

CoCreatableClass(ToastActivator);
