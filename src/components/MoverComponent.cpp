#include "components/MoverComponent.h"

#include "core/Object.h"

#include <algorithm>

namespace cf
{

void MoverComponent::on_attach()
{
	recapture();
}

void MoverComponent::recapture()
{
	m_origin = owner()->transform.position;
	m_have_origin = true;
	m_t = 0.0f;
	m_dir = 1;
}

void MoverComponent::update(float dt)
{
	if (!enabled || !m_have_origin || distance <= 0.0f)
		return;

	const float len = glm::length(axis);
	if (len < 1e-6f)
		return;
	const glm::vec3 dir = axis / len;

	// Advance along the line; bounce at the two endpoints.
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

	// Map the linear parameter to an offset. `smooth` applies an ease-in/out
	// (smoothstep) so the object slows near each end instead of snapping around.
	float s = m_t / distance;
	if (smooth)
		s = s * s * (3.0f - 2.0f * s);
	owner()->transform.position = m_origin + dir * (s * distance);
}

} // namespace cf
