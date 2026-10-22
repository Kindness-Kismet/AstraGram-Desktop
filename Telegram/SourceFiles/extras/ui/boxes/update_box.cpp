#include "extras/ui/boxes/update_box.h"

#include "core/version.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_extras_styles.h"
#include "styles/style_layers.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>

namespace ExtrasUi {
namespace {

constexpr auto kReleaseApi
	= "https://api.github.com/repos/Kindness-Kismet/AstraGram-Desktop/releases/tags/%1";
constexpr auto kReleasePage
	= "https://github.com/Kindness-Kismet/AstraGram-Desktop/releases/tag/%1";

void fillNotes(
		not_null<Ui::VerticalLayout*> notes,
		const QStringList &items) {
	notes->clear();
	if (items.isEmpty()) {
		notes->add(
			object_ptr<Ui::FlatLabel>(
				notes,
				tr::extras_UpdateNotesUnavailable(tr::now),
				st::extrasUpdateVersionLabel),
			st::boxRowPadding + QMargins(0, st::extrasUpdateNoteTopSkip, 0, 0));
		return;
	}
	const auto labels = notes->lifetime().make_state<
		std::vector<not_null<Ui::FlatLabel*>>>();
	const auto indent = st::extrasUpdateNoteIndent;
	for (const auto &item : items) {
		const auto top = labels->empty()
			? st::extrasUpdateNoteTopSkip
			: st::extrasUpdateNoteItemSkip;
		labels->push_back(notes->add(
			object_ptr<Ui::FlatLabel>(notes, item, st::extrasUpdateNoteLabel),
			st::boxRowPadding + QMargins(indent, top, 0, 0)));
	}
	// 条目圆点画在标签左侧的缩进里，换行后正文仍然对齐。
	notes->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(notes);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowSubTextFg);
		const auto size = st::extrasUpdateNoteBullet;
		const auto line = st::extrasUpdateNoteLabel.style.font->height;
		for (const auto label : *labels) {
			p.drawEllipse(
				label->x() - indent + (indent - size) / 2,
				label->y() + (line - size) / 2,
				size,
				size);
		}
	}, notes->lifetime());
}

void requestNotes(
		not_null<Ui::GenericBox*> box,
		not_null<Ui::VerticalLayout*> notes,
		const QString &tag) {
	const auto manager = new QNetworkAccessManager(box);
	auto request = QNetworkRequest(QUrl(QString::fromLatin1(kReleaseApi).arg(tag)));
	request.setRawHeader("Accept", "application/vnd.github+json");
	request.setHeader(QNetworkRequest::UserAgentHeader, u"AstraGram"_q);
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	const auto reply = manager->get(request);
	const auto chinese = ReleaseNotesInChinese();
	QObject::connect(reply, &QNetworkReply::finished, notes, [=] {
		reply->deleteLater();
		const auto body = (reply->error() == QNetworkReply::NoError)
			? QJsonDocument::fromJson(reply->readAll()).object().value(
				u"body"_q).toString()
			: QString();
		if (body.isEmpty()) {
			LOG(("Update Info: release notes for %1 unavailable, error %2."
				).arg(tag
				).arg(int(reply->error())));
		}
		fillNotes(notes, ParseReleaseNotes(body, chinese));
	});
}

} // namespace

QStringList ParseReleaseNotes(const QString &body, bool chinese) {
	static const auto separator = QRegularExpression(u"\\r?\\n---[ \\t]*\\r?\\n"_q);
	static const auto cjk = QRegularExpression(u"[\\x{4e00}-\\x{9fff}]"_q);
	const auto normalized = u"\n"_q + body + u"\n"_q;
	for (const auto &section : normalized.split(separator)) {
		auto items = QStringList();
		auto valid = true;
		for (const auto &line : section.split('\n')) {
			const auto trimmed = line.trimmed();
			if (trimmed.isEmpty()) {
				continue;
			} else if (!trimmed.startsWith(u"- "_q)) {
				valid = false;
				break;
			}
			items.push_back(trimmed.mid(2).trimmed());
		}
		if (!valid || items.isEmpty()) {
			continue;
		}
		const auto isChinese = ranges::any_of(items, [](const QString &item) {
			return cjk.match(item).hasMatch();
		});
		if (isChinese == chinese) {
			return items;
		}
	}
	return {};
}

QString UpdateVersionTag(uint64 version) {
	const auto major = version / 10'000'000;
	const auto minor = (version / 100'000) % 100;
	const auto patch = (version / 100) % 1000;
	const auto revision = version % 100;
	auto result = u"v%1.%2.%3"_q.arg(major).arg(minor).arg(patch);
	if (revision) {
		result += u".%1"_q.arg(revision);
	}
	return result;
}

bool ReleaseNotesInChinese() {
	const auto &lang = Lang::GetInstance();
	for (const auto &id : { lang.id(), lang.baseId() }) {
		if (id == u"zh-hans"_q || id.startsWith(u"zh-hans-"_q)) {
			return true;
		}
	}
	return false;
}

void UpdateBox(not_null<Ui::GenericBox*> box, UpdateBoxArgs args) {
	const auto tag = UpdateVersionTag(args.version);
	box->setTitle(tr::extras_UpdateReadyTitle());
	box->setWidth(st::boxWideWidth);

	const auto container = box->verticalLayout();
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			u"AstraGram %1 → %2"_q.arg(QString::fromLatin1(AppVersionStr), tag.mid(1)),
			st::extrasUpdateVersionLabel),
		st::boxRowPadding);
	const auto notes = container->add(object_ptr<Ui::VerticalLayout>(container));
	notes->add(
		object_ptr<Ui::FlatLabel>(
			notes,
			tr::extras_UpdateNotesLoading(tr::now),
			st::extrasUpdateVersionLabel),
		st::boxRowPadding + QMargins(0, st::extrasUpdateNoteTopSkip, 0, 0));
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			rpl::single(Ui::Text::Link(
				tr::extras_UpdateNotesLink(tr::now),
				QString::fromLatin1(kReleasePage).arg(tag))),
			st::boxLabel),
		st::boxRowPadding + QMargins(0, st::extrasUpdateNoteTopSkip, 0, 0));
	requestNotes(box, notes, tag);

	const auto update = args.update;
	const auto skip = args.skip;
	box->addButton(tr::extras_UpdateShort(), [=] {
		box->closeBox();
		update();
	});
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
	box->addLeftButton(tr::extras_UpdateSkip(), [=] {
		box->closeBox();
		skip();
	});
}

} // namespace ExtrasUi
