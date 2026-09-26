#include "ayu/features/window_material/platform/window_material_platform.h"
#include "ayu/ayu_settings.h"

#include <QtWidgets/QWidget>
#include <Cocoa/Cocoa.h>

namespace AyuFeatures::WindowMaterial::Platform {
namespace {

class MacBackend final : public Backend {
public:
	explicit MacBackend(not_null<QWidget*> widget) : _widget(widget) {
	}

	~MacBackend() override {
		clear();
	}

	bool apply(::WindowMaterial mode, bool dark) override {
		if (mode != ::WindowMaterial::Blur
			|| NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceTransparency
			|| NSWorkspace.sharedWorkspace.accessibilityDisplayShouldIncreaseContrast) {
			clear();
			return false;
		}
		const auto view = reinterpret_cast<NSView*>(_widget->winId());
		const auto window = view.window;
		const auto parent = view.superview;
		if (!window || !parent) {
			clear();
			return false;
		}
		if (_window != window || _nativeView != view || _effect.superview != parent) {
			clear();
			_window = window;
			_nativeView = view;
			_opaque = window.opaque;
			_background = [window.backgroundColor retain];
			_effect = [[NSVisualEffectView alloc] initWithFrame:view.frame];
			_effect.blendingMode = NSVisualEffectBlendingModeBehindWindow;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
			_effect.material = NSVisualEffectMaterialLight;
#pragma clang diagnostic pop
			_effect.state = NSVisualEffectStateFollowsWindowActiveState;
			_effect.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
			// 放在 Qt 内容视图的后面，避免原生材质遮住消息与交互控件。
			[parent addSubview:_effect positioned:NSWindowBelow relativeTo:view];
		}
		const auto ordered = [parent.subviews indexOfObject:_effect]
			< [parent.subviews indexOfObject:view];
		if (NSEqualRects(_effect.frame, view.frame) && _active && _dark == dark
			&& ordered && !_effect.hidden && !window.opaque
			&& window.backgroundColor.alphaComponent == 0.) {
			return true;
		}
		if (!ordered) {
			[parent addSubview:_effect positioned:NSWindowBelow relativeTo:view];
		}
		_effect.hidden = NO;
		_effect.frame = view.frame;
		window.opaque = NO;
		window.backgroundColor = NSColor.clearColor;
		_active = (_effect.superview == parent);
		_dark = dark;
		return _active;
	}

private:
	void clear() {
		_active = false;
		[_effect removeFromSuperview];
		[_effect release];
		_effect = nil;
		if (_window) {
			_window.opaque = _opaque;
			_window.backgroundColor = _background;
		}
		_window = nil;
		_nativeView = nil;
		[_background release];
		_background = nil;
	}

	const not_null<QWidget*> _widget;
	NSWindow * __weak _window = nil;
	NSView * __weak _nativeView = nil;
	NSVisualEffectView * _effect = nil;
	NSColor * _background = nil;
	BOOL _opaque = YES;
	bool _active = false;
	bool _dark = false;
};

} // namespace

std::unique_ptr<Backend> create(not_null<QWidget*> window) {
	return std::make_unique<MacBackend>(window);
}

std::vector<::WindowMaterial> availableModes() {
	return { ::WindowMaterial::Off, ::WindowMaterial::Blur };
}

} // namespace AyuFeatures::WindowMaterial::Platform
