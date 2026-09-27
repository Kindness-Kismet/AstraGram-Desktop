#pragma once

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Settings {

namespace Builder {
class SectionBuilder;
} // namespace Builder

void BuildUpdateSection(Builder::SectionBuilder &builder);
[[nodiscard]] bool HasUpdate();
void SetupUpdate(not_null<Ui::VerticalLayout*> container);

} // namespace Settings
