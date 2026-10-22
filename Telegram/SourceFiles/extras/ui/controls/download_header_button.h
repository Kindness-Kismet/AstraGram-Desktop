#pragma once

#include "base/timer.h"
#include "ui/effects/animations.h"
#include "ui/widgets/buttons.h"

namespace ExtrasUi {

// 会话列表顶部的下载入口：下载时箭头在圆内反复落下，全部完成后短暂显示对勾。
class DownloadHeaderButton final : public Ui::IconButton {
public:
	DownloadHeaderButton(QWidget *parent, const style::IconButton &st);

	// loading 为仍在下载的文件数；归零且 finished 时播放完成提示，取消下载不提示。
	void setLoading(int loading, bool finished);

	// 正在下载或正在显示完成提示。
	[[nodiscard]] bool active() const;
	[[nodiscard]] rpl::producer<bool> activeValue() const;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void paintIcon(QPainter &p, crl::time now);

	int _loading = 0;
	crl::time _loopStarted = 0;
	Ui::Animations::Basic _loop;
	Ui::Animations::Simple _check;
	base::Timer _doneTimer;
	bool _done = false;
	rpl::variable<bool> _active = false;

};

} // namespace ExtrasUi
