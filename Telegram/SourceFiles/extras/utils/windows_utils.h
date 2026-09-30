#pragma once

void reloadAppIconFromTaskBar();

// 系统隐私设置任一层级拒绝时返回 true；capability 取 microphone 或 webcam。
[[nodiscard]] bool isCapabilityDenied(const QString &capability);
