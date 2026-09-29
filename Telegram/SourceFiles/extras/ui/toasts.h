#pragma once

#include "ui/toast/toast.h"

namespace Extras::Ui {

void ShowToastWithAction(
	::Ui::Toast::Config &&config,
	const QString &buttonText,
	Fn<void()> callback);

}
