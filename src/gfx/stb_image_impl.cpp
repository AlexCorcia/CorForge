// Single translation unit that compiles the stb implementations.
#define CRT_SECURE_NO_WARNINGS // stb_image_write uses sprintf
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
