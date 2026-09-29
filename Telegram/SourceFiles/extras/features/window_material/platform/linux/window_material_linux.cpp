#include "extras/features/window_material/platform/window_material_platform.h"
#include "extras/extras_settings.h"

namespace ExtrasFeatures::WindowMaterial::Platform {
namespace {

class SolidBackend final : public Backend {
public:
	bool apply(::WindowMaterial, bool) override { return false; }
};

} // namespace

std::unique_ptr<Backend> create(not_null<QWidget*>) {
	return std::make_unique<SolidBackend>();
}

std::vector<::WindowMaterial> availableModes() {
	return { ::WindowMaterial::Off };
}

} // namespace ExtrasFeatures::WindowMaterial::Platform
