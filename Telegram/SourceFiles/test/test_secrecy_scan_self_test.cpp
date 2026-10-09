#include "test/test_secrecy_scan_internal.h"

#ifdef _DEBUG

namespace Test {

namespace SecrecyScanDetails {}

using namespace SecrecyScanDetails;

namespace SecrecyScanDetails {

[[nodiscard]] std::vector<QString> SelfPhraseA() {
	return {
		u"walnut"_q, u"giraffe"_q, u"pumpkin"_q, u"lobster"_q,
		u"volcano"_q, u"oyster"_q, u"cactus"_q, u"dolphin"_q,
		u"kangaroo"_q, u"jaguar"_q, u"mango"_q, u"velvet"_q,
	};
}

[[nodiscard]] std::vector<QString> SelfPhraseB() {
	return {
		u"orchard"_q, u"hazard"_q, u"gorilla"_q, u"harvest"_q,
		u"lizard"_q, u"noodle"_q, u"raccoon"_q, u"tomato"_q,
		u"zebra"_q, u"puppy"_q, u"squirrel"_q, u"donkey"_q,
	};
}

[[nodiscard]] SecrecySecrets SelfSecrets() {
	return {
		.phrases = { SelfPhraseA(), SelfPhraseB() },
		.shortSecrets = { kSelfPassword },
		.tokens = { kSelfToken },
	};
}

// Every string a row of a synthetic reading must never contain: the
// secrets, the words, their adjacent pairs, a scanned line's filler and
// the value of a field other than the one a hit is.
[[nodiscard]] QStringList SelfForbidden() {
	auto result = QStringList{
		kSelfPassword,
		kSelfToken,
		kSelfShortToken,
		kSelfFiller,
		kSelfOtherValue,
	};
	for (const auto &phrase : { SelfPhraseA(), SelfPhraseB() }) {
		for (auto i = 0; i != int(phrase.size()); ++i) {
			result.push_back(phrase[i]);
			if (i + 1 < int(phrase.size())) {
				result.push_back(phrase[i] + QChar(' ') + phrase[i + 1]);
			}
		}
	}
	return result;
}

[[nodiscard]] QStringList SelfLeaksIn(const QStringList &rows) {
	const auto forbidden = SelfForbidden();
	auto result = QStringList();
	for (const auto &row : rows) {
		for (auto i = 0; i != int(forbidden.size()); ++i) {
			if (row.contains(forbidden[i], Qt::CaseInsensitive)) {
				// The index only: the forbidden string itself is a secret.
				result.push_back(u"forbidden#%1"_q.arg(i));
			}
		}
	}
	return result;
}

[[nodiscard]] QString Entry(const QString &text) {
	return u"[10:05:00.000 01-0000001] "_q + text;
}

[[nodiscard]] QString MtpEntry(const QString &text) {
	return u"[10:05:02.000 03-0000002] (dc:2_main) "_q + text;
}

[[nodiscard]] QStringList SendEntry(const QStringList &body) {
	auto result = QStringList{
		MtpEntry(u"Send: { core_message"_q),
		u"  body: { invokeWithLayer"_q,
	};
	result += body;
	result += QStringList{ u"  }"_q, u"} (dc:2,key:0,session:0)"_q };
	return result;
}

[[nodiscard]] QStringList RecvEntry(const QStringList &body) {
	auto result = QStringList{
		MtpEntry(u"Recv: { rpc_result"_q),
		u"  req_msg_id: 1 [LONG]"_q,
		u"  result: { messages_forumTopics"_q,
	};
	result += body;
	result += QStringList{ u"  }"_q, u"} (dc:2,key:0,session:0)"_q };
	return result;
}

[[nodiscard]] QString PlantedEntry() {
	return u"Test Info: secrecy control "_q + kSelfNonce;
}

// A product-shaped diagnostic line with the synthetic head: "<head>
// span=<span> digest=<digest>; mode=<other value>.<tail>", so one value is
// followed by ";" and one by ".".
[[nodiscard]] QString ComputedLine(
		const QString &span,
		const QString &digest,
		const QString &tail) {
	return kSelfComputedHead
		+ u" span="_q
		+ span
		+ u" digest="_q
		+ digest
		+ u"; mode="_q
		+ kSelfOtherValue
		+ u"."_q
		+ tail;
}

[[nodiscard]] SecrecyComputedField SelfDeclaration() {
	return { .head = kSelfComputedHead, .field = kSelfComputedField };
}

// Keeps a part's planted control its last line.
void InsertBeforePlanted(QStringList &lines, const QStringList &add) {
	const auto planted = lines.back();
	lines.pop_back();
	lines += add;
	lines.push_back(planted);
}

[[nodiscard]] SelfFixture CleanFixture() {
	auto result = SelfFixture();
	result.testLog = QStringList{
		u"NOTE: "_q + kSelfFiller + u" a harness row"_q,
		u"NOTE: SECRECY_CONTROL: "_q + kSelfNonce,
	};
	result.appLog = QStringList{
		u"[2026.01.15 10:05:00] "_q + kSelfFiller + u" app line"_q,
		u"[2026.01.15 10:05:01] "_q + PlantedEntry(),
	};
	result.parts[u"log_10_00.txt"_q] = QStringList{
		kSelfToday,
		Entry(kSelfFiller + u" debug line"_q),
		Entry(PlantedEntry()),
	};
	auto mtp = QStringList{ kSelfToday };
	mtp += SendEntry({ u"    query: { wallet_getState"_q, u"    }"_q });
	mtp += RecvEntry({
		u"    topics: [ vector<0x0>"_q,
		u"      { forumTopic"_q,
		u"        title: \"Public topic\" [STRING]"_q,
		u"      }"_q,
		u"    ]"_q,
	});
	mtp.push_back(MtpEntry(PlantedEntry()));
	result.parts[u"mtp_10_00.txt"_q] = mtp;
	result.secrets = SelfSecrets();
	return result;
}

// The computed-field trap: one product-shaped line whose "digest" value is
// the synthetic password, mirrored into the app log and its DebugLogs part
// as LOG writes it, with or without the declaration of that field.
[[nodiscard]] SelfFixture MirroredFixture(bool declared) {
	auto result = CleanFixture();
	const auto line = ComputedLine(u"17"_q, kSelfPassword);
	result.appLog.push_back(u"[2026.01.15 10:05:02] "_q + line);
	result.parts[u"log_10_00.txt"_q].push_back(Entry(line));
	if (declared) {
		result.computed.push_back(SelfDeclaration());
	}
	return result;
}

// The declared mirrored line plus six log_ lines whose sites must each be
// withheld: the hit in the head, a line with no field, a head whose last
// word and the field name form a phrase pair, a field name that is the
// password itself (its value's site is withheld; the name's own hit is no
// field's value and prints the head with "-"), a head holding the token,
// and a head holding the password upper-cased, which the case-sensitive
// match does not count but the case-insensitive print rule withholds.
[[nodiscard]] SelfFixture SitesFixture() {
	auto result = MirroredFixture(true);
	const auto a = SelfPhraseA();
	const auto pw = kSelfPassword;
	result.parts[u"log_10_00.txt"_q] += QStringList{
		Entry(u"Selftest Info: computed "_q + pw + u" digest=7"_q),
		Entry(kSelfComputedHead + QChar(' ') + pw),
		Entry(u"Selftest Info: "_q
			+ a[6]
			+ QChar(' ')
			+ a[7]
			+ QChar('=')
			+ pw),
		Entry(kSelfComputedHead + QChar(' ') + pw + QChar('=') + pw),
		Entry(u"Selftest Info: "_q + kSelfToken + u" digest="_q + pw),
		Entry(u"Selftest Info: "_q + pw.toUpper() + u" digest="_q + pw),
	};
	return result;
}

[[nodiscard]] bool WriteLines(const QString &path, const QStringList &lines) {
	auto file = QFile(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	const auto bytes = (lines.join(QChar('\n')) + QChar('\n')).toUtf8();
	return (file.write(bytes) == bytes.size());
}

// Builds the fixture in a fresh QTemporaryDir, which is removed again when
// this returns, and reads it with a fixed window: 10:05 to 10:20 on the
// synthetic day, so the candidates are the 10:00 and 10:15 parts.
[[nodiscard]] SelfRun RunFixture(const SelfFixture &fixture) {
	auto result = SelfRun();
	const auto dir = QTemporaryDir();
	if (!dir.isValid()) {
		return result;
	}
	const auto evidence = dir.path() + u"/evidence/"_q;
	const auto working = dir.path() + u"/work/"_q;
	if (!QDir().mkpath(evidence) || !QDir().mkpath(working + u"DebugLogs"_q)) {
		return result;
	}
	auto ok = true;
	if (fixture.testLogFile) {
		ok = WriteLines(evidence + u"test_log.txt"_q, fixture.testLog) && ok;
	}
	for (const auto &[name, lines] : fixture.parts) {
		ok = WriteLines(working + u"DebugLogs/"_q + name, lines) && ok;
	}
	if (!ok) {
		return result;
	}
	const auto appLog = fixture.appLog.isEmpty()
		? QString()
		: (fixture.appLog.join(QChar('\n')) + QChar('\n'));
	result.logs = SelectLaunchLogs(
		working,
		evidence,
		appLog,
		QDateTime(kSelfDay, QTime(10, 5)),
		QDateTime(kSelfDay, QTime(10, 20)),
		fixture.earlier);
	result.reading = ReadSecrecy(
		fixture.secrets,
		result.logs,
		kSelfNonce,
		fixture.computed);
	result.prepared = true;
	return result;
}

[[nodiscard]] const SecrecyClassReading &ClassOf(
		const SecrecyReading &reading,
		SecrecyClass cls) {
	static const auto empty = SecrecyClassReading();
	const auto found = reading.find(cls);
	return found ? *found : empty;
}

[[nodiscard]] QString Summary(const SecrecyReading &reading) {
	return u"decided=%1 clean=%2 clientWritten=%3 received=%4 "
		"canariesHold=%5 undecidedClasses=[%6] reasons=[%7]"_q
		.arg(B(reading.decided))
		.arg(B(reading.clean))
		.arg(reading.clientWritten())
		.arg(reading.received())
		.arg(B(reading.canariesHold))
		.arg(reading.undecidedClasses.join(u", "_q))
		.arg(reading.undecidedReasons.join(u"; "_q));
}

[[nodiscard]] QString Counts(const SecrecyClassReading &c) {
	return u"%1: files=%2 foreign=%3 missing=%4 invalidNames=%5 "
		"wordRunsOther=%6 wordRunsSend=%7 wordRunsRecv=%8 bounded=%9 "
		"embedded=%10 boundedOther=%11 boundedSend=%12 boundedRecv=%13 "
		"tokensOther=%14 tokensSend=%15 tokensRecv=%16 sendHeaders=%17 "
		"recvHeaders=%18 plantedHits=%19 controlHits=%20 decided=%21 "
		"reason=[%22] slicedFrom=[%23]"_q
		.arg(SecrecyClassName(c.cls))
		.arg(c.files)
		.arg(c.foreign)
		.arg(c.missing)
		.arg(c.invalidNames)
		.arg(c.wordRunsOther)
		.arg(c.wordRunsSend)
		.arg(c.wordRunsRecv)
		.arg(c.bounded)
		.arg(c.embedded)
		.arg(c.boundedOther)
		.arg(c.boundedSend)
		.arg(c.boundedRecv)
		.arg(c.tokensOther)
		.arg(c.tokensSend)
		.arg(c.tokensRecv)
		.arg(c.sendHeaders)
		.arg(c.recvHeaders)
		.arg(c.plantedHits)
		.arg(c.controlHits)
		.arg(B(c.decided))
		.arg(c.undecidedReason)
		.arg(IntsText(c.slicedFrom));
}

[[nodiscard]] QString SiteDetails(const SecrecyClassReading &c) {
	return Counts(c)
		+ u" boundedComputed="_q
		+ QString::number(c.boundedComputed)
		+ u" plainSites=["_q
		+ SitesText(c.plainSites)
		+ u"] computedSites=["_q
		+ SitesText(c.computedSites)
		+ u"] sendSites=["_q
		+ SitesText(c.sendSites)
		+ u"]"_q;
}

void Keep(
		const std::shared_ptr<SelfState> &state,
		const SelfRun &run) {
	if (run.prepared) {
		state->readings.push_back(run.reading);
	}
}

void CheckPrepared(const QString &what, const SelfRun &run) {
	if (!run.prepared) {
		Check(false, what, u"the synthetic log tree could not be written"_q);
	}
}

void SelfCanaries(const std::shared_ptr<SelfState> &state) {
	const auto what = u"secrecy self-test: canaries hold and a clean tree "
		"reads decided and clean"_q;
	const auto run = RunFixture(CleanFixture());
	CheckPrepared(what, run);
	Keep(state, run);
	const auto &r = run.reading;
	Check(
		run.prepared
			&& r.canariesHold
			&& (r.canaryWordRunsExpected == 2 * kWordRunCanaries)
			&& (r.canaryShortExpected == 1)
			&& (r.canaryTokenExpected == 1)
			&& r.decided
			&& r.clean
			&& (r.classes.size() == 4),
		what,
		u"canaries wordRuns=%1/%2 bounded=%3 embedded=%4 send=%5 recv=%6 "
		"of %7 token=%8/%9 %10"_q
			.arg(r.canaryWordRuns)
			.arg(r.canaryWordRunsExpected)
			.arg(r.canaryBounded)
			.arg(r.canaryEmbedded)
			.arg(r.canarySend)
			.arg(r.canaryRecv)
			.arg(r.canaryShortExpected)
			.arg(r.canaryToken)
			.arg(r.canaryTokenExpected)
			.arg(Summary(r)));
}

void SelfEmbedded(const std::shared_ptr<SelfState> &state) {
	const auto what = u"secrecy self-test: an embedded short secret is not "
		"counted as bounded"_q;
	auto fixture = CleanFixture();
	auto &part = fixture.parts[u"log_10_00.txt"_q];
	part.push_back(Entry(kSelfFiller + u" x"_q + kSelfPassword + u"x"_q));
	part.push_back(Entry(u"id12"_q + kSelfPassword + u"34"_q));
	const auto run = RunFixture(fixture);
	CheckPrepared(what, run);
	Keep(state, run);
	const auto &c = ClassOf(run.reading, SecrecyClass::DebugLog);
	Check(
		run.prepared
			&& (c.embedded >= 2)
			&& (c.bounded == 0)
			&& run.reading.decided
			&& run.reading.clean,
		what,
		Counts(c) + u" | "_q + Summary(run.reading));
}

void SelfRecv(const std::shared_ptr<SelfState> &state) {
	const auto what = u"secrecy self-test: a bounded short secret in a Recv "
		"entry is reported at its site and does not decide"_q;
	auto fixture = CleanFixture();
	auto &part = fixture.parts[u"mtp_10_00.txt"_q];
	const auto planted = part.back();
	part.pop_back();
	part += RecvEntry({
		u"    topics: [ vector<0x0>"_q,
		u"      { forumTopic"_q,
		u"        title: \""_q + kSelfPassword + u"\" [STRING]"_q,
		u"      }"_q,
		u"    ]"_q,
	});
	part.push_back(planted);
	const auto run = RunFixture(fixture);
	CheckPrepared(what, run);
	Keep(state, run);
	const auto &c = ClassOf(run.reading, SecrecyClass::MtpLog);
	const auto site = u"messages_forumTopics/forumTopic.title"_q;
	Check(
		run.prepared
			&& (c.boundedRecv == 1)
			&& (c.recvSites.size() == 1)
			&& c.recvSites.contains(site)
			&& (c.boundedSend == 0)
			&& run.reading.decided
			&& run.reading.clean,
		what,
		Counts(c)
			+ u" recvSites=["_q
			+ SitesText(c.recvSites)
			+ u"] | "_q
			+ Summary(run.reading));
}

void SelfClientWritten(const std::shared_ptr<SelfState> &state) {
	struct Leak {
		QString name;
		SecrecyClass cls;
		Fn<void(SelfFixture&)> plant;
		Fn<int(const SecrecyClassReading&)> counter;
		int expected = 1;
	};
	const auto pw = kSelfPassword;
	const auto a = SelfPhraseA();
	const auto b = SelfPhraseB();
	const auto leaks = std::vector<Leak>{
		{
			u"a bounded short secret in a Send entry"_q,
			SecrecyClass::MtpLog,
			[=](SelfFixture &f) {
				InsertBeforePlanted(f.parts[u"mtp_10_00.txt"_q], SendEntry({
					u"    query: { account_password"_q,
					u"      hint: \""_q + pw + u"\" [STRING]"_q,
					u"    }"_q,
				}));
			},
			[](const SecrecyClassReading &c) { return c.boundedSend; },
		},
		{
			u"a bounded short secret in a plain log_ line"_q,
			SecrecyClass::DebugLog,
			[=](SelfFixture &f) {
				f.parts[u"log_10_00.txt"_q].push_back(
					Entry(u"value: "_q + pw));
			},
			[](const SecrecyClassReading &c) { return c.boundedOther; },
		},
		{
			u"a word run in the app log"_q,
			SecrecyClass::AppLog,
			[=](SelfFixture &f) {
				f.appLog.push_back(u"[2026.01.15 10:05:02] words: "_q
					+ a[0]
					+ QChar(' ')
					+ a[1]);
			},
			[](const SecrecyClassReading &c) { return c.wordRunsOther; },
		},
		{
			u"a word run in a Send entry"_q,
			SecrecyClass::MtpLog,
			[=](SelfFixture &f) {
				InsertBeforePlanted(f.parts[u"mtp_10_00.txt"_q], SendEntry({
					u"    query: { messages_sendMessage"_q,
					u"      message: \""_q
						+ a[3]
						+ QChar(' ')
						+ a[4]
						+ u"\" [STRING]"_q,
					u"    }"_q,
				}));
			},
			[](const SecrecyClassReading &c) { return c.wordRunsSend; },
		},
		{
			u"a token in a plain line"_q,
			SecrecyClass::DebugLog,
			[=](SelfFixture &f) {
				f.parts[u"log_10_00.txt"_q].push_back(
					Entry(u"ref="_q + kSelfToken));
			},
			[](const SecrecyClassReading &c) { return c.tokensOther; },
		},
		{
			u"newline-joined words in a multi-line app log entry"_q,
			SecrecyClass::AppLog,
			[=](SelfFixture &f) {
				f.appLog += QStringList{
					u"[2026.01.15 10:05:02] words:"_q,
					a[0],
					a[1],
				};
			},
			[](const SecrecyClassReading &c) { return c.wordRunsOther; },
		},
		{
			u"a newline-joined phrase through Note into test_log"_q,
			SecrecyClass::TestLog,
			[=](SelfFixture &f) {
				// test_log.cpp OneLine writes each line break as \u000A.
				auto joined = QStringList(b.begin(), b.end());
				f.testLog.push_back(u"NOTE: phrase: "_q
					+ joined.join(u"\\u000A"_q));
			},
			[](const SecrecyClassReading &c) { return c.wordRunsOther; },
			int(b.size()) - 1,
		},
		{
			u"a comma-joined pair"_q,
			SecrecyClass::DebugLog,
			[=](SelfFixture &f) {
				f.parts[u"log_10_00.txt"_q].push_back(
					Entry(u"words=["_q + a[8] + QChar(',') + a[9] + u"]"_q));
			},
			[](const SecrecyClassReading &c) { return c.wordRunsOther; },
		},
		{
			u"words joined by an escaped line break in a Send string"_q,
			SecrecyClass::MtpLog,
			[=](SelfFixture &f) {
				InsertBeforePlanted(f.parts[u"mtp_10_00.txt"_q], SendEntry({
					u"    query: { messages_sendMessage"_q,
					u"      message: \""_q
						+ DumpEscaped(a[10] + QChar('\n') + a[11])
						+ u"\" [STRING]"_q,
					u"    }"_q,
				}));
			},
			[](const SecrecyClassReading &c) { return c.wordRunsSend; },
		},
		{
			u"a vector of words dumped one element per line in a Send "
				"entry"_q,
			SecrecyClass::MtpLog,
			[=](SelfFixture &f) {
				InsertBeforePlanted(f.parts[u"mtp_10_00.txt"_q], SendEntry({
					u"    query: { messages_sendMessage"_q,
					u"      entities: [ vector<0x1cb5c415> (2)"_q,
					u"        \""_q + a[1] + u"\" [STRING],"_q,
					u"        \""_q + a[2] + u"\" [STRING],"_q,
					u"      ]"_q,
					u"    }"_q,
				}));
			},
			[](const SecrecyClassReading &c) { return c.wordRunsSend; },
		},
	};
	for (const auto &leak : leaks) {
		const auto what = u"secrecy self-test: "_q
			+ leak.name
			+ u" fails the scan"_q;
		auto fixture = CleanFixture();
		leak.plant(fixture);
		const auto run = RunFixture(fixture);
		CheckPrepared(what, run);
		Keep(state, run);
		const auto &c = ClassOf(run.reading, leak.cls);
		Check(
			run.prepared
				&& run.reading.decided
				&& !run.reading.clean
				&& (leak.counter(c) == leak.expected)
				&& (c.clientWritten() == leak.expected)
				&& (run.reading.clientWritten() == leak.expected),
			what,
			Counts(c) + u" | "_q + Summary(run.reading));
	}
}

// The plain-line site report and declared computed fields, over the one
// synthetic head and its declared "digest" field.
void SelfSites(const std::shared_ptr<SelfState> &state) {
	using Sites = std::map<QString, int>;
	const auto pw = kSelfPassword;
	const auto a = SelfPhraseA();
	const auto head = u"|"_q + kSelfComputedHead + u"|"_q;
	const auto part = u"DebugLogs/log_10_00.txt"_q;
	const auto appSite = u"log.txt"_q + head + kSelfComputedField;
	const auto partSite = part + head + kSelfComputedField;
	const auto runKept = [&](const QString &what, const SelfFixture &fixture) {
		const auto result = RunFixture(fixture);
		CheckPrepared(what, result);
		Keep(state, result);
		return result;
	};
	const auto tail = [](const SecrecyReading &r) {
		return u" | "_q
			+ Summary(r)
			+ u" computed="_q
			+ QString::number(r.computed());
	};
	{
		const auto what = u"secrecy self-test: a bounded short secret that "
			"is an undeclared field's value fails the scan, and its site row "
			"names the class, the file, the message head and the field"_q;
		const auto result = runKept(what, MirroredFixture(false));
		const auto &r = result.reading;
		const auto &app = ClassOf(r, SecrecyClass::AppLog);
		const auto &debug = ClassOf(r, SecrecyClass::DebugLog);
		const auto rows = SecrecyRows(r).join(QChar('\n'));
		Check(
			result.prepared
				&& r.decided
				&& !r.clean
				&& (r.clientWritten() == 2)
				&& (r.computed() == 0)
				&& (app.boundedOther == 1)
				&& (app.plainSites == Sites{ { appSite, 1 } })
				&& (debug.boundedOther == 1)
				&& (debug.plainSites == Sites{ { partSite, 1 } })
				&& rows.contains(u"plainSites=["_q + appSite + u" x1]"_q)
				&& rows.contains(u"plainSites=["_q + partSite + u" x1]"_q),
			what,
			SiteDetails(app) + u" | "_q + SiteDetails(debug) + tail(r));
	}
	{
		const auto what = u"secrecy self-test: the same value as a declared "
			"computed field's value is reported and does not decide, so a "
			"scan whose only hit is that one is clean"_q;
		const auto result = runKept(what, MirroredFixture(true));
		const auto &r = result.reading;
		const auto &app = ClassOf(r, SecrecyClass::AppLog);
		const auto &debug = ClassOf(r, SecrecyClass::DebugLog);
		const auto reported = [](
				const SecrecyClassReading &c,
				const QString &site) {
			return (c.boundedOther == 0)
				&& (c.boundedComputed == 1)
				&& c.plainSites.empty()
				&& (c.computedSites == Sites{ { site, 1 } });
		};
		const auto row = u"SECRECY_COMPUTED: total=2 sites=[AppLog|"_q
			+ appSite
			+ u" x1, DebugLog|"_q
			+ partSite
			+ u" x1] (reported, non-deciding)"_q;
		Check(
			result.prepared
				&& r.decided
				&& r.clean
				&& (r.clientWritten() == 0)
				&& (r.computed() == 2)
				&& reported(app, appSite)
				&& reported(debug, partSite)
				&& SecrecyRows(r).contains(row),
			what,
			SiteDetails(app) + u" | "_q + SiteDetails(debug) + tail(r));
	}
	{
		const auto what = u"secrecy self-test: on the declared line the same "
			"value in another, undeclared field, in free text or in part of "
			"the declared field's value still fails the scan"_q;
		auto fixture = CleanFixture();
		fixture.computed.push_back(SelfDeclaration());
		fixture.parts[u"log_10_00.txt"_q] += QStringList{
			Entry(ComputedLine(pw, pw)),
			Entry(ComputedLine(u"17"_q, pw, u" retry "_q + pw)),
			Entry(ComputedLine(u"17"_q, u"v-"_q + pw)),
		};
		const auto result = runKept(what, fixture);
		const auto &r = result.reading;
		const auto &debug = ClassOf(r, SecrecyClass::DebugLog);
		const auto plain = Sites{
			{ part + head + kSiteNoField, 2 },
			{ part + head + u"span"_q, 1 },
		};
		Check(
			result.prepared
				&& r.decided
				&& !r.clean
				&& (r.clientWritten() == 3)
				&& (r.computed() == 2)
				&& (debug.plainSites == plain)
				&& (debug.computedSites == Sites{ { partSite, 2 } }),
			what,
			SiteDetails(debug) + tail(r));
	}
	{
		const auto what = u"secrecy self-test: a site that would print the "
			"hit or another secret, or a line with no field, is withheld and "
			"still fails the scan"_q;
		const auto result = runKept(what, SitesFixture());
		const auto &r = result.reading;
		const auto &app = ClassOf(r, SecrecyClass::AppLog);
		const auto &debug = ClassOf(r, SecrecyClass::DebugLog);
		const auto plain = Sites{
			{ part + u"|"_q + kSiteWithheld, 6 },
			{ part + head + kSiteNoField, 1 },
		};
		const auto leaks = SelfLeaksIn(SecrecyRows(r));
		Check(
			result.prepared
				&& r.decided
				&& !r.clean
				&& (r.computed() == 2)
				&& (r.clientWritten() == 9)
				&& (debug.boundedOther == 7)
				&& (debug.boundedComputed == 1)
				&& (debug.wordRunsOther == 1)
				&& (debug.tokensOther == 1)
				&& (debug.plainSites == plain)
				&& leaks.isEmpty(),
			what,
			SiteDetails(app)
				+ u" | "_q
				+ SiteDetails(debug)
				+ tail(r)
				+ u" hits=["_q
				+ leaks.join(u", "_q)
				+ u"]"_q);
	}
	struct Exempt {
		QString name;
		SecrecyClass cls = SecrecyClass::TestLog;
		Fn<void(SelfFixture&)> plant;
		Fn<int(const SecrecyClassReading&)> counter;
		QString sendSite; // the Send site the hit must be reported at
	};
	const auto exempts = std::vector<Exempt>{
		{
			u"word run"_q,
			SecrecyClass::DebugLog,
			[=](SelfFixture &f) {
				f.parts[u"log_10_00.txt"_q].push_back(Entry(ComputedLine(
					u"17"_q,
					a[4] + QChar(',') + a[5])));
			},
			[](const SecrecyClassReading &c) { return c.wordRunsOther; },
		},
		{
			u"token"_q,
			SecrecyClass::DebugLog,
			[=](SelfFixture &f) {
				f.parts[u"log_10_00.txt"_q].push_back(
					Entry(ComputedLine(u"17"_q, kSelfToken)));
			},
			[](const SecrecyClassReading &c) { return c.tokensOther; },
		},
		{
			u"Send-entry hit"_q,
			SecrecyClass::MtpLog,
			[=](SelfFixture &f) {
				InsertBeforePlanted(f.parts[u"mtp_10_00.txt"_q], SendEntry({
					u"    query: { messages_sendMessage"_q,
					u"      message: \""_q
						+ ComputedLine(u"17"_q, pw)
						+ u"\" [STRING]"_q,
					u"    }"_q,
				}));
			},
			[](const SecrecyClassReading &c) { return c.boundedSend; },
			u"messages_sendMessage/messages_sendMessage.message"_q,
		},
	};
	for (const auto &entry : exempts) {
		const auto what = u"secrecy self-test: a declaration exempts no "_q
			+ entry.name
			+ u", even in the declared field"_q;
		auto fixture = CleanFixture();
		fixture.computed.push_back(SelfDeclaration());
		entry.plant(fixture);
		const auto result = runKept(what, fixture);
		const auto &r = result.reading;
		const auto &c = ClassOf(r, entry.cls);
		Check(
			result.prepared
				&& r.decided
				&& !r.clean
				&& (r.clientWritten() == 1)
				&& (r.computed() == 0)
				&& (entry.counter(c) == 1)
				&& (entry.sendSite.isEmpty()
					|| c.sendSites.contains(entry.sendSite)),
			what,
			SiteDetails(c) + tail(r));
	}
}

void SelfUndecided(const std::shared_ptr<SelfState> &state) {
	struct Case {
		QString name;
		std::optional<SecrecyClass> cls; // nullopt: a scan-level reason
		QString reason;
		Fn<void(SelfFixture&)> change;
	};
	const auto cases = std::vector<Case>{
		{
			u"no test log file"_q,
			SecrecyClass::TestLog,
			u"no accepted file"_q,
			[](SelfFixture &f) { f.testLogFile = false; },
		},
		{
			u"an app log without the planted control"_q,
			SecrecyClass::AppLog,
			u"no planted control"_q,
			[](SelfFixture &f) { f.appLog.pop_back(); },
		},
		{
			u"an mtp_ part with Send headers only"_q,
			SecrecyClass::MtpLog,
			u"both directions"_q,
			[](SelfFixture &f) {
				auto mtp = QStringList{ kSelfToday };
				mtp += SendEntry({
					u"    query: { wallet_getState"_q,
					u"    }"_q,
				});
				mtp.push_back(MtpEntry(PlantedEntry()));
				f.parts[u"mtp_10_00.txt"_q] = mtp;
			},
		},
		{
			u"no secrets at all"_q,
			std::nullopt,
			u"no secret given"_q,
			[](SelfFixture &f) { f.secrets = SecrecySecrets(); },
		},
		{
			u"a 10-character token"_q,
			std::nullopt,
			u"shorter than 16 characters"_q,
			[](SelfFixture &f) { f.secrets.tokens.push_back(kSelfShortToken); },
		},
		{
			u"an earlier part with a bad name"_q,
			SecrecyClass::EarlierParts,
			u"invalid part name"_q,
			[](SelfFixture &f) {
				f.earlier.push_back({
					.name = u"log_9_00.txt"_q,
					.day = kSelfDay,
					.control = kSelfEarlierControl,
				});
			},
		},
		{
			u"an earlier part that is missing"_q,
			SecrecyClass::EarlierParts,
			u"missing part"_q,
			[](SelfFixture &f) {
				f.earlier.push_back({
					.name = u"log_08_45.txt"_q,
					.day = kSelfDay,
					.control = kSelfEarlierControl,
				});
			},
		},
	};
	for (const auto &entry : cases) {
		const auto what = u"secrecy self-test: "_q
			+ entry.name
			+ u" is refused as undecided"_q;
		auto fixture = CleanFixture();
		entry.change(fixture);
		const auto run = RunFixture(fixture);
		CheckPrepared(what, run);
		Keep(state, run);
		const auto &r = run.reading;
		auto named = false;
		auto details = QString();
		if (entry.cls) {
			const auto &c = ClassOf(r, *entry.cls);
			named = !c.decided && c.undecidedReason.contains(entry.reason);
			details = Counts(c);
		} else {
			named = r.undecidedReasons.join(u"; "_q).contains(entry.reason);
		}
		Check(
			run.prepared && !r.decided && !r.clean && named,
			what,
			details + u" | "_q + Summary(r));
	}
}

[[nodiscard]] QStringList LeakLines() {
	const auto a = SelfPhraseA();
	return {
		Entry(u"leak: "_q + kSelfPassword),
		Entry(u"ref="_q + kSelfToken),
		Entry(u"words: "_q + a[6] + QChar(' ') + a[7]),
	};
}

[[nodiscard]] SelfFixture IdentityFixture(
		bool nextQuarterToday,
		bool withBanner) {
	auto result = CleanFixture();
	auto appended = QStringList{ kSelfToday };
	appended += LeakLines();
	if (withBanner) {
		appended += QStringList{ kSelfBannerRule, kBanner, kSelfBannerRule };
	}
	appended.push_back(Entry(kSelfFiller + u" debug line"_q));
	appended.push_back(Entry(PlantedEntry()));
	result.parts[u"log_10_00.txt"_q] = appended;

	const auto nextDay = nextQuarterToday ? kSelfToday : kSelfYesterday;
	result.parts[u"log_10_15.txt"_q] = QStringList{ nextDay } + LeakLines();
	auto mtp = QStringList{ nextDay };
	mtp += SendEntry({
		u"    query: { account_password"_q,
		u"      hint: \""_q + kSelfPassword + u"\" [STRING]"_q,
		u"    }"_q,
	});
	result.parts[u"mtp_10_15.txt"_q] = mtp;

	result.parts[u"log_11_00.txt"_q] = QStringList{ kSelfToday } + LeakLines();
	return result;
}

void SelfIdentity(const std::shared_ptr<SelfState> &state) {
	{
		const auto what = u"secrecy self-test: a same-name part of another "
			"day is foreign and not scanned, a part outside the window is "
			"no candidate, and an appended part is read from its last "
			"banner"_q;
		const auto run = RunFixture(IdentityFixture(false, true));
		CheckPrepared(what, run);
		Keep(state, run);
		const auto &r = run.reading;
		const auto &debug = ClassOf(r, SecrecyClass::DebugLog);
		const auto &mtp = ClassOf(r, SecrecyClass::MtpLog);
		const auto names = (debug.names + mtp.names).join(u", "_q);
		const auto sliced = (debug.slicedFrom.size() == 1)
			&& (debug.slicedFrom.front() > 1);
		Check(
			run.prepared
				&& (r.candidates == 4)
				&& (debug.foreign == 1)
				&& (mtp.foreign == 1)
				&& (debug.files == 1)
				&& (mtp.files == 1)
				&& !names.contains(u"11_00"_q)
				&& r.candidateIdentities.contains(
					u"DebugLogs/log_10_15.txt=foreign"_q)
				&& sliced
				&& r.decided
				&& r.clean,
			what,
			u"candidates=%1 identities=[%2] | %3 | %4 | %5"_q
				.arg(r.candidates)
				.arg(r.candidateIdentities.join(u", "_q))
				.arg(Counts(debug))
				.arg(Counts(mtp))
				.arg(Summary(r)));
	}
	{
		const auto what = u"secrecy self-test (control): the same next "
			"quarter parts carrying this day's index are scanned and their "
			"leaks count"_q;
		const auto run = RunFixture(IdentityFixture(true, true));
		CheckPrepared(what, run);
		Keep(state, run);
		const auto &r = run.reading;
		const auto &debug = ClassOf(r, SecrecyClass::DebugLog);
		const auto &mtp = ClassOf(r, SecrecyClass::MtpLog);
		Check(
			run.prepared
				&& (debug.files == 2)
				&& (debug.clientWritten() == 3)
				&& (mtp.boundedSend == 1)
				&& !r.clean,
			what,
			Counts(debug) + u" | "_q + Counts(mtp) + u" | "_q + Summary(r));
	}
	{
		const auto what = u"secrecy self-test (control): the appended part "
			"without a banner is scanned whole and its earlier lines "
			"count"_q;
		const auto run = RunFixture(IdentityFixture(false, false));
		CheckPrepared(what, run);
		Keep(state, run);
		const auto &r = run.reading;
		const auto &debug = ClassOf(r, SecrecyClass::DebugLog);
		Check(
			run.prepared
				&& (debug.slicedFrom.size() == 1)
				&& (debug.slicedFrom.front() == 0)
				&& (debug.clientWritten() == 3)
				&& !r.clean,
			what,
			Counts(debug) + u" | "_q + Summary(r));
	}
	const auto earlierAccepted = SecrecyEarlierPart{
		.name = u"log_09_00.txt"_q,
		.day = kSelfDay,
		.control = kSelfEarlierControl,
	};
	const auto earlierFixture = [&](bool withForeign) {
		auto result = CleanFixture();
		result.parts[u"log_09_00.txt"_q] = QStringList{
			kSelfToday,
			Entry(kSelfFiller + u" earlier launch"_q),
			Entry(kSelfEarlierControl),
		};
		result.earlier.push_back(earlierAccepted);
		if (withForeign) {
			result.parts[u"log_09_15.txt"_q] = QStringList{ kSelfYesterday }
				+ LeakLines();
			result.earlier.push_back({
				.name = u"log_09_15.txt"_q,
				.day = kSelfDay,
				.control = kSelfEarlierControl,
			});
		}
		return result;
	};
	{
		const auto what = u"secrecy self-test: a named earlier part with its "
			"day's index and its own control is accepted"_q;
		const auto run = RunFixture(earlierFixture(false));
		CheckPrepared(what, run);
		Keep(state, run);
		const auto &c = ClassOf(run.reading, SecrecyClass::EarlierParts);
		Check(
			run.prepared
				&& (run.reading.classes.size() == 5)
				&& (c.files == 1)
				&& (c.controlHits == 1)
				&& c.decided
				&& run.reading.decided
				&& run.reading.clean,
			what,
			Counts(c) + u" | "_q + Summary(run.reading));
	}
	{
		const auto what = u"secrecy self-test: a named earlier part of "
			"another day is foreign and makes the class undecided"_q;
		const auto run = RunFixture(earlierFixture(true));
		CheckPrepared(what, run);
		Keep(state, run);
		const auto &c = ClassOf(run.reading, SecrecyClass::EarlierParts);
		Check(
			run.prepared
				&& (c.files == 1)
				&& (c.foreign == 1)
				&& !c.decided
				&& c.undecidedReason.contains(u"foreign part"_q)
				&& (c.clientWritten() == 0)
				&& !run.reading.decided,
			what,
			Counts(c) + u" | "_q + Summary(run.reading));
	}
}

void SelfPrintsNothing(const std::shared_ptr<SelfState> &state) {
	auto rows = QStringList();
	auto readings = 0;
	for (const auto &reading : state->readings) {
		rows += SecrecyRows(reading);
		++readings;
	}
	const auto memoryLeaks = SelfLeaksIn(rows);
	// The leak check must have examined rows that do print sites: the
	// computed row, the withheld sites and a printed head with "-".
	const auto joined = rows.join(QChar('\n'));
	const auto computedRow = u"SECRECY_COMPUTED: total=2 "
		"sites=[AppLog|log.txt|"_q;
	const auto sitesPrinted = joined.contains(computedRow)
		&& joined.contains(u"|?|? x6"_q)
		&& joined.contains(u"|"_q + kSelfComputedHead + u"|- x1"_q);
	Check(
		(readings >= 15)
			&& !rows.isEmpty()
			&& memoryLeaks.isEmpty()
			&& sitesPrinted,
		u"secrecy self-test: no row of a clean, dirty or undecided reading "
		"carries a synthetic secret, word, pair, token or scanned line"_q,
		u"readings=%1 rows=%2 sitesPrinted=%3 hits=[%4]"_q
			.arg(readings)
			.arg(rows.size())
			.arg(B(sitesPrinted))
			.arg(memoryLeaks.join(u", "_q)));

	const auto prepared = u"secrecy self-test: rows written to the test log"_q;
	const auto run = RunFixture(CleanFixture());
	CheckPrepared(prepared, run);
	const auto sites = RunFixture(SitesFixture());
	CheckPrepared(prepared, sites);
	const auto path = TestLogPath();
	const auto mark = QFileInfo(path).size();
	auto written = SecrecyRows(run.reading);
	written += SecrecyRows(sites.reading);
	for (const auto &row : written) {
		Note(row);
	}
	const auto appended = AppendedSince(path, mark);
	const auto fileLeaks = SelfLeaksIn({ appended });
	Check(
		run.prepared
			&& sites.prepared
			&& !appended.isEmpty()
			&& appended.contains(u"SECRECY_SCAN:"_q)
			&& appended.contains(u"SECRECY_COMPUTED: total=2 "_q)
			&& appended.contains(u"|?|? x6"_q)
			&& fileLeaks.isEmpty(),
		u"secrecy self-test: the rows as written to test_log.txt carry no "
		"synthetic secret either"_q,
		u"appendedBytes=%1 rows=%2 hits=[%3]"_q
			.arg(appended.toUtf8().size())
			.arg(written.size())
			.arg(fileLeaks.join(u", "_q)));
}

}

}

#endif
