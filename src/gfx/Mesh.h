#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace cf
{

struct Vertex
{
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec2 uv;
	glm::vec3 tangent{1.0f, 0.0f, 0.0f}; // for normal mapping (auto-computed by Mesh)
};

// Owns a VAO/VBO/EBO for an indexed, interleaved vertex array (RAII, move-only).
class Mesh
{
public:
	Mesh(const std::vector<Vertex> &vertices, const std::vector<std::uint32_t> &indices);
	~Mesh();

	Mesh(const Mesh &) = delete;
	Mesh &operator=(const Mesh &) = delete;
	Mesh(Mesh &&other) noexcept;
	Mesh &operator=(Mesh &&other) noexcept;

	void draw() const; // glDrawElements

	// Re-upload the vertex buffer (same count) for animated meshes like cloth.
	// Also refreshes the local AABB.
	void update_vertices(const std::vector<Vertex> &vertices);

	// Local-space axis-aligned bounding box (computed from the vertices).
	const glm::vec3 &aabb_min() const { return m_aabb_min; }
	const glm::vec3 &aabb_max() const { return m_aabb_max; }

	// Local-space vertex positions + triangle indices kept on the CPU (for sampling
	// the surface, e.g. arranging a flock into the mesh's shape).
	const std::vector<glm::vec3> &points() const { return m_points; }
	const std::vector<std::uint32_t> &indices() const { return m_indices; }

	// Built-in primitives (all centered at the origin, with normals + UVs).
	static std::shared_ptr<Mesh> create_cube(float size = 1.0f);
	static std::shared_ptr<Mesh> create_plane(float size = 1.0f, int divisions = 1);
	// Flat circular disk in the XZ plane (normals +Y). UVs map the radius to a
	// Chebyshev (square) distance from the centre, so a UV-based rectangular edge
	// test lands exactly on the circle's rim -- used by round water.
	static std::shared_ptr<Mesh> create_disk(float radius = 0.5f, int segments = 64,
	                                         int rings = 32);
	static std::shared_ptr<Mesh> create_sphere(float radius = 0.5f, int segments = 32,
	                                           int rings = 16);
	// Square-based pyramid whose apex points along -Z (the engine's "forward"), so
	// it reads as an arrowhead showing which way an object faces (e.g. boids).
	static std::shared_ptr<Mesh> create_pyramid(float size = 1.0f);

private:
	GLuint m_vao = 0;
	GLuint m_vbo = 0;
	GLuint m_ebo = 0;
	GLsizei m_index_count = 0;

	glm::vec3 m_aabb_min{0.0f};
	glm::vec3 m_aabb_max{0.0f};
	std::vector<glm::vec3> m_points;      // CPU copy of vertex positions
	std::vector<std::uint32_t> m_indices; // CPU copy of triangle indices
};

} // namespace cf
