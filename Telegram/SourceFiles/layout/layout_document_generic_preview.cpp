/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "layout/layout_document_generic_preview.h"

#include "data/data_document.h"
#include "styles/style_file_preview.h"

#include <range/v3/algorithm/contains.hpp>

namespace Layout {

const style::icon *DocumentGenericPreview::icon() const {
	switch (type()) {
	case DocumentFileType::Application: return &st::filePreviewApplication;
	case DocumentFileType::Library: return &st::filePreviewLibrary;
	case DocumentFileType::Code: return &st::filePreviewCode;
	case DocumentFileType::Archive: return &st::filePreviewArchive;
	case DocumentFileType::Spreadsheet: return &st::filePreviewSpreadsheet;
	case DocumentFileType::Image: return &st::filePreviewImage;
	case DocumentFileType::Document: return &st::filePreviewDocument;
	}
	Unexpected("File type in DocumentGenericPreview::icon.");
}

DocumentFileType DocumentGenericPreview::type() const {
	const auto matches = [&](std::initializer_list<QStringView> extensions) {
		return ranges::contains(extensions, ext);
	};
	if (matches({ u"exe", u"msi", u"apk", u"app", u"appx" })) {
		return DocumentFileType::Application;
	}
	if (matches({ u"so", u"dll", u"dylib" })) {
		return DocumentFileType::Library;
	}
	if (matches({ u"py", u"js", u"ts", u"cpp", u"h", u"c", u"rs",
			u"json", u"html", u"css", u"xml", u"sh", u"java", u"qml" })) {
		return DocumentFileType::Code;
	}
	if (matches({ u"zip", u"rar", u"7z", u"tar", u"gz", u"xz" })) {
		return DocumentFileType::Archive;
	}
	if (matches({ u"xls", u"xlsx", u"csv" })) {
		return DocumentFileType::Spreadsheet;
	}
	if (matches({ u"png", u"jpg", u"jpeg", u"webp", u"svg", u"bmp" })) {
		return DocumentFileType::Image;
	}
	return DocumentFileType::Document;
}

DocumentGenericPreview DocumentGenericPreview::Create(
		DocumentData *document) {
	return Create(document ? document->filename() : QString());
}

DocumentGenericPreview DocumentGenericPreview::Create(
		const QString &filename) {
	const auto name = filename.toLower();
	const auto lastDot = name.lastIndexOf('.');
	const auto ext = (lastDot < 0 || lastDot + 2 > name.size())
		? QString()
		: name.mid(lastDot + 1);
	return { .ext = ext };
}

} // namespace Layout
