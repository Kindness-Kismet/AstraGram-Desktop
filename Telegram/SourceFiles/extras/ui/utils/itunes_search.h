#pragma once

#include <QtGui/QImage>

namespace Extras::Ui::Itunes {

QImage FetchCover(const QString &performer,
                   const QString &title,
                   int sizeHintPx = 300,
                   int timeoutMs = 5000);

}
