#include "gfx/ModelLoader.h"

#define TINYGLTF_NO_INCLUDE_JSON
#define TINYGLTF_NO_STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <nlohmann/json.hpp>
#include <tiny_gltf.h>

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace cf
{

namespace
{

namespace tg = tinygltf;

glm::mat4 node_local_matrix(const tg::Node &n)
{
	if (n.matrix.size() == 16)
	{
		glm::mat4 m(1.0f);
		float *p = &m[0][0];
		for (int i = 0; i < 16; ++i)
			p[i] = static_cast<float>(n.matrix[i]);
		return m;
	}
	glm::mat4 t(1.0f), r(1.0f), s(1.0f);
	if (n.translation.size() == 3)
		t = glm::translate(glm::mat4(1.0f), {(float)n.translation[0], (float)n.translation[1],
		                                     (float)n.translation[2]});
	if (n.rotation.size() == 4)
		r = glm::mat4_cast(glm::quat((float)n.rotation[3], (float)n.rotation[0],
		                             (float)n.rotation[1], (float)n.rotation[2]));
	if (n.scale.size() == 3)
		s = glm::scale(glm::mat4(1.0f), {(float)n.scale[0], (float)n.scale[1], (float)n.scale[2]});
	return t * r * s;
}

std::vector<glm::vec3> read_vec3(const tg::Model &m, int acc)
{
	const tg::Accessor &a = m.accessors[acc];
	const tg::BufferView &bv = m.bufferViews[a.bufferView];
	const unsigned char *base = m.buffers[bv.buffer].data.data() + bv.byteOffset + a.byteOffset;
	int stride = a.ByteStride(bv);
	if (stride <= 0)
		stride = 12;
	std::vector<glm::vec3> out;
	out.reserve(a.count);
	for (size_t i = 0; i < a.count; ++i)
	{
		const float *p = reinterpret_cast<const float *>(base + i * stride);
		out.push_back({p[0], p[1], p[2]});
	}
	return out;
}

std::vector<glm::vec2> read_vec2(const tg::Model &m, int acc)
{
	const tg::Accessor &a = m.accessors[acc];
	const tg::BufferView &bv = m.bufferViews[a.bufferView];
	const unsigned char *base = m.buffers[bv.buffer].data.data() + bv.byteOffset + a.byteOffset;
	int stride = a.ByteStride(bv);
	if (stride <= 0)
		stride = 8;
	std::vector<glm::vec2> out;
	out.reserve(a.count);
	for (size_t i = 0; i < a.count; ++i)
	{
		const float *p = reinterpret_cast<const float *>(base + i * stride);
		out.push_back({p[0], p[1]});
	}
	return out;
}

std::vector<std::uint32_t> read_indices(const tg::Model &m, int acc)
{
	const tg::Accessor &a = m.accessors[acc];
	const tg::BufferView &bv = m.bufferViews[a.bufferView];
	const unsigned char *base = m.buffers[bv.buffer].data.data() + bv.byteOffset + a.byteOffset;
	std::vector<std::uint32_t> out;
	out.reserve(a.count);
	for (size_t i = 0; i < a.count; ++i)
	{
		switch (a.componentType)
		{
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
			out.push_back(base[i]);
			break;
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
			out.push_back(reinterpret_cast<const std::uint16_t *>(base)[i]);
			break;
		default: // UNSIGNED_INT
			out.push_back(reinterpret_cast<const std::uint32_t *>(base)[i]);
			break;
		}
	}
	return out;
}

std::shared_ptr<Texture> make_texture(const tg::Model &m, int img_idx, bool srgb,
                                      std::unordered_map<int, std::shared_ptr<Texture>> &cache)
{
	const int key = img_idx * 2 + (srgb ? 1 : 0);
	if (auto it = cache.find(key); it != cache.end())
		return it->second;
	const tg::Image &im = m.images[img_idx];
	if (im.image.empty() || im.width <= 0 || im.height <= 0)
		return nullptr;

	const int comp = im.component;
	std::vector<unsigned char> rgba(static_cast<size_t>(im.width) * im.height * 4, 255);
	for (int i = 0; i < im.width * im.height; ++i)
	{
		unsigned char r, g, b, a = 255;
		if (comp == 1)
		{
			r = g = b = im.image[i];
		}
		else
		{
			r = im.image[i * comp + 0];
			g = im.image[i * comp + 1];
			b = im.image[i * comp + 2];
			if (comp == 4)
				a = im.image[i * comp + 3];
		}
		rgba[i * 4 + 0] = r;
		rgba[i * 4 + 1] = g;
		rgba[i * 4 + 2] = b;
		rgba[i * 4 + 3] = a;
	}
	auto tex = Texture::from_pixels(rgba.data(), im.width, im.height, srgb);
	cache[key] = tex;
	return tex;
}

struct Raw
{
	std::vector<Vertex> verts;
	std::vector<std::uint32_t> idx;
	glm::vec3 albedo{0.8f};
	std::shared_ptr<Texture> tex;
	float metallic{0.0f};
	float roughness{0.5f};
	std::shared_ptr<Texture> mr_map;
	float opacity{1.0f};
	bool transparent{false};
};

void process_node(const tg::Model &m, int node_idx, const glm::mat4 &parent, std::vector<Raw> &out,
                  std::unordered_map<int, std::shared_ptr<Texture>> &tex_cache)
{
	if (node_idx < 0 || node_idx >= static_cast<int>(m.nodes.size()))
		return;
	const tg::Node &node = m.nodes[node_idx];
	const glm::mat4 world = parent * node_local_matrix(node);

	if (node.mesh >= 0 && node.mesh < static_cast<int>(m.meshes.size()))
	{
		const glm::mat3 normal_mat = glm::transpose(glm::inverse(glm::mat3(world)));
		for (const tg::Primitive &prim : m.meshes[node.mesh].primitives)
		{
			if (prim.mode != TINYGLTF_MODE_TRIANGLES && prim.mode != -1)
				continue;
			auto it_pos = prim.attributes.find("POSITION");
			if (it_pos == prim.attributes.end())
				continue;

			const auto positions = read_vec3(m, it_pos->second);
			std::vector<glm::vec3> normals;
			if (auto it = prim.attributes.find("NORMAL"); it != prim.attributes.end())
				normals = read_vec3(m, it->second);
			std::vector<glm::vec2> uvs;
			if (auto it = prim.attributes.find("TEXCOORD_0"); it != prim.attributes.end())
				uvs = read_vec2(m, it->second);

			Raw r;
			r.verts.reserve(positions.size());
			for (size_t i = 0; i < positions.size(); ++i)
			{
				Vertex v;
				v.position = glm::vec3(world * glm::vec4(positions[i], 1.0f));
				v.normal = (i < normals.size()) ? glm::normalize(normal_mat * normals[i])
				                                : glm::vec3(0, 1, 0);
				v.uv = (i < uvs.size()) ? uvs[i] : glm::vec2(0.0f);
				r.verts.push_back(v);
			}
			if (prim.indices >= 0)
				r.idx = read_indices(m, prim.indices);
			else
				for (std::uint32_t i = 0; i < positions.size(); ++i)
					r.idx.push_back(i);

			if (prim.material >= 0 && prim.material < static_cast<int>(m.materials.size()))
			{
				const tg::Material &mat = m.materials[prim.material];
				const auto &pbr = mat.pbrMetallicRoughness;
				const auto &bc = pbr.baseColorFactor;
				if (bc.size() >= 3)
					r.albedo = {(float)bc[0], (float)bc[1], (float)bc[2]};
				r.metallic = static_cast<float>(pbr.metallicFactor);
				r.roughness = static_cast<float>(pbr.roughnessFactor);
				if (bc.size() >= 4)
					r.opacity = static_cast<float>(bc[3]);
				r.transparent = (mat.alphaMode == "BLEND") || r.opacity < 0.999f;

				const int ti = pbr.baseColorTexture.index;
				if (ti >= 0 && ti < static_cast<int>(m.textures.size()))
				{
					const int img = m.textures[ti].source;
					if (img >= 0)
						r.tex = make_texture(m, img, /*srgb=*/true, tex_cache);
				}
				const int mi = pbr.metallicRoughnessTexture.index;
				if (mi >= 0 && mi < static_cast<int>(m.textures.size()))
				{
					const int img = m.textures[mi].source;
					if (img >= 0)
						r.mr_map = make_texture(m, img, /*srgb=*/false, tex_cache);
				}
			}
			out.push_back(std::move(r));
		}
	}
	for (int child : node.children)
		process_node(m, child, world, out, tex_cache);
}

bool ends_with(const std::string &s, const char *suffix)
{
	const std::string suf = suffix;
	return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

} // namespace

std::vector<ModelPrimitive> load_gltf(const std::string &path)
{
	tg::TinyGLTF ctx;
	tg::Model m;
	std::string err, warn;
	const bool ok = ends_with(path, ".glb") ? ctx.LoadBinaryFromFile(&m, &err, &warn, path)
	                                        : ctx.LoadASCIIFromFile(&m, &err, &warn, path);
	if (!warn.empty())
		std::fprintf(stderr, "[glTF] %s\n", warn.c_str());
	if (!ok)
	{
		std::fprintf(stderr, "[glTF] failed: %s (%s)\n", path.c_str(), err.c_str());
		return {};
	}

	std::vector<Raw> raws;
	std::unordered_map<int, std::shared_ptr<Texture>> tex_cache;
	const int scene_idx = (m.defaultScene >= 0) ? m.defaultScene : 0;
	if (scene_idx < static_cast<int>(m.scenes.size()))
		for (int n : m.scenes[scene_idx].nodes)
			process_node(m, n, glm::mat4(1.0f), raws, tex_cache);
	else
		for (int n = 0; n < static_cast<int>(m.nodes.size()); ++n)
			process_node(m, n, glm::mat4(1.0f), raws, tex_cache);

	if (raws.empty())
		return {};

	// Center + uniform-scale the whole model to ~2 units (shared transform over
	// all primitives so they stay assembled), keeping each primitive's own
	// material/texture as a separate submesh.
	glm::vec3 mn(std::numeric_limits<float>::max());
	glm::vec3 mx(-std::numeric_limits<float>::max());
	for (const Raw &r : raws)
		for (const Vertex &v : r.verts)
		{
			mn = glm::min(mn, v.position);
			mx = glm::max(mx, v.position);
		}
	const glm::vec3 center = (mn + mx) * 0.5f;
	const glm::vec3 ext = mx - mn;
	const float maxd = std::max({ext.x, ext.y, ext.z});
	const float scale = (maxd > 1e-4f) ? 2.0f / maxd : 1.0f;

	std::vector<ModelPrimitive> out;
	out.reserve(raws.size());
	for (Raw &r : raws)
	{
		for (Vertex &v : r.verts)
			v.position = (v.position - center) * scale;
		ModelPrimitive prim;
		prim.mesh = std::make_shared<Mesh>(r.verts, r.idx);
		prim.albedo = r.albedo;
		prim.albedo_map = r.tex;
		prim.metallic = r.metallic;
		prim.roughness = r.roughness;
		prim.metallic_roughness_map = r.mr_map;
		prim.opacity = r.opacity;
		prim.transparent = r.transparent;
		out.push_back(std::move(prim));
	}
	return out;
}

// ===================== Wavefront OBJ =======================================

namespace
{

std::string lower(std::string s)
{
	for (char &c : s)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

struct MtlMat
{
	glm::vec3 kd{0.8f};
	glm::vec3 ks{0.0f};
	float ns = 32.0f;
	float d = 1.0f; // dissolve: 1 opaque, <1 transparent
	std::string map_kd;
};

void parse_mtl(const std::string &path, std::unordered_map<std::string, MtlMat> &out)
{
	std::ifstream f(path);
	if (!f)
		return;
	std::string line, cur;
	while (std::getline(f, line))
	{
		std::istringstream ss(line);
		std::string tag;
		ss >> tag;
		if (tag == "newmtl")
		{
			ss >> cur;
			out[cur];
		}
		else if (cur.empty())
			continue;
		else if (tag == "Kd")
		{
			glm::vec3 c;
			ss >> c.r >> c.g >> c.b;
			out[cur].kd = c;
		}
		else if (tag == "Ks")
		{
			glm::vec3 c;
			ss >> c.r >> c.g >> c.b;
			out[cur].ks = c;
		}
		else if (tag == "Ns")
		{
			ss >> out[cur].ns;
		}
		else if (tag == "d")
		{
			float v;
			ss >> v;
			out[cur].d = v;
		}
		else if (tag == "Tr")
		{
			float v;
			ss >> v;
			out[cur].d = 1.0f - v;
		}
		else if (tag == "map_Kd")
		{
			std::string tok, last; // last token = filename (skip any -options)
			while (ss >> tok)
				last = tok;
			out[cur].map_kd = last;
		}
	}
}

// One material's worth of geometry within an OBJ file.
struct Group
{
	std::string mat;
	std::vector<Vertex> verts;
	std::vector<std::uint32_t> idx;
};

// Parse "a", "a/b", "a//c", "a/b/c" (1-based, may be negative).
void parse_ref(const std::string &tok, int &v, int &vt, int &vn)
{
	v = vt = vn = 0;
	int field = 0;
	std::string cur;
	auto flush = [&]
	{
		if (!cur.empty())
		{
			int val = std::atoi(cur.c_str());
			if (field == 0)
				v = val;
			else if (field == 1)
				vt = val;
			else
				vn = val;
		}
		cur.clear();
	};
	for (char c : tok)
	{
		if (c == '/')
		{
			flush();
			++field;
		}
		else
			cur.push_back(c);
	}
	flush();
}
int resolve_idx(int i, std::size_t n)
{
	return i > 0 ? i - 1 : (i < 0 ? static_cast<int>(n) + i : -1);
}

// Parse an OBJ file into per-material Groups (raw, un-normalised positions). Also
// fills `materials` (from any mtllib) and `images` (an index of texture files in
// the model's folder + the shared assets/textures/ folder, name -> path). Returns
// false only if the file can't be opened. Shared by loadObjModel and loadObj.
bool parse_obj(const std::string &path, std::vector<Group> &groups,
               std::unordered_map<std::string, MtlMat> &materials,
               std::unordered_map<std::string, std::string> &images)
{
	std::ifstream file(path);
	if (!file)
		return false;

	const fs::path dir = fs::path(path).parent_path();
	auto add_dir = [&](const fs::path &d)
	{
		std::error_code ec;
		for (fs::recursive_directory_iterator it(d, ec), end; it != end; it.increment(ec))
		{
			if (ec || !it->is_regular_file())
				continue;
			const std::string e = lower(it->path().extension().string());
			if (e == ".png" || e == ".tga" || e == ".jpg" || e == ".jpeg" || e == ".bmp")
				images.emplace(lower(it->path().filename().string()), it->path().string());
		}
	};
	add_dir(dir);                                       // next to the model (priority)
	add_dir(fs::path(CORFORGE_ASSET_DIR) / "textures"); // shared textures folder

	std::vector<glm::vec3> positions, normals;
	std::vector<glm::vec2> uvs;

	std::unordered_map<std::string, std::size_t> group_of;
	std::size_t cur = 0;
	auto ensure_group = [&](const std::string &m)
	{
		auto it = group_of.find(m);
		if (it != group_of.end())
		{
			cur = it->second;
			return;
		}
		group_of[m] = groups.size();
		cur = groups.size();
		groups.push_back({m, {}, {}});
	};
	ensure_group(""); // default

	std::string line;
	while (std::getline(file, line))
	{
		std::istringstream ss(line);
		std::string tag;
		ss >> tag;
		if (tag == "v")
		{
			glm::vec3 p;
			ss >> p.x >> p.y >> p.z;
			positions.push_back(p);
		}
		else if (tag == "vt")
		{
			glm::vec2 t{0};
			ss >> t.x >> t.y;
			uvs.push_back(t);
		}
		else if (tag == "vn")
		{
			glm::vec3 n;
			ss >> n.x >> n.y >> n.z;
			normals.push_back(n);
		}
		else if (tag == "mtllib")
		{
			std::string mtl;
			ss >> mtl;
			parse_mtl((dir / mtl).string(), materials);
		}
		else if (tag == "usemtl")
		{
			std::string m;
			ss >> m;
			ensure_group(m);
		}
		else if (tag == "f")
		{
			std::vector<std::array<int, 3>> face;
			std::string tok;
			while (ss >> tok)
			{
				int v, vt, vn;
				parse_ref(tok, v, vt, vn);
				face.push_back({v, vt, vn});
			}
			if (face.size() < 3)
				continue;
			Group &g = groups[cur];
			for (std::size_t i = 1; i + 1 < face.size(); ++i)
			{
				const std::array<int, 3> tri[3] = {face[0], face[i], face[i + 1]};
				glm::vec3 p[3];
				for (int k = 0; k < 3; ++k)
				{
					int pi = resolve_idx(tri[k][0], positions.size());
					p[k] = (pi >= 0 && pi < (int)positions.size()) ? positions[pi] : glm::vec3(0);
				}
				const glm::vec3 fn = glm::normalize(glm::cross(p[1] - p[0], p[2] - p[0]));
				for (int k = 0; k < 3; ++k)
				{
					Vertex out;
					out.position = p[k];
					int ti = resolve_idx(tri[k][1], uvs.size());
					out.uv = (ti >= 0 && ti < (int)uvs.size()) ? uvs[ti] : glm::vec2(0);
					int ni = resolve_idx(tri[k][2], normals.size());
					out.normal = (ni >= 0 && ni < (int)normals.size()) ? normals[ni] : fn;
					g.idx.push_back(static_cast<std::uint32_t>(g.verts.size()));
					g.verts.push_back(out);
				}
			}
		}
	}
	return true;
}

} // namespace

std::vector<ModelPrimitive> load_obj_model(const std::string &path)
{
	std::vector<Group> groups;
	std::unordered_map<std::string, MtlMat> materials;
	std::unordered_map<std::string, std::string> images;
	if (!parse_obj(path, groups, materials, images))
	{
		std::fprintf(stderr, "[obj] cannot open %s\n", path.c_str());
		return {};
	}

	// Bounds over everything, for center + uniform scale to ~2 units.
	glm::vec3 mn(std::numeric_limits<float>::max()), mx(-std::numeric_limits<float>::max());
	for (const Group &g : groups)
		for (const Vertex &v : g.verts)
		{
			mn = glm::min(mn, v.position);
			mx = glm::max(mx, v.position);
		}
	if (mn.x > mx.x)
	{
		std::fprintf(stderr, "[obj] no geometry in %s\n", path.c_str());
		return {};
	}
	const glm::vec3 center = (mn + mx) * 0.5f;
	const float maxd = std::max({mx.x - mn.x, mx.y - mn.y, mx.z - mn.z});
	const float scale = (maxd > 1e-4f) ? 2.0f / maxd : 1.0f;

	std::unordered_map<std::string, std::shared_ptr<Texture>> tex_cache;
	std::vector<ModelPrimitive> out;
	for (Group &g : groups)
	{
		if (g.verts.empty())
			continue;
		for (Vertex &v : g.verts)
			v.position = (v.position - center) * scale;

		ModelPrimitive prim;
		prim.mesh = std::make_shared<Mesh>(g.verts, g.idx);
		const auto m = materials.find(g.mat);
		if (m != materials.end())
		{
			const MtlMat &mm = m->second;
			prim.albedo = mm.kd;
			prim.roughness = glm::clamp(std::sqrt(2.0f / (mm.ns + 2.0f)), 0.04f, 1.0f);
			prim.opacity = glm::clamp(mm.d, 0.0f, 1.0f);
			prim.transparent = mm.d < 0.999f;
			if (!mm.map_kd.empty())
			{
				const std::string base = lower(fs::path(mm.map_kd).filename().string());
				if (auto f = images.find(base); f != images.end())
				{
					auto &cached = tex_cache[f->second];
					if (!cached)
					{
						try
						{
							cached = Texture::load_from_file(f->second, true);
						}
						catch (...)
						{
						}
					}
					prim.albedo_map = cached;
				}
			}
			// Heuristic: an untextured, bright, high-specular material (Kd~white,
			// Ks~1) is almost certainly chrome/metal in these OBJ packs. Make it
			// metallic so IBL reflects the environment instead of flat white.
			if (!prim.albedo_map)
			{
				const float kd_l = std::max({mm.kd.r, mm.kd.g, mm.kd.b});
				const float ks_l = std::max({mm.ks.r, mm.ks.g, mm.ks.b});
				if (kd_l > 0.7f && ks_l > 0.5f)
				{
					prim.metallic = 1.0f;
					prim.roughness = glm::clamp(prim.roughness, 0.04f, 0.3f);
				}
			}
		}
		out.push_back(std::move(prim));
	}
	return out;
}

// ===================== Assimp (FBX / DAE / 3DS / STL / PLY / ...) ===========

std::vector<ModelPrimitive> load_assimp(const std::string &path, bool separate)
{
	Assimp::Importer imp;
	const aiScene *scene =
	    imp.ReadFile(path, aiProcess_Triangulate | aiProcess_GenSmoothNormals |
	                           aiProcess_JoinIdenticalVertices | aiProcess_CalcTangentSpace);
	if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
	{
		std::fprintf(stderr, "[assimp] failed: %s (%s)\n", path.c_str(), imp.GetErrorString());
		return {};
	}

	// Index image files in the model's folder + the shared assets/textures/ folder,
	// so a material referencing a bare filename resolves to the real file.
	const fs::path dir = fs::path(path).parent_path();
	std::unordered_map<std::string, std::string> images;
	auto add_dir = [&](const fs::path &d)
	{
		std::error_code ec;
		for (fs::recursive_directory_iterator it(d, ec), end; it != end; it.increment(ec))
		{
			if (ec || !it->is_regular_file())
				continue;
			const std::string e = lower(it->path().extension().string());
			if (e == ".png" || e == ".tga" || e == ".jpg" || e == ".jpeg" || e == ".bmp")
				images.emplace(lower(it->path().filename().string()), it->path().string());
		}
	};
	add_dir(dir);
	add_dir(fs::path(CORFORGE_ASSET_DIR) / "textures");

	std::unordered_map<std::string, std::shared_ptr<Texture>> tex_cache;
	auto resolve_tex = [&](aiMaterial *m, std::initializer_list<aiTextureType> types,
	                       bool srgb) -> std::shared_ptr<Texture>
	{
		for (aiTextureType t : types)
		{
			aiString tp;
			if (m->GetTexture(t, 0, &tp) != AI_SUCCESS)
				continue;
			const std::string base = lower(fs::path(tp.C_Str()).filename().string());
			const auto f = images.find(base);
			if (f == images.end())
				continue;
			auto &c = tex_cache[f->second + (srgb ? "#s" : "#l")];
			if (!c)
			{
				try
				{
					c = Texture::load_from_file(f->second, srgb);
				}
				catch (...)
				{
				}
			}
			return c;
		}
		return nullptr;
	};

	// Walk the node graph, baking each node's world transform into its meshes but
	// keeping the meshes SEPARATE (one Raw per mesh) -- so a multi-part model
	// (e.g. 3 crates in one file) can be split into individual objects later.
	struct Raw
	{
		std::vector<Vertex> verts;
		std::vector<std::uint32_t> idx;
		unsigned mat = 0;
	};
	std::vector<Raw> raws;
	std::function<void(const aiNode *, const glm::mat4 &)> visit =
	    [&](const aiNode *node, const glm::mat4 &parent)
	{
		const glm::mat4 world = parent * glm::transpose(glm::make_mat4(&node->mTransformation.a1));
		const glm::mat3 nmat = glm::transpose(glm::inverse(glm::mat3(world)));
		for (unsigned i = 0; i < node->mNumMeshes; ++i)
		{
			const aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
			Raw raw;
			raw.mat = mesh->mMaterialIndex;
			for (unsigned v = 0; v < mesh->mNumVertices; ++v)
			{
				Vertex vert;
				const aiVector3D &p = mesh->mVertices[v];
				vert.position = glm::vec3(world * glm::vec4(p.x, p.y, p.z, 1.0f));
				if (mesh->HasNormals())
				{
					const aiVector3D &n = mesh->mNormals[v];
					vert.normal = glm::normalize(nmat * glm::vec3(n.x, n.y, n.z));
				}
				else
					vert.normal = {0, 1, 0};
				if (mesh->mTextureCoords[0])
				{
					const aiVector3D &uv = mesh->mTextureCoords[0][v];
					vert.uv = {uv.x, uv.y};
				}
				if (mesh->mTangents)
				{
					const aiVector3D &tg = mesh->mTangents[v];
					vert.tangent = glm::normalize(nmat * glm::vec3(tg.x, tg.y, tg.z));
				}
				raw.verts.push_back(vert);
			}
			for (unsigned f = 0; f < mesh->mNumFaces; ++f)
			{
				const aiFace &face = mesh->mFaces[f];
				for (unsigned k = 0; k < face.mNumIndices; ++k)
					raw.idx.push_back(face.mIndices[k]);
			}
			if (!raw.verts.empty() && !raw.idx.empty())
				raws.push_back(std::move(raw));
		}
		for (unsigned c = 0; c < node->mNumChildren; ++c)
			visit(node->mChildren[c], world);
	};
	visit(scene->mRootNode, glm::mat4(1.0f));
	if (raws.empty())
		return {};

	// Center + uniform-scale the whole model to ~2 units (like the other loaders).
	glm::vec3 mn(std::numeric_limits<float>::max()), mx(-std::numeric_limits<float>::max());
	for (const Raw &r : raws)
		for (const Vertex &v : r.verts)
		{
			mn = glm::min(mn, v.position);
			mx = glm::max(mx, v.position);
		}
	const glm::vec3 center = (mn + mx) * 0.5f;
	const float maxd = std::max({mx.x - mn.x, mx.y - mn.y, mx.z - mn.z});
	const float scale = (maxd > 1e-4f) ? 2.0f / maxd : 1.0f;

	std::vector<ModelPrimitive> out;
	out.reserve(raws.size());
	for (Raw &r : raws)
	{
		for (Vertex &v : r.verts)
			v.position = (v.position - center) * scale;
		ModelPrimitive prim;
		if (separate)
		{
			// Re-centre this part on its own centroid and remember where it sat,
			// so it can become a standalone object with a pivot at its centre.
			glm::vec3 centroid(0.0f);
			for (const Vertex &v : r.verts)
				centroid += v.position;
			centroid /= static_cast<float>(r.verts.size());
			for (Vertex &v : r.verts)
				v.position -= centroid;
			prim.offset = centroid;
		}
		prim.mesh = std::make_shared<Mesh>(r.verts, r.idx);
		aiMaterial *m = scene->mMaterials[r.mat];
		aiColor3D kd(0.8f, 0.8f, 0.8f);
		m->Get(AI_MATKEY_COLOR_DIFFUSE, kd);
		prim.albedo = {kd.r, kd.g, kd.b};
		float metallic = 0.0f, roughness = 0.8f, opacity = 1.0f;
		m->Get(AI_MATKEY_METALLIC_FACTOR, metallic);
		m->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness);
		m->Get(AI_MATKEY_OPACITY, opacity);
		prim.metallic = glm::clamp(metallic, 0.0f, 1.0f);
		prim.roughness = glm::clamp(roughness, 0.04f, 1.0f);
		prim.opacity = glm::clamp(opacity, 0.0f, 1.0f);
		prim.transparent = opacity < 0.999f;
		prim.albedo_map = resolve_tex(m, {aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE}, true);
		prim.normal_map = resolve_tex(m, {aiTextureType_NORMALS, aiTextureType_HEIGHT}, false);
		out.push_back(std::move(prim));
	}
	return out;
}

std::vector<ModelPrimitive> load_any_model(const std::string &path)
{
	const std::string ext = lower(fs::path(path).extension().string());
	if (ext == ".obj")
		return load_obj_model(path);
	if (ext == ".gltf" || ext == ".glb")
		return load_gltf(path);
	return load_assimp(path, /*separate=*/false); // .fbx / .dae / .3ds / .stl / .ply / .blend
}

std::vector<ModelPrimitive> load_model_parts(const std::string &path)
{
	const std::string ext = lower(fs::path(path).extension().string());
	if (ext == ".obj" || ext == ".gltf" || ext == ".glb")
		return load_any_model(path); // those loaders don't split (parts share offset 0)
	return load_assimp(path, /*separate=*/true);
}

std::shared_ptr<Mesh> load_obj(const std::string &path)
{
	std::vector<Group> groups;
	std::unordered_map<std::string, MtlMat> materials;
	std::unordered_map<std::string, std::string> images;
	if (!parse_obj(path, groups, materials, images))
		throw std::runtime_error("Failed to open OBJ: " + path);

	// Merge every material group into one mesh (single-mesh asset: no materials,
	// kept at the authored size -- no centre/scale normalisation).
	std::vector<Vertex> verts;
	std::vector<std::uint32_t> idx;
	for (const Group &g : groups)
	{
		const std::uint32_t base = static_cast<std::uint32_t>(verts.size());
		verts.insert(verts.end(), g.verts.begin(), g.verts.end());
		for (std::uint32_t i : g.idx)
			idx.push_back(base + i);
	}
	if (verts.empty())
		throw std::runtime_error("OBJ has no geometry: " + path);
	return std::make_shared<Mesh>(verts, idx);
}

} // namespace cf
