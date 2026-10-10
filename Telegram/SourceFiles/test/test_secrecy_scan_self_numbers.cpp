#include "test/test_secrecy_scan_internal.h"

#ifdef _DEBUG

#include "test/test_animation_clock.h"
#include "test/test_post_paint.h"

#include <cmath>
#include <limits>

namespace Test {

namespace SecrecyScanDetails {}

using namespace SecrecyScanDetails;

namespace SecrecyScanDetails {
namespace {

constexpr auto kSelfTelemetryValue = -4821.75;
constexpr auto kSelfTelemetryDecimals = 2;
const auto kSelfTelemetryHead = u"NOTE: Selftest telemetry"_q;
// The digit runs of kSelfTelemetryValue's ordinary decimal.
const auto kSelfTelemetrySecrets = std::vector<QString>{
	u"4821"_q,
	u"75"_q,
};

// One TelemetryNumber case: the input and the exact text it must print.
struct TelemetryCase {
	double value = 0.;
	int decimals = 0;
	QString expected;
};

// One table for the exact-output and the digit-run checks: zeros of both
// signs, integers up to 2^53 - 1, negatives, fractions with a carry, a
// negative value that rounds to zero, many places, a negative |decimals|
// and the non-finite values. No case sits on an exact rounding half.
[[nodiscard]] std::vector<TelemetryCase> TelemetryCases() {
	const auto notANumber = std::numeric_limits<double>::quiet_NaN();
	const auto infinity = std::numeric_limits<double>::infinity();
	return {
		{ 0., 0, u"0p"_q },
		{ -0., 0, u"0p"_q },
		{ 7., 0, u"7p"_q },
		{ 2147483647., 0, u"2147483647p"_q },
		{ 9007199254740991., 0, u"9007199254740991p"_q },
		{ -1., 0, u"-1p"_q },
		{ -320., 0, u"-320p"_q },
		{ 0.5, 1, u"0p5"_q },
		{ 1.25, 2, u"1p25"_q },
		{ 16.31, 2, u"16p31"_q },
		{ 99.996, 2, u"100p00"_q },
		{ kSelfTelemetryValue, kSelfTelemetryDecimals, u"-4821p75"_q },
		{ -0.004, 2, u"-0p00"_q },
		{ -0.25, 3, u"-0p250"_q },
		{ 123456789.0625, 4, u"123456789p0625"_q },
		{ 3.0625, 12, u"3p062500000000"_q },
		{ 1.75, -1, u"2p"_q },
		{ notANumber, 2, u"nan"_q },
		{ infinity, 2, u"inf"_q },
		{ -infinity, 2, u"-inf"_q },
	};
}

// The one telemetry row shape, as an overlay's Note prints it: a plain
// test-log line whose value is the whole "value" field.
[[nodiscard]] QString TelemetryRow(const QString &value) {
	return kSelfTelemetryHead + u" value="_q + value;
}

// The clean tree plus |row| as one more test-log line, with |secrets| as
// extra synthetic short secrets, so that row is their only candidate.
[[nodiscard]] SelfFixture RowFixture(
		const QString &row,
		const std::vector<QString> &secrets) {
	auto result = CleanFixture();
	for (const auto &secret : secrets) {
		result.secrets.shortSecrets.push_back(secret);
	}
	InsertBeforePlanted(result.testLog, QStringList{ row });
	return result;
}

// The clean tree plus one telemetry row printing |value|, with the digit
// runs of the control value as extra synthetic short secrets, so that row
// is the scan's only candidate.
[[nodiscard]] SelfFixture TelemetryFixture(const QString &value) {
	return RowFixture(TelemetryRow(value), kSelfTelemetrySecrets);
}

// Every distinct non-empty digits-only substring of |text|, in order of
// first appearance: each is a short secret a formatted number could
// coincide with, so each must be held only embedded.
[[nodiscard]] std::vector<QString> DigitRuns(const QString &text) {
	auto result = std::vector<QString>();
	auto seen = QSet<QString>();
	const auto size = int(text.size());
	for (auto from = 0; from != size; ++from) {
		for (auto till = from; till != size && text[till].isDigit(); ++till) {
			const auto run = text.mid(from, till + 1 - from);
			if (!seen.contains(run)) {
				seen.insert(run);
				result.push_back(run);
			}
		}
	}
	return result;
}

// One row through the scan's own matcher, alone: no canary, planted
// control or other line, whose framing would leave some digit secrets
// undecided in a whole ReadSecrecy.
[[nodiscard]] SecrecyClassReading ScanRow(
		const QString &row,
		std::vector<QString> secrets) {
	const auto matcher = SecrecyMatcher({
		.shortSecrets = std::move(secrets),
	});
	auto result = SecrecyClassReading();
	ScanText(row, matcher, {}, {}, result, u"test_log.txt"_q);
	return result;
}

// Caller text for the converted helper rows, synthetic and letters only,
// with no phrase word.
const auto kSelfHelperSlots = std::vector<QString>{
	u"Selftest helper"_q,
	u"Selftest subject"_q,
	u"Selftest control"_q,
	u"Selftest rows"_q,
};

// The same slots made of SelfPhraseA() words: each slot holds one pair, and
// adjacent slots meet at another pair (cactus dolphin, lobster volcano,
// mango velvet), so any converted number placed directly between two slots
// would change the word-run count.
const auto kSelfHelperPhraseSlots = std::vector<QString>{
	u"walnut giraffe cactus"_q,
	u"dolphin pumpkin lobster"_q,
	u"volcano oyster mango"_q,
	u"velvet kangaroo jaguar"_q,
};

// Synthetic values of the converted rows. None equals a digit run the
// clean fixture or the canaries hold bounded in client-written text; the
// flag 1 is bounded there only in the Recv entry, which never decides.
const auto kSelfHelperSample = PostPaintSample{
	.seq = 4821,
	.paintAt = 37645,
	.lastPaintAt = 37693,
	.covered = 59,
	.lo = 61482,
	.hi = 61537,
};
const auto kSelfHelperStop = PostPaintStop{
	.reason = u"owner-destroyed"_q,
	.seq = 4821,
	.pending = true,
	.samples = 3764,
	.paints = 5937,
	.ignored = 6148,
	.dropped = 7293,
};
const auto kSelfHelperFrame = ClockedFrame{
	.index = 36,
	.requested = 4821,
	.tick = 37645,
	.reachedLo = 3764,
	.reachedHi = 4829,
	.waited = 5937,
	.probeTicks = 41,
	.renderTicks = 53,
};

// The distinct maximal digit runs of |text|, in order of first appearance:
// the numbers an ordinary row prints, each as a synthetic short secret.
[[nodiscard]] std::vector<QString> MaximalDigitRuns(const QString &text) {
	auto result = std::vector<QString>();
	const auto size = int(text.size());
	auto from = 0;
	while (from < size) {
		if (!text[from].isDigit()) {
			++from;
			continue;
		}
		auto till = from + 1;
		while (till < size && text[till].isDigit()) {
			++till;
		}
		const auto run = text.mid(from, till - from);
		if (!ranges::contains(result, run)) {
			result.push_back(run);
		}
		from = till;
	}
	return result;
}

// |text| without each "p" that directly follows a digit and is not
// followed by a letter or digit: the point of a TelemetryNumber with no
// fraction digits, so a requested row reads back as its ordinary text.
[[nodiscard]] QString StripTelemetryPoints(const QString &text) {
	auto result = QString();
	const auto size = int(text.size());
	for (auto i = 0; i != size; ++i) {
		const auto point = (text[i] == QChar('p'))
			&& (i > 0)
			&& text[i - 1].isDigit()
			&& ((i + 1 == size) || !text[i + 1].isLetterOrNumber());
		if (!point) {
			result += text[i];
		}
	}
	return result;
}

// The SelfPhraseA() word runs the scan's own matcher counts in |row|.
[[nodiscard]] int PhraseRuns(const QString &row) {
	const auto matcher = SecrecyMatcher({ .phrases = { SelfPhraseA() } });
	auto reading = SecrecyClassReading();
	ScanText(row, matcher, {}, {}, reading);
	return reading.wordRunsOther;
}

// The walk of the tally and refusal rows: |slots| name it, its subject and
// its control; 4821 items examined, 37 subjects and |controls| controls.
[[nodiscard]] DiscriminatingScan HelperScan(
		const std::vector<QString> &slots,
		int controls) {
	auto result = DiscriminatingScan(slots[0], slots[1], slots[2]);
	result.examined(4821);
	for (auto i = 0; i != 37; ++i) {
		result.matchedSubject();
	}
	for (auto i = 0; i != controls; ++i) {
		result.matchedControl();
	}
	return result;
}

// The refusal of a request named slots[0] that renders nothing: it has an
// action, also a handler when |handler|, and asks for |elapsed|.
[[nodiscard]] QString HelperRequestRefusal(
		const std::vector<QString> &slots,
		bool handler,
		std::vector<crl::time> elapsed,
		NumberFormat format) {
	auto request = ClockedRequest{
		.name = slots[0],
		.action = [] {},
		.elapsed = std::move(elapsed),
		.render = [](const ClockedFrame &) {},
	};
	if (handler) {
		request.handler = std::make_shared<LambdaClickHandler>([] {});
	}
	return ClockedRequestRefusal(request, format);
}

// The window of probe slots[1] holding the rows slots[2] and slots[3].
[[nodiscard]] QString HelperProbeWindow(
		const std::vector<QString> &slots,
		NumberFormat format) {
	return ProbeWindowText(
		slots[1],
		4821,
		5937,
		{ slots[2], slots[3] },
		format);
}

// A paired round trip keyed slots[1] that discarded slots[2], over an
// empty window of probe slots[3].
[[nodiscard]] QString HelperRoundTrip(
		const std::vector<QString> &slots,
		NumberFormat format) {
	const auto trip = RoundTrip{
		.state = RoundTripState::Paired,
		.issueAtMs = 37645,
		.answerAtMs = 37693,
		.roundTripMs = 48,
		.issues = 41,
		.answers = 53,
		.dropped = 52,
		.preIssues = 67,
		.preAnswers = 79,
	};
	return RoundTripText(
		slots[1],
		trip,
		{ slots[2] },
		ProbeWindowText(slots[3], 7293, 8352, {}, format),
		format);
}

// One converted shared-helper row: |line| frames its formatter's output as
// the helper's logging call writes it, for caller text |slots| and numbers
// in |format|, and |ordinary| is today's text for kSelfHelperSlots.
struct HelperRowCase {
	QString label;
	Fn<QString(const std::vector<QString> &slots, NumberFormat format)> line;
	QString ordinary;
};

// Every converted row text. A framing prefix carries no digit: the
// numbers, their field words and their order come only from the helper's
// own formatter.
[[nodiscard]] std::vector<HelperRowCase> HelperRowCases() {
	using Slots = std::vector<QString>;
	return {
		{
			u"geometry"_q,
			[](const Slots &s, NumberFormat f) {
				return GeometryText(s[0], QRect(-4821, 3764, 5937, 6148), f);
			},
			u"GEOMETRY: Selftest helper: x=-4821 y=3764 w=5937 h=6148"_q,
		},
		{
			u"near"_q,
			[](const Slots &s, NumberFormat f) {
				return u"TEST_RESULT: PASS: "_q
					+ CheckNearText(4826, 4821, 37, s[0], f);
			},
			u"TEST_RESULT: PASS: Selftest helper (actual 4826, expected "
			u"4821 ±37)"_q,
		},
		{
			u"sample"_q,
			[](const Slots &s, NumberFormat f) {
				return u"NOTE: "_q
					+ s[0]
					+ u": "_q
					+ PostPaintSampleText(kSelfHelperSample, f);
			},
			u"NOTE: Selftest helper: kind=post seq=4821 paint=37645 "
			"last=37693 covered=59 lo=61482 hi=61537"_q,
		},
		{
			u"stop"_q,
			[](const Slots &s, NumberFormat f) {
				return u"NOTE: "_q
					+ PostPaintStopText(s[0], kSelfHelperStop, f);
			},
			u"NOTE: Selftest helper: kind=stop reason=owner-destroyed "
			"seq=4821 pending=1 samples=3764 paints=5937 ignored=6148 "
			"dropped=7293"_q,
		},
		{
			u"frame"_q,
			[](const Slots &s, NumberFormat f) {
				return u"NOTE: "_q
					+ s[0]
					+ u": "_q
					+ ClockedFrameText(kSelfHelperFrame, f);
			},
			u"NOTE: Selftest helper: kind=frame index=37 requested=4821 "
			"reached=[3764,4829] tick=37645 waited=5937 probeTicks=41 "
			"renderTicks=53"_q,
		},
		{
			u"end gate"_q,
			[](const Slots &s, NumberFormat f) {
				return u"TEST_RESULT: N/A: "_q
					+ ClockedFrameSubject(s[0], kSelfHelperFrame, f)
					+ u" - "_q
					+ ClockedEndGateText(4821, 3764, f);
			},
			u"TEST_RESULT: N/A: Selftest helper: frame 37 at 4821 ms - "
			"requested 4821 ms is at or after the declared animation end "
			"3764 ms: no in-progress frame exists to judge, so nothing was "
			"waited for, ticked or rendered"_q,
		},
		{
			u"reached gate"_q,
			[](const Slots &s, NumberFormat f) {
				return u"TEST_RESULT: N/A: "_q
					+ ClockedFrameSubject(s[0], kSelfHelperFrame, f)
					+ u" - "_q
					+ ClockedReachedGateText(5937, 6148, 6143, f);
			},
			u"TEST_RESULT: N/A: Selftest helper: frame 37 at 4821 ms - the "
			"tick reached [5937,6148] ms, at or after the declared animation "
			"end 6143 ms: the animation may have finished, so no "
			"in-progress frame exists to judge; the frame was not "
			"rendered"_q,
		},
		{
			u"no tick"_q,
			[](const Slots &s, NumberFormat f) {
				return u"TEST_RESULT: FAIL: fixture gate: "_q
					+ ClockedFrameSubject(s[0], kSelfHelperFrame, f)
					+ u" - "_q
					+ ClockedNoTickText(59, 7293, 8352, f);
			},
			u"TEST_RESULT: FAIL: fixture gate: Selftest helper: frame 37 at "
			"4821 ms - manager-did-not-tick: "
			"Core::App().animationManager().update() did not call the probe "
			"animation (probe calls 59 before and after it, 7293 ms after "
			"the action started, waited 8352 ms): the manager skipped the "
			"tick - a schedule callback still pending (_scheduled; under "
			"Manager::SetScheduleWithInvokeQueued(true) it is an "
			"InvokeQueued call the postponed-call drain does not run) or an "
			"update in progress (_updating); the frame was not rendered"_q,
		},
		{
			u"render tick"_q,
			[](const Slots &s, NumberFormat f) {
				return u"TEST_RESULT: FAIL: fixture gate: "_q
					+ ClockedFrameSubject(s[0], kSelfHelperFrame, f)
					+ u" - "_q
					+ ClockedRenderTickText(41, f);
			},
			u"TEST_RESULT: FAIL: fixture gate: Selftest helper: frame 37 at "
			"4821 ms - render-advanced-the-manager: the probe animation was "
			"called 41 time(s) during the render, so the frame may show a "
			"value past its reached elapsed bounds; render synchronously "
			"(Test::GrabWidget / GrabRect), never through repaint() or "
			"processEvents()"_q,
		},
		{
			u"request flags"_q,
			[](const Slots &s, NumberFormat f) {
				return u"TEST_RESULT: FAIL: fixture gate: "_q
					+ s[0]
					+ u": clocked frames refused - "_q
					+ HelperRequestRefusal(s, true, { 4821 }, f);
			},
			u"TEST_RESULT: FAIL: fixture gate: Selftest helper: clocked "
			"frames refused - exactly one of action and handler must be set "
			"(action=1 handler=1)"_q,
		},
		{
			u"request elapsed"_q,
			[](const Slots &s, NumberFormat f) {
				return u"TEST_RESULT: FAIL: fixture gate: "_q
					+ s[0]
					+ u": clocked frames refused - "_q
					+ HelperRequestRefusal(s, false, { 4821, 3764 }, f);
			},
			u"TEST_RESULT: FAIL: fixture gate: Selftest helper: clocked "
			"frames refused - elapsed times must be non-negative and "
			"strictly increasing, read 4821,3764"_q,
		},
		{
			u"probe window"_q,
			[](const Slots &s, NumberFormat f) {
				return u"TEST_RESULT: PASS: "_q
					+ s[0]
					+ u" - "_q
					+ HelperProbeWindow(s, f);
			},
			u"TEST_RESULT: PASS: Selftest helper - probe=Selftest subject "
			"window=[4821,5937) rows=Selftest control; Selftest rows"_q,
		},
		{
			u"probe count"_q,
			[](const Slots &s, NumberFormat f) {
				return u"TEST_RESULT: PASS: "_q
					+ s[0]
					+ u" - "_q
					+ ProbeCountText(6148, 6148, HelperProbeWindow(s, f), f);
			},
			u"TEST_RESULT: PASS: Selftest helper - expected=6148 "
			"actual=6148 probe=Selftest subject window=[4821,5937) "
			"rows=Selftest control; Selftest rows"_q,
		},
		{
			u"round trip"_q,
			[](const Slots &s, NumberFormat f) {
				return u"TEST_RESULT: PASS: "_q
					+ s[0]
					+ u" - "_q
					+ HelperRoundTrip(s, f);
			},
			u"TEST_RESULT: PASS: Selftest helper - key=Selftest subject "
			"state=paired issues=41 answers=53 dropped=52 preIssues=67 "
			"preAnswers=79 issueAtMs=37645 answerAtMs=37693 roundTripMs=48 "
			"discarded=Selftest control probe=Selftest rows "
			"window=[7293,8352) rows=<none>"_q,
		},
		{
			u"scan tally"_q,
			[](const Slots &s, NumberFormat f) {
				return u"NOTE: "_q + HelperScan(s, 59).tallyText(f);
			},
			u"NOTE: Selftest helper: examined=4821 subject(Selftest "
			"subject)=37 control(Selftest control)=59"_q,
		},
		{
			u"scan refusal"_q,
			[](const Slots &s, NumberFormat f) {
				return u"TEST_RESULT: FAIL: "_q
					+ s[0]
					+ u" discriminates - "_q
					+ HelperScan(s, 0).refusalText(f);
			},
			u"TEST_RESULT: FAIL: Selftest helper discriminates - the walk "
			"matched no Selftest control, so its Selftest subject count of "
			"37 over 4821 examined items cannot tell absence from an "
			"enumeration that never reaches the subject"_q,
		},
	};
}

// One case's texts, each built once: both formats for kSelfHelperSlots
// and for kSelfHelperPhraseSlots, and the ordinary text's digit runs.
struct HelperRows {
	QString ordinary;
	QString telemetry;
	QString phraseOrdinary;
	QString phraseTelemetry;
	std::vector<QString> runs;
};

[[nodiscard]] HelperRows BuildHelperRows(const HelperRowCase &entry) {
	auto result = HelperRows{
		.ordinary = entry.line(kSelfHelperSlots, NumberFormat::Ordinary),
		.telemetry = entry.line(kSelfHelperSlots, NumberFormat::Telemetry),
		.phraseOrdinary = entry.line(
			kSelfHelperPhraseSlots,
			NumberFormat::Ordinary),
		.phraseTelemetry = entry.line(
			kSelfHelperPhraseSlots,
			NumberFormat::Telemetry),
	};
	result.runs = MaximalDigitRuns(result.ordinary);
	return result;
}

// Whether |telemetry| differs from |ordinary| only by a "p" after each
// number, and by at least one.
[[nodiscard]] bool PointOnly(
		const QString &telemetry,
		const QString &ordinary) {
	return (telemetry != ordinary)
		&& (StripTelemetryPoints(telemetry) == ordinary);
}

} // namespace

// Overlay telemetry: the digit runs of an ordinary decimal, as synthetic
// short secrets, decide at the row's plain-line site with field "-", and
// the same value as a TelemetryNumber holds them only embedded. Keeps no
// reading, so the earlier stages' rows and readings stay as they were.
void SelfTelemetry() {
	using Sites = std::map<QString, int>;
	const auto ordinary = QString::number(
		kSelfTelemetryValue,
		'f',
		kSelfTelemetryDecimals);
	const auto telemetry = TelemetryNumber(
		kSelfTelemetryValue,
		kSelfTelemetryDecimals);
	const auto site = u"test_log.txt|"_q
		+ kSelfTelemetryHead
		+ u"|"_q
		+ kSiteNoField;
	const auto secrets = int(kSelfTelemetrySecrets.size());
	{
		const auto what = u"secrecy self-test: the digit runs of an "
			"ordinary decimal telemetry value, as synthetic short secrets, "
			"fail the scan at the row's plain-line site with field -"_q;
		const auto run = RunFixture(TelemetryFixture(ordinary));
		CheckPrepared(what, run);
		const auto &r = run.reading;
		const auto &t = ClassOf(r, SecrecyClass::TestLog);
		const auto rows = SecrecyRows(r).join(QChar('\n'));
		Check(
			run.prepared
				&& r.decided
				&& !r.clean
				&& (r.clientWritten() == secrets)
				&& (r.computed() == 0)
				&& (t.bounded == secrets)
				&& (t.boundedOther == secrets)
				&& (t.embedded == 0)
				&& (t.plainSites == Sites{ { site, secrets } })
				&& rows.contains(u"plainSites=["_q
					+ site
					+ u" x"_q
					+ QString::number(secrets)
					+ u"]"_q),
			what,
			SiteDetails(t) + u" | "_q + Summary(r));
	}
	{
		const auto what = u"secrecy self-test: the same value as a "
			"TelemetryNumber holds those secrets only embedded, so a scan "
			"whose only candidate is that row is clean"_q;
		const auto run = RunFixture(TelemetryFixture(telemetry));
		CheckPrepared(what, run);
		const auto &r = run.reading;
		const auto &t = ClassOf(r, SecrecyClass::TestLog);
		Check(
			run.prepared
				&& r.decided
				&& r.clean
				&& (r.clientWritten() == 0)
				&& (r.computed() == 0)
				&& (t.bounded == 0)
				&& (t.embedded == secrets)
				&& t.plainSites.empty(),
			what,
			SiteDetails(t) + u" | "_q + Summary(r));
	}
	const auto cases = TelemetryCases();
	const auto count = int(cases.size());
	{
		// The outputs print in case order: the evidence parses them back.
		auto outputs = QStringList();
		auto failed = QStringList();
		for (auto i = 0; i != count; ++i) {
			const auto &entry = cases[i];
			const auto out = TelemetryNumber(entry.value, entry.decimals);
			outputs.push_back(out);
			if (out != entry.expected) {
				failed.push_back(QString::number(i));
			}
		}
		Check(
			failed.isEmpty(),
			u"secrecy self-test: TelemetryNumber prints every case as "
			"documented"_q,
			u"cases=%1 outputs=[%2] failed=[%3]"_q
				.arg(count)
				.arg(outputs.join(u", "_q))
				.arg(failed.join(u", "_q)));
	}
	{
		// The rule side reads every digit run of each output; the control
		// side proves the same row shape does count a run of an ordinary
		// decimal, so a zero on the rule side is not the row's doing.
		auto finite = 0;
		auto digitRuns = 0;
		auto ruleBounded = 0;
		auto ruleEmbedded = 0;
		auto ordinaryWithBoundedRun = 0;
		auto failing = QStringList();
		for (auto i = 0; i != count; ++i) {
			const auto &entry = cases[i];
			const auto out = TelemetryNumber(entry.value, entry.decimals);
			const auto runs = DigitRuns(out);
			const auto rule = ScanRow(TelemetryRow(out), runs);
			digitRuns += int(runs.size());
			ruleBounded += rule.bounded;
			ruleEmbedded += rule.embedded;
			auto ok = (rule.bounded == 0)
				&& (rule.embedded >= int(runs.size()));
			if (std::isfinite(entry.value)) {
				++finite;
				const auto text = QString::number(
					entry.value,
					'f',
					std::max(entry.decimals, 0));
				const auto plain = ScanRow(
					TelemetryRow(text),
					DigitRuns(text));
				if (plain.bounded >= 1) {
					++ordinaryWithBoundedRun;
				} else {
					ok = false;
				}
			}
			if (!ok) {
				failing.push_back(QString::number(i));
			}
		}
		Check(
			failing.isEmpty() && (finite > 0) && (digitRuns > 0),
			u"secrecy self-test: no digit run of any TelemetryNumber is "
			"bounded, while the ordinary decimal of every finite value holds "
			"a bounded one"_q,
			u"cases=%1 finite=%2 digitRuns=%3 ruleBounded=%4 "
			"ruleEmbedded=%5 ordinaryWithBoundedRun=%6/%2 failing=[%7]"_q
				.arg(count)
				.arg(finite)
				.arg(digitRuns)
				.arg(ruleBounded)
				.arg(ruleEmbedded)
				.arg(ordinaryWithBoundedRun)
				.arg(failing.join(u", "_q)));
	}
}

// The shared helpers' numbers: every converted row read through its own
// formatter for fixed synthetic values, in both formats. Never asks the
// process-wide request and keeps no reading, so the earlier stages' rows
// and readings stay as they were.
void SelfHelperNumbers() {
	const auto cases = HelperRowCases();
	const auto count = int(cases.size());
	auto rows = std::vector<HelperRows>();
	for (const auto &entry : cases) {
		rows.push_back(BuildHelperRows(entry));
	}
	{
		auto failing = QStringList();
		for (auto i = 0; i != count; ++i) {
			if (rows[i].ordinary != cases[i].ordinary) {
				failing.push_back(cases[i].label);
			}
		}
		Check(
			(count > 0) && failing.isEmpty(),
			u"secrecy self-test: without the request every converted helper "
			"row prints today's text"_q,
			u"rows=%1 equal=%2 failing=[%3]"_q
				.arg(count)
				.arg(count - int(failing.size()))
				.arg(failing.join(u", "_q)));
	}
	{
		auto failing = QStringList();
		for (auto i = 0; i != count; ++i) {
			const auto &row = rows[i];
			if (!PointOnly(row.telemetry, row.ordinary)
				|| !PointOnly(row.phraseTelemetry, row.phraseOrdinary)) {
				failing.push_back(cases[i].label);
			}
		}
		Check(
			(count > 0) && failing.isEmpty(),
			u"secrecy self-test: under the request every converted helper "
			"row differs from today's text only by a p after each number"_q,
			u"rows=%1 pointOnly=%2 failing=[%3]"_q
				.arg(count)
				.arg(count - int(failing.size()))
				.arg(failing.join(u", "_q)));
	}
	{
		const auto what = u"secrecy self-test: the digit runs of each "
			"converted helper row's ordinary text, as synthetic short "
			"secrets, fail the scan at that row"_q;
		auto secrets = 0;
		auto boundedAlone = 0;
		auto clientWritten = 0;
		auto failing = QStringList();
		for (auto i = 0; i != count; ++i) {
			const auto &row = rows[i];
			const auto runs = int(row.runs.size());
			const auto alone = ScanRow(row.ordinary, row.runs);
			const auto run = RunFixture(RowFixture(row.ordinary, row.runs));
			CheckPrepared(what, run);
			const auto &r = run.reading;
			const auto &t = ClassOf(r, SecrecyClass::TestLog);
			auto sites = 0;
			for (const auto &[site, hits] : t.plainSites) {
				sites += hits;
			}
			secrets += runs;
			boundedAlone += alone.boundedOther;
			clientWritten += r.clientWritten();
			const auto ok = run.prepared
				&& r.decided
				&& !r.clean
				&& (alone.bounded >= runs)
				&& (alone.boundedOther == alone.bounded)
				&& (t.boundedOther == alone.boundedOther)
				&& (sites == t.boundedOther)
				&& (r.clientWritten() == alone.boundedOther);
			if (!ok) {
				failing.push_back(cases[i].label);
			}
		}
		Check(
			(count > 0) && failing.isEmpty(),
			what,
			u"rows=%1 secrets=%2 boundedAlone=%3 clientWritten=%4 "
			"failing=[%5]"_q
				.arg(count)
				.arg(secrets)
				.arg(boundedAlone)
				.arg(clientWritten)
				.arg(failing.join(u", "_q)));
	}
	{
		const auto what = u"secrecy self-test: under the request no "
			"converted helper row holds a bounded digit run, and a scan whose "
			"only candidate is that row is clean"_q;
		auto digitRuns = 0;
		auto bounded = 0;
		auto embedded = 0;
		auto clean = 0;
		auto failing = QStringList();
		for (auto i = 0; i != count; ++i) {
			const auto &row = rows[i];
			const auto all = DigitRuns(row.telemetry);
			const auto alone = ScanRow(row.telemetry, all);
			const auto run = RunFixture(RowFixture(row.telemetry, row.runs));
			CheckPrepared(what, run);
			const auto &r = run.reading;
			const auto &t = ClassOf(r, SecrecyClass::TestLog);
			digitRuns += int(all.size());
			bounded += alone.bounded;
			embedded += alone.embedded;
			const auto scanClean = run.prepared && r.decided && r.clean;
			if (scanClean) {
				++clean;
			}
			const auto ok = (alone.bounded == 0)
				&& (alone.embedded >= int(all.size()))
				&& scanClean
				&& (t.bounded == 0)
				&& (r.clientWritten() == 0);
			if (!ok) {
				failing.push_back(cases[i].label);
			}
		}
		Check(
			(count > 0) && (digitRuns > 0) && failing.isEmpty(),
			what,
			u"rows=%1 digitRuns=%2 bounded=%3 embedded=%4 clean=%5 "
			"failing=[%6]"_q
				.arg(count)
				.arg(digitRuns)
				.arg(bounded)
				.arg(embedded)
				.arg(clean)
				.arg(failing.join(u", "_q)));
	}
	{
		// The premise: the phrase reading can see a split.
		const auto between = [](const QString &number) {
			return PhraseRuns(u"NOTE: "_q
				+ kSelfHelperPhraseSlots[1]
				+ QChar(' ')
				+ number
				+ QChar(' ')
				+ kSelfHelperPhraseSlots[2]);
		};
		const auto ordinary = between(u"4821"_q);
		const auto telemetry = between(TelemetryNumber(4821));
		Check(
			ordinary == telemetry + 1,
			u"secrecy self-test: a TelemetryNumber between two phrase words "
			"ends the run an ordinary number left joined"_q,
			u"ordinary=%1 telemetry=%2"_q.arg(ordinary).arg(telemetry));
	}
	{
		auto runs = QStringList();
		auto ordinary = 0;
		auto telemetry = 0;
		auto failing = QStringList();
		for (auto i = 0; i != count; ++i) {
			const auto ordinaryRuns = PhraseRuns(rows[i].phraseOrdinary);
			const auto telemetryRuns = PhraseRuns(rows[i].phraseTelemetry);
			runs.push_back(u"%1=%2/%3"_q
				.arg(cases[i].label)
				.arg(ordinaryRuns)
				.arg(telemetryRuns));
			ordinary += ordinaryRuns;
			telemetry += telemetryRuns;
			if (ordinaryRuns != telemetryRuns || ordinaryRuns < 1) {
				failing.push_back(cases[i].label);
			}
		}
		Check(
			(count > 0) && failing.isEmpty(),
			u"secrecy self-test: a synthetic phrase in each converted helper "
			"row's caller text gives the same word runs in both formats"_q,
			u"rows=%1 runs=[%2] ordinary=%3 telemetry=%4 failing=[%5]"_q
				.arg(count)
				.arg(runs.join(u", "_q))
				.arg(ordinary)
				.arg(telemetry)
				.arg(failing.join(u", "_q)));
	}
}

}

}

#endif
