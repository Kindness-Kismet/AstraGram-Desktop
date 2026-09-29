#pragma once

#include "extras/extras_settings.h"
#include "translate_provider.h"

#include <memory>

namespace Main {
class Session;
} // namespace Main

namespace Ui {

[[nodiscard]] std::unique_ptr<TranslateProvider> CreateExtrasTranslateProvider(
	not_null<Main::Session*> session,
	TranslationProvider provider);

} // namespace Ui
