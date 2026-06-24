#pragma once

#include "core/Component.h"
#include "core/Transform.h"

#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace cf
{

// A scene entity: a name, a Transform, and a bag of Components.
// Components are owned by the Object (unique_ptr) and live as long as it does.
class Object
{
public:
	explicit Object(std::string name = "Object") : m_name(std::move(name)) {}

	Transform transform{}; // LOCAL transform (relative to the parent, if any)

	const std::string &name() const { return m_name; }
	void set_name(std::string n) { m_name = std::move(n); }

	// --- Hierarchy --------------------------------------------------------
	// Parent/child links (non-owning; ObjectsManager owns every Object). A child's
	// world transform is parent.worldMatrix() * child.localMatrix().
	Object *parent() const { return m_parent; }
	const std::vector<Object *> &children() const { return m_children; }

	// Re-parent. With keepWorld the local transform is adjusted so the object stays
	// put in the world; otherwise its current local transform becomes parent-relative.
	// No-ops on cycles (can't parent to self or a descendant).
	void set_parent(Object *new_parent, bool keep_world = true);
	bool is_descendant_of(const Object *other) const;

	// World-space transform, walking up the parent chain.
	glm::mat4 world_matrix() const
	{
		return m_parent ? m_parent->world_matrix() * transform.matrix() : transform.matrix();
	}
	glm::vec3 world_position() const { return glm::vec3(world_matrix()[3]); }
	// Compose the LOCAL look direction with the parent's world rotation. We use
	// Transform::forward() (the yaw/pitch convention cameras + lights expect) rather
	// than the matrix Z column, which uses a different sign convention here.
	glm::vec3 world_forward() const
	{
		const glm::vec3 f = transform.forward();
		return m_parent ? glm::normalize(glm::mat3(m_parent->world_matrix()) * f) : f;
	}

	// Construct a component of type T in place, attach it, and return a
	// non-owning pointer. Usage: obj->addComponent<CameraComponent>();
	template<typename T, typename... Args> T *add_component(Args &&...args)
	{
		static_assert(std::is_base_of_v<Component, T>, "T must derive from cf::Component");
		auto comp = std::make_unique<T>(std::forward<Args>(args)...);
		T *ptr = comp.get();
		ptr->m_owner = this; // friend access
		m_components.push_back(std::move(comp));
		ptr->on_attach();
		return ptr;
	}

	// First component of type T (or a derived type), or nullptr.
	template<typename T> T *get_component() const
	{
		for (const auto &c : m_components)
			if (auto *p = dynamic_cast<T *>(c.get()))
				return p;
		return nullptr;
	}

	template<typename T> bool has_component() const { return getComponent<T>() != nullptr; }

	// Remove the first component of type T (its destructor runs immediately).
	template<typename T> void remove_component()
	{
		for (auto it = m_components.begin(); it != m_components.end(); ++it)
		{
			if (dynamic_cast<T *>(it->get()))
			{
				m_components.erase(it);
				return;
			}
		}
	}

	std::size_t component_count() const { return m_components.size(); }

	void update(float dt)
	{
		for (auto &c : m_components)
			c->update(dt);
	}

	// Detach from the current parent (clears the link both ways). Used by
	// ObjectsManager when an object is destroyed.
	void detach_from_parent() { set_parent(nullptr); }
	void remove_child_link(Object *child); // erase child from m_children (no transform change)

private:
	std::string m_name;
	std::vector<std::unique_ptr<Component>> m_components;
	Object *m_parent = nullptr;
	std::vector<Object *> m_children;
};

} // namespace cf
