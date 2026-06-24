#include "components/RigidbodyComponent.h"

#include "core/Object.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

namespace cf
{

void RigidbodyComponent::on_attach()
{
	const Transform &t = owner()->transform;
	initial_position = t.position;
	prev_position = t.position;
	initial_euler = t.euler_degrees;

	// Build the orientation quaternion in the same YXZ order Transform uses.
	const glm::vec3 e = glm::radians(t.euler_degrees); // x=pitch, y=yaw, z=roll
	orientation = glm::quat_cast(glm::eulerAngleYXZ(e.y, e.x, e.z));
	initial_orientation = orientation;
}

RigidbodyComponent::~RigidbodyComponent() = default;

} // namespace cf
