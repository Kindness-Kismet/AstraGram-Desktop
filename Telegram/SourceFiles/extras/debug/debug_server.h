#pragma once

namespace ExtrasDebug {

// 调试指令服务端，只在 Debug 构建里编译，Release 完全不存在。
//
// 协议是单行文本：请求 `<domain>.<verb> [args...]\n`，回复 `OK[ payload]\n`
// 或 `ERR <reason>\n`。payload 为紧凑 JSON 时不含换行，因此一行一应答。
//
// 只监听回环地址，且指令在主线程同步执行，所以处理函数可以直接访问设置和
// 界面对象，不需要额外加锁。
void StartServer();
void StopServer();

// 监听端口，未启动时返回 0。
[[nodiscard]] int ServerPort();

} // namespace ExtrasDebug
