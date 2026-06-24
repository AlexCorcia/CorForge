#pragma once

#include <glad/gl.h>

#include <memory>
#include <string>

namespace cf
{

// Owns a GL 2D texture (RAII). Use the static factories to create one.
class Texture
{
public:
	~Texture();
	Texture(const Texture &) = delete;
	Texture &operator=(const Texture &) = delete;

	// Load an image file (PNG/JPG/...). srgb = true for color/albedo maps so
	// sampling returns linear values. Throws std::runtime_error on failure.
	static std::shared_ptr<Texture> load_from_file(const std::string &path, bool srgb = true);

	// Build from tightly-packed RGBA8 pixels.
	static std::shared_ptr<Texture> from_pixels(const unsigned char *rgba, int width, int height,
	                                            bool srgb = true);

	// Procedural checkerboard (square, `size` texels, `checks` cells per side).
	static std::shared_ptr<Texture> create_checkerboard(int size = 512, int checks = 8);

	// Procedural tangent-space normal map: a grid of round bumps. Linear (not sRGB).
	static std::shared_ptr<Texture> create_normal_bumps(int size = 512, int cells = 8);

	// Shared 1x1 white texture (default albedo when a material has no map).
	static const std::shared_ptr<Texture> &white();

	void bind(unsigned int unit) const;

	GLuint id() const { return m_id; }
	int width() const { return m_width; }
	int height() const { return m_height; }

private:
	Texture() = default;

	GLuint m_id = 0;
	int m_width = 0;
	int m_height = 0;
};

} // namespace cf
