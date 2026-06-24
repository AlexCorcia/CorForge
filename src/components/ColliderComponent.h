#pragma once

#include "core/Component.h"

#include <glm/glm.hpp>

namespace cf
{

class TerrainComponent;

// A collision shape. Registers with the PhysicsManager. On attach it auto-fits
// its size to the owner's mesh (if any). Local size is scaled by the Transform.
class ColliderComponent : public Component
{
public:
	enum class Shape
	{
		Box,
		Sphere,
		Plane,
		Heightfield
	};

	Shape shape = Shape::Box;
	float radius = 0.5f;                      // Sphere (local)
	glm::vec3 half_extents{0.5f};             // Box (local)
	glm::vec3 center{0.0f};                   // local offset from the Transform's pivot
	glm::vec3 plane_normal{0.0f, 1.0f, 0.0f}; // Plane
	// Heightfield: set by the TerrainComponent that owns this collider. The physics
	// step samples it for per-point ground height + slope normal. Never serialized
	// (the terrain re-creates and re-wires its collider on load).
	TerrainComponent *terrain = nullptr;

	void on_attach() override;
	~ColliderComponent() override;

	// Resize (and pick a shape) to fit the owner's current geometry -- a single
	// mesh or all of an imported model's submeshes. Called on attach and whenever
	// the mesh changes; also exposed as a button in the inspector.
	void fit_to_mesh();
};

} // namespace cf
