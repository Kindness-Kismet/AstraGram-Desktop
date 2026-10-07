#include "extras/features/translator/html_parser.h"

namespace Extras::Translator::Html {

TextWithEntities htmlToEntities(const QString &text) {
	TextWithEntities result = {.text = text};

	// 从纯文本恢复可识别的链接，原文中的自定义链接和格式无法还原。
	TextUtilities::ApplyServerCleaning(result);
	TextUtilities::ParseEntities(result, TextParseLinks | TextParseMentions | TextParseHashtags | TextParseBotCommands);
	TextUtilities::Trim(result);

	return result;
}

}
