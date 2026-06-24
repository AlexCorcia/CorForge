#include "components/WaterComponent.h"

#include "components/ColliderComponent.h"
#include "components/MaterialComponent.h"
#include "components/ParticleComponent.h"
#include "components/RendererComponent.h"
#include "components/RigidbodyComponent.h"
#include "core/Object.h"
#include "core/ObjectsManager.h"
#include "core/RendererManager.h"
#include "gfx/Mesh.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

namespace cf
{

void WaterComponent::on_attach()
{
	Object *o = owner();

	// Draw it as a finely SUBDIVIDED surface so the vertex shader's waves have
	// enough vertices to displace (the shared 1-quad "Plane" would stay flat).
	if (!o->get_component<RendererComponent>())
		o->add_component<RendererComponent>();
	rebuild_mesh();

	// Water material: the water shader, planar reflection, alpha-blended so the
	// bottom shows through.
	MaterialComponent *mat = o->get_component<MaterialComponent>();
	if (!mat)
		mat = o->add_component<MaterialComponent>();
	Material &m = mat->material;
	m.shader = RendererManager::instance().water_shader();
	m.transparent = true;     // alpha-blended (see the bottom / sand through the rim)
	m.reflective = false;     // cartoon water: NO planar reflection (that was the
	m.reflect_planar = false; // perf killer -- it re-rendered the whole scene/frame)
	m.reflect_box = false;
	apply();

	// Auto-add the droplet emitter the splash system fires (reuse one if present, so
	// a serialized scene doesn't end up with two). It only bursts on impact (rate 0).
	m_splash_fx = o->get_component<ParticleComponent>();
	if (!m_splash_fx)
	{
		m_splash_fx = o->add_component<ParticleComponent>();
		m_splash_fx->apply_preset(ParticleComponent::Preset::Splash);
	}
}

// The water detects bodies breaking its surface and bursts droplets there. Tracks
// each rigidbody's previous side of the surface so it fires once, on entry.
void WaterComponent::update(float)
{
	if (!splash)
		return;
	if (!m_splash_fx)
		m_splash_fx = owner()->get_component<ParticleComponent>();
	if (!m_splash_fx)
		return;

	Object *self = owner();
	const glm::vec3 wc = self->transform.position;
	const float half_x = 0.5f * self->transform.scale.x;
	const float half_z = 0.5f * self->transform.scale.z;
	const float base = wc.y;
	const float t = static_cast<float>(glfwGetTime());

	for (const auto &up : ObjectsManager::instance().objects())
	{
		Object *o = up.get();
		if (o == self)
			continue;
		RigidbodyComponent *rb = o->get_component<RigidbodyComponent>();
		if (!rb || rb->is_static)
			continue;

		const glm::vec3 p = o->transform.position;
		const float dx = p.x - wc.x, dz = p.z - wc.z;
		const bool inside =
		    round ? (dx * dx) / (half_x * half_x) + (dz * dz) / (half_z * half_z) <= 1.0f
		          : std::abs(dx) <= half_x && std::abs(dz) <= half_z;

		// Lowest point of the body, vs the (wavy) surface height under it.
		const glm::vec3 s = o->transform.scale;
		float half_h = 0.3f;
		if (auto *col = o->get_component<ColliderComponent>())
		{
			half_h = (col->shape == ColliderComponent::Shape::Sphere)
			             ? col->radius * std::max({s.x, s.y, s.z})
			             : col->half_extents.y * s.y;
		}
		const float surf = base + wave_height(glm::vec2(p.x, p.z), t);
		const bool below = inside && (p.y - half_h) <= surf;

		const auto it = m_above.find(o);
		const bool was_above = (it == m_above.end()) ? true : it->second;
		if (below && was_above && rb->velocity.y < -splash_threshold)
		{
			const int n =
			    std::clamp(static_cast<int>(-rb->velocity.y * 6.0f * splash_amount), 8, 70);
			const glm::vec3 at(p.x, surf, p.z);
			m_splash_fx->burst(n, &at);
		}
		m_above[o] = !below;
	}
}

void WaterComponent::rebuild_mesh()
{
	RendererComponent *rc = owner()->get_component<RendererComponent>();
	if (!rc)
		return;
	// Same 0.5-radius footprint either way, so the Transform scale maps identically.
	rc->mesh = round ? Mesh::create_disk(0.5f, 96, 40) : Mesh::create_plane(1.0f, 64);
	rc->mesh_name = "Plane";
	rc->submeshes.clear();
}

void WaterComponent::apply()
{
	if (auto *mat = owner()->get_component<MaterialComponent>())
	{
		mat->material.albedo = color;
		mat->material.opacity = opacity;
		mat->material.calm = calm;
	}
}

// Mirrors the 4 directional waves in water.vert so buoyancy bobs with the surface.
float WaterComponent::wave_height(const glm::vec2 &p, float t)
{
	auto wave = [&](glm::vec2 dir, float amp, float len, float spd)
	{
		dir = glm::normalize(dir);
		const float w = 6.2831853f / len;
		return amp * std::sin(glm::dot(dir, p) * w + t * spd);
	};
	return wave({1.0f, 0.4f}, 0.16f, 5.0f, 1.0f) + wave({-0.6f, 1.0f}, 0.11f, 3.3f, 1.3f) +
	       wave({0.8f, -0.7f}, 0.07f, 2.1f, 1.7f) + wave({0.2f, 1.0f}, 0.05f, 1.4f, 2.2f);
}

} // namespace cf
