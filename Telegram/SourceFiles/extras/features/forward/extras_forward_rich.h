#pragma once

#include "api/api_common.h"
#include "history/history_item.h"
#include "main/main_session.h"

namespace ExtrasForward {

bool forwardRichMessage(
	not_null<Main::Session*> session,
	FullMsgId itemId,
	const Api::SendAction &action,
	Fn<bool()> cancelled = nullptr);

} // namespace ExtrasForward
