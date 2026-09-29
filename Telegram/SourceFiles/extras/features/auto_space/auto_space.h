#pragma once

struct TextWithEntities;

namespace Extras::AutoSpace {

// 在 CJK 与半角字符之间自动插入空格，并同步修正 entities 偏移。
// 仅处理消息文本，斜杠命令跳过。
void processText(TextWithEntities &text);

} // namespace Extras::AutoSpace
