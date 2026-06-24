#include "components/LightComponent.h"

#include "core/RendererManager.h"

namespace cf
{

void LightComponent::on_attach()
{
	RendererManager::instance().register_light(this);
}

LightComponent::~LightComponent()
{
	RendererManager::instance().unregister_light(this);
}

} // namespace cf
