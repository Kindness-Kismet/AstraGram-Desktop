#pragma once

namespace ExtrasDebug {

#ifdef _DEBUG
// 装 Windows 未处理异常过滤器，把崩溃调用栈写到工作目录 crash.log。
void InstallCrashHandler();
#else
// Release 下无操作，调用点不需要条件编译。
inline void InstallCrashHandler() {}
#endif

} // namespace ExtrasDebug
