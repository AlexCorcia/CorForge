#include "components/SkyComponent.h"

#include "core/RendererManager.h"

namespace cf
{

void SkyComponent::on_attach()
{
	RendererManager::instance().register_sky(this);
}

SkyComponent::~SkyComponent()
{
	RendererManager::instance().unregister_sky(this);
}

} // namespace cf
