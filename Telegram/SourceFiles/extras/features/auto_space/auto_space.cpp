#include "extras/features/auto_space/auto_space.h"

#include "ui/text/text_entity.h"

#include <QRegularExpression>

#include <algorithm>
#include <vector>

namespace Extras::AutoSpace {

namespace {

// CJK 码位段：平假名、片假名、注音、CJK 兼容表意、CJK 统一表意
// 宏展开后是真字符串字面量拼接；constexpr 变量不能参与拼接
#define EXTRAS_CJK "[\\x{3040}-\\x{309F}\\x{30A0}-\\x{30FF}\\x{3100}-\\x{312F}\\x{F900}-\\x{FAFF}\\x{4E00}-\\x{9FFF}]"

// 模式全为 ASCII，用 QLatin1String 拼接 EXTRAS_CJK 构造（QStringLiteral 是宏，不能拼接）
const QRegularExpression kCjkQuote(QLatin1String("(" EXTRAS_CJK ")([\"'])"));
const QRegularExpression kQuoteCjk(QLatin1String("([\"'])(" EXTRAS_CJK ")"));
const QRegularExpression kFixQuote(QLatin1String("([\"'])(\\s*)(.+?)(\\s*)([\"'])"));
const QRegularExpression kCjkBracketCjk(QLatin1String("(" EXTRAS_CJK ")([\\({\\[]+(.*?)[\\)}\\]]+)(" EXTRAS_CJK ")"));
const QRegularExpression kCjkBracket(QLatin1String("(" EXTRAS_CJK ")([\\(\\){}\\[\\]<>])"));
const QRegularExpression kBracketCjk(QLatin1String("([\\(\\){}\\[\\]<>])(" EXTRAS_CJK ")"));
const QRegularExpression kFixBracket(QLatin1String("([\\(\\{\\[\\)]+)(\\s*)(.+?)(\\s*)([\\)}\\]]+)"));
const QRegularExpression kCjkHash(QLatin1String("(" EXTRAS_CJK ")(#(\\S+))"));
const QRegularExpression kHashCjk(QLatin1String("((\\S+)#)(" EXTRAS_CJK ")"));
const QRegularExpression kCjkAns(
	QLatin1String("(" EXTRAS_CJK ")([a-z0-9@`~\\$%\\^&\\*\\-_\\+=\\|\\\\/])"),
	QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kAnsCjk(
	QLatin1String("([a-z0-9`~!\\$%\\^&\\*\\-_\\+=\\|\\\\;:,\\./\\?])(" EXTRAS_CJK ")"),
	QRegularExpression::CaseInsensitiveOption);
// 单字符 CJK 判定，供 URL 定界使用
const QRegularExpression kCjkChar(QLatin1String(EXTRAS_CJK));

#undef EXTRAS_CJK

// 内容不可改动的 entity：插一个空格就会破坏链接、代码或提及本身
[[nodiscard]] bool isProtectedType(EntityType type) {
	switch (type) {
	case EntityType::Url:
	case EntityType::Email:
	case EntityType::Hashtag:
	case EntityType::Cashtag:
	case EntityType::Mention:
	case EntityType::MentionName:
	case EntityType::BotCommand:
	case EntityType::Code:
	case EntityType::Pre:
	case EntityType::CustomEmoji:
	case EntityType::Phone:
	case EntityType::BankCard:
	case EntityType::MediaTimestamp:
		return true;
	default:
		return false;
	}
}

QString processRules(const QString &text) {
	auto result = text;
	result.replace(kCjkQuote, "\\1 \\2");
	result.replace(kQuoteCjk, "\\1 \\2");
	result.replace(kFixQuote, "\\1\\3\\5");

	// CJK(内容)CJK 先整体处理，已匹配时跳过单侧规则避免重复插空格
	auto bracketed = result;
	bracketed.replace(kCjkBracketCjk, "\\1 \\2 \\4");
	if (bracketed == result) {
		result.replace(kCjkBracket, "\\1 \\2");
		result.replace(kBracketCjk, "\\1 \\2");
	} else {
		result = bracketed;
	}
	result.replace(kFixBracket, "\\1\\3\\5");

	result.replace(kCjkHash, "\\1 \\2");
	result.replace(kHashCjk, "\\1 \\3");
	result.replace(kCjkAns, "\\1 \\2");
	result.replace(kAnsCjk, "\\1 \\2");
	return result;
}

[[nodiscard]] bool isCjk(QChar ch) {
	return kCjkChar.match(QString(ch)).hasMatch();
}

// 边界用探针判断：把相邻字符和整段一起套规则，看边界处是否被插入空格
[[nodiscard]] bool needsSpaceBefore(QChar before, const QString &part) {
	if (before.isSpace() || part.isEmpty() || part.at(0).isSpace()) {
		return false;
	}
	const auto head = QString(before) + QChar(' ');
	return processRules(QString(before) + part).startsWith(head);
}

[[nodiscard]] bool needsSpaceAfter(const QString &part, QChar after) {
	if (after.isSpace() || part.isEmpty() || part.back().isSpace()) {
		return false;
	}
	const auto tail = QChar(' ') + QString(after);
	return processRules(part + after).endsWith(tail);
}

QString spacingFree(const QString &text) {
	if (!text.contains("://")) {
		return processRules(text);
	}
	// 含 URL 时切段，URL 段原样保留；前定界遇 CJK 即停，中文不属于 scheme
	auto result = QString();
	auto pos = qsizetype(0);
	while (pos <= text.size()) {
		const auto idx = text.indexOf(QStringLiteral("://"), pos);
		if (idx < 0) {
			result += processRules(text.mid(pos));
			break;
		}
		auto start = idx;
		while (start > pos
			&& !text[start - 1].isSpace()
			&& !isCjk(text[start - 1])) {
			--start;
		}
		auto end = text.size();
		for (auto i = idx + 3; i < text.size(); ++i) {
			if (text[i].isSpace()) {
				end = i;
				break;
			}
		}
		result += processRules(text.mid(pos, start - pos));
		const auto url = text.mid(start, end - start);
		if (!result.isEmpty() && needsSpaceBefore(result.back(), url)) {
			result += ' ';
		}
		result += url;
		pos = end;
	}
	return result;
}

struct Range {
	int from = 0;
	int till = 0;
};

// 保护区按位置合并，相邻区间并成一段，供后续切分使用
[[nodiscard]] std::vector<Range> protectedRanges(
		const TextWithEntities &text) {
	const auto size = int(text.text.size());
	auto result = std::vector<Range>();
	for (const auto &entity : text.entities) {
		if (!isProtectedType(entity.type()) || entity.length() <= 0) {
			continue;
		}
		const auto from = std::clamp(entity.offset(), 0, size);
		const auto till = std::clamp(entity.offset() + entity.length(), from, size);
		if (from < till) {
			result.push_back({ from, till });
		}
	}
	std::sort(result.begin(), result.end(), [](Range a, Range b) {
		return (a.from != b.from) ? (a.from < b.from) : (a.till < b.till);
	});
	auto merged = std::vector<Range>();
	for (const auto &range : result) {
		if (!merged.empty() && range.from <= merged.back().till) {
			merged.back().till = std::max(merged.back().till, range.till);
		} else {
			merged.push_back(range);
		}
	}
	return merged;
}

// 规则只增删空格，逐字符对齐即可；starts/ends 记录每个原字符在新文本的边界
void alignSegment(
		const QString &oldPart,
		const QString &newPart,
		int base,
		int oldFrom,
		std::vector<int> &starts,
		std::vector<int> &ends) {
	const auto oldSize = int(oldPart.size());
	const auto newSize = int(newPart.size());
	auto i = 0;
	auto j = 0;
	while (i < oldSize) {
		if (j < newSize && oldPart[i] == newPart[j]) {
			starts[oldFrom + i] = base + j;
			ends[oldFrom + i] = base + j + 1;
			++i;
			++j;
		} else if (j < newSize
			&& newPart[j].isSpace()
			&& !oldPart[i].isSpace()) {
			++j;
		} else {
			starts[oldFrom + i] = base + j;
			ends[oldFrom + i] = base + j;
			++i;
		}
	}
}

void rebuildEntity(EntityInText &entity, int offset, int length) {
	auto updated = EntityInText(entity.type(), offset, length, entity.data());
	if (entity.isLocal()) {
		updated.setLocal();
	}
	entity = std::move(updated);
}

} // namespace

void processText(TextWithEntities &text) {
	// 斜杠命令不做处理
	if (text.text.startsWith('/')) {
		return;
	}
	const auto oldText = text.text;
	const auto oldSize = int(oldText.size());
	if (!oldSize) {
		return;
	}

	const auto ranges = protectedRanges(text);
	auto starts = std::vector<int>(oldSize, 0);
	auto ends = std::vector<int>(oldSize, 0);
	auto result = QString();
	result.reserve(oldText.size());

	const auto appendSegment = [&](int from, int till, bool verbatim) {
		if (from >= till) {
			return;
		}
		const auto part = oldText.mid(from, till - from);
		const auto base = int(result.size());
		if (verbatim) {
			for (auto i = 0; i != int(part.size()); ++i) {
				starts[from + i] = base + i;
				ends[from + i] = base + i + 1;
			}
			result += part;
		} else {
			const auto spaced = spacingFree(part);
			alignSegment(part, spaced, base, from, starts, ends);
			result += spaced;
		}
	};

	auto pos = 0;
	for (const auto &range : ranges) {
		appendSegment(pos, range.from, false);
		const auto part = oldText.mid(range.from, range.till - range.from);
		if (!result.isEmpty() && needsSpaceBefore(result.back(), part)) {
			result += ' ';
		}
		appendSegment(range.from, range.till, true);
		if (range.till < oldSize
			&& needsSpaceAfter(part, oldText[range.till])) {
			result += ' ';
		}
		pos = range.till;
	}
	appendSegment(pos, oldSize, false);

	if (result == oldText) {
		return;
	}
	for (auto &entity : text.entities) {
		const auto from = entity.offset();
		const auto length = entity.length();
		if (from < 0 || length <= 0 || from >= oldSize) {
			continue;
		}
		const auto till = std::min(from + length, oldSize);
		const auto newFrom = starts[from];
		const auto newTill = ends[till - 1];
		rebuildEntity(entity, newFrom, std::max(newTill - newFrom, 0));
	}
	text.text = std::move(result);
}

} // namespace Extras::AutoSpace
