#include "components/ColliderComponent.h"

#include "components/RendererComponent.h"
#include "core/Object.h"
#include "core/PhysicsManager.h"
#include "gfx/Mesh.h"

#include <algorithm>

namespace cf
{

void ColliderComponent::on_attach()
{
	fit_to_mesh();
	PhysicsManager::instance().register_collider(this);
}

void ColliderComponent::fit_to_mesh()
{
	if (shape == Shape::Heightfield)
		return; // terrain-driven; nothing to fit
	// Fit to the owner's geometry, and pick a sensible shape from the mesh so a
	// sphere collides as a sphere and the ground as a plane (not as a box). A scene
	// load / the inspector can still override `shape` afterwards. Imported models
	// keep geometry in submeshes (mesh == null), so union those bounds too.
	auto *r = owner()->get_component<RendererComponent>();
	if (!r)
		return;

	glm::vec3 mn(0.0f), mx(0.0f);
	bool has = false;
	auto add = [&](const Mesh *m)
	{
		if (!m)
			return;
		if (!has)
		{
			mn = m->aabb_min();
			mx = m->aabb_max();
			has = true;
		}
		else
		{
			mn = glm::min(mn, m->aabb_min());
			mx = glm::max(mx, m->aabb_max());
		}
	};
	if (r->mesh)
		add(r->mesh.get());
	else
		for (const Submesh &sm : r->submeshes)
			add(sm.mesh.get());
	if (!has)
		return;

	half_extents = (mx - mn) * 0.5f;
	center = (mn + mx) * 0.5f; // recentre on the geometry if it's off-pivot
	radius = std::max({half_extents.x, half_extents.y, half_extents.z});
	if (r->mesh_name == "Sphere")
		shape = Shape::Sphere;
	else if (r->mesh_name == "Plane")
		shape = Shape::Plane;
}

ColliderComponent::~ColliderComponent()
{
	PhysicsManager::instance().unregister_collider(this);
}

} // namespace cf
