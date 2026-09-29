#pragma once

namespace Main {
class Session;
} // namespace Main

namespace ExtrasDebug {

[[nodiscard]] bool isFakeSession(not_null<Main::Session*> session);

// 造一个只在内存里的 self 用户，绕过登录直接进主界面。成功返回空字符串，
// 前置条件不满足时返回原因。假会话不写入 tdata，重启即消失。
[[nodiscard]] QString CreateFakeSession(int64 userId = 999999999);

// 在测试数据中心与生产环境之间切换，等价于登录界面输入 testmode。
// 成功返回空字符串，前置条件不满足时返回原因。
[[nodiscard]] QString SwitchTestEnvironment();

} // namespace ExtrasDebug
