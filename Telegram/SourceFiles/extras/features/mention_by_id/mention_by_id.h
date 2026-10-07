#pragma once

#include "data/data_peer_id.h"

class UserData;
namespace Main { class Session; }
namespace MTP { class Sender; }

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Ui {
class InputField;
} // namespace Ui

namespace ExtrasMentionById {

[[nodiscard]] std::optional<UserId> parseUserId(QString text);
[[nodiscard]] bool canMention(not_null<UserData*> user);
mtpRequestId resolveUser(
	not_null<MTP::Sender*> api,
	not_null<Main::Session*> session,
	UserId id,
	Fn<void(not_null<UserData*>)> done,
	Fn<void(QString)> fail);

void install(
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<Ui::InputField*> field);

} // namespace ExtrasMentionById
