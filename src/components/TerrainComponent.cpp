#include "components/TerrainComponent.h"

#include "components/ColliderComponent.h"
#include "components/MaterialComponent.h"
#include "components/RendererComponent.h"
#include "components/TerrainPropComponent.h"
#include "core/Object.h"
#include "core/ObjectsManager.h"
#include "core/RendererManager.h"
#include "gfx/Mesh.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace cf
{

namespace
{

// Hash-based 2D value noise + fractal Brownian motion, seeded.
float hash(float x, float y, std::uint32_t seed)
{
	std::uint32_t h = seed;
	h ^= static_cast<std::uint32_t>(static_cast<int>(std::floor(x)) * 374761393);
	h ^= static_cast<std::uint32_t>(static_cast<int>(std::floor(y)) * 668265263);
	h = (h ^ (h >> 13)) * 1274126177u;
	h ^= h >> 16;
	return (h & 0xffffffu) / float(0xffffff); // 0..1
}
float vnoise(float x, float y, std::uint32_t seed)
{
	const float xi = std::floor(x), yi = std::floor(y);
	float fx = x - xi, fy = y - yi;
	fx = fx * fx * (3.0f - 2.0f * fx);
	fy = fy * fy * (3.0f - 2.0f * fy);
	const float a = hash(xi, yi, seed), b = hash(xi + 1, yi, seed);
	const float c = hash(xi, yi + 1, seed), d = hash(xi + 1, yi + 1, seed);
	return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
}

// --- low-poly prop meshes (built once, shared by every scattered instance) -----
// Flat-shaded: each face gets its own normal (non-indexed triangle soup).
std::shared_ptr<Mesh> make_flat(const std::vector<glm::vec3> &tris)
{
	std::vector<Vertex> v;
	std::vector<std::uint32_t> idx;
	v.reserve(tris.size());
	for (size_t i = 0; i + 2 < tris.size(); i += 3)
	{
		const glm::vec3 n =
		    glm::normalize(glm::cross(tris[i + 1] - tris[i], tris[i + 2] - tris[i]));
		for (int k = 0; k < 3; ++k)
		{
			Vertex vert;
			vert.position = tris[i + k];
			vert.normal = n;
			vert.uv = {0.0f, 0.0f};
			idx.push_back(static_cast<std::uint32_t>(v.size()));
			v.push_back(vert);
		}
	}
	return std::make_shared<Mesh>(v, idx);
}

// A cone: apex up, K-sided base ring. Used for tree foliage (a stack of two).
void add_cone(std::vector<glm::vec3> &t, glm::vec3 base, float radius, float height, int k)
{
	const glm::vec3 apex = base + glm::vec3(0, height, 0);
	for (int i = 0; i < k; ++i)
	{
		const float a0 = 6.2831853f * i / k, a1 = 6.2831853f * (i + 1) / k;
		const glm::vec3 p0 = base + glm::vec3(std::cos(a0) * radius, 0, std::sin(a0) * radius);
		const glm::vec3 p1 = base + glm::vec3(std::cos(a1) * radius, 0, std::sin(a1) * radius);
		t.insert(t.end(), {apex, p0, p1}); // side
		t.insert(t.end(), {base, p1, p0}); // bottom cap (faces down)
	}
}

std::shared_ptr<Mesh> foliage_mesh()
{
	static std::shared_ptr<Mesh> m = []
	{
		std::vector<glm::vec3> t;
		add_cone(t, {0.0f, 0.0f, 0.0f}, 0.55f, 0.9f, 7);
		add_cone(t, {0.0f, 0.6f, 0.0f}, 0.40f, 0.8f, 7);
		return make_flat(t);
	}();
	return m;
}
std::shared_ptr<Mesh> trunk_mesh()
{
	static std::shared_ptr<Mesh> m = []
	{
		std::vector<glm::vec3> t;
		add_cone(t, {0.0f, -0.3f, 0.0f}, 0.11f, 0.5f, 5); // a stubby 5-sided trunk
		return make_flat(t);
	}();
	return m;
}
std::shared_ptr<Mesh> rock_mesh()
{
	static std::shared_ptr<Mesh> m = []
	{
		// Octahedron with slightly squashed/raised points -> a low-poly boulder.
		const glm::vec3 px{0.5f, 0.05f, 0}, nx{-0.5f, 0.0f, 0}, pz{0, 0.0f, 0.5f},
		    nz{0, 0.1f, -0.5f};
		const glm::vec3 up{0.05f, 0.45f, 0.05f}, dn{0, -0.25f, 0};
		std::vector<glm::vec3> t = {
		    up, px, pz, up, pz, nx, up, nx, nz, up, nz, px,
		    dn, pz, px, dn, nx, pz, dn, nz, nx, dn, px, nz,
		};
		return make_flat(t);
	}();
	return m;
}

} // namespace

void TerrainComponent::on_attach()
{
	if (!owner()->get_component<RendererComponent>())
		owner()->add_component<RendererComponent>();
	if (!owner()->get_component<MaterialComponent>())
		owner()->add_component<MaterialComponent>();
	build_mesh();
}

TerrainComponent::~TerrainComponent()
{
	clear_props();
}

void TerrainComponent::regenerate()
{
	build_mesh();
}

float TerrainComponent::rnd(float a, float b)
{
	return std::uniform_real_distribution<float>(a, b)(m_rng);
}

void TerrainComponent::build_mesh()
{
	auto *rc = owner()->get_component<RendererComponent>();
	auto *mc = owner()->get_component<MaterialComponent>();
	if (!rc || !mc)
		return;

	clear_props(); // remove any props from a previous build before re-scattering

	const int res = std::clamp(resolution, 2, 512);
	const int n = res + 1;
	const float half = size * 0.5f;
	const float step = size / static_cast<float>(res);

	// --- heights first (so normals can use neighbours) ---------------------
	std::vector<float> h(static_cast<size_t>(n) * n);
	float min_h = 1e9f, max_h = -1e9f;
	for (int z = 0; z < n; ++z)
		for (int x = 0; x < n; ++x)
		{
			const float wx = -half + x * step, wz = -half + z * step;
			float amp = 1.0f, freq = frequency, sum = 0.0f, norm = 0.0f;
			for (int o = 0; o < std::max(1, octaves); ++o)
			{
				sum += amp * vnoise(wx * freq, wz * freq, seed + o * 1013u);
				norm += amp;
				amp *= gain;
				freq *= lacunarity;
			}
			float v = norm > 0 ? sum / norm : 0.0f; // 0..1
			v = v * v * (3.0f - 2.0f * v);          // gentle valleys/plateaus
			if (island)
			{
				const float dx = (wx / half), dz = (wz / half);
				const float r = std::sqrt(dx * dx + dz * dz); // 0 centre .. ~1.41 corner
				const float fall =
				    1.0f - std::clamp((r - edge_falloff) / std::max(1.0f - edge_falloff, 1e-3f),
				                      0.0f, 1.0f);
				v *= fall * fall;
			}
			if (donut)
			{
				// Raised ring (land) with a basin in the middle and water outside: a
				// plateau mask peaking at ringRadius, falling off on both sides.
				const float dx = (wx / half), dz = (wz / half);
				const float r = std::sqrt(dx * dx + dz * dz);
				const float w = std::max(ring_width, 1e-3f);
				const float inner = glm::smoothstep(ring_radius - w, ring_radius - w * 0.35f, r);
				const float outer =
				    1.0f - glm::smoothstep(ring_radius + w * 0.35f, ring_radius + w, r);
				v *= inner * outer;
			}
			const float height = v * height_scale;
			h[static_cast<size_t>(z) * n + x] = height;
			min_h = std::min(min_h, height);
			max_h = std::max(max_h, height);
		}
	const float range = std::max(max_h - min_h, 1e-4f);

	// The mesh is built as a triangle soup so each face can carry its own normal
	// when low-poly (faceted) is on. uv.x = normalised height (drives the height
	// colour bands); uv.y = "cliff marker" (0 = surface, 1 = slab side/dirt).
	auto hat = [&](int x, int z) { return h[static_cast<size_t>(z) * n + x]; };
	auto pos = [&](int x, int z)
	{ return glm::vec3(-half + x * step, hat(x, z), -half + z * step); };
	auto u_vh = [&](int x, int z) { return (hat(x, z) - min_h) / range; };
	auto vn   = [&](int x, int z) { // smooth vertex normal via central difference
        const float hl = hat(std::max(0, x - 1), z), hr = hat(std::min(n - 1, x + 1), z);
        const float hd = hat(x, std::max(0, z - 1)), hu = hat(x, std::min(n - 1, z + 1));
        return glm::normalize(glm::vec3(hl - hr, 2.0f * step, hd - hu));
    };

	std::vector<Vertex> verts;
	verts.reserve(static_cast<size_t>(res) * res * 6 + (slab ? res * 24 + 6 : 0));
	auto push = [&](const glm::vec3 &p, const glm::vec3 &n, float uh, float cliff)
	{
		Vertex v;
		v.position = p;
		v.normal = n;
		v.uv = {uh, cliff};
		verts.push_back(v);
	};
	// A flat-shaded triangle: one face normal for all three corners.
	auto flat_tri = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, float ua, float ub, float uc,
	                    float cliff, const glm::vec3 *hint)
	{
		glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
		if (hint && glm::dot(n, *hint) < 0.0f)
		{
			std::swap(b, c);
			std::swap(ub, uc);
			n = -n;
		}
		push(a, n, ua, cliff);
		push(b, n, ub, cliff);
		push(c, n, uc, cliff);
	};

	// --- top surface -------------------------------------------------------
	for (int z = 0; z < res; ++z)
		for (int x = 0; x < res; ++x)
		{
			const glm::vec3 a = pos(x, z), b = pos(x + 1, z), c = pos(x, z + 1),
			                d = pos(x + 1, z + 1);
			const float ua = u_vh(x, z), ub = u_vh(x + 1, z), uc = u_vh(x, z + 1),
			            ud = u_vh(x + 1, z + 1);
			if (low_poly)
			{
				flat_tri(a, c, b, ua, uc, ub, 0.0f, nullptr);
				flat_tri(b, c, d, ub, uc, ud, 0.0f, nullptr);
			}
			else
			{
				const glm::vec3 na = vn(x, z), nb = vn(x + 1, z), nc = vn(x, z + 1),
				                nd = vn(x + 1, z + 1);
				push(a, na, ua, 0.0f);
				push(c, nc, uc, 0.0f);
				push(b, nb, ub, 0.0f);
				push(b, nb, ub, 0.0f);
				push(c, nc, uc, 0.0f);
				push(d, nd, ud, 0.0f);
			}
		}

	// --- slab: cliff walls around the perimeter + a flat base --------------
	if (slab)
	{
		const float base_y = min_h - base_depth;
		const float top_mark = 0.45f, bot_mark = 1.0f; // grassy lip blends down into dirt
		auto wall = [&](glm::vec3 t_a, glm::vec3 t_b, glm::vec3 out)
		{
			const glm::vec3 b_a = {t_a.x, base_y, t_a.z}, b_b = {t_b.x, base_y, t_b.z};
			flat_tri(t_a, t_b, b_a, top_mark, top_mark, bot_mark, 1.0f, &out);
			flat_tri(t_b, b_b, b_a, top_mark, bot_mark, bot_mark, 1.0f, &out);
		};
		const glm::vec3 n_z{0, 0, -1}, p_z{0, 0, 1}, n_x{-1, 0, 0}, p_x{1, 0, 0}, dn{0, -1, 0};
		for (int x = 0; x < res; ++x)
		{
			wall(pos(x, 0), pos(x + 1, 0), n_z);
			wall(pos(x, res), pos(x + 1, res), p_z);
		}
		for (int z = 0; z < res; ++z)
		{
			wall(pos(0, z), pos(0, z + 1), n_x);
			wall(pos(res, z), pos(res, z + 1), p_x);
		}
		const glm::vec3 c0{-half, base_y, -half}, c1{half, base_y, -half};
		const glm::vec3 c2{-half, base_y, half}, c3{half, base_y, half};
		flat_tri(c0, c1, c2, bot_mark, bot_mark, bot_mark, 1.0f, &dn);
		flat_tri(c1, c3, c2, bot_mark, bot_mark, bot_mark, 1.0f, &dn);
	}

	std::vector<std::uint32_t> idx(verts.size());
	for (std::uint32_t i = 0; i < idx.size(); ++i)
		idx[i] = i;

	rc->mesh = std::make_shared<Mesh>(verts, idx);
	rc->mesh_name = "Terrain";
	rc->submeshes.clear();

	Material &m = mc->material;
	m.shader = RendererManager::instance().terrain_shader();
	m.albedo = grass_color;
	m.transparent = false;
	m.reflective = false;

	// Cache the heightfield and defer prop scattering to the next update() — creating
	// Objects here (during onAttach / scene load) races the load loop, so we don't.
	m_h = h;
	m_n = n;
	m_step = step;
	m_min_h = min_h;
	m_range = range;
	m_scatter_dirty = true;

	ensure_collider();
}

void TerrainComponent::ensure_collider()
{
	auto *col = owner()->get_component<ColliderComponent>();
	if (collider)
	{
		if (!col)
			col = owner()->add_component<ColliderComponent>();
		col->shape = ColliderComponent::Shape::Heightfield;
		col->terrain = this;
	}
	else if (col && col->terrain == this)
	{
		owner()->remove_component<ColliderComponent>();
	}
}

bool TerrainComponent::sample_height(float world_x, float world_z, float &out_y,
                                     glm::vec3 &out_normal) const
{
	if (m_n < 2 || m_h.empty())
		return false;
	const glm::vec3 base = owner()->world_position();
	const float half = size * 0.5f;
	const float lx = world_x - base.x, lz = world_z - base.z;
	if (lx < -half || lx > half || lz < -half || lz > half)
		return false;

	const int n = m_n;
	const float gx = std::clamp((lx + half) / m_step, 0.0f, float(n - 1) - 1e-3f);
	const float gz = std::clamp((lz + half) / m_step, 0.0f, float(n - 1) - 1e-3f);
	const int x0 = int(gx), z0 = int(gz);
	const float fx = gx - x0, fz = gz - z0;
	auto h = [&](int x, int z) { return m_h[size_t(z) * n + x]; };
	const float h00 = h(x0, z0), h10 = h(x0 + 1, z0), h01 = h(x0, z0 + 1), h11 = h(x0 + 1, z0 + 1);
	const float hy = (h00 * (1 - fx) + h10 * fx) * (1 - fz) + (h01 * (1 - fx) + h11 * fx) * fz;
	const float dhx = (h10 - h00) + fz * ((h11 - h01) - (h10 - h00));
	const float dhz = (h01 - h00) + fx * ((h11 - h10) - (h01 - h00));
	out_y = base.y + hy;
	out_normal = glm::normalize(glm::vec3(-dhx, m_step, -dhz));
	return true;
}

void TerrainComponent::update(float)
{
	if (!m_scatter_dirty)
		return;
	m_scatter_dirty = false;
	if (scatter)
		scatter_props();
}

void TerrainComponent::clear_props()
{
	ObjectsManager &om = ObjectsManager::instance();
	for (TerrainPropComponent *p : m_props)
		if (p)
			om.remove_object(p->owner());
	m_props.clear();
}

void TerrainComponent::scatter_props()
{
	if (m_n < 2 || m_h.empty())
		return;
	const std::vector<float> &h = m_h;
	const int n = m_n;
	const float step = m_step, min_h = m_min_h, range = m_range;
	ObjectsManager &om = ObjectsManager::instance();
	auto shader = RendererManager::instance().default_shader();
	const float half = size * 0.5f;
	const glm::vec3 base = owner()->world_position();

	// Bilinear height + an analytic normal (slope) at a continuous (wx,wz).
	auto sample = [&](float wx, float wz, float &out_y, float &out_slope)
	{
		const float gx = std::clamp((wx + half) / step, 0.0f, float(n - 1) - 1e-3f);
		const float gz = std::clamp((wz + half) / step, 0.0f, float(n - 1) - 1e-3f);
		const int x0 = int(gx), z0 = int(gz);
		const float fx = gx - x0, fz = gz - z0;
		auto height_at = [&](int x, int z) { return h[size_t(z) * n + x]; };
		const float h00 = height_at(x0, z0), h10 = height_at(x0 + 1, z0),
		            h01 = height_at(x0, z0 + 1), h11 = height_at(x0 + 1, z0 + 1);
		out_y = (h00 * (1 - fx) + h10 * fx) * (1 - fz) + (h01 * (1 - fx) + h11 * fx) * fz;
		const float dhx = (h10 - h00) + fz * ((h11 - h01) - (h10 - h00));
		const float dhz = (h01 - h00) + fx * ((h11 - h10) - (h01 - h00));
		glm::vec3 nrm = glm::normalize(glm::vec3(-dhx, step, -dhz));
		out_slope = 1.0f - glm::clamp(nrm.y, 0.0f, 1.0f);
	};

	// Place one prop: a Boid-style runtime Object the serializer skips.
	auto place = [&](const char *name, std::shared_ptr<Mesh> trunk, glm::vec3 trunk_col,
	                 std::shared_ptr<Mesh> crown, glm::vec3 crown_col, glm::vec3 pos, float s,
	                 float yaw)
	{
		Object *o = om.create_object(name);
		o->transform.position = pos;
		o->transform.scale = glm::vec3(s);
		o->transform.euler_degrees = {0.0f, yaw, 0.0f};
		auto *rc = o->add_component<RendererComponent>();
		rc->mesh_name = name;
		rc->submeshes.push_back({trunk, Material{}});
		rc->submeshes.back().material.shader = shader;
		rc->submeshes.back().material.albedo = trunk_col;
		rc->submeshes.back().material.roughness = 0.85f;
		if (crown)
		{
			rc->submeshes.push_back({crown, Material{}});
			rc->submeshes.back().material.shader = shader;
			rc->submeshes.back().material.albedo = crown_col;
			rc->submeshes.back().material.roughness = 0.8f;
		}
		m_props.push_back(o->add_component<TerrainPropComponent>());
		m_props.back()->terrain = this;
		o->set_parent(owner(), true); // nest under the terrain (pos set in world space)
	};

	const float margin = step * 1.5f; // keep props off the very cliff edge
	auto tree = foliage_mesh();
	auto trunk = trunk_mesh();
	auto rock = rock_mesh();

	// Trees: cluster into FORESTS via a low-frequency density mask, so dense groves
	// form with clearings between them; beaches / low / steep ground stay bare.
	// Rejection-sample until we hit treeCount placed (or run out of attempts).
	const float f_freq = 1.0f / std::max(2.0f, forest_scale);
	const int max_attempts = std::max(tree_count * 16, 256);
	int placed = 0;
	for (int a = 0; a < max_attempts && placed < tree_count; ++a)
	{
		const float wx = rnd(-half + margin, half - margin),
		            wz = rnd(-half + margin, half - margin);
		float y, slope;
		sample(wx, wz, y, slope);
		const float nh = (y - min_h) / range;
		if (slope > 0.33f || nh < 0.18f || nh > 0.72f)
			continue; // not on sand/peaks/cliffs
		if ((base.y + y) < water_level + tree_min_height)
			continue; // low ground stays bare
		// Forest mask: probabilistic acceptance by clump density -> groves + clearings.
		const float fm = vnoise(wx * f_freq + 53.0f, wz * f_freq + 19.0f, seed + 7919u);
		const float density = glm::smoothstep(0.40f, 0.72f, fm);
		if (rnd(0.0f, 1.0f) > density)
			continue;
		// A few are smaller "saplings/bushes" for variety ("cositas").
		const float s =
		    prop_scale * (rnd(0.0f, 1.0f) < 0.25f ? rnd(0.45f, 0.7f) : rnd(0.85f, 1.5f));
		place("Tree", trunk, glm::vec3(0.34f, 0.23f, 0.14f), tree, tree_color * rnd(0.85f, 1.15f),
		      base + glm::vec3(wx, y - 0.1f, wz), s, rnd(0.0f, 360.0f));
		++placed;
	}
	// Rocks: steeper or higher ground.
	for (int i = 0; i < rock_count; ++i)
	{
		const float wx = rnd(-half + margin, half - margin),
		            wz = rnd(-half + margin, half - margin);
		float y, slope;
		sample(wx, wz, y, slope);
		const float nh = (y - min_h) / range;
		if ((slope < 0.22f && nh < 0.6f) || (base.y + y) < water_level + 0.1f)
			continue;
		const float s = prop_scale * rnd(0.5f, 1.3f);
		const float g = rnd(0.36f, 0.46f);
		place("Rock", rock, glm::vec3(g, g, g * 1.02f), nullptr, {}, base + glm::vec3(wx, y, wz), s,
		      rnd(0.0f, 360.0f));
	}
}

} // namespace cf
