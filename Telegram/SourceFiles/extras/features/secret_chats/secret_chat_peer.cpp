#include "extras/features/secret_chats/secret_chat_peer.h"

#include "data/data_user.h"
#include "extras/features/secret_chats/secret_chat_manager.h"
#include "main/main_session.h"

namespace Extras::SecretChats {

Peer::Peer(not_null<Data::Session*> owner, PeerId id)
: PeerData(owner, id) {
	Expects(peerIsSecretChat(id));
}

void Peer::setUser(not_null<UserData*> user) {
	Expects(&user->owner() == &owner());
	_user = user;
	updateNameDelayed(user->name(), QString(), QString());
}

not_null<UserData*> Peer::user() const {
	Expects(_user != nullptr);
	return _user;
}

int Peer::chatId() const {
	return int(id.to<SecretChatId>().bare);
}

bool Peer::canSend() const {
	const auto &manager = session().secretChats();
	const auto chat = manager.chat(chatId());
	return manager.available() && chat && chat->state == State::Active;
}

} // namespace Extras::SecretChats
