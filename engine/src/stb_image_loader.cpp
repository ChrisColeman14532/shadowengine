// stb_image_loader.cpp — Single implementation unit for stb_image.
//
// We include stb_image.h INLINE (not through the include path) to avoid
// the header guard (#ifndef STBI_INCLUDE_STB_IMAGE_H) which would skip
// the #ifdef STB_IMAGE_IMPLEMENTATION block on subsequent includes.
//
// We do NOT define STB_IMAGE_STATIC, so the stbi_ functions get
// extern linkage and are visible to the linker.
//
// Other translation units include stb_image_loader.h which only has
// declarations (extern), guaranteeing no ODR violations.

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
