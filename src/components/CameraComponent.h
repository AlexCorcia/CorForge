#pragma once

#include "core/Component.h"

#include <glm/glm.hpp>

namespace cf
{

// Turns its Object into a camera. On attach it registers with the
// RendererManager; if no main camera exists yet, it becomes the main camera.
// The view matrix is derived from the owning Object's Transform.
class CameraComponent : public Component
{
public:
	float fov_degrees = 60.0f;
	float near_plane = 0.1f;
	float far_plane = 100.0f;

	void on_attach() override;
	~CameraComponent() override;

	glm::mat4 view() const;
	glm::mat4 projection(float aspect) const;
	glm::vec3 world_position() const;
};

} // namespace cf
