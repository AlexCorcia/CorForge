#include "components/ClothComponent.h"

#include "components/ColliderComponent.h"
#include "components/MaterialComponent.h"
#include "components/RendererComponent.h"
#include "core/Object.h"
#include "core/PhysicsManager.h"
#include "core/RendererManager.h"
#include "gfx/Mesh.h"

#include <algorithm>
#include <cmath>

namespace cf
{

void ClothComponent::on_attach()
{
	// The cloth needs a renderer + material to be drawn. Make sure they exist.
	Object *o = owner();
	if (!o->get_component<RendererComponent>())
		o->add_component<RendererComponent>();
	if (!o->get_component<MaterialComponent>())
	{
		auto *mc = o->add_component<MaterialComponent>();
		mc->material.shader = RendererManager::instance().default_shader();
		mc->material.albedo = {0.75f, 0.2f, 0.25f};
		mc->material.roughness = 0.9f;
		mc->material.metallic = 0.0f;
	}
	rebuild();
}

void ClothComponent::rebuild()
{
	cols = std::clamp(cols, 2, 80);
	rows = std::clamp(rows, 2, 80);

	m_torn_edges.clear(); // fresh cloth: nothing torn, nothing grabbed
	m_grabbed = -1;
	m_topology_dirty = false;
	m_particles.assign(static_cast<size_t>(cols) * rows, Particle{});
	// Start as a flat HORIZONTAL sheet (local XZ plane): the pinned front edge
	// holds while the rest falls and drapes -> visible motion + deep sag, instead
	// of starting already-hanging (which barely moves).
	const float w = (cols - 1) * spacing;
	for (int y = 0; y < rows; ++y)
		for (int x = 0; x < cols; ++x)
		{
			Particle &p = m_particles[idx(x, y)];
			p.pos = {x * spacing - w * 0.5f, 0.0f, y * spacing};
			p.vel = glm::vec3(0.0f);
		}

	// Pins along the top row. `slackX` pulls the pin toward the centre so the
	// cloth has extra material to sag into catenaries between pins (the draped
	// look); whole-top pinning uses no slack (a flat curtain top).
	auto pin = [&](int x, int y, float slack_x)
	{
		Particle &p = m_particles[idx(x, y)];
		p.pinned = true;
		p.pin_pos = p.pos;
		p.pin_pos.x *= (1.0f - slack_x);
		p.pos = p.pin_pos;
	};
	const float sl = std::clamp(slack, 0.0f, 0.6f);
	if (pin_mode == 0)
	{
		pin(0, 0, sl);
		pin(cols - 1, 0, sl);
	} // corners
	else if (pin_mode == 1)
	{
		for (int x = 0; x < cols; ++x)
			pin(x, 0, 0.0f);
	} // whole top
	else if (pin_mode == 2)
	{
		pin(0, 0, sl);
		pin(cols / 2, 0, 0.0f);
		pin(cols - 1, 0, sl);
	} // + centre
	else
	{
		pin(0, 0, sl);
		pin(cols - 1, 0, sl);
		pin(0, rows - 1, sl);
		pin(cols - 1, rows - 1, sl);
	} // 4 corners

	// Springs: structural (neighbours) + shear (diagonals) hold the shape; bend
	// springs (2 apart) resist folding/curling so the cloth drapes instead of
	// rolling up -- they use the separate, low `bendStiffness`.
	m_constraints.clear();
	auto add = [&](int ax, int ay, int bx, int by, bool bend)
	{
		Constraint c;
		c.a = idx(ax, ay);
		c.b = idx(bx, by);
		c.bend = bend;
		c.rest = glm::length(m_particles[c.a].pos - m_particles[c.b].pos);
		m_constraints.push_back(c);
	};
	for (int y = 0; y < rows; ++y)
		for (int x = 0; x < cols; ++x)
		{
			if (x + 1 < cols)
				add(x, y, x + 1, y, false); // structural
			if (y + 1 < rows)
				add(x, y, x, y + 1, false);
			if (x + 1 < cols && y + 1 < rows)
			{
				add(x, y, x + 1, y + 1, false);
				add(x + 1, y, x, y + 1, false);
			} // shear
			if (x + 2 < cols)
				add(x, y, x + 2, y, true); // bend
			if (y + 2 < rows)
				add(x, y, x, y + 2, true);
		}

	// Triangle indices (two per quad; skips none yet -- nothing torn).
	rebuild_indices();

	// Build the mesh and hand it to the renderer.
	std::vector<Vertex> verts(m_particles.size());
	for (int y = 0; y < rows; ++y)
		for (int x = 0; x < cols; ++x)
		{
			Vertex &v = verts[idx(x, y)];
			v.position = m_particles[idx(x, y)].pos;
			v.normal = {0, 0, 1};
			v.uv = {static_cast<float>(x) / (cols - 1),
			        1.0f - static_cast<float>(y) / (rows - 1)}; // flip V: image origin is bottom
		}
	m_mesh = std::make_shared<Mesh>(verts, m_indices);
	if (auto *rc = owner()->get_component<RendererComponent>())
	{
		rc->mesh = m_mesh;
		rc->mesh_name = "Cloth";
		rc->submeshes.clear();
	}
	m_dirty = false;
}

void ClothComponent::reset_sim()
{
	const float w = (cols - 1) * spacing;
	for (int y = 0; y < rows; ++y)
		for (int x = 0; x < cols; ++x)
		{
			Particle &p = m_particles[idx(x, y)];
			p.pos = {x * spacing - w * 0.5f, 0.0f, y * spacing};
			p.vel = glm::vec3(0.0f);
			if (p.pinned)
				p.pos = p.pin_pos;
		}
	// Mend any tears and drop the grab so the cloth comes back whole.
	for (Constraint &c : m_constraints)
		c.torn = false;
	m_torn_edges.clear();
	m_grabbed = -1;
	m_topology_dirty = true; // rebuild the full triangle list next updateMesh
}

void ClothComponent::update(float dt)
{
	if (m_dirty)
	{
		rebuild();
		return;
	}
	if (m_particles.empty())
		return;

	// Step once per FRAME so the cloth is exactly as smooth as the framerate
	// (the motion is coupled to the real fps, not locked to a 60 Hz tick that
	// judders on a 144 Hz display). A long frame is split into sub-steps no
	// bigger than 1/60 s for stability, so it stays real-time at low fps too.
	// `step()` corrects damping for the step size, so the cloth behaves
	// identically at 30, 60, 144 or 240 fps -- only smoother at higher rates.
	const float frame = std::min(dt, 0.25f); // ignore huge stalls (alt-tab / breakpoints)
	const int n = std::clamp(static_cast<int>(std::ceil(frame / (1.0f / 60.0f))), 1, 16);
	const float h = frame / n;
	for (int i = 0; i < n; ++i)
		step(h);

	update_mesh();
}

// One implicit (backward-Euler) step of size h.
void ClothComponent::step(float h)
{
	// World gravity, brought into the object's local frame (inverse rotation+scale)
	// so the cloth falls at the correct WORLD rate even when rotated/scaled.
	const glm::mat3 basis = glm::mat3(owner()->transform.matrix());
	const glm::vec3 g_world = PhysicsManager::instance().gravity * gravity_scale;
	const glm::vec3 g_local = glm::inverse(basis) * g_world;

	// A grabbed particle is held at the cursor with zero velocity (a temporary pin)
	// so dragging it pulls the rest of the cloth and can tear it.
	if (m_grabbed >= 0)
	{
		m_particles[m_grabbed].pos = m_grab_local;
		m_particles[m_grabbed].vel = glm::vec3(0.0f);
	}

	solve_implicit(h, g_local);

	// Tearing: a link stretched past rest * tearFactor snaps and is dropped.
	if (tearable)
	{
		for (Constraint &c : m_constraints)
		{
			if (c.torn)
				continue;
			const float l = glm::length(m_particles[c.b].pos - m_particles[c.a].pos);
			if (l > c.rest * tear_factor)
			{
				c.torn = true;
				m_torn_edges.insert(edge_key(c.a, c.b));
				m_topology_dirty = true;
			}
		}
	}

	collide();
	if (m_grabbed >= 0)
	{
		m_particles[m_grabbed].pos = m_grab_local;
		m_particles[m_grabbed].vel = glm::vec3(0.0f);
	}
}

// Zero out the DOFs of fixed (pinned / grabbed) particles -- the constraint
// "filter" S in Baraff-Witkin's modified CG, holding those Δv at 0.
void ClothComponent::filter_fixed(std::vector<glm::vec3> &v) const
{
	for (size_t i = 0; i < v.size(); ++i)
		if (is_fixed(static_cast<int>(i)))
			v[i] = glm::vec3(0.0f);
}

// Apply the system matrix:  out = A·p  where  A = M - h·(∂f/∂v) - h²·(∂f/∂x).
// Matrix-free: each spring contributes a 3x3 block. Per spring with unit axis u
// and factor F = max(0, 1 - rest/len):
//   Ke·d = k·[ F·d + (1-F)·(u·d)u ]      (stretch stiffness, ∂f/∂x)
//   De·d = c·(u·d)u                       (axis damping, ∂f/∂v)
// and the coupling gives (block)·(p_a - p_b) added to a, its negative to b.
void ClothComponent::apply_a(const std::vector<glm::vec3> &p, std::vector<glm::vec3> &out,
                             float h) const
{
	const size_t n = m_particles.size();
	out.assign(n, glm::vec3(0.0f));
	for (size_t i = 0; i < n; ++i)
		out[i] = mass * p[i]; // M·p

	const float h2 = h * h;
	for (size_t s = 0; s < m_constraints.size(); ++s)
	{
		const Constraint &c = m_constraints[s];
		if (c.torn)
			continue;
		const glm::vec3 u = m_spring_u[s];
		const float f = m_spring_f[s];
		const glm::vec3 d = p[c.a] - p[c.b];
		const float ud = glm::dot(u, d);
		// (h²·Ke + h·De)·d
		const glm::vec3 kd =
		    h2 * m_spring_k[s] * (f * d + (1.0f - f) * ud * u) + h * spring_damping * ud * u;
		out[c.a] += kd;
		out[c.b] -= kd;
	}
	filter_fixed(out);
}

// Backward-Euler velocity update solved with a constraint-filtered, matrix-free
// Conjugate Gradient:  A·Δv = b,  A = M - h·D - h²·K,  b = h·(f0 + h·K·v0).
void ClothComponent::solve_implicit(float h, const glm::vec3 &g_local)
{
	const size_t n = m_particles.size();

	// Cache each (intact) spring's unit axis, stiffness factor and effective k.
	m_spring_u.assign(m_constraints.size(), glm::vec3(0.0f));
	m_spring_f.assign(m_constraints.size(), 0.0f);
	m_spring_k.assign(m_constraints.size(), 0.0f);

	// Forces f0 at the current state: gravity + spring (stretch) + spring damping.
	m_force.assign(n, glm::vec3(0.0f));
	for (size_t i = 0; i < n; ++i)
		if (!is_fixed(static_cast<int>(i)))
			m_force[i] += mass * g_local;

	for (size_t s = 0; s < m_constraints.size(); ++s)
	{
		const Constraint &c = m_constraints[s];
		if (c.torn)
			continue;
		const glm::vec3 d = m_particles[c.b].pos - m_particles[c.a].pos;
		const float l = glm::length(d);
		if (l < 1e-6f)
			continue;
		const glm::vec3 u = d / l;
		const float k = c.bend ? bend_stiffness : stiffness;
		m_spring_u[s] = u;
		m_spring_f[s] = std::max(0.0f, 1.0f - c.rest / l); // PSD: drop the term in compression
		m_spring_k[s] = k;
		const glm::vec3 f_stretch = k * (l - c.rest) * u;
		const float vel_along = glm::dot(m_particles[c.b].vel - m_particles[c.a].vel, u);
		const glm::vec3 f_damp = spring_damping * vel_along * u;
		m_force[c.a] += f_stretch + f_damp;
		m_force[c.b] -= f_stretch + f_damp;
	}

	// Right-hand side  b = h·(f0 + h·K·v0).  (K·v0)_a = Σ Ke·(v_b - v_a).
	m_b.assign(n, glm::vec3(0.0f));
	for (size_t i = 0; i < n; ++i)
		m_b[i] = h * m_force[i];
	for (size_t s = 0; s < m_constraints.size(); ++s)
	{
		const Constraint &c = m_constraints[s];
		if (c.torn)
			continue;
		const glm::vec3 u = m_spring_u[s];
		const float f = m_spring_f[s];
		const glm::vec3 dv = m_particles[c.b].vel - m_particles[c.a].vel;
		const float ud = glm::dot(u, dv);
		const glm::vec3 kv = m_spring_k[s] * (f * dv + (1.0f - f) * ud * u); // Ke·(v_b - v_a)
		m_b[c.a] += (h * h) * kv;
		m_b[c.b] -= (h * h) * kv;
	}
	filter_fixed(m_b);

	// Conjugate Gradient (Δv starts at 0, so the initial residual r = b).
	m_dv.assign(n, glm::vec3(0.0f));
	m_r = m_b;
	m_p = m_r;
	auto dot = [&](const std::vector<glm::vec3> &a, const std::vector<glm::vec3> &b)
	{
		float s = 0.0f;
		for (size_t i = 0; i < a.size(); ++i)
			s += glm::dot(a[i], b[i]);
		return s;
	};
	float rr = dot(m_r, m_r);
	for (int it = 0; it < iterations && rr > 1e-12f; ++it)
	{
		apply_a(m_p, m_ap, h);
		const float p_ap = dot(m_p, m_ap);
		if (p_ap <= 1e-20f)
			break;
		const float alpha = rr / p_ap;
		for (size_t i = 0; i < n; ++i)
		{
			m_dv[i] += alpha * m_p[i];
			m_r[i] -= alpha * m_ap[i];
		}
		const float rr_new = dot(m_r, m_r);
		const float beta = rr_new / rr;
		for (size_t i = 0; i < n; ++i)
			m_p[i] = m_r[i] + beta * m_p[i];
		rr = rr_new;
	}

	// Integrate:  v += Δv (+ light air drag);  x += h·v.  Fixed particles hold.
	const float drag = std::pow(damping, h * 60.0f); // framerate-independent air drag
	for (size_t i = 0; i < n; ++i)
	{
		if (is_fixed(static_cast<int>(i)))
			continue;
		m_particles[i].vel = (m_particles[i].vel + m_dv[i]) * drag;
		m_particles[i].pos += h * m_particles[i].vel;
	}
}

// Push particles out of the scene's colliders (sphere / box / plane). The cloth
// simulates in local space; the owner position is the anchor, so world = local +
// anchor (cloth objects are assumed un-rotated/un-scaled).
void ClothComponent::collide()
{
	const auto &cols2 = PhysicsManager::instance().colliders();
	if (cols2.empty())
		return;
	const glm::vec3 anchor = owner()->transform.position;
	const float skin = 0.02f; // keep the cloth slightly off the surface

	for (size_t i = 0; i < m_particles.size(); ++i)
	{
		if (is_fixed(static_cast<int>(i)))
			continue; // pinned or held by the cursor
		Particle &p = m_particles[i];
		glm::vec3 wp = anchor + p.pos;
		for (ColliderComponent *col : cols2)
		{
			if (col->owner() == owner())
				continue; // ignore self
			Object *co = col->owner();
			const glm::vec3 c = co->transform.position;
			const glm::vec3 s = co->transform.scale;
			if (col->shape == ColliderComponent::Shape::Sphere)
			{
				const float r = col->radius * std::max({s.x, s.y, s.z}) + skin;
				glm::vec3 d = wp - c;
				const float dist = glm::length(d);
				if (dist < r && dist > 1e-5f)
					wp = c + d * (r / dist);
			}
			else if (col->shape == ColliderComponent::Shape::Box)
			{
				const glm::vec3 he = col->half_extents * s + skin;
				const glm::vec3 mn = c - he, mx = c + he;
				if (wp.x > mn.x && wp.x < mx.x && wp.y > mn.y && wp.y < mx.y && wp.z > mn.z &&
				    wp.z < mx.z)
				{
					// Push out along the axis of least penetration.
					const float px = std::min(wp.x - mn.x, mx.x - wp.x);
					const float py = std::min(wp.y - mn.y, mx.y - wp.y);
					const float pz = std::min(wp.z - mn.z, mx.z - wp.z);
					if (px <= py && px <= pz)
						wp.x = (wp.x - mn.x < mx.x - wp.x) ? mn.x : mx.x;
					else if (py <= pz)
						wp.y = (wp.y - mn.y < mx.y - wp.y) ? mn.y : mx.y;
					else
						wp.z = (wp.z - mn.z < mx.z - wp.z) ? mn.z : mx.z;
				}
			}
			else
			{ // Plane (e.g. the ground)
				glm::vec3 n = glm::normalize(
				    glm::vec3(co->transform.matrix() * glm::vec4(col->plane_normal, 0.0f)));
				const float d = glm::dot(wp - c, n);
				if (d < skin)
					wp += n * (skin - d);
			}
		}
		const glm::vec3 new_local = wp - anchor;
		const glm::vec3 push = new_local - p.pos;
		if (glm::dot(push, push) > 1e-12f)
		{
			p.pos = new_local;
			// Remove the velocity going INTO the surface (keep tangential -> sliding).
			const glm::vec3 n = glm::normalize(push);
			const float vn = glm::dot(p.vel, n);
			if (vn < 0.0f)
				p.vel -= vn * n;
		}
	}
}

void ClothComponent::update_mesh()
{
	if (!m_mesh)
		return;
	std::vector<Vertex> verts(m_particles.size());
	for (int y = 0; y < rows; ++y)
		for (int x = 0; x < cols; ++x)
		{
			Vertex &v = verts[idx(x, y)];
			v.position = m_particles[idx(x, y)].pos;
			// Smooth normal from grid neighbours.
			const glm::vec3 px0 = m_particles[idx(std::max(x - 1, 0), y)].pos;
			const glm::vec3 px1 = m_particles[idx(std::min(x + 1, cols - 1), y)].pos;
			const glm::vec3 py0 = m_particles[idx(x, std::max(y - 1, 0))].pos;
			const glm::vec3 py1 = m_particles[idx(x, std::min(y + 1, rows - 1))].pos;
			glm::vec3 n = glm::cross(py0 - py1, px1 - px0);
			const float len = glm::length(n);
			v.normal = (len > 1e-6f) ? n / len : glm::vec3(0, 0, 1);
			v.uv = {static_cast<float>(x) / (cols - 1),
			        1.0f - static_cast<float>(y) / (rows - 1)}; // flip V: image origin is bottom
		}

	if (m_topology_dirty)
	{
		// A tear changed which triangles exist -> rebuild the index list and the
		// mesh (new EBO). Only happens on frames where something actually snapped.
		rebuild_indices();
		m_mesh = std::make_shared<Mesh>(verts, m_indices);
		m_topology_dirty = false;
		if (auto *rc = owner()->get_component<RendererComponent>())
			rc->mesh = m_mesh;
		return;
	}

	m_mesh->update_vertices(verts);
	if (auto *rc = owner()->get_component<RendererComponent>())
		if (rc->mesh != m_mesh)
			rc->mesh = m_mesh; // keep the renderer pointing at us
}

// Build the triangle list, skipping any triangle that has a torn edge -- this is
// what makes a tear show as a hole. Every triangle edge is a structural or shear
// constraint, so a snapped link removes exactly the triangles spanning it.
void ClothComponent::rebuild_indices()
{
	m_indices.clear();
	const bool any_torn = !m_torn_edges.empty();
	auto edge_ok = [&](int a, int b)
	{ return !any_torn || m_torn_edges.find(edge_key(a, b)) == m_torn_edges.end(); };
	for (int y = 0; y + 1 < rows; ++y)
		for (int x = 0; x + 1 < cols; ++x)
		{
			const int i0 = idx(x, y), i1 = idx(x + 1, y);
			const int i2 = idx(x, y + 1), i3 = idx(x + 1, y + 1);
			if (edge_ok(i0, i2) && edge_ok(i2, i1) && edge_ok(i1, i0))
				m_indices.insert(m_indices.end(), {(unsigned)i0, (unsigned)i2, (unsigned)i1});
			if (edge_ok(i1, i2) && edge_ok(i2, i3) && edge_ok(i3, i1))
				m_indices.insert(m_indices.end(), {(unsigned)i1, (unsigned)i2, (unsigned)i3});
		}
}

bool ClothComponent::grab(const glm::vec3 &ro, const glm::vec3 &rd, float max_dist,
                          glm::vec3 &out_world)
{
	const glm::mat4 model = owner()->transform.matrix();
	int best = -1;
	float best_d2 = max_dist * max_dist;
	glm::vec3 best_w(0.0f);
	for (size_t i = 0; i < m_particles.size(); ++i)
	{
		const glm::vec3 wp = glm::vec3(model * glm::vec4(m_particles[i].pos, 1.0f));
		const float t = glm::dot(wp - ro, rd);
		if (t < 0.0f)
			continue; // behind the camera
		const glm::vec3 closest = ro + rd * t;
		const glm::vec3 diff = wp - closest;
		const float d2 = glm::dot(diff, diff);
		if (d2 < best_d2)
		{
			best_d2 = d2;
			best = static_cast<int>(i);
			best_w = wp;
		}
	}
	m_grabbed = best;
	if (best >= 0)
	{
		out_world = best_w;
		m_grab_local = m_particles[best].pos;
	}
	return best >= 0;
}

void ClothComponent::move_grab(const glm::vec3 &world_target)
{
	if (m_grabbed < 0)
		return;
	const glm::mat4 inv = glm::inverse(owner()->transform.matrix());
	m_grab_local = glm::vec3(inv * glm::vec4(world_target, 1.0f));
}

} // namespace cf
