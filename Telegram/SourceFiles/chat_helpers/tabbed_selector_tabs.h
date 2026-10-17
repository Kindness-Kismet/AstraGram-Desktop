#pragma once

#include "ui/widgets/discrete_sliders.h"

namespace Ui { class IconButton; }

namespace ChatHelpers {

class TabbedSelectorTabs final : public Ui::DiscreteSlider {
public:
	TabbedSelectorTabs(QWidget *parent, const style::SettingsSlider &st);
	void setIcons(std::vector<const style::icon*> icons);
	void setActions(Fn<void()> settings, Fn<void()> close);
	void setSettingsVisible(bool visible);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	const style::TextStyle &getLabelStyle() const override;
	int getAnimationDuration() const override;
	[[nodiscard]] int sectionAt(QPoint position) const;

	const style::SettingsSlider &_st;
	const not_null<Ui::IconButton*> _settings;
	const not_null<Ui::IconButton*> _close;
	std::vector<const style::icon*> _icons;
	bool _settingsEnabled = false;
	bool _settingsAvailable = false;
	bool _showIcons = true;
	int _hovered = -1;
	int _pressed = -1;
};

}
