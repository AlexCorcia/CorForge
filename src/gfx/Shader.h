#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <string>
#include <unordered_map>

namespace cf
{

// Compiles + links a vertex/fragment GLSL program from files and exposes
// typed uniform setters. Owns the GL program object (RAII).
class Shader
{
public:
	Shader(const std::string &vertex_path, const std::string &fragment_path);
	~Shader();

	Shader(const Shader &) = delete;
	Shader &operator=(const Shader &) = delete;
	Shader(Shader &&other) noexcept;
	Shader &operator=(Shader &&other) noexcept;

	void use() const;
	GLuint id() const { return m_id; }

	void set_bool(const std::string &name, bool v) const;
	void set_int(const std::string &name, int v) const;
	void set_float(const std::string &name, float v) const;
	void set_vec2(const std::string &name, const glm::vec2 &v) const;
	void set_vec3(const std::string &name, const glm::vec3 &v) const;
	void set_vec4(const std::string &name, const glm::vec4 &v) const;
	void set_mat3(const std::string &name, const glm::mat3 &m) const;
	void set_mat4(const std::string &name, const glm::mat4 &m) const;

private:
	GLint location(const std::string &name) const;
	static GLuint compile(GLenum type, const std::string &source, const std::string &path);

	GLuint m_id = 0;
	mutable std::unordered_map<std::string, GLint> m_locations; // cache (warns once)
};

} // namespace cf
