#pragma once

#include "data/data_document.h"

struct DocumentData::LocalSourceState {
	LocalSource source;
	std::shared_ptr<Data::DocumentMedia> media;
	QString saveTo;
	qint64 offset = 0;
	bool active = true;
	bool downloading = false;
};
