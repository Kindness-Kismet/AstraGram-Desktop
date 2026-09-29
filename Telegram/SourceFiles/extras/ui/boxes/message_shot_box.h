#pragma once

#include "extras/features/message_shot/message_shot.h"
#include "ui/layers/box_content.h"

class MessageShotBox : public Ui::BoxContent
{
public:
	MessageShotBox(QWidget *parent, ExtrasFeatures::MessageShot::ShotConfig config);

	bool tookShot() const {
		return _tookShot;
	}

protected:
	void prepare() override;

private:
	void setupContent();

	ExtrasFeatures::MessageShot::ShotConfig _config;
	std::shared_ptr<style::palette> _selectedPalette;

	bool _tookShot = false;
};
