#include "components/BuoyancyComponent.h"

#include "components/ColliderComponent.h"
#include "components/RigidbodyComponent.h"
#include "components/WaterComponent.h"
#include "core/Object.h"
#include "core/ObjectsManager.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>

namespace cf
{

namespace
{
// First water surface in the scene (its owner's Y is the base level).
WaterComponent *find_water()
{
	for (const auto &up : ObjectsManager::instance().objects())
		if (auto *w = up->get_component<WaterComponent>())
			return w;
	return nullptr;
}
} // namespace

void BuoyancyComponent::on_attach()
{
	Object *o = owner();
	if (!o->get_component<ColliderComponent>())
		o->add_component<ColliderComponent>(); // auto-fits to the mesh
	if (!o->get_component<RigidbodyComponent>())
		o->add_component<RigidbodyComponent>(); // dynamic, falls under gravity
}

void BuoyancyComponent::update(float dt)
{
	if (dt <= 0.0f)
		return;
	Object *o = owner();
	RigidbodyComponent *rb = o->get_component<RigidbodyComponent>();
	ColliderComponent *col = o->get_component<ColliderComponent>();
	if (!rb || !col || rb->is_static || rb->kinematic || rb->grabbed)
		return;
	WaterComponent *water = find_water();
	if (!water)
		return;

	const glm::vec3 c = o->transform.position;
	const glm::vec3 s = o->transform.scale;

	// Footprint half-extents (x,z) + vertical half-height of the body.
	float ex, ez, half_h;
	if (col->shape == ColliderComponent::Shape::Sphere)
	{
		const float r = col->radius * std::max({s.x, s.y, s.z});
		ex = ez = half_h = r;
	}
	else
	{
		ex = col->half_extents.x * s.x;
		ez = col->half_extents.z * s.z;
		half_h = col->half_extents.y * s.y;
	}
	const float h = std::max(half_h, 0.2f); // smooth band even for flat rafts
	const float water_base = water->owner()->transform.position.y;
	const float t = static_cast<float>(glfwGetTime());
	const glm::mat3 r = glm::mat3_cast(rb->orientation); // current tilt

	// Sample buoyancy at 4 footprint points: each pushes up by how submerged it is,
	// and the DIFFERENCE between them is the torque that tilts the body to follow
	// the wave slope. (Cheap: a handful of wave evals per object.)
	const glm::vec3 offsets[4] = {{ex, 0, 0}, {-ex, 0, 0}, {0, 0, ez}, {0, 0, -ez}};
	glm::vec3 force(0.0f), torque(0.0f);
	float sub_sum = 0.0f;
	for (const glm::vec3 &off : offsets)
	{
		const glm::vec3 r_w = r * off; // offset in world space (rotated)
		const glm::vec3 pw = c + r_w;  // sample point
		const float w_y = water_base + WaterComponent::wave_height(glm::vec2(pw.x, pw.z), t);
		const float sub = std::clamp(0.5f + (w_y - pw.y) / (2.0f * h), 0.0f, 1.0f);
		sub_sum += sub;
		const glm::vec3 f = glm::vec3(0.0f, 1.0f, 0.0f) * (strength * sub * rb->mass * 0.25f);
		force += f;
		torque += glm::cross(r_w, f);
	}
	const float avg_sub = sub_sum * 0.25f;
	if (avg_sub <= 0.0f)
		return; // floating clear of the water

	const float inv_mass = 1.0f / std::max(rb->mass, 1e-3f);
	rb->velocity += force * inv_mass * dt;

	if (!rb->freeze_rotation)
	{
		const float inv_i = inv_mass / (ex * ex + ez * ez + 1e-3f); // ~1/(m·r²)
		rb->angular_velocity += torque * inv_i * dt;
	}

	// Water drag (only while in the water): damps the bob + the tilt so it settles.
	const float kl = std::clamp(drag * avg_sub * dt, 0.0f, 1.0f);
	rb->velocity -= rb->velocity * kl;
	rb->angular_velocity -= rb->angular_velocity * std::clamp(kl * 2.0f, 0.0f, 1.0f);
}

} // namespace cf
