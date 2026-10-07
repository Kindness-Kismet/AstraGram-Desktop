#pragma once

#include "data/data_types.h"

namespace Main {
class Session;
} // namespace Main

namespace ExtrasForward {

enum class SavedForwardError {
	None,
	Unavailable,
	UnsupportedMode,
};

[[nodiscard]] std::vector<Data::ForwardOptions> savedForwardOptions(
	not_null<Main::Session*> session,
	const MessageIdsList &ids);
[[nodiscard]] SavedForwardError forwardToSaved(
	not_null<Main::Session*> session,
	const MessageIdsList &ids,
	Data::ForwardOptions options);

} // namespace ExtrasForward
