#pragma once

#include "extras/features/translator/implementations/base.h"

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QSet>
#include <QtCore/QString>

namespace Extras::Translator {

class YandexTranslator final : public MultiThreadTranslator
{
	Q_OBJECT

public:
	static YandexTranslator &instance();

	[[nodiscard]] QSet<QString> supportedLanguages() const override;

	[[nodiscard]] QPointer<QNetworkReply> startSingleTranslation(
		const MultiThreadArgs &args
	) override;

private:
	explicit YandexTranslator(QObject *parent = nullptr);

	QNetworkAccessManager _nam;
	QString _uuid;
};

}