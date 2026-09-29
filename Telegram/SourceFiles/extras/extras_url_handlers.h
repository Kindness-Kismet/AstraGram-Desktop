#pragma once

#include "base/qthelp_regex.h"
#include "window/window_session_controller.h"

namespace ExtrasUrlHandlers {

using Match = qthelp::RegularExpressionMatch;

bool ResolveUser(
	Window::SessionController *controller,
	const Match &match,
	const QVariant &context);

bool ResolveChat(
	Window::SessionController *controller,
	const Match &match,
	const QVariant &context);

bool HandleExtras(
	Window::SessionController *controller,
	const Match &match,
	const QVariant &context);

bool HandleExtrasSettings(
	Window::SessionController *controller,
	const Match &match,
	const QVariant &context);

bool TryHandleSpotify(const QString &url);

}
