#include "core/Object.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>

namespace cf
{

bool Object::is_descendant_of(const Object *other) const
{
	for (const Object *p = m_parent; p; p = p->m_parent)
		if (p == other)
			return true;
	return false;
}

void Object::remove_child_link(Object *child)
{
	m_children.erase(std::remove(m_children.begin(), m_children.end(), child), m_children.end());
}

void Object::set_parent(Object *new_parent, bool keep_world)
{
	if (new_parent == m_parent)
		return;
	// Guard against cycles: can't parent to self or to one of our own descendants.
	if (new_parent == this || (new_parent && new_parent->is_descendant_of(this)))
		return;

	const glm::mat4 world = world_matrix(); // capture before relinking

	if (m_parent)
		m_parent->remove_child_link(this);
	m_parent = new_parent;
	if (m_parent)
		m_parent->m_children.push_back(this);

	if (!keep_world)
		return;

	// Recompute the local transform so the object stays put in world space:
	// local = inverse(parentWorld) * world, then decompose back into T/R/S.
	const glm::mat4 local = m_parent ? glm::inverse(m_parent->world_matrix()) * world : world;

	glm::vec3 scale, translation, skew;
	glm::vec4 perspective;
	glm::quat orientation;
	if (glm::decompose(local, scale, orientation, translation, skew, perspective))
	{
		float yaw, pitch, roll;
		glm::extractEulerAngleYXZ(glm::mat4_cast(orientation), yaw, pitch, roll);
		transform.position = translation;
		transform.scale = scale;
		transform.euler_degrees = glm::degrees(glm::vec3(pitch, yaw, roll));
	}
}

} // namespace cf
