#include "extras/utils/rc_manager.h"

#include <QCoreApplication>
#include <QtNetwork/QNetworkAccessManager>
#include <QTimer>
#include <algorithm>

namespace {

constexpr auto kBadgeUrl = "https://badge.astragram.dpdns.org/v1/badges";
constexpr auto kFetchTimeout = 15 * 1000;

void appendAffectedPeers(std::vector<PeerId> &result, const BadgeRoster &roster) {
	const auto append = [&](const auto &ids, bool channel) {
		for (const auto id : ids) {
			result.push_back(channel ? peerFromChannel(ChannelId(id)) : peerFromUser(UserId(id)));
		}
	};
	append(roster.developers, false);
	append(roster.supporters, false);
	append(roster.officialChannels, true);
	append(roster.supporterChannels, true);
	for (const auto &[id, badge] : roster.customBadges) {
		result.push_back(peerFromUser(UserId(id)));
		result.push_back(peerFromChannel(ChannelId(id)));
		result.push_back(peerFromChat(ChatId(id)));
	}
}

} // namespace

void RCManager::start() {
	if (_manager) {
		return;
	}
	_manager = std::make_unique<QNetworkAccessManager>();
	_timer = new QTimer(this);
	connect(_timer, &QTimer::timeout, this, &RCManager::makeRequest);
	connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [=] {
		stop();
	});
	_timer->start(60 * 60 * 1000);
	makeRequest();
}

void RCManager::makeRequest() {
	if (!_manager || _reply) {
		return;
	}
	auto request = QNetworkRequest(QUrl(QString::fromLatin1(kBadgeUrl)));
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setTransferTimeout(kFetchTimeout);
	if (!_etag.isEmpty()) {
		request.setRawHeader("If-None-Match", _etag);
	}
	_reply = _manager->get(request);
	_reply->setReadBufferSize(kBadgeRosterMaxBytes + 1);
	connect(_reply, &QNetworkReply::readyRead, this, [=] {
		if (_reply && _reply->bytesAvailable() > kBadgeRosterMaxBytes) {
			clearSentRequest();
			LOG(("BadgeRoster: response exceeds size limit"));
		}
	});
	connect(_reply, &QNetworkReply::finished, this, &RCManager::gotResponse);
}

void RCManager::gotResponse() {
	if (!_reply) {
		return;
	}
	const auto status = _reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
	const auto success = _reply->error() == QNetworkReply::NoError;
	const auto etag = _reply->rawHeader("ETag");
	const auto response = _reply->readAll();
	clearSentRequest();
	if (status == 304 && success) {
		return;
	}
	const auto parsed = (status == 200 && success)
		? parseBadgeRoster(response)
		: std::nullopt;
	if (!parsed) {
		LOG(("BadgeRoster: failed to refresh, keeping the last valid roster"));
		return;
	}
	_etag = etag;
	if (_roster == *parsed) {
		return;
	}
	auto affected = std::vector<PeerId>();
	appendAffectedPeers(affected, _roster);
	appendAffectedPeers(affected, *parsed);
	std::sort(affected.begin(), affected.end());
	affected.erase(std::unique(affected.begin(), affected.end()), affected.end());
	_roster = *parsed;
	_changes.fire(std::move(affected));
}

void RCManager::clearSentRequest() {
	const auto reply = base::take(_reply);
	if (!reply) {
		return;
	}
	disconnect(reply, nullptr, this, nullptr);
	reply->abort();
	reply->deleteLater();
}

void RCManager::stop() {
	clearSentRequest();
	delete base::take(_timer);
	_manager = nullptr;
}

RCManager::~RCManager() {
	stop();
}
