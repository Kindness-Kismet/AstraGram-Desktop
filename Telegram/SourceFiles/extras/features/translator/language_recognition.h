#pragma once

#include "spellcheck/spellcheck_types.h"

namespace Extras::Language {

// 结果与 Platform::Language::Recognize 一致；Windows 复用识别服务，避免每条消息重新枚举。
[[nodiscard]] LanguageId Recognize(QStringView text);

} // namespace Extras::Language
