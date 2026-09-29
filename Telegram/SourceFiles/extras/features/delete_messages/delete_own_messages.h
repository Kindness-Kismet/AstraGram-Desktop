#pragma once

namespace Window {
class SessionController;
}

namespace ExtrasDeleteMessages {

// 倒计时结束才执行删除；撤销或提示条销毁会取消等待。
void scheduleDeleteOwnMessages(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	Fn<void()> deleteMessages);

}
