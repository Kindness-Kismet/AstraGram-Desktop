#pragma once

#include "base/unique_qptr.h"
#include "mtproto/mtproto_proxy_data.h"
#include "ui/widgets/buttons.h"

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace ExtrasUi {

// 代理类型选择字段：显示当前类型的图标、名称和说明，点开弹出同宽的类型菜单。
class ProxyTypeField final : public Ui::RippleButton {
public:
	using Type = MTP::ProxyData::Type;

	ProxyTypeField(
		QWidget *parent,
		rpl::producer<Type> type,
		Fn<void(Type)> chosen);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;
	QImage prepareRippleMask() const override;

private:
	void showMenu();

	Type _type = Type::Socks5;
	Fn<void(Type)> _chosen;
	base::unique_qptr<Ui::PopupMenu> _menu;

};

} // namespace ExtrasUi
