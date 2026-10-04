#pragma once

namespace Ui {

class GenericBox;

void fillDonateQrBox(
	not_null<Ui::GenericBox*> box,
	const QString &address,
	const QString &iconResourcePath);

} // namespace Ui
