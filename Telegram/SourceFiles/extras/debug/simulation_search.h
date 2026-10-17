#pragma once

#ifdef _DEBUG
#include "data/data_messages.h"
#include "data/data_search_controller.h"
#include "extras/debug/debug_commands.h"

namespace ExtrasDebug::Commands {

[[nodiscard]] Result seedSearchPreview(const QStringList &args);
[[nodiscard]] std::optional<Api::GlobalMediaResult> searchPreviewMedia(
	not_null<Main::Session*> session, Storage::SharedMediaType type, const QString &query);

}
#endif
