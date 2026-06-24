#include "components/FlockComponent.h"

#include "components/BoidComponent.h"
#include "components/BuoyancyComponent.h"
#include "components/ColliderComponent.h"
#include "components/MaterialComponent.h"
#include "components/RendererComponent.h"
#include "components/RigidbodyComponent.h"
#include "core/AssetManager.h"
#include "core/Object.h"
#include "core/ObjectsManager.h"
#include "core/PhysicsManager.h"
#include "core/RendererManager.h"
#include "gfx/Mesh.h"

#include <algorithm>
#include <cmath>

namespace cf
{

float FlockComponent::rnd(float a, float b)
{
	return std::uniform_real_distribution<float>(a, b)(m_rng);
}
glm::vec3 FlockComponent::rnd_dir()
{
	glm::vec3 d(rnd(-1, 1), rnd(-1, 1), rnd(-1, 1));
	const float l = glm::length(d);
	return l > 1e-4f ? d / l : glm::vec3(0, 0, 1);
}

void FlockComponent::on_attach()
{
	// Spawn lazily on the first update so serialized fields (count, physics, ...) are
	// applied before the boids are created.
}

FlockComponent::~FlockComponent()
{
	clear_boids(); // no-op during a full scene clear (ObjectsManager guards re-entry)
}

void FlockComponent::respawn()
{
	clear_boids();
	spawn_boids();
	m_spawned = true;
}

void FlockComponent::clear_boids()
{
	ObjectsManager &om = ObjectsManager::instance();
	for (BoidComponent *bc : m_boids)
		if (bc)
			om.remove_object(bc->owner());
	m_boids.clear();
}

void FlockComponent::spawn_boids()
{
	ObjectsManager &om = ObjectsManager::instance();
	AssetManager &assets = AssetManager::instance();
	auto shader = RendererManager::instance().default_shader();
	auto mesh = assets.mesh(boid_mesh.empty() ? "Pyramid" : boid_mesh);
	if (!mesh)
		mesh = assets.mesh("Pyramid"); // fall back if the name didn't resolve
	const glm::vec3 center = owner()->world_position();
	// The pyramid is stretched along -Z so its heading reads; other meshes stay uniform.
	const glm::vec3 shape =
	    (boid_mesh == "Pyramid") ? boid_scale * glm::vec3(0.7f, 0.7f, 1.7f) : glm::vec3(boid_scale);

	m_boids.reserve(static_cast<size_t>(std::max(0, count)));
	for (int i = 0; i < count; ++i)
	{
		Object *b = om.create_object("Boid");
		b->transform.position = center + rnd_dir() * rnd(0.0f, spawn_radius);
		b->transform.scale = shape;

		auto *rc = b->add_component<RendererComponent>();
		rc->mesh = mesh;
		rc->mesh_name = boid_mesh.empty() ? "Pyramid" : boid_mesh;
		auto *mc = b->add_component<MaterialComponent>();
		mc->material.shader = shader;
		mc->material.albedo = boid_color;
		mc->material.roughness = 0.5f;

		auto *bc = b->add_component<BoidComponent>();
		bc->flock = this;
		bc->velocity = rnd_dir() * (0.5f * (min_speed + max_speed));

		if (physics)
		{
			auto *rb = b->add_component<RigidbodyComponent>();
			rb->use_gravity = gravity;
			rb->freeze_rotation = true; // we orient them to face their velocity
			rb->mass = 1.0f;
			b->add_component<ColliderComponent>(); // auto-fits to the cube
			if (buoyancy)
				b->add_component<BuoyancyComponent>();
			rb->velocity = bc->velocity;
		}
		m_boids.push_back(bc);
	}
}

void FlockComponent::update(float dt)
{
	if (!m_spawned)
	{
		spawn_boids();
		m_spawned = true;
	}
	if (m_boids.empty() || dt <= 0.0f)
		return;

	const int n = static_cast<int>(m_boids.size());
	// Snapshot positions + velocities so every boid steers off the SAME frame state.
	std::vector<glm::vec3> pos(n), vel(n);
	for (int i = 0; i < n; ++i)
	{
		Object *o = m_boids[i]->owner();
		pos[i] = o->transform.position;
		if (physics)
			if (auto *rb = o->get_component<RigidbodyComponent>())
				m_boids[i]->velocity = rb->velocity;
		vel[i] = m_boids[i]->velocity;
	}

	const glm::vec3 center = owner()->world_position();
	const float percept2 = perception * perception;
	const float sep2 = separation_dist * separation_dist;

	// Gather the scene's obstacle colliders once (skip the boids' own colliders --
	// boid-vs-boid spacing is the separation rule's job, not obstacle avoidance).
	std::vector<ColliderComponent *> obstacles;
	for (ColliderComponent *col : PhysicsManager::instance().colliders())
		if (!col->owner()->get_component<BoidComponent>())
			obstacles.push_back(col);

	// Resolve the target by name each frame (handles load order, renames and deletion
	// gracefully -- a dangling name just yields no target). Used by both the seek and
	// shape-formation behaviours.
	Object *target = nullptr;
	if ((w_seek > 0.0f || form_shape) && !target_name.empty())
		for (const auto &up : ObjectsManager::instance().objects())
			if (up->name() == target_name && !up->get_component<BoidComponent>())
			{
				target = up.get();
				break;
			}
	const glm::vec3 target_pos = target ? target->world_position() : glm::vec3(0.0f);

	// Shape formation: sample one local-space slot PER BOID spread evenly over the
	// target mesh's SURFACE (area-weighted triangle sampling, not just vertices, so
	// low-poly models fill out instead of clumping at corners). Cached per target+count.
	if (form_shape && target)
	{
		RendererComponent *rc = target->get_component<RendererComponent>();
		// Signature of the target's CURRENT mesh(es) so the cache rebuilds when the
		// user swaps the target's mesh (not just when the target name / count change).
		std::size_t mesh_sig = 0;
		if (rc)
		{
			if (rc->mesh)
				mesh_sig = reinterpret_cast<std::size_t>(rc->mesh.get());
			else
				for (const Submesh &sm : rc->submeshes)
					if (sm.mesh)
						mesh_sig ^= reinterpret_cast<std::size_t>(sm.mesh.get()) + 0x9e3779b9u +
						            (mesh_sig << 6) + (mesh_sig >> 2);
		}
		const std::string key =
		    target_name + "#" + std::to_string(n) + "#" + std::to_string(mesh_sig);
		if (m_form_cache_for != key)
		{
			m_form_points.clear();
			std::vector<glm::vec3> tri; // flat list, 3 verts per triangle
			if (rc)
			{
				auto add_mesh = [&](const Mesh *m)
				{
					const auto &p = m->points();
					const auto &i = m->indices();
					for (size_t k = 0; k + 2 < i.size(); k += 3)
						if (i[k] < p.size() && i[k + 1] < p.size() && i[k + 2] < p.size())
						{
							tri.push_back(p[i[k]]);
							tri.push_back(p[i[k + 1]]);
							tri.push_back(p[i[k + 2]]);
						}
				};
				if (rc->mesh)
					add_mesh(rc->mesh.get());
				else
					for (const Submesh &sm : rc->submeshes)
						if (sm.mesh)
							add_mesh(sm.mesh.get());
			}
			const size_t tcount = tri.size() / 3;
			if (tcount > 0)
			{
				std::vector<float> cum(tcount); // cumulative triangle area
				float total = 0.0f;
				for (size_t t = 0; t < tcount; ++t)
				{
					total += 0.5f * glm::length(glm::cross(tri[t * 3 + 1] - tri[t * 3],
					                                       tri[t * 3 + 2] - tri[t * 3]));
					cum[t] = total;
				}
				m_form_points.reserve(n);
				for (int s = 0; s < n; ++s)
				{
					size_t t =
					    std::lower_bound(cum.begin(), cum.end(), rnd(0.0f, total)) - cum.begin();
					if (t >= tcount)
						t = tcount - 1;
					float u = rnd(0.0f, 1.0f), v = rnd(0.0f, 1.0f);
					if (u + v > 1.0f)
					{
						u = 1.0f - u;
						v = 1.0f - v;
					} // reflect into the triangle
					m_form_points.push_back(tri[t * 3] + u * (tri[t * 3 + 1] - tri[t * 3]) +
					                        v * (tri[t * 3 + 2] - tri[t * 3]));
				}
			}
			m_form_cache_for = key;
		}
	}
	else
	{
		m_form_cache_for.clear(); // force a rebuild next time formation is enabled
	}
	const bool forming = form_shape && target && !m_form_points.empty();
	const glm::mat4 target_mat = target ? target->world_matrix() : glm::mat4(1.0f);
	const int p = static_cast<int>(m_form_points.size());

	for (int i = 0; i < n; ++i)
	{
		glm::vec3 sep(0.0f), ali(0.0f), coh(0.0f);
		int near = 0;
		for (int j = 0; j < n; ++j)
		{
			if (j == i)
				continue;
			const glm::vec3 d = pos[i] - pos[j];
			const float d2 = glm::dot(d, d);
			if (d2 > percept2 || d2 < 1e-6f)
				continue;
			ali += vel[j];
			coh += pos[j];
			++near;
			if (d2 < sep2)
				sep += d / d2; // push harder the closer they are
		}

		glm::vec3 accel(0.0f);

		if (forming)
		{
			// Each boid flies to its slot on the model's surface and ARRIVES (slows as
			// it nears), so the flock settles into the mesh's shape. A little
			// separation keeps boids from stacking on a shared vertex.
			const glm::vec3 home = glm::vec3(
			    target_mat * glm::vec4(m_form_points[(static_cast<size_t>(i) * p) / n], 1.0f));
			const glm::vec3 to_home = home - pos[i];
			const float dist = glm::length(to_home);
			const float desired_speed = std::min(max_speed, dist * 3.0f);
			const glm::vec3 desired_vel =
			    dist > 1e-4f ? (to_home / dist) * desired_speed : glm::vec3(0.0f);
			accel = (desired_vel - vel[i]) * w_form + w_separation * sep;
			const float al = glm::length(accel);
			if (al > max_force)
				accel *= max_force / al;
			// Apply + orient happens below (shared with the flocking path).
			glm::vec3 v = vel[i] + accel * dt;
			Object *o = m_boids[i]->owner();
			if (physics)
			{
				if (auto *rb = o->get_component<RigidbodyComponent>())
				{
					rb->velocity = v;
					m_boids[i]->velocity = v;
				}
			}
			else
			{
				const float s = glm::length(v);
				if (s > max_speed)
					v *= max_speed / s; // allow stopping (no min speed)
				m_boids[i]->velocity = v;
				o->transform.position += v * dt;
			}
			const glm::vec3 vv = m_boids[i]->velocity;
			if (glm::dot(vv, vv) > 1e-4f)
			{
				const glm::vec3 d = glm::normalize(vv);
				o->transform.euler_degrees =
				    glm::vec3(glm::degrees(std::asin(std::clamp(d.y, -1.0f, 1.0f))),
				              glm::degrees(std::atan2(-d.x, -d.z)), 0.0f);
			}
			continue; // skip the normal flocking rules for this boid
		}

		if (near > 0)
		{
			accel += w_separation * sep;
			ali = ali / float(near) - vel[i];
			accel += w_alignment * ali;
			coh = coh / float(near) - pos[i];
			accel += w_cohesion * coh;
		}

		// Soft bounds: steer back toward the box when a boid leaves it.
		for (int a = 0; a < 3; ++a)
		{
			const float over = pos[i][a] - center[a];
			if (over > bounds[a])
				accel[a] -= w_bounds * (over - bounds[a]);
			if (over < -bounds[a])
				accel[a] -= w_bounds * (over + bounds[a]);
		}

		// Obstacle avoidance: push away from nearby scene colliders. Planes push
		// along their normal; spheres/boxes push radially from a bounding sphere.
		if (w_avoid > 0.0f)
		{
			glm::vec3 avoid(0.0f);
			for (ColliderComponent *col : obstacles)
			{
				Object *co = col->owner();
				const glm::vec3 sc = co->transform.scale;
				if (col->shape == ColliderComponent::Shape::Plane)
				{
					const glm::vec3 nrm = glm::normalize(col->plane_normal);
					const glm::vec3 pp = co->world_position() + col->center * sc;
					const float dist = glm::dot(pos[i] - pp, nrm); // signed height over plane
					if (dist < avoid_radius)
						avoid += nrm * ((avoid_radius - dist) / avoid_radius);
				}
				else
				{
					const glm::vec3 cc = co->world_position() + col->center * sc;
					const float bound_r = (col->shape == ColliderComponent::Shape::Sphere)
					                          ? col->radius * std::max({sc.x, sc.y, sc.z})
					                          : glm::length(col->half_extents * sc);
					const glm::vec3 to = pos[i] - cc;
					const float d = glm::length(to);
					const float reach = avoid_radius + bound_r;
					if (d < reach && d > 1e-3f)
						avoid += (to / d) * ((reach - d) / reach);
				}
			}
			accel += w_avoid * avoid;
		}

		// Seek: steer toward the follow-target (the flock swarms/chases it).
		if (target)
			accel += w_seek * (target_pos - pos[i]);

		// Clamp the steering acceleration.
		const float al = glm::length(accel);
		if (al > max_force)
			accel *= max_force / al;

		glm::vec3 v = vel[i] + accel * dt;
		Object *o = m_boids[i]->owner();

		if (physics)
		{
			// ADD the steering to the rigidbody's velocity (don't overwrite it), so
			// gravity, collision impulses and Buoyancy all survive and compose. Only
			// the HORIZONTAL speed is capped -- the vertical axis is left to physics.
			if (auto *rb = o->get_component<RigidbodyComponent>())
			{
				rb->velocity += accel * dt;
				// Keep the HORIZONTAL speed within [minSpeed, maxSpeed] so the school
				// keeps cruising (buoyancy drag would otherwise damp it to a stop);
				// the vertical axis is left to gravity + buoyancy.
				glm::vec2 h(rb->velocity.x, rb->velocity.z);
				float hs = glm::length(h);
				if (hs < 1e-3f)
				{
					h = glm::vec2(accel.x, accel.z);
					hs = glm::length(h);
				}
				if (hs < 1e-3f)
				{
					h = glm::vec2(1.0f, 0.0f);
					hs = 1.0f;
				}
				const float target_speed = std::clamp(hs, min_speed, max_speed);
				h *= target_speed / hs;
				rb->velocity.x = h.x;
				rb->velocity.z = h.y;
				m_boids[i]->velocity = rb->velocity;
			}
		}
		else
		{
			// Pure boid: keep speed in [minSpeed, maxSpeed] and move the transform.
			float s = glm::length(v);
			if (s < 1e-4f)
			{
				v = rnd_dir() * min_speed;
				s = min_speed;
			}
			v *= std::clamp(s, min_speed, max_speed) / s;
			m_boids[i]->velocity = v;
			o->transform.position += v * dt;
		}

		// Face the direction of travel (freezeRotation keeps it for physics boids).
		// The mesh is oriented by the model MATRIX, whose local -Z maps to world
		// (-cos p sin y, sin p, -cos p cos y) -- so yaw uses atan2(-dx, -dz), NOT the
		// Transform::forward() convention (which disagrees in the X sign here).
		const glm::vec3 vv = m_boids[i]->velocity;
		if (glm::dot(vv, vv) > 1e-4f)
		{
			const glm::vec3 d = glm::normalize(vv);
			const float yaw = glm::degrees(std::atan2(-d.x, -d.z));
			const float pitch = glm::degrees(std::asin(std::clamp(d.y, -1.0f, 1.0f)));
			o->transform.euler_degrees = glm::vec3(pitch, yaw, 0.0f);
		}
	}
}

} // namespace cf
