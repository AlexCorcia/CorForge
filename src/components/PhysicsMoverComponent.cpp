#include "components/PhysicsMoverComponent.h"

#include "components/ColliderComponent.h"
#include "components/RigidbodyComponent.h"
#include "core/Object.h"

#include <algorithm>
#include <cmath>

namespace cf
{

void PhysicsMoverComponent::on_attach()
{
	// It needs a collision shape, and a kinematic rigidbody so the physics solver
	// treats it as an immovable mover that pushes whatever it touches.
	Object *o = owner();
	if (!o->get_component<ColliderComponent>())
		o->add_component<ColliderComponent>(); // auto-fits to the mesh
	RigidbodyComponent *rb = o->get_component<RigidbodyComponent>();
	if (!rb)
		rb = o->add_component<RigidbodyComponent>();
	rb->kinematic = true;
	rb->use_gravity = false;
	recapture();
}

void PhysicsMoverComponent::recapture()
{
	m_origin = owner()->transform.position;
	m_have_origin = true;
	m_t = 0.0f;
	m_dir = 1;
}

void PhysicsMoverComponent::update(float dt)
{
	// Stay kinematic for as long as the component lives (physics derives its push
	// velocity from the position delta we set below).
	if (auto *rb = owner()->get_component<RigidbodyComponent>())
		rb->kinematic = true;

	if (!enabled || !m_have_origin || distance <= 0.0f)
		return;

	const float len = glm::length(axis);
	if (len < 1e-6f)
		return;
	const glm::vec3 n = axis / len;

	if (circular)
	{
		// Orbit around the start position, in the plane perpendicular to `axis`.
		// Build an orthonormal basis (u, v) of that plane; the circle's centre is
		// offset so that angle 0 lands on the start point (no jump on the 1st frame).
		const glm::vec3 ref = (std::abs(n.y) < 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
		const glm::vec3 u = glm::normalize(glm::cross(ref, n));
		const glm::vec3 v = glm::cross(n, u);
		const float radius = distance;
		const float circumference = 2.0f * 3.14159265f * radius;
		m_t += speed * dt; // arc length travelled
		if (circumference > 1e-4f)
			m_t = std::fmod(m_t, circumference);
		const float a = m_t / radius; // angle
		const glm::vec3 centre = m_origin - u * radius;
		owner()->transform.position = centre + radius * (std::cos(a) * u + std::sin(a) * v);
		return;
	}

	// Linear: shuttle along the line, bouncing at the endpoints.
	m_t += speed * dt * static_cast<float>(m_dir);
	if (m_t >= distance)
	{
		m_t = distance;
		m_dir = -1;
	}
	else if (m_t <= 0.0f)
	{
		m_t = 0.0f;
		m_dir = 1;
	}

	float s = m_t / distance;
	if (smooth)
		s = s * s * (3.0f - 2.0f * s); // ease in/out
	owner()->transform.position = m_origin + n * (s * distance);
}

} // namespace cf
