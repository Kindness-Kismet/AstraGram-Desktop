#pragma once

#include "data/data_peer.h"

class UserData;

namespace Extras::SecretChats {

class Peer final : public PeerData {
public:
	Peer(not_null<Data::Session*> owner, PeerId id);
	void setUser(not_null<UserData*> user);
	[[nodiscard]] not_null<UserData*> user() const;
	[[nodiscard]] UserData *userOrNull() const { return _user; }
	[[nodiscard]] int chatId() const;
	[[nodiscard]] bool canSend() const;

private:
	UserData *_user = nullptr;
};

} // namespace Extras::SecretChats
