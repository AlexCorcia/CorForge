#include "core/ComponentRegistry.h"

#include "components/BuoyancyComponent.h"
#include "components/CameraComponent.h"
#include "components/ColliderComponent.h"
#include "components/FlockComponent.h"
#include "components/FlyCameraController.h"
#include "components/LightComponent.h"
#include "components/MaterialComponent.h"
#include "components/MoverComponent.h"
#include "components/ParticleComponent.h"
#include "components/PhysicsMoverComponent.h"
#include "components/RendererComponent.h"
#include "components/ClothComponent.h"
#include "components/RigidbodyComponent.h"
#include "components/SkyComponent.h"
#include "components/TerrainComponent.h"
#include "components/WaterComponent.h"
#include "core/Object.h"

namespace cf
{

namespace
{

template<typename T> ComponentType make_type(const char *name)
{
	return ComponentType{
	    name,
	    [](const Object &o) { return o.get_component<T>() != nullptr; },
	    [](Object &o)
	    {
		    if (!o.get_component<T>())
			    o.add_component<T>();
	    },
	    [](Object &o) { o.remove_component<T>(); },
	};
}

} // namespace

const std::vector<ComponentType> &component_registry()
{
	static const std::vector<ComponentType> types = {
	    make_type<RendererComponent>("RendererComponent"),
	    make_type<MaterialComponent>("MaterialComponent"),
	    make_type<LightComponent>("LightComponent"),
	    make_type<SkyComponent>("SkyComponent"),
	    make_type<WaterComponent>("WaterComponent"),
	    make_type<TerrainComponent>("TerrainComponent"),
	    make_type<BuoyancyComponent>("BuoyancyComponent"),
	    make_type<ParticleComponent>("ParticleComponent"),
	    make_type<FlockComponent>("FlockComponent"),
	    make_type<ClothComponent>("ClothComponent"),
	    make_type<MoverComponent>("MoverComponent"),
	    make_type<PhysicsMoverComponent>("PhysicsMoverComponent"),
	    make_type<RigidbodyComponent>("RigidbodyComponent"),
	    make_type<ColliderComponent>("ColliderComponent"),
	    make_type<CameraComponent>("CameraComponent"),
	    make_type<FlyCameraController>("FlyCameraController"),
	};
	return types;
}

} // namespace cf
