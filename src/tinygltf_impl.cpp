#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE

// Include stb headers from vendor/ so tinygltf's calls (stbi_*, stbi_write_*)
// are available when tiny_gltf.h implementation is compiled.
#include "stb_image.h"
#include "stb_image_write.h"

#include "tiny_gltf.h"

// This file provides the implementation for tinygltf and the stb image
// implementations. Inclusion triggers the implementations via the
// STB_*_IMPLEMENTATION macros above.
