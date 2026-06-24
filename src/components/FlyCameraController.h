#pragma once

#include "core/Component.h"

namespace cf
{

// Attach next to a CameraComponent to fly the camera around:
//   - hold RIGHT MOUSE to look (mouse-look); release to free the cursor
//   - W/A/S/D move, Q/E down/up, hold LEFT SHIFT to sprint
// Moves the owning Object's Transform; the camera reads that Transform.
class FlyCameraController : public Component
{
public:
	float move_speed = 3.0f;  // units per second
	float sprint_mul = 3.0f;  // speed multiplier while holding shift
	float sensitivity = 0.1f; // degrees of rotation per pixel of mouse motion

	void update(float dt) override;

private:
	bool m_first_mouse = true;
	double m_last_x = 0.0;
	double m_last_y = 0.0;
};

} // namespace cf
