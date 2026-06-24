cmake_minimum_required(VERSION 3.21)
project(CorForge LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Emit build/compile_commands.json so the VS Code C/C++ extension gets accurate
# include paths + defines (e.g. CORFORGE_SHADER_DIR) for IntelliSense.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Put all binaries in one place so the .exe sits next to runtime DLLs (if any).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

include(FetchContent)

# ---------------------------------------------------------------------------
# Dependencies (downloaded & built at configure time)
# ---------------------------------------------------------------------------

# GLFW -- window + input + GL context
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.4
  GIT_SHALLOW    TRUE
)

# glm -- math (header-only, target glm::glm)
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG        1.0.1
  GIT_SHALLOW    TRUE
)

# glad2 -- OpenGL function loader. The cmake/ subdir exposes glad_add_library().
FetchContent_Declare(
  glad
  GIT_REPOSITORY https://github.com/Dav1dde/glad.git
  GIT_TAG        v2.0.6
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  cmake
)

# Dear ImGui -- immediate-mode GUI. Ships no CMakeLists, so we only download it
# here and build our own target below. (docking branch = dockable panels.)
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.92.8-docking
  GIT_SHALLOW    TRUE
)

# stb -- single-header image loader (stb_image.h). Header-only, no build.
FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG        master
  GIT_SHALLOW    TRUE
)

# nlohmann/json -- single-header JSON (for scene save/load).
FetchContent_Declare(
  json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/include.zip
)

# tinygltf -- glTF 2.0 loader (we use it header-only with our own json + stb).
FetchContent_Declare(
  tinygltf
  GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
  GIT_TAG        v2.9.7
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  do-not-build
)

# ImGuizmo -- transform gizmos for ImGui. master tracks current ImGui internals.
# SOURCE_SUBDIR points at a folder with no CMakeLists so MakeAvailable only
# downloads it (its own CMakeLists would build extras without our imgui includes);
# we compile just ImGuizmo.cpp into the imgui target below.
FetchContent_Declare(
  imguizmo
  GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
  GIT_TAG        master
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  src
)

FetchContent_MakeAvailable(glfw glm glad imgui stb imguizmo json tinygltf)

# Assimp -- model import library, used only by the `modelconvert` CLI tool to
# convert OBJ/FBX/3DS/DAE/STL/PLY/glTF/blend into .glb. Curated importers +
# the glTF exporter keep the build smaller. Not linked into the engine itself.
set(ASSIMP_BUILD_TESTS OFF CACHE INTERNAL "")
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE INTERNAL "")
set(ASSIMP_BUILD_SAMPLES OFF CACHE INTERNAL "")
set(ASSIMP_INSTALL OFF CACHE INTERNAL "")
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE INTERNAL "")
set(ASSIMP_BUILD_ZLIB ON CACHE INTERNAL "")
set(ASSIMP_NO_EXPORT OFF CACHE INTERNAL "")
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE INTERNAL "")
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE INTERNAL "")
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE INTERNAL "")
set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE INTERNAL "")
set(ASSIMP_BUILD_3DS_IMPORTER ON CACHE INTERNAL "")
set(ASSIMP_BUILD_COLLADA_IMPORTER ON CACHE INTERNAL "")
set(ASSIMP_BUILD_STL_IMPORTER ON CACHE INTERNAL "")
set(ASSIMP_BUILD_PLY_IMPORTER ON CACHE INTERNAL "")
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE INTERNAL "")
set(ASSIMP_BUILD_BLEND_IMPORTER ON CACHE INTERNAL "")
set(ASSIMP_BUILD_GLTF_EXPORTER ON CACHE INTERNAL "")
set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
FetchContent_Declare(
  assimp
  GIT_REPOSITORY https://github.com/assimp/assimp.git
  GIT_TAG        v5.4.3
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(assimp)

# Generate a static GL 4.6 core loader target: glad_gl_core
glad_add_library(glad_gl_core STATIC API gl:core=4.6)

# Build ImGui (core + GLFW/OpenGL3 backends) as a static library.
add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw)

# Add ImGuizmo into the same target (it depends on imgui + imgui_internal).
# Master keeps its sources under src/.
target_sources(imgui PRIVATE ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp)
target_include_directories(imgui PUBLIC ${imguizmo_SOURCE_DIR}/src)

# ---------------------------------------------------------------------------
# Engine executable
# ---------------------------------------------------------------------------
add_executable(corforge
  src/main.cpp
  src/core/WindowManager.cpp
  src/core/Object.cpp
  src/core/ObjectsManager.cpp
  src/core/RendererManager.cpp
  src/core/ComponentRegistry.cpp
  src/core/AssetManager.cpp
  src/core/PhysicsManager.cpp
  src/core/SceneSerializer.cpp
  src/components/CameraComponent.cpp
  src/components/ColliderComponent.cpp
  src/components/RigidbodyComponent.cpp
  src/components/FlyCameraController.cpp
  src/components/LightComponent.cpp
  src/components/MaterialComponent.cpp
  src/components/BuoyancyComponent.cpp
  src/components/ClothComponent.cpp
  src/components/FlockComponent.cpp
  src/components/MoverComponent.cpp
  src/components/ParticleComponent.cpp
  src/components/PhysicsMoverComponent.cpp
  src/components/RendererComponent.cpp
  src/components/SkyComponent.cpp
  src/components/TerrainComponent.cpp
  src/components/WaterComponent.cpp
  src/gfx/Shader.cpp
  src/gfx/Mesh.cpp
  src/gfx/Texture.cpp
  src/gfx/ModelLoader.cpp
  src/gfx/gltf_impl.cpp
  src/gfx/stb_image_impl.cpp
  src/ui/UIManager.cpp
  src/ui/DebugUI.cpp
)

# Includes are written relative to src/, e.g. #include "core/WindowManager.h"
target_include_directories(corforge PRIVATE
  src ${stb_SOURCE_DIR} ${json_SOURCE_DIR}/include ${tinygltf_SOURCE_DIR})
target_link_libraries(corforge PRIVATE glfw glm::glm glad_gl_core imgui assimp)

# Let the app find shaders + assets from the source tree during development.
target_compile_definitions(corforge PRIVATE
  CORFORGE_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
  CORFORGE_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

if(MSVC)
  target_compile_options(corforge PRIVATE /W4 /permissive- /utf-8)
endif()

# ---------------------------------------------------------------------------
# modelconvert -- standalone CLI: convert any model file to .glb (via Assimp)
# ---------------------------------------------------------------------------
add_executable(modelconvert tools/modelconvert.cpp)
target_link_libraries(modelconvert PRIVATE assimp)
