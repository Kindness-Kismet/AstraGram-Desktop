#include "extras/features/translator/html_parser.h"

namespace Extras::Translator::Html {

// yandex messes up HTML badly, so formatting removed for now

QString entitiesToHtml(const TextWithEntities &text) {
	return text.text;
}

TextWithEntities htmlToEntities(const QString &text) {
	TextWithEntities result = {.text = text};

	// links parsing doesn't work actually as it's not even accounted in ParseEntities
	// todo: find a way to parse links
	TextUtilities::ApplyServerCleaning(result);
	TextUtilities::ParseEntities(result, TextParseLinks | TextParseMentions | TextParseHashtags | TextParseBotCommands);
	TextUtilities::Trim(result);

	return result;
}

}