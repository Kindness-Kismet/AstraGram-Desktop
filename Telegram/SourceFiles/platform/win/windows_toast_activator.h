/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/platform/win/base_windows_rpcndr_h.h"
#include "windows_toastactivator_h.h"

#include "base/platform/win/wrl/wrl_implements_h.h"

#if defined _DEBUG && !defined OS_WIN_STORE
class DECLSPEC_UUID("48C93F92-B51B-443E-B1A5-7D3BCF122A30") ToastActivator
#else
class DECLSPEC_UUID("F11932D3-6110-4BBC-9B02-B2EC07A1BD19") ToastActivator
#endif
	: public ::Microsoft::WRL::RuntimeClass<
		::Microsoft::WRL::RuntimeClassFlags<::Microsoft::WRL::ClassicCom>,
		INotificationActivationCallback,
		::Microsoft::WRL::FtmBase> {
public:
	ToastActivator() = default;
	~ToastActivator() = default;

	HRESULT STDMETHODCALLTYPE Activate(
		_In_ LPCWSTR appUserModelId,
		_In_ LPCWSTR invokedArgs,
		_In_reads_(dataCount) const NOTIFICATION_USER_INPUT_DATA *data,
		ULONG dataCount) override;

	HRESULT STDMETHODCALLTYPE QueryInterface(
		REFIID riid,
		void **ppObj);
	ULONG STDMETHODCALLTYPE AddRef();
	ULONG STDMETHODCALLTYPE Release();

private:
	long _ref = 1;

};

struct ToastActivation {
	struct UserInput {
		QString key;
		QString value;
	};
	QString args;
	std::vector<UserInput> input;

	[[nodiscard]] static QString String(LPCWSTR value);
};
[[nodiscard]] bool RegisterToastActivator();
bool UnregisterToastActivator();
void QueueToastActivation(ToastActivation activation);
void ClearToastActivations();
void ProcessToastActivations(Fn<bool(const ToastActivation&)> process);
[[nodiscard]] rpl::producer<> ToastActivations();
[[nodiscard]] QByteArray SerializeToastActivation(const ToastActivation &activation);
[[nodiscard]] std::optional<ToastActivation> ParseToastActivation(const QByteArray &data);
