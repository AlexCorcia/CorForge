// Compiles the tinygltf implementation, reusing the json + stb_image we
// already vendor (so there's only one copy of each in the program).
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_INCLUDE_JSON             // we include nlohmann/json ourselves
#define TINYGLTF_NO_STB_IMAGE_IMPLEMENTATION // stb_image impl lives in stb_image_impl.cpp
#define TINYGLTF_NO_STB_IMAGE_WRITE          // loading only; no image writing here

#include <nlohmann/json.hpp>
#include <tiny_gltf.h>
