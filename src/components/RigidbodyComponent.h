#pragma once

#include "core/Component.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace cf
{

// Gives an Object dynamics: it's integrated under gravity + collision impulses
// by the PhysicsManager. Needs a sibling ColliderComponent to actually collide.
class RigidbodyComponent : public Component
{
public:
	glm::vec3 velocity{0.0f};
	glm::vec3 angular_velocity{0.0f}; // radians/sec, world space
	float mass = 1.0f;
	bool use_gravity = true;
	bool is_static = false;   // immovable (infinite mass)
	float restitution = 0.4f; // bounciness [0,1]
	float friction = 0.5f;
	bool freeze_rotation = false;

	// Set by the editor while the object is dragged with the gizmo: it becomes
	// kinematic (moves where dragged, pushes others, keeps velocity on release).
	bool grabbed = false;
	// Driven by an external system (e.g. PhysicsMoverComponent): same kinematic
	// behaviour as `grabbed` (immovable, but its motion pushes dynamic bodies),
	// but persistent rather than only-while-dragged.
	bool kinematic = false;
	glm::vec3 prev_position{0.0f};

	// Orientation is integrated as a quaternion, then written back to the
	// Transform's Euler angles each step.
	glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};

	// Captured on attach, used by reset().
	glm::vec3 initial_position{0.0f};
	glm::vec3 initial_euler{0.0f};
	glm::quat initial_orientation{1.0f, 0.0f, 0.0f, 0.0f};

	void on_attach() override;
	~RigidbodyComponent() override;
};

} // namespace cf
