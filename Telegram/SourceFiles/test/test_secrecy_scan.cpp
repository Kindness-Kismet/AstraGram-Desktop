#include "test/test_secrecy_scan_internal.h"

#ifdef _DEBUG

namespace Test {

namespace SecrecyScanDetails {}

using namespace SecrecyScanDetails;

QString SecrecyClassName(SecrecyClass value) {
	switch (value) {
	case SecrecyClass::TestLog: return u"TestLog"_q;
	case SecrecyClass::AppLog: return u"AppLog"_q;
	case SecrecyClass::DebugLog: return u"DebugLog"_q;
	case SecrecyClass::MtpLog: return u"MtpLog"_q;
	case SecrecyClass::EarlierParts: return u"EarlierParts"_q;
	}
	return u"?"_q;
}

QString PartIdentityName(PartIdentity value) {
	switch (value) {
	case PartIdentity::Missing: return u"missing"_q;
	case PartIdentity::Unreadable: return u"unreadable"_q;
	case PartIdentity::Foreign: return u"foreign"_q;
	case PartIdentity::Accepted: return u"accepted"_q;
	}
	return u"?"_q;
}

int SecrecyClassReading::clientWritten() const {
	return wordRunsOther
		+ wordRunsSend
		+ boundedOther
		+ boundedSend
		+ tokensOther
		+ tokensSend;
}

int SecrecyClassReading::received() const {
	return wordRunsRecv + boundedRecv + tokensRecv;
}

const SecrecyClassReading *SecrecyReading::find(SecrecyClass cls) const {
	for (const auto &reading : classes) {
		if (reading.cls == cls) {
			return &reading;
		}
	}
	return nullptr;
}

int SecrecyReading::clientWritten() const {
	auto result = 0;
	for (const auto &reading : classes) {
		result += reading.clientWritten();
	}
	return result;
}

int SecrecyReading::received() const {
	auto result = 0;
	for (const auto &reading : classes) {
		result += reading.received();
	}
	return result;
}

int SecrecyReading::computed() const {
	auto result = 0;
	for (const auto &reading : classes) {
		result += reading.boundedComputed;
	}
	return result;
}

SecrecyLaunchLogs SelectLaunchLogs(
		const QString &workingDir,
		const QString &evidenceDir,
		const QString &appLogText,
		const QDateTime &launchStart,
		const QDateTime &scanAt,
		const std::vector<SecrecyEarlierPart> &earlier) {
	auto result = SecrecyLaunchLogs{
		.launchStart = launchStart,
		.scanAt = scanAt,
	};
	{
		const auto path = evidenceDir + u"test_log.txt"_q;
		result.sources.push_back({
			.cls = SecrecyClass::TestLog,
			.name = u"test_log.txt"_q,
			.path = path,
			.identity = (QFile::exists(path)
				? PartIdentity::Accepted
				: PartIdentity::Missing),
		});
	}
	result.sources.push_back({
		.cls = SecrecyClass::AppLog,
		.name = u"log.txt"_q,
		.text = appLogText,
		.fromText = true,
		.identity = (appLogText.isEmpty()
			? PartIdentity::Missing
			: PartIdentity::Accepted),
	});
	if (launchStart.isValid() && scanAt.isValid()) {
		const auto time = launchStart.time();
		auto part = QDateTime(
			launchStart.date(),
			QTime(time.hour(), (time.minute() / kPartMinutes) * kPartMinutes));
		for (auto step = 0
			; (part <= scanAt) && (step < kMaxWindowSteps)
			; ++step, part = part.addSecs(kPartMinutes * 60)) {
			const auto postfix = u"_%1_%2.txt"_q
				.arg(part.time().hour(), 2, 10, QChar('0'))
				.arg(part.time().minute(), 2, 10, QChar('0'));
			const auto day = part.date().toString(u"yyyyMMdd"_q);
			for (const auto &[kind, cls] : {
				std::pair{ u"log"_q, SecrecyClass::DebugLog },
				std::pair{ u"mtp"_q, SecrecyClass::MtpLog },
			}) {
				const auto name = u"DebugLogs/"_q + kind + postfix;
				const auto path = workingDir + name;
				result.sources.push_back({
					.cls = cls,
					.name = name,
					.path = path,
					.sliceAtLastBanner = true,
					.identity = ReadPartIdentity(path, day),
				});
				++result.candidates;
			}
		}
	}
	for (const auto &part : earlier) {
		auto source = SecrecySource{
			.cls = SecrecyClass::EarlierParts,
			.control = part.control,
		};
		if (!ValidPartName(part.name) || !part.day.isValid()) {
			// The refused name is the caller's; it is not echoed.
			source.name = u"DebugLogs/(invalid name)"_q;
			source.invalidName = true;
			source.identity = PartIdentity::Missing;
		} else {
			source.name = u"DebugLogs/"_q + part.name;
			source.path = workingDir + source.name;
			source.identity = ReadPartIdentity(
				source.path,
				part.day.toString(u"yyyyMMdd"_q));
		}
		result.sources.push_back(std::move(source));
	}
	return result;
}

SecrecyLaunchLogs SelectThisLaunchLogs(
		const std::vector<SecrecyEarlierPart> &earlier) {
	// crl::now() counts from lib_crl's static initializer (crl_time.cpp),
	// which runs before the logger starts, so now - crl::now() is the
	// process start. No last-write time takes part: on Windows a file the
	// process still holds open keeps its old one, which is how "modified
	// since the scenario started" selected none of this launch's logs.
	const auto now = QDateTime::currentDateTime();
	const auto launchStart = now.addMSecs(-crl::now());
	return SelectLaunchLogs(
		cWorkingDir(),
		EvidenceDir(),
		Logs::full(),
		launchStart,
		now,
		earlier);
}

SecrecyReading ReadSecrecy(
		const SecrecySecrets &secrets,
		const SecrecyLaunchLogs &logs,
		const QString &plantedControl,
		const std::vector<SecrecyComputedField> &computedFields) {
	auto result = SecrecyReading();
	result.launchStart = logs.launchStart;
	result.scanAt = logs.scanAt;
	result.candidates = logs.candidates;

	const auto usable = UsableSecrets(secrets, &result);
	const auto matcher = SecrecyMatcher(usable, computedFields);
	ReadCanaries(usable, result);

	auto order = std::vector<SecrecyClass>{
		SecrecyClass::TestLog,
		SecrecyClass::AppLog,
		SecrecyClass::DebugLog,
		SecrecyClass::MtpLog,
	};
	const auto hasEarlier = ranges::any_of(logs.sources, [](const auto &s) {
		return (s.cls == SecrecyClass::EarlierParts);
	});
	if (hasEarlier) {
		order.push_back(SecrecyClass::EarlierParts);
	}
	for (const auto cls : order) {
		auto reading = SecrecyClassReading{ .cls = cls };
		for (const auto &source : logs.sources) {
			if (source.cls != cls) {
				continue;
			}
			++reading.candidates;
			if (IsWindowClass(cls)) {
				result.candidateIdentities.push_back(source.name
					+ QChar('=')
					+ PartIdentityName(source.identity));
				switch (source.identity) {
				case PartIdentity::Missing: ++result.missing; break;
				case PartIdentity::Unreadable: ++result.unreadable; break;
				case PartIdentity::Foreign: ++result.foreign; break;
				case PartIdentity::Accepted: ++result.accepted; break;
				}
			}
			ReadSource(source, matcher, plantedControl, reading);
		}
		DecideClass(reading);
		if (!reading.decided) {
			result.undecidedClasses.push_back(SecrecyClassName(cls)
				+ u": "_q
				+ reading.undecidedReason);
		}
		result.classes.push_back(std::move(reading));
	}

	if (!result.phrasesGiven && !result.shortGiven && !result.tokensGiven) {
		result.undecidedReasons.push_back(u"no secret given"_q);
	}
	if (result.tokensTooShort) {
		result.undecidedReasons.push_back(u"%1 token(s) shorter than 16 "
			"characters: not a token; hand it as a short secret"_q
			.arg(result.tokensTooShort));
	}
	if (result.phrasesTooShort) {
		result.undecidedReasons.push_back(
			u"%1 phrase(s) shorter than 2 words"_q.arg(result.phrasesTooShort));
	}
	if (!result.canariesHold) {
		result.undecidedReasons.push_back(u"a canary failed"_q);
	}
	result.decided = result.undecidedReasons.isEmpty()
		&& result.undecidedClasses.isEmpty();
	result.clean = result.decided && !result.clientWritten();
	return result;
}

QStringList SecrecyRows(const SecrecyReading &reading) {
	auto result = QStringList();
	auto classes = QStringList();
	auto files = QStringList();
	for (const auto &c : reading.classes) {
		classes.push_back(SecrecyClassName(c.cls));
		files += c.names;
	}
	result.push_back(u"SECRECY_SCAN: decided=%1 clean=%2 classes=[%3] "
		"undecided=[%4] reasons=[%5] canaries=[wordRuns=%6/%7 bounded=%8/%9 "
		"embedded=%10/%9 send=%11/%9 recv=%12/%9 tokens=%13/%14 hold=%15] "
		"secrets=[phrases=%16 short=%17 tokens=%18 tokensTooShort=%19 "
		"phrasesTooShort=%20] window=[start=%21 scan=%22 candidates=%23 "
		"accepted=%24 foreign=%25 missing=%26 unreadable=%27] "
		"clientWritten=%28 received=%29 files=[%30]"_q
		.arg(B(reading.decided))
		.arg(B(reading.clean))
		.arg(classes.join(u", "_q))
		.arg(reading.undecidedClasses.join(u"; "_q))
		.arg(reading.undecidedReasons.join(u"; "_q))
		.arg(reading.canaryWordRuns)
		.arg(reading.canaryWordRunsExpected)
		.arg(reading.canaryBounded)
		.arg(reading.canaryShortExpected)
		.arg(reading.canaryEmbedded)
		.arg(reading.canarySend)
		.arg(reading.canaryRecv)
		.arg(reading.canaryToken)
		.arg(reading.canaryTokenExpected)
		.arg(B(reading.canariesHold))
		.arg(reading.phrasesGiven)
		.arg(reading.shortGiven)
		.arg(reading.tokensGiven)
		.arg(reading.tokensTooShort)
		.arg(reading.phrasesTooShort)
		.arg(TimeText(reading.launchStart))
		.arg(TimeText(reading.scanAt))
		.arg(reading.candidates)
		.arg(reading.accepted)
		.arg(reading.foreign)
		.arg(reading.missing)
		.arg(reading.unreadable)
		.arg(reading.clientWritten())
		.arg(reading.received())
		.arg(files.join(u", "_q)));
	result.push_back(u"SECRECY_WINDOW: candidates=["_q
		+ reading.candidateIdentities.join(u", "_q)
		+ u"]"_q);
	auto recvSites = std::map<QString, int>();
	auto computedSites = std::map<QString, int>();
	auto wordRunsRecv = 0;
	auto boundedRecv = 0;
	auto tokensRecv = 0;
	for (const auto &c : reading.classes) {
		result.push_back(ClassRow(c));
		for (const auto &[site, hits] : c.recvSites) {
			recvSites[site] += hits;
		}
		for (const auto &[site, hits] : c.computedSites) {
			computedSites[SecrecyClassName(c.cls) + QChar('|') + site] += hits;
		}
		wordRunsRecv += c.wordRunsRecv;
		boundedRecv += c.boundedRecv;
		tokensRecv += c.tokensRecv;
	}
	result.push_back(u"SECRECY_RECEIVED: total=%1 wordRunsRecv=%2 "
		"boundedRecv=%3 tokensRecv=%4 recvSites=[%5] (reported, "
		"non-deciding)"_q
		.arg(reading.received())
		.arg(wordRunsRecv)
		.arg(boundedRecv)
		.arg(tokensRecv)
		.arg(SitesText(recvSites)));
	result.push_back(u"SECRECY_COMPUTED: total=%1 sites=[%2] (reported, "
		"non-deciding)"_q
		.arg(reading.computed())
		.arg(SitesText(computedSites)));
	return result;
}

bool CheckSecrecy(const SecrecyScanArgs &args, const QString &what) {
	if (!Active()) {
		return false;
	}
	const auto nonce = u"secrecy-control-"_q
		+ QString::number(base::RandomValue<uint64>(), 16).rightJustified(
			16,
			QChar('0'));
	Note(u"SECRECY_CONTROL: "_q + nonce);
	LOG(("Test Info: secrecy control %1").arg(nonce));
	MTP_LOG(0, ("Test Info: secrecy control %1").arg(nonce));

	const auto path = TestLogPath();
	const auto mark = QFileInfo(path).size();
	auto logs = SelectThisLaunchLogs(args.earlierParts);
	for (auto &source : logs.sources) {
		source.control = [&] {
			switch (source.cls) {
			case SecrecyClass::TestLog: return args.testLogControl;
			case SecrecyClass::AppLog: return args.appLogControl;
			case SecrecyClass::DebugLog: return args.debugLogControl;
			case SecrecyClass::MtpLog: return args.mtpLogControl;
			case SecrecyClass::EarlierParts: return source.control;
			}
			return source.control;
		}();
	}
	const auto reading
		= ReadSecrecy(args.secrets, logs, nonce, args.computedFields);
	for (const auto &c : reading.classes) {
		ReportClass(c);
	}
	for (const auto &row : SecrecyRows(reading)) {
		Note(row);
	}
	Check(
		reading.decided,
		what + u": every class decided"_q,
		u"classes=%1 undecidedClasses=[%2] reasons=[%3] canariesHold=%4"_q
			.arg(reading.classes.size())
			.arg(reading.undecidedClasses.join(u"; "_q))
			.arg(reading.undecidedReasons.join(u"; "_q))
			.arg(B(reading.canariesHold)));
	Check(
		reading.clean,
		what + u": no client-written fixture secret"_q,
		u"clientWritten=%1 received=%2 computed=%3 decided=%4"_q
			.arg(reading.clientWritten())
			.arg(reading.received())
			.arg(reading.computed())
			.arg(B(reading.decided)));

	// The same matcher over exactly the bytes this call appended, raw, so
	// the scan's own rows are proven to carry no secret.
	const auto usable = UsableSecrets(args.secrets, nullptr);
	const auto matcher = SecrecyMatcher(usable);
	auto own = SecrecyClassReading();
	ScanText(AppendedSince(path, mark), matcher, {}, {}, own);
	Check(
		own.lines > 0 && !HitTotal(own),
		what + u": the scan's own rows carry no fixture secret"_q,
		u"lines=%1 wordRuns=%2 bounded=%3 embedded=%4 tokens=%5"_q
			.arg(own.lines)
			.arg(own.wordRunsOther + own.wordRunsSend + own.wordRunsRecv)
			.arg(own.bounded)
			.arg(own.embedded)
			.arg(own.tokensOther + own.tokensSend + own.tokensRecv));
	return reading.decided && reading.clean;
}

void AppendSecrecyScanSelfTest(not_null<Runner*> runner) {
	const auto state = std::make_shared<SelfState>();
	const auto stages = std::vector<std::pair<QString, Fn<void()>>>{
		{ u"secrecy_self_canaries"_q, [=] { SelfCanaries(state); } },
		{ u"secrecy_self_embedded"_q, [=] { SelfEmbedded(state); } },
		{ u"secrecy_self_recv"_q, [=] { SelfRecv(state); } },
		{
			u"secrecy_self_client_written"_q,
			[=] { SelfClientWritten(state); },
		},
		{ u"secrecy_self_sites"_q, [=] { SelfSites(state); } },
		{ u"secrecy_self_undecided"_q, [=] { SelfUndecided(state); } },
		{ u"secrecy_self_identity"_q, [=] { SelfIdentity(state); } },
		{
			u"secrecy_self_prints_nothing"_q,
			[=] { SelfPrintsNothing(state); },
		},
	};
	for (const auto &[name, then] : stages) {
		runner->add({
			.name = name,
			.then = then,
			.timeout = kDefaultStageTimeout,
		});
	}
}

}

#endif
