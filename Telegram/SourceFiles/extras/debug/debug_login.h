#pragma once

namespace Main {
class Session;
} // namespace Main

namespace ExtrasDebug {

[[nodiscard]] bool isSimulationSession(not_null<Main::Session*> session);

// 造一个只在内存里的 self 用户，绕过登录直接进主界面。成功返回空字符串，
// 前置条件不满足时返回原因。模拟模式不写入 tdata，重启即消失。
[[nodiscard]] QString enterSimulation(int64 userId = 999999999);

} // namespace ExtrasDebug
