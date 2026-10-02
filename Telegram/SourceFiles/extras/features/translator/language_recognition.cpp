#include "extras/features/translator/language_recognition.h"

#include "spellcheck/platform/platform_language.h"

#ifdef Q_OS_WIN
#include "base/platform/win/base_windows_safe_library.h"

#include <Windows.h>
#include <ElsCore.h>
#include <ElsSrvc.h> // ELS_GUID_LANGUAGE_DETECTION.
#endif // Q_OS_WIN

namespace Extras::Language {
#ifdef Q_OS_WIN
namespace {

HRESULT (__stdcall *MappingGetServices)(
	_In_opt_ PMAPPING_ENUM_OPTIONS pOptions,
	_Out_ PMAPPING_SERVICE_INFO *prgServices,
	_Out_ DWORD *pdwServicesCount);

HRESULT (__stdcall *MappingRecognizeText)(
	_In_ PMAPPING_SERVICE_INFO pServiceInfo,
	_In_reads_(dwLength) LPCWSTR pszText,
	_In_ DWORD dwLength,
	_In_ DWORD dwIndex,
	_In_opt_ PMAPPING_OPTIONS pOptions,
	_Inout_ PMAPPING_PROPERTY_BAG pbag);

HRESULT (__stdcall *MappingFreePropertyBag)(
	_In_ PMAPPING_PROPERTY_BAG pBag);

// 枚举服务约占单次识别耗时的 99%，只在首次使用时枚举，常驻到进程退出。
[[nodiscard]] MAPPING_SERVICE_INFO *DetectionService() {
	static const auto result = []() -> MAPPING_SERVICE_INFO* {
#define LOAD_SYMBOL(lib, name) base::Platform::LoadMethod(lib, #name, name)
		const auto els = base::Platform::SafeLoadLibrary(L"elscore.dll");
		if (!LOAD_SYMBOL(els, MappingGetServices)
			|| !LOAD_SYMBOL(els, MappingRecognizeText)
			|| !LOAD_SYMBOL(els, MappingFreePropertyBag)) {
			return nullptr;
		}
#undef LOAD_SYMBOL
		auto service = ELS_GUID_LANGUAGE_DETECTION;
		auto options = MAPPING_ENUM_OPTIONS{};
		options.Size = sizeof(options);
		options.pGuid = &service;
		auto services = PMAPPING_SERVICE_INFO();
		auto count = DWORD(0);
		const auto enumerated = MappingGetServices(&options, &services, &count);
		return (SUCCEEDED(enumerated) && count) ? services : nullptr;
	}();
	return result;
}

} // namespace
#endif // Q_OS_WIN

LanguageId Recognize(QStringView text) {
#ifdef Q_OS_WIN
	const auto service = DetectionService();
	if (!service || text.isEmpty()) {
		return {};
	}
	auto bag = MAPPING_PROPERTY_BAG{};
	bag.Size = sizeof(bag);
	const auto recognized = MappingRecognizeText(
		service,
		reinterpret_cast<LPCWSTR>(text.utf16()),
		DWORD(text.size()),
		0,
		nullptr,
		&bag);
	if (recognized != S_OK) {
		return {};
	}
	auto result = LanguageId();
	if (bag.dwRangesCount
		&& bag.prgResultRanges
		&& bag.prgResultRanges[0].pData) {
		// 结果是以空串结尾的多串列表，取第一个，截掉 "sr-Cyrl" 这类后缀。
		auto pos = reinterpret_cast<LPCWSTR>(bag.prgResultRanges[0].pData);
		for (; *pos; pos += wcslen(pos) + 1) {
			if (wcslen(pos) >= 2) {
				result = { QLocale(QString::fromWCharArray(pos, 2)).language() };
				break;
			}
		}
	}
	MappingFreePropertyBag(&bag);
	return result;
#else // Q_OS_WIN
	return Platform::Language::Recognize(text);
#endif // Q_OS_WIN
}

} // namespace Extras::Language
