#include "crash_dump.h"

#ifdef _DEBUG

#include <windows.h>
#include <dbghelp.h>

#include <cstdio>
#include <cstring>

#pragma comment(lib, "dbghelp.lib")

namespace {

SRWLOCK LogLock = SRWLOCK_INIT;

void WriteLine(const char *text) {
	// 崩溃现场绕过应用日志系统，直接落盘
	AcquireSRWLockExclusive(&LogLock);
	if (auto file = _fsopen("crash.log", "a", _SH_DENYNO)) {
		fprintf(file, "%s\r\n", text);
		fclose(file);
	}
	ReleaseSRWLockExclusive(&LogLock);
}

void DumpException(EXCEPTION_POINTERS *info) {
	char line[1024];

	const auto process = GetCurrentProcess();
	SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);

	// PDB 与 exe 同目录，只有带 --pdb 构建时存在
	char exePath[MAX_PATH] = {};
	GetModuleFileNameA(nullptr, exePath, MAX_PATH);
	if (auto slash = strrchr(exePath, '\\')) {
		*slash = '\0';
	}
	SymInitialize(process, exePath, TRUE);

	snprintf(line, sizeof(line), "=== exception 0x%08lX at 0x%p ===",
		info->ExceptionRecord->ExceptionCode,
		info->ExceptionRecord->ExceptionAddress);
	WriteLine(line);

	auto context = info->ContextRecord;
	auto frame = STACKFRAME64{};
	frame.AddrPC.Offset = context->Rip;
	frame.AddrPC.Mode = AddrModeFlat;
	frame.AddrFrame.Offset = context->Rbp;
	frame.AddrFrame.Mode = AddrModeFlat;
	frame.AddrStack.Offset = context->Rsp;
	frame.AddrStack.Mode = AddrModeFlat;

	for (auto i = 0; i < 64; ++i) {
		if (!StackWalk64(
				IMAGE_FILE_MACHINE_AMD64,
				process,
				GetCurrentThread(),
				&frame,
				context,
				nullptr,
				SymFunctionTableAccess64,
				SymGetModuleBase64,
				nullptr)) {
			break;
		}
		const auto pc = frame.AddrPC.Offset;
		if (!pc) {
			break;
		}

		const auto moduleBase = SymGetModuleBase64(process, pc);
		char moduleName[MAX_PATH] = "?";
		GetModuleFileNameA(
			reinterpret_cast<HMODULE>(moduleBase),
			moduleName,
			MAX_PATH);
		if (auto slash = strrchr(moduleName, '\\')) {
			memmove(moduleName, slash + 1, strlen(slash + 1) + 1);
		}

		char symbolBuffer[sizeof(SYMBOL_INFO) + 256] = {};
		const auto symbol = reinterpret_cast<SYMBOL_INFO *>(symbolBuffer);
		symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
		symbol->MaxNameLen = 255;
		auto displacement = DWORD64{};
		if (!SymFromAddr(process, pc, &displacement, symbol)) {
			// 没有符号时输出模块内偏移，对照反汇编定位
			snprintf(
				line, sizeof(line),
				"#%02d %s+0x%llx",
				i,
				moduleName,
				static_cast<unsigned long long>(pc - moduleBase));
			WriteLine(line);
			continue;
		}

		auto lineInfo = IMAGEHLP_LINE64{};
		lineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
		auto lineDisplacement = DWORD{};
		const auto hasLine = SymGetLineFromAddr64(
			process,
			pc,
			&lineDisplacement,
			&lineInfo);
		const auto filePart = hasLine
			? strrchr(lineInfo.FileName, '\\')
			: nullptr;

		snprintf(
			line, sizeof(line),
			"#%02d %s!%s+0x%llx [%s:%lu]",
			i,
			moduleName,
			symbol->Name,
			static_cast<unsigned long long>(displacement),
			filePart ? filePart + 1 : (hasLine ? lineInfo.FileName : "?"),
			lineInfo.LineNumber);
		WriteLine(line);
	}

	SymCleanup(process);
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS *info) {
	DumpException(info);
	// 继续默认流程（WER / 结束进程）
	return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

namespace ExtrasDebug {

void InstallCrashHandler() {
	SetUnhandledExceptionFilter(CrashFilter);
}

} // namespace ExtrasDebug

#endif // _DEBUG
