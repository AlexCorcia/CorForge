#pragma once

#include "core/Component.h"

namespace cf
{

// Makes the object FLOAT in the scene's water: applies an Archimedes-style upward
// push proportional to how submerged it is, plus water drag, and bobs with the
// waves. Needs a Rigidbody + Collider (added on attach). Set `strength` above
// gravity (9.81) so it floats; the higher, the more it rides on top.
class BuoyancyComponent : public Component
{
public:
	float strength = 22.0f; // buoyant acceleration when fully submerged
	float drag = 2.5f;      // water resistance (linear + angular) while submerged

	void on_attach() override;
	void update(float dt) override;
};

} // namespace cf
