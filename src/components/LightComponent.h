#pragma once

#include "core/Component.h"

#include <glm/glm.hpp>

namespace cf
{

// A light source. Its position/direction come from the owning Object's
// Transform (position for point/spot; forward() = travel direction for
// directional/spot). Registers with the RendererManager on attach.
class LightComponent : public Component
{
public:
	enum class Type
	{
		Directional,
		Point,
		Spot
	};

	Type type = Type::Directional;
	glm::vec3 color = {1.0f, 1.0f, 1.0f};
	float intensity = 1.0f;
	float range = 12.0f;       // point / spot falloff distance
	float inner_angle = 18.0f; // spot inner cone half-angle (degrees)
	float outer_angle = 26.0f; // spot outer cone half-angle (degrees)

	void on_attach() override;
	~LightComponent() override;
};

} // namespace cf
