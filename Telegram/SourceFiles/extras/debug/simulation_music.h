#pragma once

#ifdef _DEBUG
#include "extras/debug/debug_commands.h"
#include "base/basic_types.h"

class DocumentData;
namespace Media::Streaming {
class Loader;
} // namespace Media::Streaming

namespace ExtrasDebug::Commands {

[[nodiscard]] Result simulateMusic(const QStringList &args);
[[nodiscard]] std::unique_ptr<Media::Streaming::Loader> musicLoaderForDebug(
	not_null<const DocumentData*> document);

} // namespace ExtrasDebug::Commands
#endif
