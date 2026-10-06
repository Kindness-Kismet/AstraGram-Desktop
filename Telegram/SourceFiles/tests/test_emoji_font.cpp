#include "extras/features/emoji_packs/emoji_font.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <iostream>

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	const auto args = app.arguments();
	const auto hasMissing = args.size() == 4 && args.at(2) == "--missing";
	if (args.size() != 2 && !hasMissing) {
		std::cerr << "Provide a color font path; use --missing with a comma-separated list of known missing emoji.\n";
		return 1;
	}
	const auto missing = hasMissing ? args.at(3).split(',') : QStringList();
	QFile file(app.arguments().at(1));
	if (!file.open(QIODevice::ReadOnly)) {
		std::cerr << "Could not read test font.\n";
		return 1;
	}
	const auto bytes = file.readAll();
	Extras::EmojiPacks::EmojiFont font(bytes);
	if (!font.valid()) {
		std::cerr << "Could not load test font.\n";
		return 1;
	}
	const auto samples = QStringList{
		QString::fromUtf8("😀"), QString::fromUtf8("❤️"),
		QString::fromUtf8("👍🏻"), QString::fromUtf8("🇨🇳"),
		QString::fromUtf8("👨‍👩‍👧‍👦"), QString::fromUtf8("👩🏽‍💻"),
	};
	auto colored = false;
	auto failed = false;
	for (const auto &sample : samples) {
		const auto image = font.render(sample, 72);
		if (missing.contains(sample)) {
			if (!image.isNull()) {
				std::cerr << "Missing emoji should keep the built-in image: " << sample.toUtf8().constData() << '\n';
				failed = true;
			}
			continue;
		}
		if (image.isNull() || image.width() > 72 || image.height() > 72) {
			std::cerr << "Emoji rendering failed: " << sample.toUtf8().constData() << '\n';
			failed = true;
			continue;
		}
		auto visible = false;
		for (auto y = 0; y < image.height(); ++y) {
			for (auto x = 0; x < image.width(); ++x) {
				const auto pixel = image.pixel(x, y);
				visible |= qAlpha(pixel) != 0;
				colored |= qAlpha(pixel) && qRed(pixel) != qGreen(pixel);
			}
		}
		if (!visible) {
			std::cerr << "Emoji image is empty.\n";
			return 1;
		}
	}
	if (failed || !colored
		|| !font.render(QString::fromUtf8("😀😀"), 72).isNull()
		|| !font.render(QString::fromUtf8("A"), 72).isNull()
		|| !font.render(QString::fromUtf8("abc"), 72).isNull()
		|| !font.render(QString::fromUtf8("\xF4\x8F\xBF\xBF"), 72).isNull()
		|| Extras::EmojiPacks::EmojiFont(QByteArray("broken font")).valid()
		|| Extras::EmojiPacks::EmojiFont(bytes.left(128)).valid()
		|| !Extras::EmojiPacks::EmojiFont(QByteArray()).render(samples.front(), 72).isNull()) {
		std::cerr << "Missing character or corrupt font validation failed.\n";
		return 1;
	}
	std::cout << "Passed: color emoji, uncomposed sequences, missing characters, corrupt and truncated fonts.\n";
}
