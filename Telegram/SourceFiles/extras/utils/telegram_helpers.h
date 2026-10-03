#pragma once

#include "api/api_common.h"
#include "extras/data/entities.h"
#include "core/application.h"
#include "data/data_media_types.h"
#include "info/profile/info_profile_badge.h"

namespace Api {
struct SendOptions;
}

using UsernameResolverCallback = Fn<void(const QString &, PeerData *)>;

class TimedCountDownLatch
{
public:
    explicit TimedCountDownLatch(int count)
        : count_(count) {
    }

    TimedCountDownLatch(const TimedCountDownLatch &) = delete;
    TimedCountDownLatch &operator=(const TimedCountDownLatch &) = delete;

    void countDown() {
        std::unique_lock lock(mutex_);
        if (count_ > 0) {
            count_--;
        }
        if (count_ == 0) {
            cv_.notify_all();
        }
    }

    bool await(std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        if (count_ == 0) {
            return true;
        }
        return cv_.wait_for(lock, timeout, [this] { return count_ == 0; });
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    int count_;
};

Main::Session *getSession(ID userId);
void dispatchToMainThread(const std::function<void()> &callback, int delay = 0);
ID getDialogIdFromPeer(not_null<PeerData*> peer);

ID getBareID(not_null<PeerData*> peer);

bool isExteraPeer(ID peerId);

rpl::producer<Info::Profile::Badge::Content> ExteraBadgeTypeFromPeer(not_null<PeerData*> peer);
Fn<void()> badgeClickHandler(not_null<PeerData *> peer);

bool isMessageHidden(not_null<HistoryItem*> item);

void markReadAfterAction(not_null<History*> history);
void readHistory(not_null<HistoryItem*> message);
// 隐身模式只拦截被动已读；用户主动标为已读时由此发给服务器，重启后不会回到未读。
void readThreadOnServer(not_null<Data::Thread*> thread);

QString formatTTL(int time, bool isDoc);
QString formatDateTime(const QDateTime &date);
QString formatMessageTime(const QTime &time);

QString getDCName(int dc);

QString getMediaSize(not_null<HistoryItem*> message);
QString getMediaMime(not_null<HistoryItem*> message);
QString getMediaName(not_null<HistoryItem*> message);
QString getMediaResolution(not_null<HistoryItem*> message);
QString getMediaDC(not_null<HistoryItem*> message);

QString getPeerDC(not_null<PeerData*> peer);

int getScheduleTime(int64 sumSize);

bool isMessageSavable(not_null<HistoryItem *> item);
void processMessageDelete(not_null<HistoryItem *> item);

void searchUserById(ID userId, Main::Session *session, const UsernameResolverCallback &callback);
void searchChatById(ID chatId, Main::Session *session, const UsernameResolverCallback &callback);

ID getUserIdFromPackId(uint64 id);

TextWithTags extractText(not_null<HistoryItem*> item);
bool mediaDownloadable(const Data::Media* media);

TextWithEntities reverseLocalPremiumEmoji(const TextWithEntities &text, not_null<History *> history, bool isForQuote = false);
void applyLocalPremiumEmoji(TextWithEntities &text);

not_null<Main::Session *> currentSession();

PeerData* getPeerFromDialogId(ID id);
PeerData* getPeerFromDialogId(unsigned long long id);

QString filterZalgo(const QString &text);

bool prependPseudoReply(Api::MessageToSend &message);
bool prependPseudoReply(
	not_null<Main::Session*> session,
	not_null<History*> history,
	TextWithTags &caption,
	FullReplyTo &replyTo);

void getRegistrationDate(not_null<PeerData*> peer, Fn<void(TextWithEntities)> callback);

QString getBetterLinkPreview(const QString &url);

void applyGhostScheduling(
	not_null<Main::Session*> session,
	Api::SendOptions &options,
	int delaySeconds = 12);
