#pragma once

#include "window/themes/window_theme_editor_block.h"
#include "base/object_ptr.h"

namespace Ui {
class AbstractButton;
class InputField;
class MediaSlider;
class FlatLabel;
class RoundButton;
} // namespace Ui

namespace Window::Theme {

class EditorDetails final : public Ui::RpWidget {
public:
	struct Change {
		QString name;
		QString value;
	};
	EditorDetails(QWidget *parent);
	void showColor(const EditorColor &row, std::vector<EditorColor> sources);
	void setBackVisible(bool visible);
	void setCallbacks(Fn<void()> back, Fn<void()> restore);
	void setInnerFocus();
	bool submitColor();
	[[nodiscard]] rpl::producer<Change> changes() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	void chooseReference();
	void openPicker();
	void updateColor(QColor color);
	void refreshControls();
	void paintPreview(Painter &p, QRect rect);

	EditorColor _row;
	std::vector<EditorColor> _sources;
	object_ptr<Ui::RoundButton> _back;
	object_ptr<Ui::FlatLabel> _title;
	object_ptr<Ui::FlatLabel> _key;
	object_ptr<Ui::FlatLabel> _description;
	object_ptr<Ui::FlatLabel> _colorLabel;
	object_ptr<Ui::InputField> _hex;
	object_ptr<Ui::RoundButton> _picker;
	object_ptr<Ui::FlatLabel> _opacityLabel;
	object_ptr<Ui::MediaSlider> _opacity;
	object_ptr<Ui::RoundButton> _reference;
	object_ptr<Ui::FlatLabel> _referenceNote;
	object_ptr<Ui::FlatLabel> _previewLabel;
	object_ptr<Ui::FlatLabel> _previewNote;
	object_ptr<Ui::RoundButton> _restore;
	std::vector<QPointer<Ui::AbstractButton>> _quickColors;
	QRect _previewRect;
	QRect _colorRect;
	bool _backVisible = false;
	bool _updating = false;
	bool _valid = true;
	QColor _previewColor;
	rpl::event_stream<Change> _changes;
};

} // namespace Window::Theme
