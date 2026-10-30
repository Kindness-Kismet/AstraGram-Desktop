#include "extras/ui/boxes/donate_qr_box.h"

#include "lang_auto.h"
#include "qr/qr_generate.h"
#include "styles/style_boxes.h"
#include "styles/style_intro.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/rp_widget.h"
#include "ui/vertical_list.h"
#include "ui/controls/invite_link_label.h"
#include "ui/layers/generic_box.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QPainterPath>
#include <QtSvg/QSvgRenderer>

namespace Ui {
namespace {

[[nodiscard]] QImage makeQrWithIcon(
		const QByteArray &payload,
		const QString &iconPath,
		int pixel,
		int max) {
	Expects(!payload.isEmpty());

	const auto data = Qr::Encode(payload, Qr::Redundancy::Default);
	Expects(data.size > 0);
	if (max > 0 && data.size * pixel > max) {
		pixel = std::max(max / data.size, 1);
	}

	auto qr = Qr::Generate(
		data,
		pixel * style::DevicePixelRatio(),
		Qt::black,
		Qt::white);

	{
		QPainter p(&qr);
		PainterHighQualityEnabler hq(p);
		const auto size = qr.rect().size();
		constexpr auto kCenterRatio = 0.20;
		const auto centerRect = Rect(size)
			- Margins((size.width() - (size.width() * kCenterRatio)) / 2);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		p.drawEllipse(centerRect);

		QSvgRenderer svg(iconPath);
		if (svg.isValid()) {
			svg.render(&p, centerRect);
		}
	}
	return qr;
}

} // namespace

void fillDonateQrBox(
	not_null<Ui::GenericBox*> box,
	const QString &address,
	const QString &iconResourcePath) {
	box->setNoContentMargin(true);
	box->setWidth(int(st::aboutWidth * 1.25));
	box->setTitle(tr::lng_group_invite_context_qr());
	box->verticalLayout()->resizeToWidth(box->width());

	Ui::AddSkip(box->verticalLayout());
	Ui::AddSkip(box->verticalLayout());

	const auto qrWidget = box->verticalLayout()->add(
		object_ptr<Ui::RpWidget>(box->verticalLayout()));

	const auto qrMaxSize = box->width()
		- st::boxRowPadding.left()
		- st::boxRowPadding.right();
	const auto downTo = qrMaxSize - qrMaxSize % st::introQrPixel;
	const auto qrImage = makeQrWithIcon(
		address.toUtf8(),
		iconResourcePath,
		st::introQrPixel,
		downTo);

	qrWidget->paintRequest(
	) | rpl::on_next([=](QRect) {
		QPainter p(qrWidget);
		PainterHighQualityEnabler hq(p);

		const auto side = std::min(qrWidget->width(), qrWidget->height());
		const auto rect = QRect((qrWidget->width() - side) / 2, 0, side, side);

		QPainterPath path;
		path.addRoundedRect(rect, st::roundRadiusLarge, st::roundRadiusLarge);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		p.drawPath(path);
		p.setClipPath(path);

		const auto padding = st::boxRowPadding.left();
		const auto innerRect = rect - Margins(padding);

		p.drawImage(innerRect, qrImage);
	}, qrWidget->lifetime());

	Ui::AddSkip(box->verticalLayout());
	Ui::AddSkip(box->verticalLayout());

	const auto label = box->lifetime().make_state<Ui::InviteLinkLabel>(
		box->verticalLayout(),
		rpl::single(address),
		Fn<base::unique_qptr<Ui::PopupMenu>()>());
	const auto labelWidget = box->verticalLayout()->add(label->take(), st::boxRowPadding);
	rpl::combine(
		qrWidget->widthValue(),
		box->getDelegate()->contentHeightMaxValue(),
		labelWidget->heightValue()
	) | rpl::on_next([=](int width, int height, int labelHeight) {
		const auto available = height - labelHeight - 4 * st::defaultVerticalListSkip;
		qrWidget->resize(width, std::max(1, std::min(width, available)));
	}, qrWidget->lifetime());

	box->addButton(tr::lng_context_copy_link(), [=] {
		QGuiApplication::clipboard()->setText(address);
		Ui::Toast::Show(tr::lng_text_copied(tr::now));
	});
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

} // namespace Ui
