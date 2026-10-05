#pragma once

#include "extras/utils/badge_roster.h"
#include "data/data_peer_id.h"

#include <QtNetwork/QNetworkReply>
#include <rpl/event_stream.h>
#include <vector>

class QTimer;

class RCManager final : public QObject {
	Q_OBJECT
public:
	static RCManager &getInstance() {
		static RCManager instance;
		return instance;
	}

	RCManager(const RCManager &) = delete;
	RCManager &operator=(const RCManager &) = delete;
	RCManager(RCManager &&) = delete;
	RCManager &operator=(RCManager &&) = delete;

	void start();
	[[nodiscard]] const BadgeRoster &roster() const { return _roster; }
	[[nodiscard]] rpl::producer<std::vector<PeerId>> changes() const {
		return _changes.events();
	}

private:
	RCManager() = default;
	~RCManager();

	void makeRequest();
	void gotResponse();
	void clearSentRequest();
	void stop();

	BadgeRoster _roster;
	QByteArray _etag;
	QTimer *_timer = nullptr;
	std::unique_ptr<QNetworkAccessManager> _manager;
	QNetworkReply *_reply = nullptr;
	rpl::event_stream<std::vector<PeerId>> _changes;
};
