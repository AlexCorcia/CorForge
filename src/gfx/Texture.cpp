#include "gfx/Texture.h"

#include <stb_image.h>
#include <glm/glm.hpp>

#include <cmath>
#include <stdexcept>
#include <vector>

namespace cf
{

std::shared_ptr<Texture> Texture::from_pixels(const unsigned char *rgba, int width, int height,
                                              bool srgb)
{
	std::shared_ptr<Texture> tex(new Texture());
	tex->m_width = width;
	tex->m_height = height;

	glGenTextures(1, &tex->m_id);
	glBindTexture(GL_TEXTURE_2D, tex->m_id);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	const GLint internal = srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
	glTexImage2D(GL_TEXTURE_2D, 0, internal, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	glGenerateMipmap(GL_TEXTURE_2D);

	glBindTexture(GL_TEXTURE_2D, 0);
	return tex;
}

std::shared_ptr<Texture> Texture::load_from_file(const std::string &path, bool srgb)
{
	stbi_set_flip_vertically_on_load(1); // GL expects origin at bottom-left
	int w = 0, h = 0, channels = 0;
	unsigned char *pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
	if (!pixels)
		throw std::runtime_error("Failed to load image: " + path + " (" + stbi_failure_reason() +
		                         ")");

	std::shared_ptr<Texture> tex = from_pixels(pixels, w, h, srgb);
	stbi_image_free(pixels);
	return tex;
}

std::shared_ptr<Texture> Texture::create_checkerboard(int size, int checks)
{
	if (size < 2)
		size = 2;
	if (checks < 1)
		checks = 1;

	std::vector<unsigned char> px(static_cast<size_t>(size) * size * 4);
	const int cell = size / checks;
	for (int y = 0; y < size; ++y)
	{
		for (int x = 0; x < size; ++x)
		{
			const bool a = ((x / cell) + (y / cell)) % 2 == 0;
			const unsigned char v = a ? 230 : 140;
			const size_t i = (static_cast<size_t>(y) * size + x) * 4;
			px[i + 0] = v;
			px[i + 1] = v;
			px[i + 2] = v;
			px[i + 3] = 255;
		}
	}
	return from_pixels(px.data(), size, size, /*srgb=*/true);
}

std::shared_ptr<Texture> Texture::create_normal_bumps(int size, int cells)
{
	if (size < 2)
		size = 2;
	if (cells < 1)
		cells = 1;

	std::vector<unsigned char> px(static_cast<size_t>(size) * size * 4);
	const float cell = static_cast<float>(size) / static_cast<float>(cells);
	const float radius = cell * 0.42f;
	for (int y = 0; y < size; ++y)
	{
		for (int x = 0; x < size; ++x)
		{
			// Distance to this cell's centre -> a round dome bump.
			const float cx = (std::floor(x / cell) + 0.5f) * cell;
			const float cy = (std::floor(y / cell) + 0.5f) * cell;
			const float dx = (x - cx) / radius;
			const float dy = (y - cy) / radius;
			const float r2 = dx * dx + dy * dy;
			glm::vec3 n(0.0f, 0.0f, 1.0f);
			if (r2 < 1.0f)
			{
				const float h = std::sqrt(1.0f - r2);
				n = glm::normalize(glm::vec3(dx, dy, h + 0.4f));
			}
			const glm::vec3 enc = n * 0.5f + 0.5f; // [-1,1] -> [0,1]
			const size_t i = (static_cast<size_t>(y) * size + x) * 4;
			px[i + 0] = static_cast<unsigned char>(enc.x * 255.0f);
			px[i + 1] = static_cast<unsigned char>(enc.y * 255.0f);
			px[i + 2] = static_cast<unsigned char>(enc.z * 255.0f);
			px[i + 3] = 255;
		}
	}
	return from_pixels(px.data(), size, size, /*srgb=*/false);
}

const std::shared_ptr<Texture> &Texture::white()
{
	static std::shared_ptr<Texture> w = []
	{
		const unsigned char px[4] = {255, 255, 255, 255};
		return from_pixels(px, 1, 1, /*srgb=*/false);
	}();
	return w;
}

Texture::~Texture()
{
	if (m_id)
		glDeleteTextures(1, &m_id);
}

void Texture::bind(unsigned int unit) const
{
	glActiveTexture(GL_TEXTURE0 + unit);
	glBindTexture(GL_TEXTURE_2D, m_id);
}

} // namespace cf
