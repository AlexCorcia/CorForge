#pragma once

#include "core/Component.h"

#include <glm/glm.hpp>

#include <random>
#include <string>
#include <vector>

namespace cf
{

class BoidComponent;
class Object;

// Classic Reynolds boids/flocking. Spawns a configurable number of entities and
// steers them every frame with separation + alignment + cohesion (+ a soft bounds
// force keeping them in a box around the flock's position).
//
// The boids are REAL Objects, so any component you add to them composes:
//  - physics = true gives each boid a Rigidbody + Collider and applies the flocking
//    as a steering force, so gravity, collisions and Buoyancy all affect them.
//  - buoyancy = true additionally drops a BuoyancyComponent on each boid (the
//    requested "if I add buoyancy they should be affected" demo, over water).
class FlockComponent : public Component
{
public:
	int count = 80;                 // number of boids
	float spawn_radius = 4.0f;      // initial scatter radius
	glm::vec3 bounds = {12, 6, 12}; // half-extents of the containing box
	float max_speed = 6.0f;
	float min_speed = 2.5f;
	float perception = 2.6f;      // neighbour radius (align/cohesion)
	float separation_dist = 1.1f; // closer than this -> push apart
	float w_separation = 1.7f;
	float w_alignment = 1.0f;
	float w_cohesion = 1.0f;
	float w_bounds = 2.5f;
	float w_avoid = 3.0f;      // steer away from scene colliders
	float avoid_radius = 2.0f; // how far ahead obstacles are felt
	float w_seek = 0.0f;       // steer toward `targetName` (0 = ignore)
	std::string target_name;   // name of the Object to follow ("" = none)
	bool form_shape = false;   // boids arrange into the target's MESH shape
	float w_form = 6.0f;       // how hard boids pull to their shape slot
	float max_force = 14.0f;   // steering acceleration clamp
	float boid_scale = 0.35f;
	glm::vec3 boid_color = {0.25f, 0.7f, 1.0f};
	std::string boid_mesh = "Pyramid"; // mesh each boid uses (applied on respawn)
	bool physics = false;              // boids get Rigidbody+Collider (force-steered)
	bool gravity = false;              // physics boids fall (for a water demo)
	bool buoyancy = false;             // physics boids also get Buoyancy

	void on_attach() override;
	void update(float dt) override;
	~FlockComponent() override;

	void respawn(); // destroy + recreate all boids (UI / count change)
	int alive() const { return static_cast<int>(m_boids.size()); }

private:
	void spawn_boids();
	void clear_boids();
	float rnd(float a, float b);
	glm::vec3 rnd_dir();

	// Cached sample points (local space) of the current shape target's mesh, plus the
	// name they were built for so we rebuild when the target changes.
	std::vector<glm::vec3> m_form_points;
	std::string m_form_cache_for;

	std::vector<BoidComponent *> m_boids;
	std::mt19937 m_rng{0xB0D5EEDu};
	bool m_spawned = false;
};

} // namespace cf
