#include "core/PhysicsManager.h"

#include "components/ColliderComponent.h"
#include "components/RigidbodyComponent.h"
#include "components/TerrainComponent.h"
#include "core/Object.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace cf
{

namespace
{

using Shape = ColliderComponent::Shape;

glm::vec3 scale_of(ColliderComponent *c)
{
	return c->owner()->transform.scale;
}

// Box's world rotation matrix, derived from the Transform's Euler (YXZ) so it
// matches what's rendered (and the rigidbody's integrated orientation).
glm::mat3 box_rot(ColliderComponent *c)
{
	const Transform &t = c->owner()->transform;
	const glm::vec3 e = glm::radians(t.euler_degrees);
	return glm::mat3(glm::eulerAngleYXZ(e.y, e.x, e.z));
}

// World-space centre of the shape: the Transform pivot plus the collider's local
// `center` offset, rotated + scaled into world space.
glm::vec3 center_of(ColliderComponent *c)
{
	return c->owner()->transform.position + box_rot(c) * (c->center * scale_of(c));
}

float world_radius(ColliderComponent *c)
{
	const glm::vec3 s = scale_of(c);
	return c->radius * std::max({s.x, s.y, s.z});
}
glm::vec3 world_half(ColliderComponent *c)
{
	return c->half_extents * scale_of(c);
}

RigidbodyComponent *rb_of(ColliderComponent *c)
{
	return c->owner()->get_component<RigidbodyComponent>();
}
float inv_mass_of(ColliderComponent *c)
{
	RigidbodyComponent *rb = rb_of(c);
	if (rb && (rb->grabbed || rb->kinematic))
		return 0.0f; // kinematic: immovable but carries velocity (pushes others)
	if (rb && !rb->is_static && rb->mass > 0.0f)
		return 1.0f / rb->mass;
	return 0.0f;
}
float restitution_of(ColliderComponent *c)
{
	RigidbodyComponent *rb = rb_of(c);
	return rb ? rb->restitution : 0.3f;
}
float friction_of(ColliderComponent *c)
{
	RigidbodyComponent *rb = rb_of(c);
	return rb ? rb->friction : 0.6f;
}

// World-space inverse inertia tensor (0 for static / no rigidbody).
glm::mat3 inv_inertia_world(ColliderComponent *c)
{
	RigidbodyComponent *rb = rb_of(c);
	if (!rb || rb->is_static || rb->mass <= 0.0f || rb->freeze_rotation)
		return glm::mat3(0.0f);

	if (c->shape == Shape::Sphere)
	{
		const float r = world_radius(c);
		const float i = 0.4f * rb->mass * r * r; // 2/5 m r^2
		return glm::mat3(1.0f / i);
	}
	// Box: diagonal local inertia, rotated into world by the orientation.
	const glm::vec3 d = world_half(c) * 2.0f; // full extents
	const float m = rb->mass;
	const float ix = (1.0f / 12.0f) * m * (d.y * d.y + d.z * d.z);
	const float iy = (1.0f / 12.0f) * m * (d.x * d.x + d.z * d.z);
	const float iz = (1.0f / 12.0f) * m * (d.x * d.x + d.y * d.y);
	glm::mat3 inv_local(0.0f);
	inv_local[0][0] = 1.0f / ix;
	inv_local[1][1] = 1.0f / iy;
	inv_local[2][2] = 1.0f / iz;
	const glm::mat3 r = glm::mat3_cast(rb->orientation);
	return r * inv_local * glm::transpose(r);
}

struct Contact
{
	glm::vec3 normal; // A -> B
	float depth;
	glm::vec3 point; // world contact point
};

// Sphere vs oriented box. Outputs the box->sphere normal, depth, world point.
bool sphere_vs_box(ColliderComponent *sphere, ColliderComponent *box, glm::vec3 &n_box_to_sphere,
                   float &depth, glm::vec3 &point)
{
	const glm::mat3 rot = box_rot(box);
	const glm::vec3 c_b = center_of(box);
	const glm::vec3 h = world_half(box);
	const glm::vec3 cs = center_of(sphere);
	const float r = world_radius(sphere);

	const glm::vec3 local = glm::transpose(rot) * (cs - c_b); // sphere center in box space
	const glm::vec3 cl = glm::clamp(local, -h, h);
	const glm::vec3 d = local - cl;
	const float l = glm::length(d);
	if (l >= r)
		return false;

	const glm::vec3 n_local = (l > 1e-6f) ? d / l : glm::vec3(0, 1, 0);
	n_box_to_sphere = rot * n_local;
	point = c_b + rot * cl;
	depth = r - l;
	return true;
}

// --- Oriented box-vs-box (SAT + face clipping) -----------------------------
struct OBB
{
	glm::vec3 c;
	glm::vec3 ax[3]; // world axes (unit)
	glm::vec3 h;     // half extents
};

OBB to_obb(ColliderComponent *b)
{
	OBB o;
	o.c = center_of(b);
	const glm::mat3 r = box_rot(b);
	o.ax[0] = r[0];
	o.ax[1] = r[1];
	o.ax[2] = r[2];
	o.h = world_half(b);
	return o;
}

float proj_radius(const OBB &o, const glm::vec3 &l)
{
	return o.h.x * std::abs(glm::dot(o.ax[0], l)) + o.h.y * std::abs(glm::dot(o.ax[1], l)) +
	       o.h.z * std::abs(glm::dot(o.ax[2], l));
}

// Clip a convex polygon to the half-space dot(p - c, m) <= limit.
void clip_to_plane(const std::vector<glm::vec3> &in, std::vector<glm::vec3> &out,
                   const glm::vec3 &c, const glm::vec3 &m, float limit)
{
	out.clear();
	const int n = static_cast<int>(in.size());
	for (int i = 0; i < n; ++i)
	{
		const glm::vec3 &cur = in[i];
		const glm::vec3 &nxt = in[(i + 1) % n];
		const float fc = glm::dot(cur - c, m) - limit;
		const float fn = glm::dot(nxt - c, m) - limit;
		if (fc <= 0.0f)
			out.push_back(cur);
		if ((fc < 0.0f) != (fn < 0.0f))
		{
			const float t = fc / (fc - fn);
			out.push_back(cur + t * (nxt - cur));
		}
	}
}

void generate_box_box(ColliderComponent *col_a, ColliderComponent *col_b, std::vector<Contact> &out)
{
	const OBB a = to_obb(col_a), b = to_obb(col_b);
	const glm::vec3 d = b.c - a.c;

	// SAT over 15 axes; track minimum-overlap axis (oriented A -> B).
	float min_overlap = std::numeric_limits<float>::max();
	glm::vec3 n(0.0f);
	auto test = [&](glm::vec3 l) -> bool
	{
		const float len = glm::length(l);
		if (len < 1e-6f)
			return true; // parallel edges: skip
		l /= len;
		const float overlap = proj_radius(a, l) + proj_radius(b, l) - std::abs(glm::dot(d, l));
		if (overlap <= 0.0f)
			return false; // separating axis found
		if (overlap < min_overlap)
		{
			min_overlap = overlap;
			n = (glm::dot(d, l) < 0.0f) ? -l : l;
		}
		return true;
	};
	for (int i = 0; i < 3; ++i)
		if (!test(a.ax[i]))
			return;
	for (int i = 0; i < 3; ++i)
		if (!test(b.ax[i]))
			return;
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j)
			if (!test(glm::cross(a.ax[i], b.ax[j])))
				return;

	// Reference = box whose axis is most aligned with n; incident = the other.
	auto max_axis_dot = [](const OBB &o, const glm::vec3 &v)
	{
		return std::max({std::abs(glm::dot(o.ax[0], v)), std::abs(glm::dot(o.ax[1], v)),
		                 std::abs(glm::dot(o.ax[2], v))});
	};
	const bool ref_is_a = max_axis_dot(a, n) >= max_axis_dot(b, n);
	const OBB &ref = ref_is_a ? a : b;
	const OBB &inc = ref_is_a ? b : a;
	const glm::vec3 ref_normal = ref_is_a ? n : -n; // ref outward toward incident

	auto face_axis = [](const OBB &o, const glm::vec3 &dir, int &idx, float &sign)
	{
		idx = 0;
		float best = -1.0f;
		for (int i = 0; i < 3; ++i)
		{
			const float dt = glm::dot(o.ax[i], dir);
			if (std::abs(dt) > best)
			{
				best = std::abs(dt);
				idx = i;
				sign = (dt < 0) ? -1.0f : 1.0f;
			}
		}
	};

	int ri;
	float rs;
	face_axis(ref, ref_normal, ri, rs);
	const glm::vec3 r_n = ref.ax[ri] * rs; // ~refNormal
	const glm::vec3 ref_center = ref.c + r_n * ref.h[ri];
	const int rt0 = (ri + 1) % 3, rt1 = (ri + 2) % 3;

	int ii;
	float is;
	face_axis(inc, ref_normal, ii, is);
	const glm::vec3 in_n = inc.ax[ii] * (is > 0 ? -1.0f : 1.0f); // face pointing at ref
	const glm::vec3 inc_center = inc.c + in_n * inc.h[ii];
	const int it0 = (ii + 1) % 3, it1 = (ii + 2) % 3;

	const glm::vec3 iu = inc.ax[it0] * inc.h[it0];
	const glm::vec3 iv = inc.ax[it1] * inc.h[it1];

	std::vector<glm::vec3> poly = {inc_center + iu + iv, inc_center - iu + iv, inc_center - iu - iv,
	                               inc_center + iu - iv};
	std::vector<glm::vec3> tmp;

	clip_to_plane(poly, tmp, ref_center, ref.ax[rt0], ref.h[rt0]);
	poly.swap(tmp);
	clip_to_plane(poly, tmp, ref_center, -ref.ax[rt0], ref.h[rt0]);
	poly.swap(tmp);
	clip_to_plane(poly, tmp, ref_center, ref.ax[rt1], ref.h[rt1]);
	poly.swap(tmp);
	clip_to_plane(poly, tmp, ref_center, -ref.ax[rt1], ref.h[rt1]);
	poly.swap(tmp);

	for (const glm::vec3 &p : poly)
	{
		const float sep = glm::dot(p - ref_center, r_n);
		if (sep <= 0.0f) // only points below the reference face actually touch
			out.push_back({n, -sep, p});
	}
	if (out.empty()) // edge-edge or grazing: one fallback contact
		out.push_back({n, min_overlap, a.c + n * proj_radius(a, n)});
}

// --- Heightfield (terrain surface) vs sphere/box ---------------------------
// `sign` flips the normal into the A->B convention (+1 if the heightfield is A).
// Bodies rest on the terrain's local tangent plane (sampled height + slope normal).
void heightfield_vs_sphere(ColliderComponent *hf, ColliderComponent *sph, float sign,
                           std::vector<Contact> &out)
{
	if (!hf->terrain)
		return;
	const glm::vec3 c = center_of(sph);
	const float r = world_radius(sph);
	float gy;
	glm::vec3 nrm;
	if (!hf->terrain->sample_height(c.x, c.z, gy, nrm))
		return;
	const glm::vec3 surf(c.x, gy, c.z);
	const float dist = glm::dot(c - surf, nrm); // signed distance to the tangent plane
	if (dist >= r)
		return;
	out.push_back({nrm * sign, r - dist, c - nrm * r});
}
void heightfield_vs_box(ColliderComponent *hf, ColliderComponent *box, float sign,
                        std::vector<Contact> &out)
{
	if (!hf->terrain)
		return;
	const glm::mat3 r = box_rot(box);
	const glm::vec3 c = center_of(box), h = world_half(box);
	for (int sx = -1; sx <= 1; sx += 2)
		for (int sy = -1; sy <= 1; sy += 2)
			for (int sz = -1; sz <= 1; sz += 2)
			{
				const glm::vec3 corner = c + r * glm::vec3(sx * h.x, sy * h.y, sz * h.z);
				float gy;
				glm::vec3 nrm;
				if (!hf->terrain->sample_height(corner.x, corner.z, gy, nrm))
					continue;
				const glm::vec3 surf(corner.x, gy, corner.z);
				const float d = glm::dot(corner - surf, nrm); // below tangent plane => penetrating
				if (d < 0.0f)
					out.push_back({nrm * sign, -d, corner});
			}
}

// Append all contacts for the pair (normals point A -> B).
void generate_contacts(ColliderComponent *a, ColliderComponent *b, std::vector<Contact> &out)
{
	const Shape sa = a->shape, sb = b->shape;

	// Terrain heightfield against a dynamic sphere/box (terrain is always static).
	if (sa == Shape::Heightfield || sb == Shape::Heightfield)
	{
		if (sa == Shape::Heightfield && sb == Shape::Sphere)
			heightfield_vs_sphere(a, b, 1.0f, out);
		else if (sa == Shape::Sphere && sb == Shape::Heightfield)
			heightfield_vs_sphere(b, a, -1.0f, out);
		else if (sa == Shape::Heightfield && sb == Shape::Box)
			heightfield_vs_box(a, b, 1.0f, out);
		else if (sa == Shape::Box && sb == Shape::Heightfield)
			heightfield_vs_box(b, a, -1.0f, out);
		return;
	}

	const glm::vec3 ca = center_of(a), cb = center_of(b);

	if (sa == Shape::Sphere && sb == Shape::Sphere)
	{
		const glm::vec3 d = cb - ca;
		const float l = glm::length(d);
		const float sum = world_radius(a) + world_radius(b);
		if (l >= sum)
			return;
		const glm::vec3 n = (l > 1e-6f) ? d / l : glm::vec3(0, 1, 0);
		out.push_back({n, sum - l, ca + n * world_radius(a)});
		return;
	}
	if (sa == Shape::Sphere && sb == Shape::Box)
	{
		glm::vec3 n_bs;
		float depth;
		glm::vec3 p;
		if (sphere_vs_box(a, b, n_bs, depth, p))
			out.push_back({-n_bs, depth, p});
		return;
	}
	if (sa == Shape::Box && sb == Shape::Sphere)
	{
		glm::vec3 n_bs;
		float depth;
		glm::vec3 p;
		if (sphere_vs_box(b, a, n_bs, depth, p))
			out.push_back({n_bs, depth, p});
		return;
	}
	if (sa == Shape::Box && sb == Shape::Box)
	{
		generate_box_box(a, b, out);
		return;
	}
	// sphere vs plane (single point)
	auto sphere_plane = [&](ColliderComponent *s, ColliderComponent *pl, float sign)
	{
		const glm::vec3 nrm = glm::normalize(pl->plane_normal);
		const float dist = glm::dot(center_of(s) - center_of(pl), nrm) - world_radius(s);
		if (dist < 0)
			out.push_back({nrm * sign, -dist, center_of(s) - nrm * world_radius(s)});
	};
	// box vs plane: one contact per penetrating corner -> a manifold that lies flat
	auto box_plane = [&](ColliderComponent *bx, ColliderComponent *pl, float sign)
	{
		const glm::vec3 nrm = glm::normalize(pl->plane_normal);
		const glm::vec3 pp = center_of(pl);
		const glm::mat3 r = box_rot(bx);
		const glm::vec3 c = center_of(bx), h = world_half(bx);
		for (int sx = -1; sx <= 1; sx += 2)
			for (int sy = -1; sy <= 1; sy += 2)
				for (int sz = -1; sz <= 1; sz += 2)
				{
					const glm::vec3 corner = c + r * glm::vec3(sx * h.x, sy * h.y, sz * h.z);
					const float dist = glm::dot(corner - pp, nrm);
					if (dist < 0)
						out.push_back({nrm * sign, -dist, corner});
				}
	};

	if (sa == Shape::Sphere && sb == Shape::Plane)
	{
		sphere_plane(a, b, -1.0f);
		return;
	}
	if (sa == Shape::Plane && sb == Shape::Sphere)
	{
		sphere_plane(b, a, 1.0f);
		return;
	}
	if (sa == Shape::Box && sb == Shape::Plane)
	{
		box_plane(a, b, -1.0f);
		return;
	}
	if (sa == Shape::Plane && sb == Shape::Box)
	{
		box_plane(b, a, 1.0f);
		return;
	}
}

void resolve(ColliderComponent *a, ColliderComponent *b, const Contact &c)
{
	const float inv_ma = inv_mass_of(a), inv_mb = inv_mass_of(b);
	if (inv_ma + inv_mb <= 0.0f)
		return;

	RigidbodyComponent *rb_a = rb_of(a);
	RigidbodyComponent *rb_b = rb_of(b);
	const glm::mat3 inv_ia = inv_inertia_world(a);
	const glm::mat3 inv_ib = inv_inertia_world(b);
	const glm::vec3 n = c.normal;

	// Positional correction (linear only). Gentle, since a manifold may have
	// several contacts each nudging the same body.
	const float percent = 0.2f, slop = 0.005f;
	const glm::vec3 corr = n * (std::max(c.depth - slop, 0.0f) / (inv_ma + inv_mb) * percent);
	a->owner()->transform.position -= corr * inv_ma;
	b->owner()->transform.position += corr * inv_mb;

	const glm::vec3 r_a = c.point - center_of(a);
	const glm::vec3 r_b = c.point - center_of(b);

	auto w_a = [&] { return rb_a ? rb_a->angular_velocity : glm::vec3(0.0f); };
	auto w_b = [&] { return rb_b ? rb_b->angular_velocity : glm::vec3(0.0f); };
	auto v_a = [&] { return rb_a ? rb_a->velocity : glm::vec3(0.0f); };
	auto v_b = [&] { return rb_b ? rb_b->velocity : glm::vec3(0.0f); };

	// Relative velocity at the contact point.
	glm::vec3 rel_v = (v_b() + glm::cross(w_b(), r_b)) - (v_a() + glm::cross(w_a(), r_a));
	const float vn = glm::dot(rel_v, n);
	if (vn >= 0.0f)
		return;

	const glm::vec3 ra_n = glm::cross(r_a, n), rb_n = glm::cross(r_b, n);
	const float denom = inv_ma + inv_mb + glm::dot(n, glm::cross(inv_ia * ra_n, r_a)) +
	                    glm::dot(n, glm::cross(inv_ib * rb_n, r_b));

	// Only bounce above a small speed, so resting contacts don't jitter.
	const float e = (vn < -1.0f) ? std::min(restitution_of(a), restitution_of(b)) : 0.0f;
	const float j = -(1.0f + e) * vn / denom;
	const glm::vec3 imp = n * j;
	if (rb_a && inv_ma > 0)
	{
		rb_a->velocity -= imp * inv_ma;
		rb_a->angular_velocity -= inv_ia * glm::cross(r_a, imp);
	}
	if (rb_b && inv_mb > 0)
	{
		rb_b->velocity += imp * inv_mb;
		rb_b->angular_velocity += inv_ib * glm::cross(r_b, imp);
	}

	// Tangential friction.
	rel_v = (v_b() + glm::cross(w_b(), r_b)) - (v_a() + glm::cross(w_a(), r_a));
	glm::vec3 t = rel_v - glm::dot(rel_v, n) * n;
	const float tl = glm::length(t);
	if (tl > 1e-5f)
	{
		t /= tl;
		const glm::vec3 ra_t = glm::cross(r_a, t), rb_t = glm::cross(r_b, t);
		const float denom_t = inv_ma + inv_mb + glm::dot(t, glm::cross(inv_ia * ra_t, r_a)) +
		                      glm::dot(t, glm::cross(inv_ib * rb_t, r_b));
		const float mu = std::sqrt(friction_of(a) * friction_of(b));
		float jt = -glm::dot(rel_v, t) / denom_t;
		jt = glm::clamp(jt, -j * mu, j * mu);
		const glm::vec3 fimp = t * jt;
		if (rb_a && inv_ma > 0)
		{
			rb_a->velocity -= fimp * inv_ma;
			rb_a->angular_velocity -= inv_ia * glm::cross(r_a, fimp);
		}
		if (rb_b && inv_mb > 0)
		{
			rb_b->velocity += fimp * inv_mb;
			rb_b->angular_velocity += inv_ib * glm::cross(r_b, fimp);
		}
	}
}

// Integrate orientation by angular velocity and write it back to the Transform.
void integrate_orientation(RigidbodyComponent *rb, Transform &t, float h)
{
	if (rb->freeze_rotation)
	{
		rb->angular_velocity = glm::vec3(0.0f);
		return;
	}
	const glm::vec3 w = rb->angular_velocity;
	const glm::quat dq = glm::quat(0.0f, w.x, w.y, w.z) * rb->orientation;
	rb->orientation = glm::normalize(rb->orientation + dq * (0.5f * h));

	const glm::mat4 r = glm::mat4_cast(rb->orientation);
	float y = 0, x = 0, z = 0;
	glm::extractEulerAngleYXZ(r, y, x, z);
	t.euler_degrees = glm::degrees(glm::vec3(x, y, z));
}

} // namespace

PhysicsManager &PhysicsManager::instance()
{
	static PhysicsManager s;
	return s;
}

void PhysicsManager::register_collider(ColliderComponent *c)
{
	m_colliders.push_back(c);
}

void PhysicsManager::unregister_collider(ColliderComponent *c)
{
	m_colliders.erase(std::remove(m_colliders.begin(), m_colliders.end(), c), m_colliders.end());
}

void PhysicsManager::step(float dt)
{
	if (!enabled || dt <= 0.0f)
		return;
	dt = std::min(dt, 0.05f);

	const int sub = std::max(1, substeps);
	const float h = dt / static_cast<float>(sub);

	// Kinematic bodies (gizmo-dragged or path-driven) derive their velocity from
	// their position delta so they push others and keep momentum. Sync orientation
	// from the Transform so rotation doesn't snap back.
	for (ColliderComponent *c : m_colliders)
	{
		RigidbodyComponent *rb = rb_of(c);
		if (!rb || !(rb->grabbed || rb->kinematic))
			continue;
		Transform &t = c->owner()->transform;
		glm::vec3 v = (t.position - rb->prev_position) / dt;
		const float vl = glm::length(v);
		if (vl > 50.0f)
			v *= 50.0f / vl; // clamp wild drags
		rb->velocity = v;
		rb->angular_velocity = glm::vec3(0.0f);
		const glm::vec3 e = glm::radians(t.euler_degrees);
		rb->orientation = glm::quat_cast(glm::eulerAngleYXZ(e.y, e.x, e.z));
	}

	for (int s = 0; s < sub; ++s)
	{
		// Integrate (skip static + kinematic).
		for (ColliderComponent *c : m_colliders)
		{
			RigidbodyComponent *rb = rb_of(c);
			if (!rb || rb->is_static || rb->grabbed || rb->kinematic || rb->mass <= 0.0f)
				continue;
			if (rb->use_gravity)
				rb->velocity += gravity * h;
			rb->angular_velocity *= std::max(0.0f, 1.0f - 0.05f * h); // mild damping
			c->owner()->transform.position += rb->velocity * h;
			integrate_orientation(rb, c->owner()->transform, h);
		}

		// Resolve collisions (regenerate manifolds each iteration).
		const int n = static_cast<int>(m_colliders.size());
		std::vector<Contact> contacts;
		for (int it = 0; it < iterations; ++it)
		{
			for (int i = 0; i < n; ++i)
				for (int k = i + 1; k < n; ++k)
				{
					contacts.clear();
					generate_contacts(m_colliders[i], m_colliders[k], contacts);
					for (const Contact &ct : contacts)
						resolve(m_colliders[i], m_colliders[k], ct);
				}
		}
	}

	// Remember positions for next frame's grab-velocity estimate.
	for (ColliderComponent *c : m_colliders)
		if (RigidbodyComponent *rb = rb_of(c))
			rb->prev_position = c->owner()->transform.position;
}

void PhysicsManager::reset()
{
	for (ColliderComponent *c : m_colliders)
	{
		if (RigidbodyComponent *rb = rb_of(c))
		{
			c->owner()->transform.position = rb->initial_position;
			c->owner()->transform.euler_degrees = rb->initial_euler;
			rb->velocity = glm::vec3(0.0f);
			rb->angular_velocity = glm::vec3(0.0f);
			rb->orientation = rb->initial_orientation;
		}
	}
}

} // namespace cf
