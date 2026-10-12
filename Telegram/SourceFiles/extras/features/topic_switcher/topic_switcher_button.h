#pragma once

#include "ui/effects/animations.h"
#include "ui/text/text.h"
#include "ui/widgets/buttons.h"

namespace ExtrasFeatures::TopicSwitcher {

struct ButtonContent {
	TextWithEntities label;
	Ui::Text::MarkedContext context;
	int unread = 0;
	bool unreadMuted = false;
};

// 顶栏标题第二行的切换按钮：当前话题、展开箭头、其他话题的未读总数。
class Button final : public Ui::RippleButton {
public:
	Button(QWidget *parent, Fn<bool()> paused);

	void setContent(ButtonContent &&content);
	void setOpened(bool opened);

private:
	void paintEvent(QPaintEvent *e) override;
	QImage prepareRippleMask() const override;

	void paintChevron(QPainter &p, int left, float64 opened) const;
	void refreshNaturalWidth();
	[[nodiscard]] int badgeWidth() const;

	const Fn<bool()> _paused;
	Ui::Text::String _text;
	QString _unread;
	bool _unreadMuted = false;
	bool _opened = false;
	Ui::Animations::Simple _openedAnimation;

};

} // namespace ExtrasFeatures::TopicSwitcher
