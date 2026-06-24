#include "components/ParticleComponent.h"

#include "core/Object.h"
#include "core/RendererManager.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

#include <algorithm>
#include <cmath>

namespace cf
{

void ParticleComponent::on_attach()
{
	RendererManager::instance().register_particles(this);
}

ParticleComponent::~ParticleComponent()
{
	RendererManager::instance().unregister_particles(this);
}

float ParticleComponent::rnd()
{
	return std::uniform_real_distribution<float>(0.0f, 1.0f)(m_rng);
}
float ParticleComponent::rnd(float a, float b)
{
	return std::uniform_real_distribution<float>(a, b)(m_rng);
}

void ParticleComponent::spawn_one(const glm::vec3 *at_world, const glm::vec3 &extra_vel)
{
	if (static_cast<int>(m_particles.size()) >= max_particles)
		return;

	// World orientation of the emitter (normalised, scale removed) so a parented
	// emitter spawns correctly in world space.
	glm::mat3 r = glm::mat3(owner()->world_matrix());
	r[0] = glm::normalize(r[0]);
	r[1] = glm::normalize(r[1]);
	r[2] = glm::normalize(r[2]);

	// Emit axis + a perpendicular basis (for disk/ring placement + the cone spread).
	const glm::vec3 axis =
	    glm::normalize(r * (glm::length(emit_dir) > 1e-4f ? emit_dir : glm::vec3(0, 1, 0)));
	const glm::vec3 ref = std::abs(axis.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
	const glm::vec3 tangent = glm::normalize(glm::cross(ref, axis));
	const glm::vec3 bitan = glm::cross(axis, tangent);

	glm::vec3 pos = at_world ? *at_world : owner()->world_position();
	glm::vec3 ring_tangent(0.0f); // tangential start dir for Ring (orbits -> portals)
	switch (shape)
	{
	case Shape::Disk:
	{
		const float a = rnd(0.0f, 6.2831853f), r = emit_radius * std::sqrt(rnd());
		pos += (tangent * std::cos(a) + bitan * std::sin(a)) * r;
		break;
	}
	case Shape::Ring:
	{
		const float a = rnd(0.0f, 6.2831853f);
		pos += (tangent * std::cos(a) + bitan * std::sin(a)) * emit_radius;
		ring_tangent = -tangent * std::sin(a) + bitan * std::cos(a); // around the axis
		break;
	}
	case Shape::Sphere:
	{
		glm::vec3 d(rnd(-1, 1), rnd(-1, 1), rnd(-1, 1));
		if (glm::dot(d, d) > 1e-4f)
			d = glm::normalize(d);
		pos += d * (emit_radius * std::cbrt(rnd()));
		break;
	}
	default:
		break; // Point / Cone spawn at the origin
	}

	// Direction: a cone of half-angle `spread` around the emit axis.
	const float cos_max = std::cos(glm::radians(spread));
	const float z = rnd(cos_max, 1.0f); // cos(theta)
	const float phi = rnd(0.0f, 6.2831853f);
	const float s = std::sqrt(std::max(0.0f, 1.0f - z * z));
	const glm::vec3 dir =
	    glm::normalize(axis * z + tangent * (s * std::cos(phi)) + bitan * (s * std::sin(phi)));

	Particle p;
	p.pos = pos;
	// Ring starts orbiting (tangential); other shapes fire along the cone direction.
	p.vel = (shape == Shape::Ring ? ring_tangent * start_speed + axis * (start_speed * 0.1f)
	                              : dir * (start_speed + rnd(-speed_var, speed_var))) +
	        extra_vel;
	p.age = 0.0f;
	p.life = std::max(0.05f, lifetime + rnd(-lifetime_var, lifetime_var));
	m_particles.push_back(p);
}

void ParticleComponent::burst(int n, const glm::vec3 *at_world, const glm::vec3 &extra_vel)
{
	for (int i = 0; i < n; ++i)
		spawn_one(at_world, extra_vel);
}

void ParticleComponent::build_bolt()
{
	m_instances.clear();
	glm::mat3 r = glm::mat3(owner()->world_matrix());
	r[0] = glm::normalize(r[0]);
	r[1] = glm::normalize(r[1]);
	r[2] = glm::normalize(r[2]);
	const glm::vec3 origin = owner()->world_position();
	const glm::vec3 dir =
	    glm::normalize(r * (glm::length(emit_dir) > 1e-4f ? emit_dir : glm::vec3(0, -1, 0)));
	const glm::vec3 ref = std::abs(dir.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
	const glm::vec3 u = glm::normalize(glm::cross(ref, dir)), v = glm::cross(dir, u);

	const int seg = std::max(2, bolt_segments);
	std::vector<glm::vec3> pts(static_cast<size_t>(seg) + 1);
	for (int i = 0; i <= seg; ++i)
	{
		const float t = static_cast<float>(i) / seg;
		const float amp = bolt_jitter * std::sin(t * 3.14159265f); // 0 at the ends
		pts[i] = origin + dir * (bolt_length * t) + u * rnd(-amp, amp) + v * rnd(-amp, amp);
	}
	// Lay bright billboards along each segment for a continuous glowing line.
	const int k = 4;
	for (int i = 0; i < seg; ++i)
		for (int k = 0; k < k; ++k)
		{
			ParticleInstance inst;
			inst.pos = glm::mix(pts[i], pts[i + 1], static_cast<float>(k) / k);
			inst.size = start_size;
			inst.color = start_color;
			inst.vel = glm::vec3(0.0f);
			m_instances.push_back(inst);
		}
}

void ParticleComponent::update(float dt)
{
	if (dt <= 0.0f)
		dt = 0.0f;

	// Bolt (lightning): re-strike a fresh jagged line on the flicker timer.
	if (mode == Mode::Bolt)
	{
		m_bolt_timer += dt;
		const float interval = (flicker_hz > 0.1f) ? 1.0f / flicker_hz : 0.07f;
		if (enabled && (m_bolt_timer >= interval || m_instances.empty()))
		{
			m_bolt_timer = 0.0f;
			build_bolt();
		}
		if (!enabled)
			m_instances.clear();
		return;
	}

	// Continuous emission (fractional carry so low rates still emit smoothly).
	if (enabled && rate > 0.0f && dt > 0.0f)
	{
		m_accum += rate * dt;
		int n = static_cast<int>(m_accum);
		m_accum -= static_cast<float>(n);
		for (int i = 0; i < n; ++i)
			spawn_one(nullptr, glm::vec3(0.0f));
	}

	// Swirl / attraction around the emit axis (orbiting -> portals/vortex).
	const bool vortex = (swirl != 0.0f || attraction != 0.0f);
	glm::vec3 center(0.0f), axis(0.0f, 1.0f, 0.0f);
	if (vortex)
	{
		glm::mat3 r = glm::mat3(owner()->world_matrix());
		axis = glm::normalize(r * (glm::length(emit_dir) > 1e-4f ? emit_dir : glm::vec3(0, 1, 0)));
		center = owner()->world_position();
	}

	// Integrate + age, compacting dead particles out in place.
	m_instances.clear();
	m_instances.reserve(m_particles.size());
	std::vector<glm::vec3> splash_pts; // ground-hit points to spray droplets from
	size_t alive = 0;
	for (Particle &p : m_particles)
	{
		p.age += dt;
		if (p.age >= p.life)
			continue;
		p.vel.y += gravity * dt;
		if (vortex)
		{
			const glm::vec3 rel = p.pos - center;
			const glm::vec3 radial = rel - axis * glm::dot(rel, axis);
			const float rlen = glm::length(radial);
			if (rlen > 1e-4f)
			{
				const glm::vec3 rdir = radial / rlen;
				p.vel += (glm::cross(axis, rdir) * swirl - rdir * attraction) * dt;
			}
		}
		p.vel -= p.vel * std::clamp(drag * dt, 0.0f, 1.0f);
		p.pos += p.vel * dt;

		// Rain hitting the floor: consume the drop, remember where to splash.
		if (splash_on_ground && !p.splash && p.pos.y <= ground_y && p.vel.y < 0.0f)
		{
			splash_pts.emplace_back(p.pos.x, ground_y, p.pos.z);
			continue;
		}
		m_particles[alive++] = p;

		const float u = p.age / p.life;
		ParticleInstance inst;
		inst.pos = p.pos;
		inst.size = glm::mix(start_size, end_size, u);
		inst.color = glm::mix(start_color, end_color, u);
		inst.vel = p.vel;
		m_instances.push_back(inst);
	}
	m_particles.resize(alive);

	// Spawn a little crown of upward droplets at each ground impact (next frame).
	for (const glm::vec3 &sp : splash_pts)
	{
		const int n = 3 + static_cast<int>(rnd() * 3.0f);
		for (int i = 0; i < n && static_cast<int>(m_particles.size()) < max_particles; ++i)
		{
			const float a = rnd(0.0f, 6.2831853f);
			Particle s;
			s.pos = sp + glm::vec3(0.0f, 0.02f, 0.0f);
			s.vel = glm::vec3(std::cos(a) * rnd(0.4f, 1.2f), rnd(1.2f, 2.4f),
			                  std::sin(a) * rnd(0.4f, 1.2f));
			s.age = 0.0f;
			s.life = rnd(0.18f, 0.36f);
			s.splash = true;
			m_particles.push_back(s);
		}
	}
}

void ParticleComponent::apply_preset(Preset p)
{
	switch (p)
	{
	case Preset::Fountain:
		shape = Shape::Disk;
		emit_dir = {0, 1, 0};
		emit_radius = 0.2f;
		spread = 14.0f;
		rate = 220.0f;
		lifetime = 1.5f;
		lifetime_var = 0.25f;
		start_speed = 6.0f;
		speed_var = 1.2f;
		gravity = -9.0f;
		drag = 0.05f;
		start_size = 0.16f;
		end_size = 0.05f;
		blend = Blend::Alpha;
		soft = true;
		start_color = {0.8f, 0.92f, 1.0f, 0.85f};
		end_color = {0.5f, 0.72f, 1.0f, 0.0f};
		max_particles = 3000;
		break;
	case Preset::Splash:
		shape = Shape::Disk;
		emit_dir = {0, 1, 0};
		emit_radius = 0.25f;
		spread = 55.0f;
		rate = 0.0f;
		lifetime = 0.7f;
		lifetime_var = 0.2f;
		start_speed = 3.5f;
		speed_var = 1.5f;
		gravity = -11.0f;
		drag = 0.1f;
		start_size = 0.18f;
		end_size = 0.02f;
		blend = Blend::Alpha;
		soft = true;
		start_color = {0.95f, 0.98f, 1.0f, 0.95f};
		end_color = {0.7f, 0.85f, 1.0f, 0.0f};
		max_particles = 1500;
		break;
	case Preset::Fire:
		shape = Shape::Disk;
		emit_dir = {0, 1, 0};
		emit_radius = 0.18f;
		spread = 16.0f;
		rate = 140.0f;
		lifetime = 1.1f;
		lifetime_var = 0.3f;
		start_speed = 1.8f;
		speed_var = 0.5f;
		gravity = 1.6f;
		drag = 0.4f;
		start_size = 0.45f;
		end_size = 0.05f;
		blend = Blend::Additive;
		soft = true;
		start_color = {3.0f, 1.4f, 0.4f, 0.9f};
		end_color = {1.2f, 0.2f, 0.05f, 0.0f};
		max_particles = 2000;
		break;
	case Preset::Smoke:
		shape = Shape::Disk;
		emit_dir = {0, 1, 0};
		emit_radius = 0.2f;
		spread = 20.0f;
		rate = 40.0f;
		lifetime = 3.0f;
		lifetime_var = 0.6f;
		start_speed = 1.2f;
		speed_var = 0.4f;
		gravity = 0.5f;
		drag = 0.5f;
		start_size = 0.4f;
		end_size = 1.4f;
		blend = Blend::Alpha;
		soft = true;
		start_color = {0.25f, 0.25f, 0.28f, 0.5f};
		end_color = {0.1f, 0.1f, 0.12f, 0.0f};
		max_particles = 1500;
		break;
	case Preset::Sparks:
		shape = Shape::Point;
		emit_dir = {0, 1, 0};
		spread = 80.0f;
		rate = 60.0f;
		lifetime = 1.0f;
		lifetime_var = 0.4f;
		start_speed = 4.5f;
		speed_var = 2.0f;
		gravity = -12.0f;
		drag = 0.05f;
		start_size = 0.06f;
		end_size = 0.01f;
		blend = Blend::Additive;
		soft = false;
		start_color = {4.0f, 2.2f, 0.6f, 1.0f};
		end_color = {2.0f, 0.5f, 0.1f, 0.0f};
		max_particles = 1200;
		break;
	case Preset::Portal:
		mode = Mode::Emitter;
		shape = Shape::Ring;
		emit_dir = {0, 0, 1};
		emit_radius = 1.0f;
		spread = 8.0f;
		rate = 320.0f;
		lifetime = 1.4f;
		lifetime_var = 0.3f;
		start_speed = 2.2f;
		speed_var = 0.4f;
		gravity = 0.0f;
		drag = 0.7f;
		swirl = 11.0f;
		attraction = 3.0f;
		start_size = 0.18f;
		end_size = 0.02f;
		blend = Blend::Additive;
		soft = true;
		start_color = {1.8f, 0.5f, 3.2f, 0.9f};
		end_color = {0.3f, 0.9f, 2.4f, 0.0f};
		max_particles = 3500;
		break;
	case Preset::Lightning:
		mode = Mode::Bolt;
		emit_dir = {0, -1, 0};
		bolt_length = 5.0f;
		bolt_segments = 16;
		bolt_jitter = 0.55f;
		flicker_hz = 14.0f;
		start_size = 0.12f;
		blend = Blend::Additive;
		soft = false;
		start_color = {2.2f, 2.6f, 5.0f, 1.0f};
		end_color = {1.0f, 1.4f, 3.0f, 0.0f};
		max_particles = 600;
		break;
	case Preset::Rain:
		mode = Mode::Emitter;
		shape = Shape::Disk;
		emit_dir = {0, -1, 0};
		emit_radius = 11.0f;
		spread = 2.0f;
		rate = 1300.0f;
		lifetime = 1.6f;
		lifetime_var = 0.2f;
		start_speed = 22.0f;
		speed_var = 2.0f;
		gravity = -14.0f;
		drag = 0.0f;
		swirl = 0.0f;
		attraction = 0.0f;
		stretch = 0.06f;
		splash_on_ground = true;
		ground_y = 0.0f;
		start_size = 0.05f;
		end_size = 0.05f;
		blend = Blend::Alpha;
		soft = true;
		start_color = {0.55f, 0.65f, 0.85f, 0.55f};
		end_color = {0.55f, 0.65f, 0.85f, 0.45f};
		max_particles = 7000;
		break;
	case Preset::Snow:
		mode = Mode::Emitter;
		shape = Shape::Disk;
		emit_dir = {0, -1, 0};
		emit_radius = 8.0f;
		spread = 8.0f;
		rate = 220.0f;
		lifetime = 5.0f;
		lifetime_var = 1.0f;
		start_speed = 1.2f;
		speed_var = 0.5f;
		gravity = -0.6f;
		drag = 0.3f;
		swirl = 1.2f;
		attraction = 0.0f;
		start_size = 0.09f;
		end_size = 0.07f;
		blend = Blend::Alpha;
		soft = true;
		start_color = {1.0f, 1.0f, 1.0f, 0.9f};
		end_color = {0.9f, 0.95f, 1.0f, 0.4f};
		max_particles = 3000;
		break;
	case Preset::Embers:
		mode = Mode::Emitter;
		shape = Shape::Disk;
		emit_dir = {0, 1, 0};
		emit_radius = 0.4f;
		spread = 35.0f;
		rate = 40.0f;
		lifetime = 2.4f;
		lifetime_var = 0.6f;
		start_speed = 1.6f;
		speed_var = 0.7f;
		gravity = 0.8f;
		drag = 0.25f;
		swirl = 1.5f;
		attraction = 0.0f;
		start_size = 0.05f;
		end_size = 0.01f;
		blend = Blend::Additive;
		soft = false;
		start_color = {4.0f, 1.6f, 0.4f, 1.0f};
		end_color = {1.5f, 0.3f, 0.05f, 0.0f};
		max_particles = 800;
		break;
	case Preset::Magic:
		mode = Mode::Emitter;
		shape = Shape::Sphere;
		emit_dir = {0, 1, 0};
		emit_radius = 0.5f;
		spread = 60.0f;
		rate = 70.0f;
		lifetime = 1.8f;
		lifetime_var = 0.5f;
		start_speed = 0.6f;
		speed_var = 0.3f;
		gravity = -0.3f;
		drag = 0.4f;
		swirl = 3.0f;
		attraction = 1.5f;
		start_size = 0.13f;
		end_size = 0.01f;
		blend = Blend::Additive;
		soft = true;
		start_color = {0.5f, 2.0f, 2.6f, 0.9f};
		end_color = {1.8f, 0.4f, 2.4f, 0.0f};
		max_particles = 1500;
		break;
	case Preset::Explosion:
		mode = Mode::Emitter;
		shape = Shape::Sphere;
		emit_dir = {0, 1, 0};
		emit_radius = 0.2f;
		spread = 180.0f;
		rate = 0.0f;
		lifetime = 1.1f;
		lifetime_var = 0.4f; // burst-only
		start_speed = 9.0f;
		speed_var = 3.0f;
		gravity = -3.0f;
		drag = 1.2f;
		swirl = 0.0f;
		attraction = 0.0f;
		start_size = 0.4f;
		end_size = 0.02f;
		blend = Blend::Additive;
		soft = true;
		start_color = {5.0f, 2.4f, 0.7f, 1.0f};
		end_color = {1.5f, 0.2f, 0.05f, 0.0f};
		max_particles = 1500;
		break;
	case Preset::Mist:
		mode = Mode::Emitter;
		shape = Shape::Disk;
		emit_dir = {0, 1, 0};
		emit_radius = 5.0f;
		spread = 80.0f;
		rate = 24.0f;
		lifetime = 6.0f;
		lifetime_var = 1.5f;
		start_speed = 0.3f;
		speed_var = 0.2f;
		gravity = 0.0f;
		drag = 0.8f;
		swirl = 0.4f;
		attraction = 0.0f;
		start_size = 2.0f;
		end_size = 3.5f;
		blend = Blend::Alpha;
		soft = true;
		start_color = {0.55f, 0.6f, 0.68f, 0.16f};
		end_color = {0.5f, 0.55f, 0.62f, 0.0f};
		max_particles = 600;
		break;
	}
}

} // namespace cf
