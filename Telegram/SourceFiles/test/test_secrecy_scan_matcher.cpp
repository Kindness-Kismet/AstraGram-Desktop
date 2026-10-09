#include "test/test_secrecy_scan_internal.h"

#ifdef _DEBUG

namespace Test {

namespace SecrecyScanDetails {}

using namespace SecrecyScanDetails;

namespace SecrecyScanDetails {

[[nodiscard]] QString B(bool value) {
	return value ? u"1"_q : u"0"_q;
}

[[nodiscard]] QString NormalizedWord(const QString &word) {
	return word.trimmed().toLower();
}

// DumpToText writes a string as "\"...\" [STRING]" with backslash, quote
// and newline escaped (mtproto_dump_to_text.cpp), so inside a dump the
// secret can only appear in this form.
[[nodiscard]] QString DumpEscaped(QString text) {
	return text
		.replace(QChar('\\'), u"\\\\"_q)
		.replace(QChar('"'), u"\\\""_q)
		.replace(QChar('\n'), u"\\n"_q);
}

[[nodiscard]] bool IsHexDigit(QChar ch) {
	const auto code = ch.unicode();
	return (code >= '0' && code <= '9')
		|| (code >= 'a' && code <= 'f')
		|| (code >= 'A' && code <= 'F');
}

// DumpToText's type tags: " [STRING]", " [12 BYTES]", " [LONG]" and the
// like (mtproto_dump_to_text.cpp). Answers the offset of the closing
// bracket of a tag opening at |open|, or -1.
[[nodiscard]] int TypeTagEnd(const QString &text, int open) {
	static const auto tags = QSet<QString>{
		u"STRING"_q,
		u"BYTES"_q,
		u"INT"_q,
		u"LONG"_q,
		u"DOUBLE"_q,
		u"INT128"_q,
		u"INT256"_q,
		u"GZIPPED"_q,
	};
	const auto size = int(text.size());
	auto close = open + 1;
	while (close < size
		&& (close - open) <= kMaxTypeTag
		&& text[close] != QChar(']')) {
		++close;
	}
	if (close >= size || text[close] != QChar(']')) {
		return -1;
	}
	auto from = open + 1;
	while (from < close && text[from].isDigit()) {
		++from;
	}
	if (from > open + 1) {
		if (from >= close || text[from] != QChar(' ')) {
			return -1;
		}
		++from;
	}
	return tags.contains(text.mid(from, close - from)) ? close : -1;
}

// What joins two words without a space, or sits between them, is blanked
// to as many spaces, so every offset into the line stays valid: the
// DumpToText string escapes (a backslash before n, t, r, a backslash or a
// quote), the test log's \uXXXX line-break escapes (test_log.cpp OneLine)
// and the type tags between a vector's one-per-line elements.
[[nodiscard]] QString WordSeparated(const QString &text) {
	auto result = text;
	const auto size = int(text.size());
	const auto blank = [&](int from, int count) {
		for (auto i = from; i != from + count; ++i) {
			result[i] = QChar(' ');
		}
	};
	for (auto i = 0; i < size; ++i) {
		const auto ch = text[i];
		if (ch == QChar('\\') && (i + 1 < size)) {
			const auto next = text[i + 1].unicode();
			if (next == 'u'
				&& (i + 5 < size)
				&& IsHexDigit(text[i + 2])
				&& IsHexDigit(text[i + 3])
				&& IsHexDigit(text[i + 4])
				&& IsHexDigit(text[i + 5])) {
				blank(i, 6);
				i += 5;
			} else if (next == 'n'
				|| next == 't'
				|| next == 'r'
				|| next == '\\'
				|| next == '"') {
				blank(i, 2);
				++i;
			}
		} else if (ch == QChar('[')) {
			const auto close = TypeTagEnd(text, i);
			if (close > i) {
				blank(i, close - i + 1);
				i = close;
			}
		}
	}
	return result;
}

[[nodiscard]] bool AllDigits(const QString &text, int from, int till) {
	for (auto i = from; i != till; ++i) {
		if (!text[i].isDigit()) {
			return false;
		}
	}
	return true;
}

// A log entry starts with "[hh:mm:ss.zzz tid-index] " (logs.cpp
// _logsEntryStart).
[[nodiscard]] bool IsEntryHeader(const QString &line) {
	if (line.size() < 15 || line[0] != QChar('[') || line[13] != QChar(' ')) {
		return false;
	}
	for (const auto index : { 1, 2, 4, 5, 7, 8, 10, 11, 12 }) {
		if (!line[index].isDigit()) {
			return false;
		}
	}
	return (line[3] == QChar(':'))
		&& (line[6] == QChar(':'))
		&& (line[9] == QChar('.'));
}

// An mtp entry is "<entry start> (dc:<dc>) <text>" (logs.cpp writeMtp); a
// dump entry's text starts with exactly "Send: " or "Recv: "
// (session_private.cpp). |dumpFrom| is where the dump text itself starts.
[[nodiscard]] DumpDirection HeaderDirection(
		const QString &line,
		int *dumpFrom) {
	const auto dc = int(line.indexOf(u" (dc:"_q));
	if (dc < 0) {
		return DumpDirection::None;
	}
	const auto close = int(line.indexOf(u") "_q, dc));
	if (close < 0) {
		return DumpDirection::None;
	}
	const auto text = line.mid(close + 2, 6);
	const auto result = (text == u"Send: "_q)
		? DumpDirection::Send
		: (text == u"Recv: "_q)
		? DumpDirection::Recv
		: DumpDirection::None;
	if (result != DumpDirection::None) {
		*dumpFrom = close + 8;
	}
	return result;
}

[[nodiscard]] bool IdentifierChar(QChar ch) {
	return ch.isLetterOrNumber() || (ch == QChar('_'));
}

// A public schema identifier as DumpToText prints it ("{ name", "name: ");
// anything else is never printed.
[[nodiscard]] QString SafeIdentifier(const QString &name) {
	if (name.isEmpty() || name.size() > 64 || !name[0].isLetter()) {
		return u"?"_q;
	}
	for (const auto ch : name) {
		if ((ch.unicode() > 0x7F) || !IdentifierChar(ch)) {
			return u"?"_q;
		}
	}
	return name;
}

DumpWalk WalkDump(
		const QString &text,
		int from,
		int till,
		std::vector<QString> &stack,
		bool wantField) {
	auto result = DumpWalk();
	for (auto i = from; i < till; ++i) {
		const auto ch = text[i];
		if (result.quoted) {
			if (ch == QChar('\\')) {
				++i;
			} else if (ch == QChar('"')) {
				result.quoted = false;
			}
			continue;
		}
		if (ch == QChar('"')) {
			result.quoted = true;
		} else if (ch == QChar('{')) {
			auto start = i + 1;
			if (start < till && text[start] == QChar(' ')) {
				++start;
			}
			auto end = start;
			while (end < till && IdentifierChar(text[end])) {
				++end;
			}
			stack.push_back(text.mid(start, end - start));
			result.field.clear();
			i = end - 1;
		} else if (ch == QChar('}')) {
			if (!stack.empty()) {
				stack.pop_back();
			}
			result.field.clear();
		} else if (wantField
			&& (ch == QChar(':'))
			&& (i + 1 < till)
			&& (text[i + 1] == QChar(' '))) {
			auto start = i;
			while (start > from && IdentifierChar(text[start - 1])) {
				--start;
			}
			result.field = text.mid(start, i - start);
		}
	}
	return result;
}

// Outside dumps: inside a double-quoted segment when an odd number of
// unescaped quotes precede the position.
[[nodiscard]] bool InsideQuotes(const QString &text, int at) {
	auto inside = false;
	for (auto i = 0; i < at; ++i) {
		if (inside && text[i] == QChar('\\')) {
			++i;
		} else if (text[i] == QChar('"')) {
			inside = !inside;
		}
	}
	return inside;
}

// "<top>/<inner>.<field>": the first object of the chain that is not
// transport framing, the innermost open object and the field the hit
// belongs to on its line ("-" when the line prints no "name: ").
[[nodiscard]] QString SiteText(
		const std::vector<QString> &chain,
		const QString &field) {
	static const auto framing = QSet<QString>{
		u"core_message"_q,
		u"msg_container"_q,
		u"rpc_result"_q,
		u"invokeAfterMsg"_q,
		u"invokeAfterMsgs"_q,
		u"initConnection"_q,
		u"invokeWithLayer"_q,
		u"invokeWithoutUpdates"_q,
		u"invokeWithMessagesRange"_q,
		u"invokeWithTakeout"_q,
	};
	auto top = QString();
	for (const auto &name : chain) {
		if (!framing.contains(name)) {
			top = name;
			break;
		}
	}
	if (top.isEmpty() && !chain.empty()) {
		top = chain.back();
	}
	return u"%1/%2.%3"_q
		.arg(top.isEmpty() ? u"?"_q : SafeIdentifier(top))
		.arg(chain.empty() ? u"?"_q : SafeIdentifier(chain.back()))
		.arg(field.isEmpty() ? u"-"_q : SafeIdentifier(field));
}

LineContext DumpTracker::begin(
		const QString &line,
		SecrecyClassReading &reading) {
	dumpFrom = 0;
	if (IsEntryHeader(line)) {
		stack.clear();
		direction = HeaderDirection(line, &dumpFrom);
		if (direction == DumpDirection::Send) {
			++reading.sendHeaders;
		} else if (direction == DumpDirection::Recv) {
			++reading.recvHeaders;
		}
	}
	return { direction, &stack, dumpFrom };
}

void DumpTracker::end(const QString &line) {
	if (direction != DumpDirection::None) {
		WalkDump(line, dumpFrom, int(line.size()), stack);
	}
}

[[nodiscard]] bool InDump(const LineContext &context, int at) {
	return (context.direction != DumpDirection::None)
		&& context.stack
		&& (at >= context.dumpFrom);
}

[[nodiscard]] QString SiteAt(
		const QString &text,
		const LineContext &context,
		int at,
		bool *quoted) {
	auto chain = *context.stack;
	const auto walk = WalkDump(text, context.dumpFrom, at, chain, true);
	if (quoted) {
		*quoted = walk.quoted;
	}
	return SiteText(chain, walk.field);
}

// The app log's "[yyyy.MM.dd hh:mm:ss] " (logs.cpp writeMain).
[[nodiscard]] bool IsMainLogStart(const QString &line) {
	if (line.size() < kMainLogStart
		|| line[0] != QChar('[')
		|| line[20] != QChar(']')
		|| line[21] != QChar(' ')) {
		return false;
	}
	for (const auto index : {
		1, 2, 3, 4, 6, 7, 9, 10, 12, 13, 15, 16, 18, 19,
	}) {
		if (!line[index].isDigit()) {
			return false;
		}
	}
	return (line[5] == QChar('.'))
		&& (line[8] == QChar('.'))
		&& (line[11] == QChar(' '))
		&& (line[14] == QChar(':'))
		&& (line[17] == QChar(':'));
}

// Where the message of a plain line starts, per writer (logs.cpp): after
// the app log's timestamp (writeMain), after a DebugLogs entry start
// (writeDebug) and, in an mtp_ part, also after its "(dc:<dc>) "
// (writeMtp). 0 for a test-log row, which has no prefix, and for a
// continuation line of a multi-line entry.
[[nodiscard]] int MessageFrom(const QString &line) {
	if (IsMainLogStart(line)) {
		return kMainLogStart;
	} else if (!IsEntryHeader(line)) {
		return 0;
	}
	const auto close = int(line.indexOf(u"] "_q, 14));
	if (close < 0) {
		return 0;
	}
	auto result = close + 2;
	if (line.mid(result, 4) == u"(dc:"_q) {
		const auto dc = int(line.indexOf(u") "_q, result));
		if (dc > 0) {
			result = dc + 2;
		}
	}
	return result;
}

[[nodiscard]] bool IsBlank(QChar ch) {
	return (ch == QChar(' ')) || (ch == QChar('\t'));
}

// One left-to-right pass over the message from |from|. Scanning resumes at
// the blank that ended a value, so a "name=" inside a value, quoted or not,
// is never a field.
[[nodiscard]] std::vector<PlainField> PlainFields(
		const QString &text,
		int from) {
	static const auto trailing = u";,.:"_q;
	auto result = std::vector<PlainField>();
	const auto size = int(text.size());
	auto i = from;
	while (i < size) {
		if (IsBlank(text[i]) || (i > from && !IsBlank(text[i - 1]))) {
			++i;
			continue;
		}
		auto end = i;
		while (end < size && IdentifierChar(text[end])) {
			++end;
		}
		const auto name = text.mid(i, end - i);
		if (end == i
			|| end >= size
			|| text[end] != QChar('=')
			|| SafeIdentifier(name) != name
			|| InsideQuotes(text, i)) {
			i = std::max(end, i + 1);
			continue;
		}
		auto till = end + 1;
		while (till < size && !IsBlank(text[till])) {
			++till;
		}
		auto valueFrom = end + 1;
		auto valueTill = till;
		while (valueTill > valueFrom && trailing.contains(text[valueTill - 1])) {
			--valueTill;
		}
		if (valueTill - valueFrom >= 2
			&& text[valueFrom] == QChar('"')
			&& text[valueTill - 1] == QChar('"')) {
			++valueFrom;
			--valueTill;
		}
		result.push_back({
			.name = name,
			.nameFrom = i,
			.valueFrom = valueFrom,
			.valueTill = valueTill,
		});
		i = till;
	}
	return result;
}

// A message head prints only in this identifier-like shape, so no quote,
// bracket, "=", "|" or escape can make it carry more than product text.
[[nodiscard]] bool HeadShaped(const QString &head) {
	static const auto extra = u" _.:-/"_q;
	if (head.isEmpty() || head.size() > kMaxHeadLength) {
		return false;
	}
	for (const auto ch : head) {
		if ((ch.unicode() > 0x7F)
			|| !(ch.isLetterOrNumber() || extra.contains(ch))) {
			return false;
		}
	}
	return true;
}

// The secrets a reading can decide on: phrases of at least two normalized
// words, non-empty short secrets and tokens of at least kMinTokenLength
// characters, each once. What was refused is counted into |reading|.
[[nodiscard]] SecrecySecrets UsableSecrets(
		const SecrecySecrets &secrets,
		SecrecyReading *reading) {
	auto result = SecrecySecrets();
	auto phrasesTooShort = 0;
	auto tokensTooShort = 0;
	for (const auto &phrase : secrets.phrases) {
		auto words = std::vector<QString>();
		for (const auto &word : phrase) {
			const auto normalized = NormalizedWord(word);
			if (!normalized.isEmpty()) {
				words.push_back(normalized);
			}
		}
		if (words.size() >= 2) {
			result.phrases.push_back(std::move(words));
		} else if (!words.empty()) {
			++phrasesTooShort;
		}
	}
	auto seen = QSet<QString>();
	for (const auto &secret : secrets.shortSecrets) {
		if (!secret.isEmpty() && !seen.contains(secret)) {
			seen.insert(secret);
			result.shortSecrets.push_back(secret);
		}
	}
	seen.clear();
	for (const auto &token : secrets.tokens) {
		if (token.isEmpty() || seen.contains(token)) {
			continue;
		}
		seen.insert(token);
		if (token.size() < kMinTokenLength) {
			++tokensTooShort;
		} else {
			result.tokens.push_back(token);
		}
	}
	if (reading) {
		reading->phrasesGiven = int(result.phrases.size());
		reading->shortGiven = int(result.shortSecrets.size());
		reading->tokensGiven = int(result.tokens.size());
		reading->phrasesTooShort = phrasesTooShort;
		reading->tokensTooShort = tokensTooShort;
	}
	return result;
}

SecrecyMatcher::SecrecyMatcher(
	const SecrecySecrets &usable,
	std::vector<SecrecyComputedField> computed)
: _shortSecrets(usable.shortSecrets)
, _tokens(usable.tokens)
, _computed(std::move(computed)) {
	for (const auto &words : usable.phrases) {
		auto previous = QString();
		for (const auto &word : words) {
			const auto normalized = NormalizedWord(word);
			_words.insert(normalized);
			_longest = std::max(_longest, int(normalized.size()));
			if (!previous.isEmpty()) {
				_pairs.insert(previous + QChar(' ') + normalized);
			}
			previous = normalized;
		}
	}
	for (const auto &secret : _shortSecrets) {
		const auto escaped = DumpEscaped(secret);
		_escaped.push_back((escaped != secret) ? escaped : QString());
	}
}

void SecrecyMatcher::line(
		const QString &text,
		const LineContext &context,
		QString &run,
		SecrecyClassReading &reading) const {
	if (!_pairs.isEmpty()) {
		countWordRuns(text, context, run, reading);
	}
	for (auto i = 0; i != int(_shortSecrets.size()); ++i) {
		countShort(text, _shortSecrets[i], context, false, reading);
		if (!_escaped[i].isEmpty()) {
			countShort(text, _escaped[i], context, true, reading);
		}
	}
	for (const auto &token : _tokens) {
		countToken(text, token, context, reading);
	}
}

// A word run is an ordered adjacent pair of one phrase's words,
// case-insensitive. Words are the letter-and-digit runs of the line once
// WordSeparated has blanked escapes and type tags, so whitespace, commas,
// quotes, brackets and line breaks all separate them, and the run carries
// across the lines of one file: a phrase one word per line is still a run.
// A number is neutral (a list index), any other word ends the run, and
// every entry header ends it (LineFeed). A token longer than every phrase
// word matches nothing, so it is not lowered.
void SecrecyMatcher::countWordRuns(
		const QString &raw,
		const LineContext &context,
		QString &run,
		SecrecyClassReading &reading) const {
	const auto text = WordSeparated(raw);
	const auto size = int(text.size());
	auto i = 0;
	while (i < size) {
		while (i < size && !text[i].isLetterOrNumber()) {
			++i;
		}
		const auto from = i;
		while (i < size && text[i].isLetterOrNumber()) {
			++i;
		}
		const auto length = i - from;
		if (!length) {
			break;
		} else if (AllDigits(text, from, i)) {
			continue;
		} else if (length <= _longest) {
			const auto normalized = text.mid(from, length).toLower();
			if (_words.contains(normalized)) {
				if (!run.isEmpty()
					&& _pairs.contains(run + QChar(' ') + normalized)) {
					if (!InDump(context, from)) {
						++reading.wordRunsOther;
					} else if (context.direction == DumpDirection::Send) {
						++reading.wordRunsSend;
					} else {
						++reading.wordRunsRecv;
					}
				}
				run = normalized;
				continue;
			}
		}
		run.clear();
	}
}

// A short secret's occurrence is bounded when the characters right before
// and right after it are no letter or digit (or the line's edges), and
// embedded otherwise. A bounded one sits in a line the client composed
// (Other), in a dump entry the client sent (Send) or in one it received
// (Recv); only Other and Send decide. |dumpOnly| is the escaped form, which
// only a dump can carry. A plain-line hit is reported at its
// "<file>|<head>|<field>" site (plainHit), and one that is exactly the
// value of a declared computed field is reported and does not decide.
void SecrecyMatcher::countShort(
		const QString &text,
		const QString &needle,
		const LineContext &context,
		bool dumpOnly,
		SecrecyClassReading &reading) const {
	const auto size = int(text.size());
	const auto length = int(needle.size());
	auto from = 0;
	while (true) {
		const auto at = int(text.indexOf(needle, from));
		if (at < 0) {
			break;
		}
		from = at + 1;
		const auto inDump = InDump(context, at);
		if (dumpOnly && !inDump) {
			continue;
		}
		const auto till = at + length;
		const auto before = (at == 0) || !text[at - 1].isLetterOrNumber();
		const auto after = (till >= size) || !text[till].isLetterOrNumber();
		if (!before || !after) {
			++reading.embedded;
			continue;
		}
		++reading.bounded;
		if (!inDump) {
			++(InsideQuotes(text, at)
				? reading.boundedQuoted
				: reading.boundedUnquoted);
			const auto hit = plainHit(text, context, at, till);
			if (hit.computed) {
				++reading.boundedComputed;
				++reading.computedSites[hit.site];
			} else {
				++reading.boundedOther;
				++reading.plainSites[hit.site];
			}
			continue;
		}
		auto quoted = false;
		const auto site = SiteAt(text, context, at, &quoted);
		++(quoted ? reading.boundedQuoted : reading.boundedUnquoted);
		if (context.direction == DumpDirection::Send) {
			++reading.boundedSend;
			++reading.sendSites[site];
		} else {
			++reading.boundedRecv;
			++reading.recvSites[site];
		}
	}
}

// Every substring occurrence of a token counts: a record id or a secret ref
// is long and random enough that a coincidence is not a concern.
void SecrecyMatcher::countToken(
		const QString &text,
		const QString &token,
		const LineContext &context,
		SecrecyClassReading &reading) const {
	auto from = 0;
	while (true) {
		const auto at = int(text.indexOf(token, from));
		if (at < 0) {
			break;
		}
		from = at + 1;
		if (!InDump(context, at)) {
			++reading.tokensOther;
			continue;
		}
		const auto site = SiteAt(text, context, at);
		if (context.direction == DumpDirection::Send) {
			++reading.tokensSend;
			++reading.sendSites[site];
		} else {
			++reading.tokensRecv;
			++reading.recvSites[site];
		}
	}
}

// The site of a plain-line hit over text[at, till): the file, the message
// head (the text from the message start to the first field's name) and the
// name of the field whose whole value the hit is, or "-". The whole site is
// withheld as "<file>|?|?", for every reason alike, when the line has no
// field, the hit lies in the head, the head is not HeadShaped, or the head
// (with the printed field name) holds any given secret. Only a hit whose
// printed head and field equal a declaration is computed, so a declaration
// never matches a withheld site.
PlainHit SecrecyMatcher::plainHit(
		const QString &text,
		const LineContext &context,
		int at,
		int till) const {
	const auto file = context.file ? *context.file : QString();
	const auto withheld = PlainHit{ .site = file + QChar('|') + kSiteWithheld };
	const auto from = MessageFrom(text);
	const auto fields = PlainFields(text, from);
	if (fields.empty()) {
		return withheld;
	}
	const auto headTill = fields.front().nameFrom;
	if (at < headTill && till > from) {
		return withheld;
	}
	const auto head = text.mid(from, headTill - from).trimmed();
	auto name = QString();
	for (const auto &field : fields) {
		if (field.valueFrom == at && field.valueTill == till) {
			name = field.name;
			break;
		}
	}
	const auto shown = name.isEmpty() ? head : (head + QChar(' ') + name);
	if (!HeadShaped(head) || holds(shown)) {
		return withheld;
	}
	const auto computed = !name.isEmpty()
		&& ranges::any_of(_computed, [&](const SecrecyComputedField &field) {
			return (field.head == head) && (field.field == name);
		});
	return {
		.site = file
			+ QChar('|')
			+ head
			+ QChar('|')
			+ (name.isEmpty() ? kSiteNoField : name),
		.computed = computed,
	};
}

// Whether printed text would carry secret material: any given short secret
// or token, case-insensitively (stricter than the case-sensitive match, so
// no case variant prints), or an adjacent pair of one phrase's words.
bool SecrecyMatcher::holds(const QString &text) const {
	const auto contains = [&](const std::vector<QString> &list) {
		return ranges::any_of(list, [&](const QString &secret) {
			return text.contains(secret, Qt::CaseInsensitive);
		});
	};
	if (contains(_shortSecrets) || contains(_tokens)) {
		return true;
	} else if (_pairs.isEmpty()) {
		return false;
	}
	auto run = QString();
	auto reading = SecrecyClassReading();
	countWordRuns(text, LineContext(), run, reading);
	return (reading.wordRunsOther > 0);
}

LineFeed::LineFeed(
	const SecrecyMatcher &matcher,
	const QString &planted,
	const QString &control,
	SecrecyClassReading &reading,
	const QString &file)
: _matcher(matcher)
, _planted(planted)
, _control(control)
, _file(file)
, _reading(reading) {
}

void LineFeed::feed(const QString &line) {
	++_reading.lines;
	if (IsEntryHeader(line)) {
		_run.clear();
	}
	auto context = _dump.begin(line, _reading);
	context.file = &_file;
	_matcher.line(line, context, _run, _reading);
	if (!_planted.isEmpty() && line.contains(_planted)) {
		++_reading.plantedHits;
	}
	if (!_control.isEmpty() && line.contains(_control)) {
		++_reading.controlHits;
	}
	_dump.end(line);
}

[[nodiscard]] QString ChopLineEnd(QString line) {
	while (!line.isEmpty()
		&& (line.back() == QChar('\n') || line.back() == QChar('\r'))) {
		line.chop(1);
	}
	return line;
}

void ScanText(
		const QString &text,
		const SecrecyMatcher &matcher,
		const QString &planted,
		const QString &control,
		SecrecyClassReading &reading,
		const QString &file) {
	auto feed = LineFeed(matcher, planted, control, reading, file);
	const auto size = int(text.size());
	auto from = 0;
	while (from < size) {
		auto till = int(text.indexOf(QChar('\n'), from));
		if (till < 0) {
			till = size;
		}
		feed.feed(ChopLineEnd(text.mid(from, till - from)));
		from = till + 1;
	}
}

// Raw bytes, no QIODevice::Text, streamed line by line: an mtp part can be
// large. With |slice| a first pass finds the last banner reopen() wrote
// when this launch appended to a same-day part (logs.cpp), and the second
// pass scans from it, so an earlier launch's lines are not charged here.
// |name| is the file's public name, printed in plain-line sites.
FileScan ScanFile(
		const QString &path,
		bool slice,
		const SecrecyMatcher &matcher,
		const QString &planted,
		const QString &control,
		SecrecyClassReading &reading,
		const QString &name) {
	auto result = FileScan();
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return result;
	}
	result.opened = true;
	if (slice) {
		auto index = 0;
		while (!file.atEnd()) {
			++index;
			const auto line = QString::fromUtf8(file.readLine());
			if (line.trimmed() == kBanner) {
				result.slicedFrom = index;
			}
		}
		file.seek(0);
	}
	auto feed = LineFeed(matcher, planted, control, reading, name);
	auto index = 0;
	while (!file.atEnd()) {
		++index;
		const auto bytes = file.readLine();
		if (index < result.slicedFrom) {
			continue;
		}
		feed.feed(ChopLineEnd(QString::fromUtf8(bytes)));
	}
	return result;
}

// A part is a given day's only when its first line is that day's index; a
// same-name file of another day is foreign and is not scanned.
[[nodiscard]] PartIdentity ReadPartIdentity(
		const QString &path,
		const QString &dayIndex) {
	if (!QFile::exists(path)) {
		return PartIdentity::Missing;
	}
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return PartIdentity::Unreadable;
	}
	const auto first = QString::fromUtf8(file.readLine()).trimmed();
	return (first == dayIndex)
		? PartIdentity::Accepted
		: PartIdentity::Foreign;
}

[[nodiscard]] bool ValidPartName(const QString &name) {
	static const auto re = QRegularExpression(
		u"^(log|mtp)_\\d\\d_\\d\\d\\.txt$"_q);
	return re.match(name).hasMatch();
}

[[nodiscard]] bool IsMtpName(const QString &name) {
	return name.mid(name.lastIndexOf(QChar('/')) + 1).startsWith(u"mtp_"_q);
}

[[nodiscard]] bool IsWindowClass(SecrecyClass cls) {
	return (cls == SecrecyClass::DebugLog) || (cls == SecrecyClass::MtpLog);
}

[[nodiscard]] QString CanaryDump(
		const QString &direction,
		const QString &secret) {
	return u"[00:00:00.000 00-0000000] (dc:2_main) "_q
		+ direction
		+ u": { core_message\n"
		"  body: { account_password\n"
		"    hint: \""_q
		+ DumpEscaped(secret)
		+ u"\" [STRING]\n"
		"  }\n"
		"} (dc:2,key:0,session:0)"_q;
}

// A Send entry dumping a vector of strings one element per line.
[[nodiscard]] QString CanaryVectorDump(
		const QString &first,
		const QString &second) {
	return u"[00:00:00.000 00-0000000] (dc:2_main) Send: { core_message\n"
		"  body: { wallet_canary\n"
		"    words: [ vector<0xb5286e24> (2)\n"
		"      \""_q
		+ DumpEscaped(first)
		+ u"\" [STRING],\n"
		"      \""_q
		+ DumpEscaped(second)
		+ u"\" [STRING],\n"
		"    ]\n"
		"  }\n"
		"} (dc:2,key:0,session:0)"_q;
}

// Negative-control canaries, built in memory and never written. Each runs
// through the same matcher class, LineFeed and dump walk as the files, but
// over a matcher of that one secret, so another secret it happens to
// contain (a ref contains its record id) cannot change its count.
void ReadCanaries(const SecrecySecrets &usable, SecrecyReading &result) {
	const auto site = u"account_password/account_password.hint"_q;
	for (const auto &words : usable.phrases) {
		const auto matcher = SecrecyMatcher({ .phrases = { words } });
		const auto last = int(words.size()) - 2;
		const auto pair = [&](int first, const QString &separator) {
			return words[first] + separator + words[first + 1];
		};
		struct WordCanary {
			QString text;
			bool send = false;
		};
		const auto canaries = std::vector<WordCanary>{
			{ pair(0, u" "_q) },
			{ pair(last, u" "_q) },
			{ u"[00:00:00.000 00-0000000] words:\n"_q + pair(0, u"\n"_q) },
			{ u"NOTE: words: "_q + pair(last, u"\\u000A"_q) },
			{ u"words=["_q + pair(0, u","_q) + u"]"_q },
			{ CanaryDump(u"Send"_q, pair(last, u"\n"_q)), true },
			{ CanaryVectorDump(words[0], words[1]), true },
		};
		for (const auto &canary : canaries) {
			auto reading = SecrecyClassReading();
			ScanText(canary.text, matcher, {}, {}, reading);
			++result.canaryWordRunsExpected;
			const auto other = canary.send ? 0 : 1;
			const auto send = canary.send ? 1 : 0;
			if (reading.wordRunsOther == other
				&& reading.wordRunsSend == send
				&& !reading.wordRunsRecv) {
				++result.canaryWordRuns;
			}
		}
	}
	for (const auto &secret : usable.shortSecrets) {
		const auto matcher = SecrecyMatcher({ .shortSecrets = { secret } });
		++result.canaryShortExpected;

		auto bounded = SecrecyClassReading();
		ScanText(u"canary="_q + secret, matcher, {}, {}, bounded);
		if (bounded.bounded == 1 && bounded.boundedOther == 1) {
			++result.canaryBounded;
		}

		auto wrap = QChar('x');
		for (const auto ch : { 'x', 'q', 'z', 'j', 'k' }) {
			if (!secret.contains(QChar(ch), Qt::CaseInsensitive)) {
				wrap = QChar(ch);
				break;
			}
		}
		auto embedded = SecrecyClassReading();
		ScanText(QString(wrap) + secret + wrap, matcher, {}, {}, embedded);
		if (embedded.embedded == 1 && embedded.bounded == 0) {
			++result.canaryEmbedded;
		}

		auto send = SecrecyClassReading();
		ScanText(CanaryDump(u"Send"_q, secret), matcher, {}, {}, send);
		if (send.boundedSend == 1
			&& send.boundedRecv == 0
			&& send.bounded == 1
			&& send.sendSites.size() == 1
			&& send.sendSites.contains(site)) {
			++result.canarySend;
		}

		auto recv = SecrecyClassReading();
		ScanText(CanaryDump(u"Recv"_q, secret), matcher, {}, {}, recv);
		if (recv.boundedRecv == 1
			&& recv.boundedSend == 0
			&& recv.bounded == 1
			&& recv.recvSites.size() == 1
			&& recv.recvSites.contains(site)) {
			++result.canaryRecv;
		}
	}
	for (const auto &token : usable.tokens) {
		const auto matcher = SecrecyMatcher({ .tokens = { token } });
		++result.canaryTokenExpected;
		auto canary = SecrecyClassReading();
		ScanText(u"ref="_q + token, matcher, {}, {}, canary);
		if (canary.tokensOther == 1
			&& !canary.tokensSend
			&& !canary.tokensRecv) {
			++result.canaryToken;
		}
	}
	const auto shortOk = [&](int passed) {
		return (passed == result.canaryShortExpected);
	};
	result.canariesHold
		= (result.canaryWordRuns == result.canaryWordRunsExpected)
		&& shortOk(result.canaryBounded)
		&& shortOk(result.canaryEmbedded)
		&& shortOk(result.canarySend)
		&& shortOk(result.canaryRecv)
		&& (result.canaryToken == result.canaryTokenExpected);
}

void ReadSource(
		const SecrecySource &source,
		const SecrecyMatcher &matcher,
		const QString &planted,
		SecrecyClassReading &reading) {
	if (source.invalidName) {
		++reading.invalidNames;
		return;
	}
	switch (source.identity) {
	case PartIdentity::Missing: ++reading.missing; return;
	case PartIdentity::Unreadable: ++reading.unreadable; return;
	case PartIdentity::Foreign: ++reading.foreign; return;
	case PartIdentity::Accepted: break;
	}
	if (!source.control.isEmpty()) {
		reading.namedControl = true;
	}
	const auto controlsBefore = reading.controlHits;
	auto slicedFrom = 0;
	if (source.fromText) {
		if (source.text.isEmpty()) {
			++reading.missing;
			return;
		}
		reading.fromLogger = true;
		ScanText(
			source.text,
			matcher,
			planted,
			source.control,
			reading,
			source.name);
	} else {
		const auto scan = ScanFile(
			source.path,
			source.sliceAtLastBanner,
			matcher,
			planted,
			source.control,
			reading,
			source.name);
		if (!scan.opened) {
			++reading.unreadable;
			return;
		}
		slicedFrom = scan.slicedFrom;
	}
	++reading.files;
	reading.names.push_back(source.name);
	reading.slicedFrom.push_back(slicedFrom);
	if (IsMtpName(source.name)) {
		++reading.mtpFiles;
	}
	if (reading.cls == SecrecyClass::EarlierParts
		&& (source.control.isEmpty()
			|| reading.controlHits == controlsBefore)) {
		++reading.withoutControl;
	}
}

void DecideClass(SecrecyClassReading &reading) {
	auto reasons = QStringList();
	if (IsWindowClass(reading.cls) && !reading.candidates) {
		reasons.push_back(u"the window yielded no candidates"_q);
	}
	if (!reading.files) {
		reasons.push_back(u"no accepted file"_q);
	}
	if (reading.unreadable) {
		reasons.push_back(u"unreadable part"_q);
	}
	if (reading.cls == SecrecyClass::EarlierParts) {
		if (reading.invalidNames) {
			reasons.push_back(u"invalid part name"_q);
		}
		if (reading.missing) {
			reasons.push_back(u"missing part"_q);
		}
		if (reading.foreign) {
			reasons.push_back(u"foreign part"_q);
		}
		if (reading.withoutControl) {
			reasons.push_back(u"part without its own control"_q);
		}
	} else if (reading.files) {
		if (!reading.plantedHits) {
			reasons.push_back(u"no planted control"_q);
		}
		if (reading.namedControl && !reading.controlHits) {
			reasons.push_back(u"named control absent"_q);
		}
	}
	if (reading.mtpFiles
		&& !(reading.sendHeaders > 0 && reading.recvHeaders > 0)) {
		reasons.push_back(u"mtp part without headers of both directions"_q);
	}
	reading.decided = reasons.isEmpty();
	reading.undecidedReason = reasons.join(u"; "_q);
}

[[nodiscard]] QString SitesText(const std::map<QString, int> &sites) {
	auto list = QStringList();
	for (const auto &[site, hits] : sites) {
		list.push_back(site + u" x"_q + QString::number(hits));
	}
	return list.join(u", "_q);
}

[[nodiscard]] QString IntsText(const std::vector<int> &values) {
	auto list = QStringList();
	for (const auto value : values) {
		list.push_back(QString::number(value));
	}
	return list.join(u", "_q);
}

[[nodiscard]] QString TimeText(const QDateTime &value) {
	return value.isValid()
		? value.toString(u"yyyy-MM-dd hh:mm:ss"_q)
		: u"invalid"_q;
}

[[nodiscard]] QString ClassRow(const SecrecyClassReading &c) {
	const auto pairs = std::vector<std::pair<QString, int>>{
		{ u"candidates"_q, c.candidates },
		{ u"files"_q, c.files },
		{ u"foreign"_q, c.foreign },
		{ u"unreadable"_q, c.unreadable },
		{ u"missing"_q, c.missing },
		{ u"invalidNames"_q, c.invalidNames },
		{ u"withoutControl"_q, c.withoutControl },
		{ u"mtpFiles"_q, c.mtpFiles },
		{ u"lines"_q, c.lines },
		{ u"wordRunsOther"_q, c.wordRunsOther },
		{ u"wordRunsSend"_q, c.wordRunsSend },
		{ u"wordRunsRecv"_q, c.wordRunsRecv },
		{ u"bounded"_q, c.bounded },
		{ u"embedded"_q, c.embedded },
		{ u"boundedOther"_q, c.boundedOther },
		{ u"boundedSend"_q, c.boundedSend },
		{ u"boundedRecv"_q, c.boundedRecv },
		{ u"boundedComputed"_q, c.boundedComputed },
		{ u"boundedQuoted"_q, c.boundedQuoted },
		{ u"boundedUnquoted"_q, c.boundedUnquoted },
		{ u"tokensOther"_q, c.tokensOther },
		{ u"tokensSend"_q, c.tokensSend },
		{ u"tokensRecv"_q, c.tokensRecv },
		{ u"sendHeaders"_q, c.sendHeaders },
		{ u"recvHeaders"_q, c.recvHeaders },
		{ u"plantedHits"_q, c.plantedHits },
		{ u"controlHits"_q, c.controlHits },
		{ u"clientWritten"_q, c.clientWritten() },
		{ u"received"_q, c.received() },
	};
	auto parts = QStringList();
	parts.push_back(u"SECRECY_CLASS: "_q + SecrecyClassName(c.cls));
	parts.push_back(u"source="_q
		+ (c.fromLogger ? u"logger"_q : u"files"_q));
	parts.push_back(u"decided="_q + B(c.decided));
	parts.push_back(u"undecidedReason=["_q + c.undecidedReason + u"]"_q);
	for (const auto &[key, value] : pairs) {
		parts.push_back(key + QChar('=') + QString::number(value));
	}
	parts.push_back(u"namedControl="_q + B(c.namedControl));
	parts.push_back(u"sendSites=["_q + SitesText(c.sendSites) + u"]"_q);
	parts.push_back(u"plainSites=["_q + SitesText(c.plainSites) + u"]"_q);
	parts.push_back(u"names=["_q + c.names.join(u", "_q) + u"]"_q);
	parts.push_back(u"slicedFrom=["_q + IntsText(c.slicedFrom) + u"]"_q);
	return parts.join(QChar(' '));
}

[[nodiscard]] QString AppendedSince(const QString &path, qint64 from) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return QString();
	}
	file.seek(from);
	return QString::fromUtf8(file.readAll());
}

[[nodiscard]] QString TestLogPath() {
	return EvidenceDir() + u"test_log.txt"_q;
}

[[nodiscard]] int HitTotal(const SecrecyClassReading &c) {
	return c.wordRunsOther
		+ c.wordRunsSend
		+ c.wordRunsRecv
		+ c.bounded
		+ c.tokensOther
		+ c.tokensSend
		+ c.tokensRecv;
}

void ReportClass(const SecrecyClassReading &c) {
	auto scan = DiscriminatingScan(
		u"SECRECY: "_q + SecrecyClassName(c.cls),
		u"client-written fixture secret"_q,
		u"the planted control or the named control"_q);
	scan.examined(c.lines);
	const auto subjects = std::vector<std::pair<QString, int>>{
		{ u"word-run/plain"_q, c.wordRunsOther },
		{ u"word-run/Send"_q, c.wordRunsSend },
		{ u"token/plain"_q, c.tokensOther },
	};
	for (const auto &[detail, count] : subjects) {
		for (auto i = 0; i != count; ++i) {
			scan.matchedSubject(detail);
		}
	}
	for (const auto &[site, count] : c.plainSites) {
		for (auto i = 0; i != count; ++i) {
			scan.matchedSubject(u"bounded/plain:"_q + site);
		}
	}
	for (const auto &[site, count] : c.sendSites) {
		for (auto i = 0; i != count; ++i) {
			scan.matchedSubject(u"Send:"_q + site);
		}
	}
	auto details = 0;
	const auto controls = std::vector<std::pair<QString, int>>{
		{ u"planted"_q, c.plantedHits },
		{ u"named"_q, c.controlHits },
	};
	for (const auto &[detail, count] : controls) {
		for (auto i = 0; i != count; ++i) {
			scan.matchedControl((details++ < kControlDetails)
				? detail
				: QString());
		}
	}
	scan.report();
}

}

}

#endif
