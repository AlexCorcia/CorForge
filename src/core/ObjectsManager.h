#pragma once

#include "core/Object.h"

#include <memory>
#include <string>
#include <vector>

namespace cf
{

// Singleton that owns every Object in the scene and ticks them each frame.
class ObjectsManager
{
public:
	static ObjectsManager &instance();

	ObjectsManager(const ObjectsManager &) = delete;
	ObjectsManager &operator=(const ObjectsManager &) = delete;

	// Creates an Object, takes ownership, and returns a non-owning pointer.
	Object *create_object(std::string name = "Object");

	// Destroys an object (and its components). Clears selection if it was selected.
	void remove_object(Object *obj);

	void update_all(float dt);
	void clear();

	const std::vector<std::unique_ptr<Object>> &objects() const { return m_objects; }

	// Currently selected object (e.g. via click-picking or the hierarchy UI).
	Object *selected() const { return m_selected; }
	void set_selected(Object *obj) { m_selected = obj; }

	// True while clear() is destroying everything -- removeObject becomes a no-op so
	// a component destructor (e.g. a flock removing its boids) can't touch m_objects
	// mid-teardown.
	bool is_clearing() const { return m_clearing; }

private:
	ObjectsManager() = default;
	std::vector<std::unique_ptr<Object>> m_objects;
	Object *m_selected = nullptr;

	// Re-entrancy guard: erasing an object runs its component destructors, which may
	// remove MORE objects (a flock destroys its boids). We queue those and drain the
	// queue in the outer call so we never erase from m_objects recursively.
	std::vector<Object *> m_pending_removal;
	bool m_removing = false;
	bool m_clearing = false;
};

} // namespace cf
