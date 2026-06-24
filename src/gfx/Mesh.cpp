#include "gfx/Mesh.h"

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <limits>
#include <utility>

namespace cf
{

namespace
{
// Per-vertex tangents from positions + UVs (for the TBN used in normal mapping).
void compute_tangents(std::vector<Vertex> &v, const std::vector<std::uint32_t> &idx)
{
	std::vector<glm::vec3> tan(v.size(), glm::vec3(0.0f));
	for (size_t i = 0; i + 2 < idx.size(); i += 3)
	{
		const std::uint32_t a = idx[i], b = idx[i + 1], c = idx[i + 2];
		const glm::vec3 e1 = v[b].position - v[a].position;
		const glm::vec3 e2 = v[c].position - v[a].position;
		const glm::vec2 d1 = v[b].uv - v[a].uv;
		const glm::vec2 d2 = v[c].uv - v[a].uv;
		const float det = d1.x * d2.y - d2.x * d1.y;
		const float f = (std::abs(det) > 1e-8f) ? 1.0f / det : 0.0f;
		const glm::vec3 t = f * (d2.y * e1 - d1.y * e2);
		tan[a] += t;
		tan[b] += t;
		tan[c] += t;
	}
	for (size_t i = 0; i < v.size(); ++i)
	{
		const glm::vec3 n = v[i].normal;
		glm::vec3 t = tan[i] - n * glm::dot(n, tan[i]); // Gram-Schmidt vs the normal
		if (glm::dot(t, t) < 1e-12f)
		{ // degenerate UVs: pick any perpendicular
			t = std::abs(n.x) < 0.9f ? glm::cross(n, glm::vec3(1, 0, 0))
			                         : glm::cross(n, glm::vec3(0, 1, 0));
		}
		v[i].tangent = glm::normalize(t);
	}
}
} // namespace

Mesh::Mesh(const std::vector<Vertex> &vertices, const std::vector<std::uint32_t> &indices)
    : m_index_count(static_cast<GLsizei>(indices.size()))
{
	// Local-space bounds for picking / culling.
	constexpr float big = std::numeric_limits<float>::max();
	m_aabb_min = glm::vec3(big);
	m_aabb_max = glm::vec3(-big);
	for (const Vertex &v : vertices)
	{
		m_aabb_min = glm::min(m_aabb_min, v.position);
		m_aabb_max = glm::max(m_aabb_max, v.position);
	}
	if (vertices.empty())
	{
		m_aabb_min = m_aabb_max = glm::vec3(0.0f);
	}

	m_points.reserve(vertices.size());
	for (const Vertex &v : vertices)
		m_points.push_back(v.position);
	m_indices = indices;

	std::vector<Vertex> verts = vertices; // mutable copy so we can fill tangents
	compute_tangents(verts, indices);

	glGenVertexArrays(1, &m_vao);
	glGenBuffers(1, &m_vbo);
	glGenBuffers(1, &m_ebo);

	glBindVertexArray(m_vao);

	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)),
	             verts.data(), GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER,
	             static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)), indices.data(),
	             GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
	                      reinterpret_cast<void *>(offsetof(Vertex, position)));
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
	                      reinterpret_cast<void *>(offsetof(Vertex, normal)));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
	                      reinterpret_cast<void *>(offsetof(Vertex, uv)));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
	                      reinterpret_cast<void *>(offsetof(Vertex, tangent)));
	glEnableVertexAttribArray(3);

	glBindVertexArray(0);
}

Mesh::~Mesh()
{
	if (m_ebo)
		glDeleteBuffers(1, &m_ebo);
	if (m_vbo)
		glDeleteBuffers(1, &m_vbo);
	if (m_vao)
		glDeleteVertexArrays(1, &m_vao);
}

Mesh::Mesh(Mesh &&o) noexcept
    : m_vao(o.m_vao), m_vbo(o.m_vbo), m_ebo(o.m_ebo), m_index_count(o.m_index_count)
{
	o.m_vao = o.m_vbo = o.m_ebo = 0;
	o.m_index_count = 0;
}

Mesh &Mesh::operator=(Mesh &&o) noexcept
{
	if (this != &o)
	{
		if (m_ebo)
			glDeleteBuffers(1, &m_ebo);
		if (m_vbo)
			glDeleteBuffers(1, &m_vbo);
		if (m_vao)
			glDeleteVertexArrays(1, &m_vao);
		m_vao = o.m_vao;
		m_vbo = o.m_vbo;
		m_ebo = o.m_ebo;
		m_index_count = o.m_index_count;
		o.m_vao = o.m_vbo = o.m_ebo = 0;
		o.m_index_count = 0;
	}
	return *this;
}

void Mesh::draw() const
{
	glBindVertexArray(m_vao);
	glDrawElements(GL_TRIANGLES, m_index_count, GL_UNSIGNED_INT, nullptr);
}

void Mesh::update_vertices(const std::vector<Vertex> &vertices)
{
	if (vertices.empty())
		return;
	constexpr float big = std::numeric_limits<float>::max();
	m_aabb_min = glm::vec3(big);
	m_aabb_max = glm::vec3(-big);
	m_points.resize(vertices.size());
	for (size_t i = 0; i < vertices.size(); ++i)
	{
		m_aabb_min = glm::min(m_aabb_min, vertices[i].position);
		m_aabb_max = glm::max(m_aabb_max, vertices[i].position);
		m_points[i] = vertices[i].position;
	}
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
	glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
	                vertices.data());
}

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------

std::shared_ptr<Mesh> Mesh::create_cube(float size)
{
	const float h = size * 0.5f;

	// 6 faces x 4 verts; each face has its own normal + a full UV quad.
	struct Face
	{
		glm::vec3 n, u, v;
	};
	const Face faces[6] = {
	    {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},   // +Z
	    {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}}, // -Z
	    {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},  // +X
	    {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},  // -X
	    {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},  // +Y
	    {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},  // -Y
	};

	std::vector<Vertex> verts;
	std::vector<std::uint32_t> indices;
	verts.reserve(24);
	indices.reserve(36);

	for (const Face &f : faces)
	{
		const glm::vec3 center = f.n * h;
		const auto base = static_cast<std::uint32_t>(verts.size());
		// 4 corners from the in-plane axes (u, v)
		verts.push_back({center - f.u * h - f.v * h, f.n, {0, 0}});
		verts.push_back({center + f.u * h - f.v * h, f.n, {1, 0}});
		verts.push_back({center + f.u * h + f.v * h, f.n, {1, 1}});
		verts.push_back({center - f.u * h + f.v * h, f.n, {0, 1}});
		indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
	}
	return std::make_shared<Mesh>(verts, indices);
}

std::shared_ptr<Mesh> Mesh::create_plane(float size, int divisions)
{
	divisions = divisions < 1 ? 1 : divisions;
	const float half = size * 0.5f;
	const float step = size / static_cast<float>(divisions);

	std::vector<Vertex> verts;
	std::vector<std::uint32_t> indices;

	for (int z = 0; z <= divisions; ++z)
	{
		for (int x = 0; x <= divisions; ++x)
		{
			const float px = -half + static_cast<float>(x) * step;
			const float pz = -half + static_cast<float>(z) * step;
			const float u = static_cast<float>(x) / static_cast<float>(divisions);
			const float v = static_cast<float>(z) / static_cast<float>(divisions);
			verts.push_back({{px, 0.0f, pz}, {0, 1, 0}, {u, v}});
		}
	}
	const int stride = divisions + 1;
	for (int z = 0; z < divisions; ++z)
	{
		for (int x = 0; x < divisions; ++x)
		{
			const auto i0 = static_cast<std::uint32_t>(z * stride + x);
			const auto i1 = static_cast<std::uint32_t>(z * stride + x + 1);
			const auto i2 = static_cast<std::uint32_t>((z + 1) * stride + x);
			const auto i3 = static_cast<std::uint32_t>((z + 1) * stride + x + 1);
			indices.insert(indices.end(), {i0, i2, i1, i1, i2, i3});
		}
	}
	return std::make_shared<Mesh>(verts, indices);
}

std::shared_ptr<Mesh> Mesh::create_pyramid(float size)
{
	const float h = size * 0.5f;
	const glm::vec3 a(0, 0, -h); // apex: points -Z (forward)
	const glm::vec3 b0(-h, -h, h), b1(h, -h, h), b2(h, h, h), b3(-h, h, h); // base at +Z

	std::vector<Vertex> verts;
	std::vector<std::uint32_t> indices;
	// Flat-shaded faces: each triangle gets its own outward normal (origin is inside
	// the pyramid, so the normal pointing away from the origin is the outward one).
	auto tri = [&](glm::vec3 p0, glm::vec3 p1, glm::vec3 p2)
	{
		glm::vec3 n = glm::normalize(glm::cross(p1 - p0, p2 - p0));
		const glm::vec3 c = (p0 + p1 + p2) / 3.0f;
		if (glm::dot(n, c) < 0.0f)
		{
			n = -n;
			std::swap(p1, p2);
		}
		const auto base = static_cast<std::uint32_t>(verts.size());
		verts.push_back({p0, n, {0.0f, 0.0f}});
		verts.push_back({p1, n, {0.0f, 1.0f}});
		verts.push_back({p2, n, {1.0f, 1.0f}});
		indices.insert(indices.end(), {base, base + 1, base + 2});
	};
	tri(a, b0, b1);
	tri(a, b1, b2);
	tri(a, b2, b3);
	tri(a, b3, b0); // 4 sides
	tri(b0, b2, b1);
	tri(b0, b3, b2); // square base
	return std::make_shared<Mesh>(verts, indices);
}

std::shared_ptr<Mesh> Mesh::create_disk(float radius, int segments, int rings)
{
	segments = segments < 3 ? 3 : segments;
	rings = rings < 1 ? 1 : rings;
	const float pi2 = 2.0f * glm::pi<float>();

	std::vector<Vertex> verts;
	std::vector<std::uint32_t> indices;

	// UV maps a point at polar (r, theta) to Chebyshev distance r from the centre:
	// uv = 0.5 + r * (cos,sin)/max(|cos|,|sin|). Then min(u,1-u,v,1-v) == 0.5 - r,
	// so the water shader's rectangular rim/foam test traces the circle exactly.
	auto uv_for = [](float r_norm, float c, float s)
	{
		const float ac = std::fabs(c), as = std::fabs(s);
		const float m = ac > as ? ac : as;
		const glm::vec2 d = (m > 1e-5f) ? glm::vec2(c, s) / m : glm::vec2(0.0f);
		return glm::vec2(0.5f) + 0.5f * r_norm * d;
	};

	verts.push_back({{0.0f, 0.0f, 0.0f}, {0, 1, 0}, {0.5f, 0.5f}}); // centre
	for (int ring = 1; ring <= rings; ++ring)
	{
		const float r_norm = static_cast<float>(ring) / static_cast<float>(rings);
		const float rr = radius * r_norm;
		for (int s = 0; s < segments; ++s)
		{
			const float a = pi2 * static_cast<float>(s) / static_cast<float>(segments);
			const float c = std::cos(a), sn = std::sin(a);
			verts.push_back({{rr * c, 0.0f, rr * sn}, {0, 1, 0}, uv_for(r_norm, c, sn)});
		}
	}

	// Inner fan: centre (0) to the first ring.
	for (int s = 0; s < segments; ++s)
	{
		const auto a = static_cast<std::uint32_t>(1 + s);
		const auto b = static_cast<std::uint32_t>(1 + (s + 1) % segments);
		indices.insert(indices.end(), {0u, a, b});
	}
	// Quad strips between successive rings.
	for (int ring = 1; ring < rings; ++ring)
	{
		const auto base0 = static_cast<std::uint32_t>(1 + (ring - 1) * segments);
		const auto base1 = static_cast<std::uint32_t>(1 + ring * segments);
		for (int s = 0; s < segments; ++s)
		{
			const int sn = (s + 1) % segments;
			const std::uint32_t i0 = base0 + s, i1 = base0 + sn;
			const std::uint32_t i2 = base1 + s, i3 = base1 + sn;
			indices.insert(indices.end(), {i0, i2, i1, i1, i2, i3});
		}
	}
	return std::make_shared<Mesh>(verts, indices);
}

std::shared_ptr<Mesh> Mesh::create_sphere(float radius, int segments, int rings)
{
	segments = segments < 3 ? 3 : segments;
	rings = rings < 2 ? 2 : rings;

	std::vector<Vertex> verts;
	std::vector<std::uint32_t> indices;
	const float pi = glm::pi<float>();

	for (int y = 0; y <= rings; ++y)
	{
		const float v = static_cast<float>(y) / static_cast<float>(rings);
		const float phi = v * pi; // 0..pi (top to bottom)
		for (int x = 0; x <= segments; ++x)
		{
			const float u = static_cast<float>(x) / static_cast<float>(segments);
			const float theta = u * 2.0f * pi;
			const glm::vec3 n{std::cos(theta) * std::sin(phi), std::cos(phi),
			                  std::sin(theta) * std::sin(phi)};
			verts.push_back({n * radius, n, {u, v}});
		}
	}
	const int stride = segments + 1;
	for (int y = 0; y < rings; ++y)
	{
		for (int x = 0; x < segments; ++x)
		{
			const auto i0 = static_cast<std::uint32_t>(y * stride + x);
			const auto i1 = static_cast<std::uint32_t>(y * stride + x + 1);
			const auto i2 = static_cast<std::uint32_t>((y + 1) * stride + x);
			const auto i3 = static_cast<std::uint32_t>((y + 1) * stride + x + 1);
			indices.insert(indices.end(), {i0, i2, i1, i1, i2, i3});
		}
	}
	return std::make_shared<Mesh>(verts, indices);
}

} // namespace cf
