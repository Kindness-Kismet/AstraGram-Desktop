#pragma once

class PeerData;

namespace Ui {
class DropdownMenu;
}

namespace Webrtc {
enum class RecordAvailability : uchar;
}

namespace ExtrasUi {

// 加号自身不触发操作：开启悬停弹出时悬停展开，关闭时点击只切换菜单。
void setupAttachMenu(
	not_null<QWidget*> button,
	not_null<Ui::DropdownMenu*> menu);

struct RecordMenuOptions {
	bool voice = false;
	bool round = false;
};

// 设置开关、聊天发送权限、录制设备与系统隐私授权都满足时才提供对应录制项。
[[nodiscard]] RecordMenuOptions recordMenuOptions(
	not_null<PeerData*> peer,
	Webrtc::RecordAvailability availability);

// 切回应用时重新检查系统授权，有变化才触发，供输入区重建加号菜单。
[[nodiscard]] rpl::producer<> recordPermissionChanges();

// 在菜单末尾追加可展开的“录制消息”分组；展开项插在分组之后，须最后添加。
void addRecordMenu(
	not_null<Ui::DropdownMenu*> menu,
	RecordMenuOptions options,
	Fn<void(bool round)> record);

}
