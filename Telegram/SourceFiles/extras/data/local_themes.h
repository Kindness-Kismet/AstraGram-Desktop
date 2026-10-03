#pragma once

#include "data/data_cloud_themes.h"

namespace Window::Theme {
struct Object;
} // namespace Window::Theme

namespace Extras::LocalThemes {

[[nodiscard]] std::vector<Data::CloudTheme> list();
[[nodiscard]] bool save(Window::Theme::Object &object);
[[nodiscard]] bool remove(const QString &path);
[[nodiscard]] rpl::producer<> changes();

} // namespace Extras::LocalThemes
