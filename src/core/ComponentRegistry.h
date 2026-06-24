#pragma once

#include <functional>
#include <vector>

namespace cf
{

class Object;

// Describes one addable/removable component type for the editor UI. Lets the
// inspector enumerate "all possible components" without hard-coding a list
// (C++ has no reflection, so we register them explicitly).
struct ComponentType
{
	const char *name;
	std::function<bool(const Object &)> has;
	std::function<void(Object &)> add;
	std::function<void(Object &)> remove;
};

// All component types the editor knows how to add/remove.
const std::vector<ComponentType> &component_registry();

} // namespace cf
