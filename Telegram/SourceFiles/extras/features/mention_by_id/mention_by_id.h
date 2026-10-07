#pragma once

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Ui {
class InputField;
} // namespace Ui

namespace ExtrasMentionById {

void install(
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<Ui::InputField*> field);

} // namespace ExtrasMentionById
