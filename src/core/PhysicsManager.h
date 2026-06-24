#pragma once

#include <glm/glm.hpp>

#include <vector>

namespace cf
{

class ColliderComponent;

// Singleton physics world: integrates rigidbodies under gravity and resolves
// collisions between colliders with a simple impulse solver.
class PhysicsManager
{
public:
	static PhysicsManager &instance();

	PhysicsManager(const PhysicsManager &) = delete;
	PhysicsManager &operator=(const PhysicsManager &) = delete;

	void register_collider(ColliderComponent *c);
	void unregister_collider(ColliderComponent *c);

	// For other systems (e.g. cloth) to collide against the scene's shapes.
	const std::vector<ColliderComponent *> &colliders() const { return m_colliders; }

	void step(float dt);
	void reset(); // restore rigidbodies to their initial positions

	bool enabled = true;
	glm::vec3 gravity{0.0f, -9.81f, 0.0f};
	int iterations = 10; // collision solver iterations per substep
	int substeps = 2;

private:
	PhysicsManager() = default;

	std::vector<ColliderComponent *> m_colliders;
};

} // namespace cf
