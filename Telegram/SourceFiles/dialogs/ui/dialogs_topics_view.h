/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class Painter;

namespace style {
struct DialogRow;
} // namespace style

namespace Data {
class Forum;
class ForumTopic;
class SavedMessages;
class SavedSublist;
} // namespace Data

namespace Dialogs::Ui {

using namespace ::Ui;

struct PaintContext;
struct TopicJumpCorners;

struct JumpToLastBg {
	not_null<const style::DialogRow*> st;
	not_null<TopicJumpCorners*> corners;
	QRect geometry;
	const style::color &bg;
	int width1 = 0;
	int width2 = 0;
};
struct JumpToLastGeometry {
	int rightCut = 0;
	QRect area1;
	QRect area2;

	friend inline bool operator==(
		const JumpToLastGeometry&,
		const JumpToLastGeometry&) = default;
};
JumpToLastGeometry FillJumpToLastBg(QPainter &p, JumpToLastBg context);

struct JumpToLastPrepared {
	not_null<const style::DialogRow*> st;
	not_null<TopicJumpCorners*> corners;
	const style::color &bg;
	const JumpToLastGeometry &prepared;
};
void FillJumpToLastPrepared(QPainter &p, JumpToLastPrepared context);

class TopicsView final {
public:
	TopicsView(Data::Forum *forum, Data::SavedMessages *monoforum);
	~TopicsView();

	[[nodiscard]] Data::Forum *forum() const {
		return _forum;
	}
	[[nodiscard]] Data::SavedMessages *monoforum() const {
		return _monoforum;
	}

	[[nodiscard]] bool prepared() const;
	void prepare(MsgId frontRootId, Fn<void()> customEmojiRepaint);
	void prepare(PeerId frontPeerId, Fn<void()> customEmojiRepaint);

	[[nodiscard]] int jumpToTopicWidth() const;

	void paint(
		Painter &p,
		const QRect &geometry,
		const PaintContext &context) const;

	void changeTopicJumpGeometry(JumpToLastGeometry geometry);
	void clearTopicJumpGeometry();
	[[nodiscard]] bool isInTopicJumpArea(int x, int y) const;

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	struct Title {
		Text::String title;
		uint64 key = 0;
		int version = -1;
		bool unread = false;
	};

	Data::Forum * const _forum = nullptr;
	Data::SavedMessages * const _monoforum = nullptr;

	mutable std::vector<Title> _titles;
	JumpToLastGeometry _lastTopicJumpGeometry;
	int _version = -1;
	bool _jumpToTopic = false;
	bool _allLoaded = false;

	rpl::lifetime _lifetime;

};

} // namespace Dialogs::Ui
