#include "core/AssetManager.h"

#include "gfx/Mesh.h"
#include "gfx/ModelLoader.h"
#include "gfx/Texture.h"

#include <nlohmann/json.hpp>
#include <stb_image_write.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace cf
{

namespace
{
const char *k_asset_dir = CORFORGE_ASSET_DIR;

bool is_image_ext(const std::string &ext)
{
	std::string e = ext;
	std::transform(e.begin(), e.end(), e.begin(),
	               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".bmp" || e == ".tga";
}
} // namespace

AssetManager &AssetManager::instance()
{
	static AssetManager s;
	return s;
}

void AssetManager::init()
{
	scan_meshes();
	seed_sample_textures();
	scan_textures();
	seed_sample_models();
	scan_models();
}

void AssetManager::scan_meshes()
{
	m_mesh_names = {"Cube", "Sphere", "Plane", "Pyramid"}; // built-ins first

	const fs::path dir = fs::path(k_asset_dir) / "meshes";
	std::error_code ec;
	fs::create_directories(dir, ec);
	// Recursive: plain .obj meshes can be organised into subfolders.
	for (fs::recursive_directory_iterator it(dir, ec), end; it != end; it.increment(ec))
	{
		if (ec || !it->is_regular_file())
			continue;
		const fs::path &p = it->path();
		std::string ext = p.extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(),
		               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (ext != ".obj")
			continue;

		const std::string name = p.stem().string();
		if (m_mesh_files.emplace(name, p.string()).second)
			m_mesh_names.push_back(name);
	}
}

void AssetManager::scan_textures()
{
	m_texture_names = {"Checker", "White", "Bumps"}; // built-ins first ("Bumps" = normal map)

	const fs::path dir = fs::path(k_asset_dir) / "textures";
	std::error_code ec;
	fs::create_directories(dir, ec);
	// Recursive: textures can be organised into subfolders. Still keyed by the bare
	// file stem so existing scenes / model material references resolve unchanged.
	for (fs::recursive_directory_iterator it(dir, ec), end; it != end; it.increment(ec))
	{
		if (ec || !it->is_regular_file())
			continue;
		const fs::path &p = it->path();
		if (!is_image_ext(p.extension().string()))
			continue;

		const std::string name = p.stem().string();
		if (m_texture_files.emplace(name, p.string()).second) // first wins on stem clash
			m_texture_names.push_back(name);
	}
}

void AssetManager::seed_sample_textures()
{
	const fs::path dir = fs::path(k_asset_dir) / "textures";
	std::error_code ec;
	fs::create_directories(dir, ec);

	// If the folder already has an image, leave it alone.
	for (const auto &entry : fs::directory_iterator(dir, ec))
		if (entry.is_regular_file() && is_image_ext(entry.path().extension().string()))
			return;

	// Otherwise write a UV-grid PNG so there's a real file to select.
	const int size = 256;
	const int cell = 32;
	std::vector<unsigned char> px(static_cast<size_t>(size) * size * 4);
	for (int y = 0; y < size; ++y)
	{
		for (int x = 0; x < size; ++x)
		{
			const bool line = (x % cell == 0) || (y % cell == 0);
			unsigned char r = 40, g = 44, b = 52;
			if (line)
			{
				r = 200;
				g = 200;
				b = 210;
			}
			// tint the lower/left edges so orientation is visible
			if (x < 4)
			{
				r = 220;
				g = 60;
				b = 60;
			}
			if (y < 4)
			{
				r = 60;
				g = 200;
				b = 90;
			}
			const size_t i = (static_cast<size_t>(y) * size + x) * 4;
			px[i + 0] = r;
			px[i + 1] = g;
			px[i + 2] = b;
			px[i + 3] = 255;
		}
	}
	const std::string out = (dir / "uvgrid.png").string();
	if (stbi_write_png(out.c_str(), size, size, 4, px.data(), size * 4))
		std::printf("[AssetManager] seeded sample texture: %s\n", out.c_str());
}

void AssetManager::scan_models()
{
	const fs::path dir = fs::path(k_asset_dir) / "models";
	std::error_code ec;
	fs::create_directories(dir, ec);
	// Recursive: models can live in their own subfolder (with a Textures/ folder).
	for (fs::recursive_directory_iterator it(dir, ec), end; it != end; it.increment(ec))
	{
		if (ec || !it->is_regular_file())
			continue;
		std::string ext = it->path().extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(),
		               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (ext != ".gltf" && ext != ".glb" && ext != ".obj" && ext != ".fbx" && ext != ".dae" &&
		    ext != ".3ds" && ext != ".stl" && ext != ".ply" && ext != ".blend")
			continue;
		const std::string name = it->path().stem().string();
		if (m_model_files.emplace(name, it->path().string()).second) // dedup on stem clash
			m_model_names.push_back(name);
	}
}

std::string AssetManager::model_path(const std::string &name) const
{
	const auto it = m_model_files.find(name);
	return it != m_model_files.end() ? it->second : std::string();
}

void AssetManager::seed_sample_models()
{
	const fs::path dir = fs::path(k_asset_dir) / "models";
	std::error_code ec;
	fs::create_directories(dir, ec);
	for (const auto &entry : fs::directory_iterator(dir, ec))
	{
		std::string ext = entry.path().extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(),
		               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (ext == ".gltf" || ext == ".glb" || ext == ".obj" || ext == ".fbx" || ext == ".dae" ||
		    ext == ".3ds" || ext == ".stl" || ext == ".ply" || ext == ".blend")
			return; // a model already exists -> don't seed the sample cube
	}

	// Build a unit cube: 24 verts (pos, normal, uv) + 36 indices.
	struct Face
	{
		float n[3], u[3], v[3];
	};
	const Face faces[6] = {
	    {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
	    {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
	    {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}}, {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
	};
	const float h = 0.5f;
	const float sg[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
	const float tc[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
	std::vector<float> pos, nrm, uv;
	std::vector<std::uint16_t> idx;
	for (const Face &f : faces)
	{
		const std::uint16_t base = static_cast<std::uint16_t>(pos.size() / 3);
		for (int c = 0; c < 4; ++c)
		{
			const float su = sg[c][0] * h, sv = sg[c][1] * h;
			pos.push_back(f.n[0] * h + f.u[0] * su + f.v[0] * sv);
			pos.push_back(f.n[1] * h + f.u[1] * su + f.v[1] * sv);
			pos.push_back(f.n[2] * h + f.u[2] * su + f.v[2] * sv);
			nrm.push_back(f.n[0]);
			nrm.push_back(f.n[1]);
			nrm.push_back(f.n[2]);
			uv.push_back(tc[c][0]);
			uv.push_back(tc[c][1]);
		}
		idx.insert(idx.end(), {base, std::uint16_t(base + 1), std::uint16_t(base + 2), base,
		                       std::uint16_t(base + 2), std::uint16_t(base + 3)});
	}

	std::vector<unsigned char> buf;
	auto append_f = [&](const std::vector<float> &v)
	{
		const size_t off = buf.size();
		buf.resize(off + v.size() * 4);
		std::memcpy(buf.data() + off, v.data(), v.size() * 4);
	};
	append_f(pos);
	append_f(nrm);
	append_f(uv);                      // 288 + 288 + 192
	const size_t idx_off = buf.size(); // 768
	buf.resize(idx_off + idx.size() * 2);
	std::memcpy(buf.data() + idx_off, idx.data(), idx.size() * 2); // 72 -> 840

	auto b64 = [](const unsigned char *d, size_t n)
	{
		static const char *t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string o;
		o.reserve((n + 2) / 3 * 4);
		for (size_t i = 0; i < n; i += 3)
		{
			unsigned x = static_cast<unsigned>(d[i]) << 16;
			if (i + 1 < n)
				x |= static_cast<unsigned>(d[i + 1]) << 8;
			if (i + 2 < n)
				x |= d[i + 2];
			o.push_back(t[(x >> 18) & 63]);
			o.push_back(t[(x >> 12) & 63]);
			o.push_back(i + 1 < n ? t[(x >> 6) & 63] : '=');
			o.push_back(i + 2 < n ? t[x & 63] : '=');
		}
		return o;
	};

	using nlohmann::json;
	json g;
	g["asset"] = {{"version", "2.0"}, {"generator", "CorForge"}};
	g["scene"] = 0;
	g["scenes"] = json::array({json{{"nodes", json::array({0})}}});
	g["nodes"] = json::array({json{{"name", "Box"}, {"mesh", 0}}});
	json prim = {{"attributes", {{"POSITION", 0}, {"NORMAL", 1}, {"TEXCOORD_0", 2}}},
	             {"indices", 3},
	             {"material", 0}};
	g["meshes"] = json::array({json{{"primitives", json::array({prim})}}});
	g["materials"] = json::array({json{{"name", "Box"},
	                                   {"pbrMetallicRoughness",
	                                    {{"baseColorFactor", json::array({0.85, 0.45, 0.2, 1.0})},
	                                     {"metallicFactor", 0.0},
	                                     {"roughnessFactor", 0.85}}}}});
	g["buffers"] = json::array(
	    {json{{"uri", "data:application/octet-stream;base64," + b64(buf.data(), buf.size())},
	          {"byteLength", static_cast<int>(buf.size())}}});
	g["bufferViews"] = json::array({
	    json{{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 288}, {"target", 34962}},
	    json{{"buffer", 0}, {"byteOffset", 288}, {"byteLength", 288}, {"target", 34962}},
	    json{{"buffer", 0}, {"byteOffset", 576}, {"byteLength", 192}, {"target", 34962}},
	    json{{"buffer", 0}, {"byteOffset", 768}, {"byteLength", 72}, {"target", 34963}},
	});
	g["accessors"] = json::array({
	    json{{"bufferView", 0},
	         {"componentType", 5126},
	         {"count", 24},
	         {"type", "VEC3"},
	         {"min", json::array({-0.5, -0.5, -0.5})},
	         {"max", json::array({0.5, 0.5, 0.5})}},
	    json{{"bufferView", 1}, {"componentType", 5126}, {"count", 24}, {"type", "VEC3"}},
	    json{{"bufferView", 2}, {"componentType", 5126}, {"count", 24}, {"type", "VEC2"}},
	    json{{"bufferView", 3}, {"componentType", 5123}, {"count", 36}, {"type", "SCALAR"}},
	});

	std::ofstream f((dir / "box.gltf").string());
	if (f)
	{
		f << g.dump(2);
		std::printf("[AssetManager] seeded sample model: box.gltf\n");
	}
}

std::shared_ptr<Mesh> AssetManager::mesh(const std::string &name)
{
	if (auto it = m_mesh_cache.find(name); it != m_mesh_cache.end())
		return it->second;

	std::shared_ptr<Mesh> m;
	if (name == "Cube")
		m = Mesh::create_cube();
	else if (name == "Sphere")
		m = Mesh::create_sphere(0.6f, 48, 24);
	else if (name == "Plane")
		m = Mesh::create_plane(1.0f, 1);
	else if (name == "Pyramid")
		m = Mesh::create_pyramid(1.0f);
	else if (auto it = m_mesh_files.find(name); it != m_mesh_files.end())
	{
		try
		{
			m = load_obj(it->second);
		}
		catch (const std::exception &e)
		{
			std::fprintf(stderr, "%s\n", e.what());
		}
	}

	if (m)
		m_mesh_cache[name] = m;
	return m;
}

std::shared_ptr<Texture> AssetManager::texture(const std::string &name)
{
	if (auto it = m_texture_cache.find(name); it != m_texture_cache.end())
		return it->second;

	std::shared_ptr<Texture> t;
	if (name == "Checker")
		t = Texture::create_checkerboard(512, 16);
	else if (name == "White")
		t = Texture::white();
	else if (name == "Bumps")
		t = Texture::create_normal_bumps(512, 8); // mainly a normal map
	else if (auto it = m_texture_files.find(name); it != m_texture_files.end())
	{
		try
		{
			t = Texture::load_from_file(it->second, /*srgb=*/true);
		}
		catch (const std::exception &e)
		{
			std::fprintf(stderr, "%s\n", e.what());
		}
	}

	if (t)
		m_texture_cache[name] = t;
	return t;
}

std::shared_ptr<Texture> AssetManager::texture_linear(const std::string &name)
{
	const std::string key = name + "@linear";
	if (auto it = m_texture_cache.find(key); it != m_texture_cache.end())
		return it->second;

	std::shared_ptr<Texture> t;
	if (name == "Bumps")
		t = Texture::create_normal_bumps(512, 8);
	else if (name == "Checker")
		t = Texture::create_checkerboard(512, 16);
	else if (name == "White")
		t = Texture::white();
	else if (auto it = m_texture_files.find(name); it != m_texture_files.end())
	{
		try
		{
			t = Texture::load_from_file(it->second, /*srgb=*/false);
		}
		catch (const std::exception &e)
		{
			std::fprintf(stderr, "%s\n", e.what());
		}
	}

	if (t)
		m_texture_cache[key] = t;
	return t;
}

} // namespace cf
