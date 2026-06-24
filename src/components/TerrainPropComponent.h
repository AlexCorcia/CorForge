#pragma once

#include "core/Component.h"

namespace cf
{

class TerrainComponent;

// Marker put on every prop (tree / rock) a TerrainComponent scatters over its
// surface. Like BoidComponent, its two jobs are to let the terrain find/clear its
// props and to let the scene serializer SKIP them (the terrain re-scatters them on
// load, so they must not be saved as standalone objects). Not user-addable.
class TerrainPropComponent : public Component
{
public:
	TerrainComponent *terrain = nullptr;
};

} // namespace cf
