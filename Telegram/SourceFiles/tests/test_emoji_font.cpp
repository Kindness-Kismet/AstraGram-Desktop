#include "extras/features/emoji_packs/emoji_font.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <iostream>

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	const auto args = app.arguments();
	const auto hasMissing = args.size() == 4 && args.at(2) == "--missing";
	if (args.size() != 2 && !hasMissing) {
		std::cerr << "请提供彩色字体路径；已知缺失表情可用 --missing 指定，以逗号分隔。\n";
		return 1;
	}
	const auto missing = hasMissing ? args.at(3).split(',') : QStringList();
	QFile file(app.arguments().at(1));
	if (!file.open(QIODevice::ReadOnly)) {
		std::cerr << "无法读取测试字体。\n";
		return 1;
	}
	const auto bytes = file.readAll();
	Extras::EmojiPacks::EmojiFont font(bytes);
	if (!font.valid()) {
		std::cerr << "测试字体无法加载。\n";
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
				std::cerr << "缺失表情应保留内置图案：" << sample.toUtf8().constData() << '\n';
				failed = true;
			}
			continue;
		}
		if (image.isNull() || image.width() > 72 || image.height() > 72) {
			std::cerr << "表情渲染失败：" << sample.toUtf8().constData() << '\n';
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
			std::cerr << "表情图像为空。\n";
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
		std::cerr << "缺失字符或损坏字体校验失败。\n";
		return 1;
	}
	std::cout << "通过：彩色表情、未合成序列、缺失字符、损坏和截断字体。\n";
}
