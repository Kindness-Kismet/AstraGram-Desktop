#pragma once

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_secrecy_scan.h"

#include "base/random.h"
#include "test/test_agent.h"
#include "test/test_log.h"
#include "test/test_probe.h"
#include "test/test_runner.h"
#include "logs.h"
#include "settings.h"

#include <QtCore/QTemporaryDir>

namespace Test {

namespace SecrecyScanDetails {}

using namespace SecrecyScanDetails;

namespace SecrecyScanDetails {

constexpr auto kMinTokenLength = 16;

constexpr auto kPartMinutes = 15;

constexpr auto kMaxWindowSteps = 200;

constexpr auto kControlDetails = 3;

constexpr auto kMaxTypeTag = 24;

constexpr auto kWordRunCanaries = 7;

// per phrase, see ReadCanaries
constexpr auto kMaxHeadLength = 64;

constexpr auto kMainLogStart = 22;

// "[yyyy.MM.dd hh:mm:ss] "
const auto kBanner = u"NEW LOGGING INSTANCE STARTED!!!"_q;

const auto kSiteWithheld = u"?|?"_q;

const auto kSiteNoField = u"-"_q;

// Where a line sits: outside any transport dump, or inside a dump entry the
// client sent or received (session_private.cpp logs "Send: " + DumpToText
// of what it writes and "Recv: " + DumpToText of what it read).
enum class DumpDirection {
	None,
	Send,
	Recv,
};

[[nodiscard]] QString B(bool value);

[[nodiscard]] QString NormalizedWord(const QString &word);

[[nodiscard]] QString DumpEscaped(QString text);

[[nodiscard]] bool IsHexDigit(QChar ch);

[[nodiscard]] int TypeTagEnd(const QString &text, int open);

[[nodiscard]] QString WordSeparated(const QString &text);

[[nodiscard]] bool AllDigits(const QString &text, int from, int till);

[[nodiscard]] bool IsEntryHeader(const QString &line);

[[nodiscard]] DumpDirection HeaderDirection(
		const QString &line,
		int *dumpFrom);

[[nodiscard]] bool IdentifierChar(QChar ch);

[[nodiscard]] QString SafeIdentifier(const QString &name);

// Walks DumpToText's own syntax (mtproto_dump_to_text.cpp and the generated
// scheme-dump_to_text.cpp): an object opens as "{ name" and closes with
// "}", a field is "  name: value", a string is "\"...\" [STRING]" with "\\"
// and "\"" escaped and never spans lines. Over text[from, till) it keeps the
// open-object stack, the quote state at |till| and the last field name.
struct DumpWalk {
	bool quoted = false;
	QString field;
};

DumpWalk WalkDump(
		const QString &text,
		int from,
		int till,
		std::vector<QString> &stack,
		bool wantField = false);

[[nodiscard]] bool InsideQuotes(const QString &text, int at);

[[nodiscard]] QString SiteText(
		const std::vector<QString> &chain,
		const QString &field);

struct LineContext {
	DumpDirection direction = DumpDirection::None;
	const std::vector<QString> *stack = nullptr;
	int dumpFrom = 0;
	const QString *file = nullptr; // the public name of the scanned file
};

// Per file: a dump entry runs from its header to the next entry header;
// the open-object stack is carried across its lines.
struct DumpTracker {
	DumpDirection direction = DumpDirection::None;
	std::vector<QString> stack;
	int dumpFrom = 0;

	LineContext begin(const QString &line, SecrecyClassReading &reading);
	void end(const QString &line);
};

[[nodiscard]] bool InDump(const LineContext &context, int at);

[[nodiscard]] QString SiteAt(
		const QString &text,
		const LineContext &context,
		int at,
		bool *quoted = nullptr);

[[nodiscard]] bool IsMainLogStart(const QString &line);

[[nodiscard]] int MessageFrom(const QString &line);

// A "name=value" field of a plain line. The name is an ASCII identifier
// (SafeIdentifier) at the message start or right after a blank, outside a
// double-quoted segment; the value runs to the next blank, less a trailing
// run of ";,.:" and then one enclosing pair of quotes.
struct PlainField {
	QString name;
	int nameFrom = 0;
	int valueFrom = 0;
	int valueTill = 0;
};

[[nodiscard]] bool IsBlank(QChar ch);

[[nodiscard]] std::vector<PlainField> PlainFields(
		const QString &text,
		int from);

[[nodiscard]] bool HeadShaped(const QString &head);

[[nodiscard]] SecrecySecrets UsableSecrets(
		const SecrecySecrets &secrets,
		SecrecyReading *reading);

// A plain-line bounded short-secret hit: its public site, and whether it is
// exactly the value of a declared computed field.
struct PlainHit {
	QString site;
	bool computed = false;
};

class SecrecyMatcher final {
public:
	explicit SecrecyMatcher(
		const SecrecySecrets &usable,
		std::vector<SecrecyComputedField> computed = {});

	// |run| is the last phrase word of the current run, carried from line
	// to line of one file or text by the caller.
	void line(
		const QString &text,
		const LineContext &context,
		QString &run,
		SecrecyClassReading &reading) const;

private:
	void countWordRuns(
		const QString &text,
		const LineContext &context,
		QString &run,
		SecrecyClassReading &reading) const;
	void countShort(
		const QString &text,
		const QString &needle,
		const LineContext &context,
		bool dumpOnly,
		SecrecyClassReading &reading) const;
	void countToken(
		const QString &text,
		const QString &token,
		const LineContext &context,
		SecrecyClassReading &reading) const;
	[[nodiscard]] PlainHit plainHit(
		const QString &text,
		const LineContext &context,
		int at,
		int till) const;
	[[nodiscard]] bool holds(const QString &text) const;

	QSet<QString> _words;
	QSet<QString> _pairs;
	int _longest = 0;
	std::vector<QString> _shortSecrets;
	std::vector<QString> _escaped; // empty where it equals the raw form
	std::vector<QString> _tokens;
	std::vector<SecrecyComputedField> _computed;

};

// Feeds the lines of one file or text through a matcher, tracking the dump
// context and counting the known-present controls.
class LineFeed final {
public:
	LineFeed(
		const SecrecyMatcher &matcher,
		const QString &planted,
		const QString &control,
		SecrecyClassReading &reading,
		const QString &file);

	void feed(const QString &line);

private:
	const SecrecyMatcher &_matcher;
	const QString _planted;
	const QString _control;
	const QString _file; // public name, printed in plain-line sites
	SecrecyClassReading &_reading;
	DumpTracker _dump;
	QString _run;

};

[[nodiscard]] QString ChopLineEnd(QString line);

void ScanText(
		const QString &text,
		const SecrecyMatcher &matcher,
		const QString &planted,
		const QString &control,
		SecrecyClassReading &reading,
		const QString &file = QString());

struct FileScan {
	bool opened = false;
	int slicedFrom = 0; // 1-based line of the last banner, 0 when whole
};

FileScan ScanFile(
		const QString &path,
		bool slice,
		const SecrecyMatcher &matcher,
		const QString &planted,
		const QString &control,
		SecrecyClassReading &reading,
		const QString &name);

[[nodiscard]] PartIdentity ReadPartIdentity(
		const QString &path,
		const QString &dayIndex);

[[nodiscard]] bool ValidPartName(const QString &name);

[[nodiscard]] bool IsMtpName(const QString &name);

[[nodiscard]] bool IsWindowClass(SecrecyClass cls);

[[nodiscard]] QString CanaryDump(
		const QString &direction,
		const QString &secret);

[[nodiscard]] QString CanaryVectorDump(
		const QString &first,
		const QString &second);

void ReadCanaries(const SecrecySecrets &usable, SecrecyReading &result);

void ReadSource(
		const SecrecySource &source,
		const SecrecyMatcher &matcher,
		const QString &planted,
		SecrecyClassReading &reading);

void DecideClass(SecrecyClassReading &reading);

[[nodiscard]] QString SitesText(const std::map<QString, int> &sites);

[[nodiscard]] QString IntsText(const std::vector<int> &values);

[[nodiscard]] QString TimeText(const QDateTime &value);

[[nodiscard]] QString ClassRow(const SecrecyClassReading &c);

[[nodiscard]] QString AppendedSince(const QString &path, qint64 from);

[[nodiscard]] QString TestLogPath();

[[nodiscard]] int HitTotal(const SecrecyClassReading &c);

void ReportClass(const SecrecyClassReading &c);

// ---- self-test -------------------------------------------------------------
const auto kSelfPassword = u"k9Tq2"_q;

const auto kSelfToken = u"3f6c1a2e-9b7d-4c58-a1e0-5d2f8b9c7e41"_q;

const auto kSelfShortToken = u"tok4567890"_q;

const auto kSelfNonce = u"secrecy-control-selftest00000001"_q;

const auto kSelfFiller = u"FILLER_LINE_MARKER"_q;

const auto kSelfEarlierControl = u"EARLIER_CONTROL_A"_q;

const auto kSelfDay = QDate(2026, 1, 15);

const auto kSelfToday = u"20260115"_q;

const auto kSelfYesterday = u"20260114"_q;

const auto kSelfBannerRule = QString(64, QChar('-'));

const auto kSelfComputedHead = u"Selftest Info: computed state"_q;

const auto kSelfComputedField = u"digest"_q;

const auto kSelfOtherValue = u"OTHER_VALUE_MARKER"_q;

[[nodiscard]] std::vector<QString> SelfPhraseA();

[[nodiscard]] std::vector<QString> SelfPhraseB();

[[nodiscard]] SecrecySecrets SelfSecrets();

[[nodiscard]] QStringList SelfForbidden();

[[nodiscard]] QStringList SelfLeaksIn(const QStringList &rows);

[[nodiscard]] QString Entry(const QString &text);

[[nodiscard]] QString MtpEntry(const QString &text);

[[nodiscard]] QStringList SendEntry(const QStringList &body);

[[nodiscard]] QStringList RecvEntry(const QStringList &body);

[[nodiscard]] QString PlantedEntry();

[[nodiscard]] QString ComputedLine(
		const QString &span,
		const QString &digest,
		const QString &tail = QString());

[[nodiscard]] SecrecyComputedField SelfDeclaration();

void InsertBeforePlanted(QStringList &lines, const QStringList &add);

struct SelfFixture {
	bool testLogFile = true;
	QStringList testLog;
	QStringList appLog;
	std::map<QString, QStringList> parts; // DebugLogs file name -> lines
	std::vector<SecrecyEarlierPart> earlier;
	SecrecySecrets secrets;
	std::vector<SecrecyComputedField> computed; // declared computed fields
};

[[nodiscard]] SelfFixture CleanFixture();

[[nodiscard]] SelfFixture MirroredFixture(bool declared);

[[nodiscard]] SelfFixture SitesFixture();

[[nodiscard]] bool WriteLines(const QString &path, const QStringList &lines);

struct SelfRun {
	bool prepared = false;
	SecrecyLaunchLogs logs;
	SecrecyReading reading;
};

[[nodiscard]] SelfRun RunFixture(const SelfFixture &fixture);

[[nodiscard]] const SecrecyClassReading &ClassOf(
		const SecrecyReading &reading,
		SecrecyClass cls);

[[nodiscard]] QString Summary(const SecrecyReading &reading);

[[nodiscard]] QString Counts(const SecrecyClassReading &c);

[[nodiscard]] QString SiteDetails(const SecrecyClassReading &c);

struct SelfState {
	std::vector<SecrecyReading> readings;
};

void Keep(
		const std::shared_ptr<SelfState> &state,
		const SelfRun &run);

void CheckPrepared(const QString &what, const SelfRun &run);

void SelfCanaries(const std::shared_ptr<SelfState> &state);

void SelfEmbedded(const std::shared_ptr<SelfState> &state);

void SelfRecv(const std::shared_ptr<SelfState> &state);

void SelfClientWritten(const std::shared_ptr<SelfState> &state);

void SelfSites(const std::shared_ptr<SelfState> &state);

void SelfUndecided(const std::shared_ptr<SelfState> &state);

[[nodiscard]] QStringList LeakLines();

[[nodiscard]] SelfFixture IdentityFixture(
		bool nextQuarterToday,
		bool withBanner);

void SelfIdentity(const std::shared_ptr<SelfState> &state);

void SelfPrintsNothing(const std::shared_ptr<SelfState> &state);

}

QString SecrecyClassName(SecrecyClass value);

QString PartIdentityName(PartIdentity value);

SecrecyLaunchLogs SelectLaunchLogs(
		const QString &workingDir,
		const QString &evidenceDir,
		const QString &appLogText,
		const QDateTime &launchStart,
		const QDateTime &scanAt,
		const std::vector<SecrecyEarlierPart> &earlier);

SecrecyLaunchLogs SelectThisLaunchLogs(
		const std::vector<SecrecyEarlierPart> &earlier);

SecrecyReading ReadSecrecy(
		const SecrecySecrets &secrets,
		const SecrecyLaunchLogs &logs,
		const QString &plantedControl,
		const std::vector<SecrecyComputedField> &computedFields);

QStringList SecrecyRows(const SecrecyReading &reading);

bool CheckSecrecy(const SecrecyScanArgs &args, const QString &what);

void AppendSecrecyScanSelfTest(not_null<Runner*> runner);

}

#endif
