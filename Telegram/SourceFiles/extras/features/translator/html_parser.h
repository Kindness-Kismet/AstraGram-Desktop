#pragma once

#include "ui/text/text_entity.h"

#include <QtCore/QString>

namespace Extras::Translator::Html {

[[nodiscard]] TextWithEntities htmlToEntities(const QString &text);

}
