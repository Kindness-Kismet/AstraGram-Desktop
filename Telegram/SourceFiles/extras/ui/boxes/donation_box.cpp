#include "extras/ui/boxes/donation_box.h"
#include "extras/ui/boxes/donate_qr_box.h"

#include "core/file_utilities.h"
#include "lang/lang_keys.h"
#include "styles/style_layers.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"

#include <QtGui/QPainterPath>

namespace ExtrasUi {
namespace {

constexpr auto kAvatarSize = 96;
const auto kDonationUrl = u"https://afdian.com/a/KiritoXD"_q;

void fillDonationBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	box->setTitle(tr::extras_DonationDetails());
	box->setWidth(st::boxWidth);
	box->verticalLayout()->resizeToWidth(box->width());
	box->addSkip(style::ConvertScale(12));

	const auto avatarSize = style::ConvertScale(kAvatarSize);
	auto avatarWidget = object_ptr<Ui::RpWidget>(box);
	const auto avatar = avatarWidget.data();
	avatar->setNaturalWidth(avatarSize);
	avatar->resize(avatarSize, avatarSize);
	box->addRow(std::move(avatarWidget), style::al_center);
	avatar->setObjectName(u"donation.avatar"_q);
	avatar->setAccessibleName(u"KiritoXDone"_q);
	const auto image = QImage(u":/gui/art/extras/donation/kiritoxdone.png"_q);
	avatar->paintRequest() | rpl::on_next([=] {
		auto painter = Painter(avatar);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setRenderHint(QPainter::SmoothPixmapTransform);
		auto clip = QPainterPath();
		clip.addEllipse(avatar->rect());
		painter.setClipPath(clip);
		painter.drawImage(avatar->rect(), image);
	}, avatar->lifetime());

	box->addSkip(style::ConvertScale(12));
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			u"KiritoXDone"_q,
			st::boxTitle),
		style::al_center);
	box->addSkip(style::ConvertScale(12));
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::extras_DonationMessage(),
		st::boxLabel));
	box->addSkip(style::ConvertScale(12));

	const auto link = box->addRow(
		object_ptr<Ui::LinkButton>(box, kDonationUrl),
		style::al_center);
	link->setObjectName(u"donation.url"_q);
	link->setClickedCallback([] { File::OpenUrl(kDonationUrl); });
	box->addSkip(style::ConvertScale(12));

	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::extras_DonationBadgeInstructions(),
		st::boxLabel));
	box->addSkip(style::ConvertScale(12));
	const auto qr = box->addRow(
		object_ptr<Ui::LinkButton>(box, tr::lng_group_invite_context_qr(tr::now)),
		style::al_center);
	qr->setObjectName(u"donation.qr"_q);
	qr->setClickedCallback([=] {
		controller->show(Box(
			Ui::fillDonateQrBox,
			kDonationUrl,
			u":/gui/icons/extras/donates/support_logo.svg"_q));
	});
	box->addSkip(style::ConvertScale(12));

	box->addButton(tr::extras_DonationOpenAfdian(), [] {
		File::OpenUrl(kDonationUrl);
	});
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

} // namespace

void showDonationBox(not_null<Window::SessionController*> controller) {
	controller->show(Box(fillDonationBox, controller));
}

} // namespace ExtrasUi
