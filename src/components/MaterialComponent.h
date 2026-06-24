#pragma once

#include "core/Component.h"
#include "gfx/Material.h"

namespace cf
{

// Holds the surface appearance (color, textures, shading params) for an
// Object. A RendererComponent on the same Object reads this when drawing.
class MaterialComponent : public Component
{
public:
	Material material;

	MaterialComponent(); // defaults material.shader to the engine default
};

} // namespace cf
