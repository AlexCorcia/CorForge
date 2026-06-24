#include "components/CameraComponent.h"

#include "core/Object.h"
#include "core/RendererManager.h"

#include <glm/gtc/matrix_transform.hpp>

namespace cf
{

void CameraComponent::on_attach()
{
	RendererManager::instance().register_camera(this);
}

CameraComponent::~CameraComponent()
{
	RendererManager::instance().unregister_camera(this);
}

glm::mat4 CameraComponent::view() const
{
	Object *o = owner();
	const glm::vec3 p = o->world_position();
	return glm::lookAt(p, p + o->world_forward(), glm::vec3(0, 1, 0));
}

glm::mat4 CameraComponent::projection(float aspect) const
{
	return glm::perspective(glm::radians(fov_degrees), aspect, near_plane, far_plane);
}

glm::vec3 CameraComponent::world_position() const
{
	return owner()->world_position();
}

} // namespace cf
