#pragma once

#include "base/basic_types.h"
#include <memory>
#include <vector>

class QWidget;
enum class WindowMaterial;

namespace ExtrasFeatures::WindowMaterial::Platform {

class Backend {
public:
	virtual ~Backend() = default;
	[[nodiscard]] virtual bool apply(::WindowMaterial mode, bool dark) = 0;
};

[[nodiscard]] std::unique_ptr<Backend> create(not_null<QWidget*> window);
[[nodiscard]] std::vector<::WindowMaterial> availableModes();

} // namespace ExtrasFeatures::WindowMaterial::Platform
