#pragma once

#include "core/Component.h"

#include <glm/glm.hpp>

#include <string>

namespace cf
{

// Adds a sky/environment to the scene: a skybox you can see plus image-based
// lighting (IBL) on every surface. The environment is either a procedural
// gradient (zenith/horizon/ground) or an equirectangular image from
// assets/textures (set `imageName`). The first SkyComponent is the active one.
class SkyComponent : public Component
{
public:
	glm::vec3 zenith = {0.10f, 0.28f, 0.62f};
	glm::vec3 horizon = {0.62f, 0.74f, 0.90f};
	glm::vec3 ground = {0.20f, 0.19f, 0.17f};
	float intensity = 1.0f; // IBL strength on surfaces

	std::string image_name = "None"; // equirect image (texture name) or "None" = procedural

	void on_attach() override;
	~SkyComponent() override;
};

} // namespace cf
