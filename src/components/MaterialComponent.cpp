#include "components/MaterialComponent.h"

#include "core/RendererManager.h"

namespace cf
{

MaterialComponent::MaterialComponent()
{
	// So a material added via the editor is usable right away.
	material.shader = RendererManager::instance().default_shader();
}

} // namespace cf
