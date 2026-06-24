#include "gfx/Shader.h"

#include <glm/gtc/type_ptr.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <cstdio>

namespace cf
{

static std::string read_file(const std::string &path)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
		throw std::runtime_error("Failed to open shader file: " + path);
	std::stringstream ss;
	ss << file.rdbuf();
	return ss.str();
}

GLuint Shader::compile(GLenum type, const std::string &source, const std::string &path)
{
	GLuint shader = glCreateShader(type);
	const char *src = source.c_str();
	glShaderSource(shader, 1, &src, nullptr);
	glCompileShader(shader);

	GLint ok = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok)
	{
		GLint len = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
		std::vector<char> log(static_cast<size_t>(len) + 1);
		glGetShaderInfoLog(shader, len, nullptr, log.data());
		glDeleteShader(shader);
		throw std::runtime_error("Shader compile error (" + path + "):\n" + log.data());
	}
	return shader;
}

Shader::Shader(const std::string &vertex_path, const std::string &fragment_path)
{
	GLuint vs = compile(GL_VERTEX_SHADER, read_file(vertex_path), vertex_path);
	GLuint fs = compile(GL_FRAGMENT_SHADER, read_file(fragment_path), fragment_path);

	m_id = glCreateProgram();
	glAttachShader(m_id, vs);
	glAttachShader(m_id, fs);
	glLinkProgram(m_id);

	GLint ok = GL_FALSE;
	glGetProgramiv(m_id, GL_LINK_STATUS, &ok);
	if (!ok)
	{
		GLint len = 0;
		glGetProgramiv(m_id, GL_INFO_LOG_LENGTH, &len);
		std::vector<char> log(static_cast<size_t>(len) + 1);
		glGetProgramInfoLog(m_id, len, nullptr, log.data());
		glDeleteProgram(m_id);
		m_id = 0;
		throw std::runtime_error(std::string("Program link error:\n") + log.data());
	}

	glDeleteShader(vs);
	glDeleteShader(fs);
}

Shader::~Shader()
{
	if (m_id)
		glDeleteProgram(m_id);
}

Shader::Shader(Shader &&other) noexcept : m_id(other.m_id)
{
	other.m_id = 0;
}

Shader &Shader::operator=(Shader &&other) noexcept
{
	if (this != &other)
	{
		if (m_id)
			glDeleteProgram(m_id);
		m_id = other.m_id;
		other.m_id = 0;
	}
	return *this;
}

void Shader::use() const
{
	glUseProgram(m_id);
}

GLint Shader::location(const std::string &name) const
{
	if (auto it = m_locations.find(name); it != m_locations.end())
		return it->second; // cached (one glGetUniformLocation + one warning per name)
	const GLint loc = glGetUniformLocation(m_id, name.c_str());
	if (loc < 0)
		std::fprintf(stderr, "[Shader] uniform '%s' not found (or optimized out)\n", name.c_str());
	m_locations.emplace(name, loc);
	return loc;
}

void Shader::set_bool(const std::string &n, bool v) const
{
	glUniform1i(location(n), v ? 1 : 0);
}
void Shader::set_int(const std::string &n, int v) const
{
	glUniform1i(location(n), v);
}
void Shader::set_float(const std::string &n, float v) const
{
	glUniform1f(location(n), v);
}
void Shader::set_vec2(const std::string &n, const glm::vec2 &v) const
{
	glUniform2fv(location(n), 1, glm::value_ptr(v));
}
void Shader::set_vec3(const std::string &n, const glm::vec3 &v) const
{
	glUniform3fv(location(n), 1, glm::value_ptr(v));
}
void Shader::set_vec4(const std::string &n, const glm::vec4 &v) const
{
	glUniform4fv(location(n), 1, glm::value_ptr(v));
}
void Shader::set_mat3(const std::string &n, const glm::mat3 &m) const
{
	glUniformMatrix3fv(location(n), 1, GL_FALSE, glm::value_ptr(m));
}
void Shader::set_mat4(const std::string &n, const glm::mat4 &m) const
{
	glUniformMatrix4fv(location(n), 1, GL_FALSE, glm::value_ptr(m));
}

} // namespace cf
