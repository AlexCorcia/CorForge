#pragma once

#include "core/Component.h"

#include <glm/glm.hpp>

namespace cf
{

class FlockComponent;

// Marker put on every entity a FlockComponent spawns. It holds the boid's current
// velocity and a back-pointer to its flock. Two jobs: let the flock find/identify
// its boids, and let the scene serializer SKIP them (the flock regenerates them on
// load, so they must not be saved as standalone objects). Not user-addable.
class BoidComponent : public Component
{
public:
	glm::vec3 velocity{0.0f};
	FlockComponent *flock = nullptr;
};

} // namespace cf
