#pragma once

#include "extras/features/chat_filters/local_rules.h"

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace ExtrasChatFilters {

[[nodiscard]] Fn<Data::ChatFilterLocalRules()> setupRulesFields(
	not_null<Ui::VerticalLayout*> content,
	const Data::ChatFilterLocalRules &rules,
	Fn<void(Data::ChatFilterLocalRules)> changed);

} // namespace ExtrasChatFilters
