/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_common_session.h"

#include "info/info_controller.h"
#include "info/info_layer_widget.h"
#include "info/info_memento.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"

#include "settings/cloud_password/settings_cloud_password_email_confirm.h"
#include "settings/settings_experimental.h"
#include "settings/sections/settings_chat.h"
#include "settings/sections/settings_main.h"

// AyuGram includes
#include "extras/ui/settings/settings_filters.h"


namespace Settings {

void ShowSettingsLayer(
		not_null<Window::SessionController*> controller,
		Type type,
		Fn<void()> closed) {
	auto memento = Info::Memento(
		Info::Settings::Tag{ controller->session().user() },
		Info::Section(type));
	auto layer = object_ptr<Info::LayerWidget>(
		controller,
		&memento);
	if (closed) {
		layer->lifetime().add(std::move(closed));
	}
	controller->showSpecialLayer(std::move(layer));
}

bool HasMenu(Type type) {
	return (type == ::Settings::CloudPasswordEmailConfirmId())
		|| (type == MainId())
		|| (type == ChatId())
		|| (type == Experimental::Id())
		|| (type == ExtrasFiltersId());
}

} // namespace Settings
