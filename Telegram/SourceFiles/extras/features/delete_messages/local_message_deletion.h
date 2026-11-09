#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Extras::DeleteMessages {

// 清理当前账号主动删除的消息，缓存与驻留条目清理后调用完成回调。
void forgetLocalMessages(
	not_null<Main::Session*> session,
	PeerId peer,
	QVector<MTPint> ids,
	Fn<void()> done = {});

} // namespace Extras::DeleteMessages
