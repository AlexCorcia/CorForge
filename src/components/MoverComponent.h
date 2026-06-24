#pragma once

#include "core/Component.h"

#include <glm/glm.hpp>

namespace cf
{

// Drives its Object back and forth along a straight line at constant speed: it
// travels `distance` units along `axis` from its starting position, then returns,
// ping-ponging forever (a simple patrol / shuttle motion). Pure linear by default;
// `smooth` eases in/out at the endpoints. The starting position is captured on
// attach and used as the path's anchor.
class MoverComponent : public Component
{
public:
	glm::vec3 axis = {1.0f, 0.0f, 0.0f}; // travel direction (normalised internally)
	float distance = 4.0f;               // length of the path (units)
	float speed = 2.0f;                  // units per second
	bool smooth = false;                 // ease in/out instead of constant speed
	bool enabled = true;

	void on_attach() override;
	void update(float dt) override;

	// Re-anchor the path to the current position (e.g. after moving the object).
	void recapture();

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
