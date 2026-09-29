#include "extras/features/emoji_packs/emoji_font.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ot.h>

#include <cmath>

namespace Extras::EmojiPacks {

struct EmojiFont::Private {
	QByteArray data;
	FT_Library library = nullptr;
	FT_Face face = nullptr;
	hb_font_t *font = nullptr;
	hb_buffer_t *buffer = nullptr;

	~Private() {
		if (buffer) hb_buffer_destroy(buffer);
		if (font) hb_font_destroy(font);
		if (face) FT_Done_Face(face);
		if (library) FT_Done_FreeType(library);
	}
};

EmojiFont::EmojiFont(QByteArray data) : _private(std::make_unique<Private>()) {
	auto &d = *_private;
	d.data = std::move(data);
	if (FT_Init_FreeType(&d.library)
		|| FT_New_Memory_Face(d.library,
			reinterpret_cast<const FT_Byte*>(d.data.constData()),
			d.data.size(), 0, &d.face)
		|| !FT_HAS_COLOR(d.face)) {
		return;
	}
	if (d.face->num_fixed_sizes) {
		auto best = 0;
		for (auto i = 1; i < d.face->num_fixed_sizes; ++i) {
			if (std::abs(d.face->available_sizes[i].height - 72)
				< std::abs(d.face->available_sizes[best].height - 72)) {
				best = i;
			}
		}
		if (FT_Select_Size(d.face, best)) {
			return;
		}
	} else if (FT_Set_Pixel_Sizes(d.face, 0, 72)) {
		return;
	}
	const auto blob = hb_blob_create(d.data.constData(), d.data.size(),
		HB_MEMORY_MODE_READONLY, nullptr, nullptr);
	const auto face = hb_face_create(blob, 0);
	d.font = hb_font_create(face);
	hb_ot_font_set_funcs(d.font);
	d.buffer = hb_buffer_create();
	hb_face_destroy(face);
	hb_blob_destroy(blob);
}

EmojiFont::~EmojiFont() = default;

bool EmojiFont::valid() const {
	return _private->buffer != nullptr;
}

QImage EmojiFont::render(const QString &text, int size) const {
	auto &d = *_private;
	if (!valid()) {
		return {};
	}
	hb_buffer_clear_contents(d.buffer);
	hb_buffer_set_flags(d.buffer, HB_BUFFER_FLAG_REMOVE_DEFAULT_IGNORABLES);
	hb_buffer_add_utf16(d.buffer, text.utf16(), text.size(), 0, text.size());
	hb_buffer_guess_segment_properties(d.buffer);
	hb_shape(d.font, d.buffer, nullptr, 0);
	auto count = 0U;
	const auto glyphs = hb_buffer_get_glyph_infos(d.buffer, &count);
	// 未合成的肤色、旗帜和连接符序列保留内置图案，避免拆成多个表情。
	if (count != 1 || !glyphs[0].codepoint
		|| FT_Load_Glyph(d.face, glyphs[0].codepoint, FT_LOAD_COLOR | FT_LOAD_RENDER)) {
		return {};
	}
	const auto &bitmap = d.face->glyph->bitmap;
	if (bitmap.pixel_mode != FT_PIXEL_MODE_BGRA
		|| !bitmap.width || !bitmap.rows
		|| bitmap.width > 4096 || bitmap.rows > 4096) {
		return {};
	}
	auto image = QImage(bitmap.width, bitmap.rows, QImage::Format_ARGB32_Premultiplied);
	for (auto y = 0; y < image.height(); ++y) {
		const auto source = bitmap.buffer + y * bitmap.pitch;
		const auto target = reinterpret_cast<QRgb*>(image.scanLine(y));
		for (auto x = 0; x < image.width(); ++x) {
			const auto pixel = source + x * 4;
			target[x] = qRgba(pixel[2], pixel[1], pixel[0], pixel[3]);
		}
	}
	return image.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

} // namespace Extras::EmojiPacks
