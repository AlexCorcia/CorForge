#pragma once

namespace cf
{

class Object;

// Base class for everything you attach to an Object. Subclass it and override
// the lifecycle hooks. The owning Object is injected when the component is added.
class Component
{
public:
	virtual ~Component() = default;

	// Called once, right after the component is attached to its Object.
	virtual void on_attach() {}
	// Called every frame by ObjectsManager. dt is seconds since last frame.
	virtual void update(float /*dt*/) {}

	Object *owner() const { return m_owner; }

private:
	friend class Object; // sets m_owner during Object::addComponent
	Object *m_owner = nullptr;
};

} // namespace cf
