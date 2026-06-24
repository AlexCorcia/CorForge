#pragma once

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <string>

namespace cf
{

// Singleton owning the GLFW window + OpenGL 4.6 core context.
// Call init() exactly once after the program starts.
class WindowManager
{
public:
	static WindowManager &instance();

	WindowManager(const WindowManager &) = delete;
	WindowManager &operator=(const WindowManager &) = delete;

	void init(int width, int height, const std::string &title);

	bool should_close() const;
	void poll_events() const;
	void swap_buffers() const;
	bool key_pressed(int key) const;

	// Mouse / cursor
	bool mouse_button_pressed(int button) const;
	void cursor_pos(double &x, double &y) const;
	void set_cursor_captured(bool captured) const; // true = hidden + locked (FPS look)

	int width() const { return m_width; }
	int height() const { return m_height; }
	float aspect() const { return m_height ? float(m_width) / float(m_height) : 1.0f; }

	GLFWwindow *handle() const { return m_window; }

private:
	WindowManager() = default;
	~WindowManager();

	static void framebuffer_size_callback(GLFWwindow *win, int w, int h);

	GLFWwindow *m_window = nullptr;
	int m_width = 0;
	int m_height = 0;
	bool m_initialized = false;
};

} // namespace cf
