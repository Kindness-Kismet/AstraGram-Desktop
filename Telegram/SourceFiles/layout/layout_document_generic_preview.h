/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Layout {

enum class DocumentFileType {
	Application,
	Library,
	Code,
	Archive,
	Spreadsheet,
	Image,
	Document,
};

struct DocumentGenericPreview final {
	static DocumentGenericPreview Create(DocumentData *document);
	static DocumentGenericPreview Create(const QString &filename);
	const style::icon *icon() const;
	[[nodiscard]] DocumentFileType type() const;
	const QString ext;
};

} // namespace Layout
