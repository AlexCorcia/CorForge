#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace cf
{

// Position / rotation (Euler degrees) / scale, with helpers to derive the
// model matrix and basis vectors. Every Object owns one.
struct Transform
{
	glm::vec3 position{0.0f};
	glm::vec3 euler_degrees{0.0f}; // x = pitch, y = yaw, z = roll
	glm::vec3 scale{1.0f};

	glm::mat4 matrix() const
	{
		glm::mat4 m(1.0f);
		m = glm::translate(m, position);
		m = glm::rotate(m, glm::radians(euler_degrees.y), glm::vec3(0, 1, 0));
		m = glm::rotate(m, glm::radians(euler_degrees.x), glm::vec3(1, 0, 0));
		m = glm::rotate(m, glm::radians(euler_degrees.z), glm::vec3(0, 0, 1));
		m = glm::scale(m, scale);
		return m;
	}

	// Forward (look) direction from yaw/pitch. Identity rotation looks down -Z.
	glm::vec3 forward() const
	{
		const float pitch = glm::radians(euler_degrees.x);
		const float yaw = glm::radians(euler_degrees.y);
		return glm::normalize(glm::vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch),
		                                -std::cos(pitch) * std::cos(yaw)));
	}

	glm::vec3 right() const { return glm::normalize(glm::cross(forward(), glm::vec3(0, 1, 0))); }
	glm::vec3 up() const { return glm::normalize(glm::cross(right(), forward())); }
};

} // namespace cf
