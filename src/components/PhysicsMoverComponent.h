#pragma once

#include "core/Component.h"

#include <glm/glm.hpp>

namespace cf
{

// Like MoverComponent, but it drives the object THROUGH the physics system as a
// kinematic body: it shuttles back and forth along a straight line and PUSHES any
// dynamic rigidbodies it runs into (a piston / sweeper / moving platform). On
// attach it ensures the object has a Collider and a kinematic Rigidbody, so the
// physics solver derives its velocity from the motion and transfers it to whatever
// it hits. The starting position is the path's anchor.
class PhysicsMoverComponent : public Component
{
public:
	bool circular = false; // false = line shuttle, true = orbit
	// Linear: `axis` = travel direction, `distance` = path length.
	// Circular: `axis` = the axis it orbits around, `distance` = orbit radius.
	glm::vec3 axis = {1.0f, 0.0f, 0.0f};
	float distance = 4.0f;
	float speed = 2.0f;  // units per second (linear or along the arc)
	bool smooth = false; // (linear only) ease in/out at the endpoints
	bool enabled = true;

	void on_attach() override;
	void update(float dt) override;

	void recapture(); // re-anchor the path to the current position

	glm::vec3 origin() const { return m_origin; }
	void set_origin(const glm::vec3 &o)
	{
		m_origin = o;
		m_have_origin = true;
	}

private:
	glm::vec3 m_origin{0.0f};
	bool m_have_origin = false;
	float m_t = 0.0f; // distance travelled from origin (0..distance)
	int m_dir = 1;    // +1 outbound, -1 returning
};

} // namespace cf
