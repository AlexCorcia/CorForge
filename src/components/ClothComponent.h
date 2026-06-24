#pragma once

#include "core/Component.h"

#include <glm/glm.hpp>

#include <memory>
#include <unordered_set>
#include <vector>

namespace cf
{

class Mesh;

// An IMPLICIT (backward-Euler) mass-spring cloth, à la Baraff-Witkin "Large Steps
// in Cloth Simulation": a grid of point masses joined by springs, advanced by
// solving A·Δv = b each step with a matrix-free, constraint-filtered Conjugate
// Gradient. Unconditionally stable (stiff cloth never explodes). Drives its
// Object's RendererComponent mesh; simulated in the Object's local space, so the
// Object position is the cloth's anchor.
class ClothComponent : public Component
{
public:
	// Grid + simulation parameters (editable in the inspector). Changing the grid
	// re-creates the cloth.
	int cols = 24;
	int rows = 18;
	float spacing = 0.12f;
	int iterations = 25;          // Conjugate-Gradient iterations per step
	float stiffness = 1200.0f;    // stretch/shear spring constant (higher = less stretchy)
	float bend_stiffness = 20.0f; // resistance to folding (low = drapes, high = stiff sheet)
	float spring_damping = 0.3f;  // spring damping along its axis (keep low: ~8 springs/node)
	float mass = 0.03f;           // mass per particle
	float damping = 0.985f; // global air drag (velocity retained per 1/60 s) -> settles the swing
	float gravity_scale = 1.0f; // multiplies the world gravity (PhysicsManager)
	int pin_mode = 0;           // 0 corners, 1 top row, 2 corners+centre, 3 four corners
	float slack = 0.18f;        // pulls pins toward centre -> deeper drape / catenary sag

	// Tearing: a link snaps when stretched past rest * tearFactor, ripping a hole
	// (the triangles spanning the snapped link are dropped from the mesh).
	bool tearable = false;
	float tear_factor = 1.8f;

	void on_attach() override;
	void update(float dt) override;

	void mark_dirty() { m_dirty = true; } // params changed -> rebuild next update
	void reset_sim();                     // drop back to the flat starting grid

	// Mouse interaction: grab the particle nearest the world ray (within maxDist),
	// then drag it with moveGrab() (a grabbed particle is pinned to the cursor and
	// pulls the rest of the cloth -- pull hard to tear). Returns the grabbed
	// particle's world position via outWorld.
	bool grab(const glm::vec3 &ray_origin, const glm::vec3 &ray_dir, float max_dist,
	          glm::vec3 &out_world);
	void move_grab(const glm::vec3 &world_target);
	void release_grab() { m_grabbed = -1; }
	bool is_grabbing() const { return m_grabbed >= 0; }

private:
	struct Particle
	{
		glm::vec3 pos{0.0f}, vel{0.0f};
		bool pinned = false;
		glm::vec3 pin_pos{0.0f};
	};
	struct Constraint
	{
		int a = 0, b = 0;
		float rest = 0.0f;
		bool torn = false;
		bool bend = false;
	};

	void rebuild();     // (re)create particles, springs, pins and the mesh
	void step(float h); // one implicit step + tearing + collide
	void solve_implicit(float h, const glm::vec3 &g_local); // backward-Euler + CG
	void apply_a(const std::vector<glm::vec3> &p, std::vector<glm::vec3> &out, float h) const;
	void filter_fixed(std::vector<glm::vec3> &v) const; // zero constrained DOFs
	void collide(); // push particles out of the scene's colliders
	void update_mesh();
	void rebuild_indices(); // triangle list, skipping triangles with a torn edge
	bool is_fixed(int i) const { return m_particles[i].pinned || i == m_grabbed; }
	long long edge_key(int a, int b) const
	{
		return static_cast<long long>(a < b ? a : b) * static_cast<long long>(m_particles.size()) +
		       static_cast<long long>(a < b ? b : a);
	}
	int idx(int x, int y) const { return y * cols + x; }

	std::vector<Particle> m_particles;
	std::vector<Constraint> m_constraints;
	std::vector<unsigned int> m_indices;
	std::shared_ptr<Mesh> m_mesh;
	std::unordered_set<long long> m_torn_edges; // snapped links (for triangle culling)

	// Solver scratch (per step): CG vectors + per-spring cached direction/factor.
	std::vector<glm::vec3> m_force, m_b, m_dv, m_r, m_p, m_ap;
	std::vector<glm::vec3> m_spring_u; // unit axis per spring (this step)
	std::vector<float> m_spring_f;     // max(0, 1 - rest/len) per spring
	std::vector<float> m_spring_k;     // effective stiffness per spring (stretch or bend)

	int m_grabbed = -1;            // dragged particle index, or -1
	glm::vec3 m_grab_local{0.0f};  // drag target in local space
	bool m_topology_dirty = false; // a tear changed the triangle list
	bool m_dirty = true;
};

} // namespace cf
