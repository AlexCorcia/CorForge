#include "core/ObjectsManager.h"

#include "core/Object.h"

#include <algorithm>
#include <utility>

namespace cf
{

ObjectsManager &ObjectsManager::instance()
{
	static ObjectsManager s;
	return s;
}

Object *ObjectsManager::create_object(std::string name)
{
	m_objects.push_back(std::make_unique<Object>(std::move(name)));
	return m_objects.back().get();
}

void ObjectsManager::remove_object(Object *obj)
{
	if (!obj || m_clearing)
		return;
	m_pending_removal.push_back(obj);
	if (m_removing)
		return; // re-entrant call (from a destructor): the outer call drains the queue

	m_removing = true;
	while (!m_pending_removal.empty())
	{
		Object *o = m_pending_removal.back();
		m_pending_removal.pop_back();

		// Skip stale duplicates. An object can be queued twice -- e.g. removing a
		// parent queues its children (subtree walk), then the parent's destructor
		// re-queues those same children (a component like TerrainComponent clearing
		// its props). The first pass erases them; the duplicate must NOT dereference
		// the freed pointer. Comparing addresses here touches no freed memory.
		if (std::none_of(m_objects.begin(), m_objects.end(),
		                 [o](const std::unique_ptr<Object> &p) { return p.get() == o; }))
			continue;

		// Removing a parent removes its whole subtree; queue children first.
		for (Object *child : o->children())
			m_pending_removal.push_back(child);
		o->detach_from_parent();
		if (m_selected == o)
			m_selected = nullptr;

		// Erasing destroys o's components; a destructor may removeObject() more, which
		// is now safely queued (m_removing == true) rather than erased re-entrantly.
		m_objects.erase(std::remove_if(m_objects.begin(), m_objects.end(),
		                               [o](const std::unique_ptr<Object> &p)
		                               { return p.get() == o; }),
		                m_objects.end());
	}
	m_removing = false;
}

void ObjectsManager::update_all(float dt)
{
	// Index-based (not range-for): a component's update() may append objects -- a
	// FlockComponent spawns its boids on first tick -- which reallocates m_objects.
	// Re-fetching by index each step stays valid across that growth.
	for (size_t i = 0; i < m_objects.size(); ++i)
		m_objects[i]->update(dt);
}

void ObjectsManager::clear()
{
	m_selected = nullptr;
	m_clearing = true; // component destructors that call removeObject become no-ops
	m_objects.clear();
	m_pending_removal.clear();
	m_clearing = false;
}

} // namespace cf
