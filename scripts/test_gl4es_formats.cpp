// Host-side regression tests for the GL4ES-inspired WebGL2 texture adapter.
// Compile with: c++ -std=c++11 -Wall -Wextra -Werror -Ibrowser_port
//               scripts/test_gl4es_formats.cpp -o /tmp/test_gl4es_formats
#include "emuwasm_gl4es_formats.h"
#include <cassert>
#include <cstdio>
using namespace emuwasm_gl4es;

int main() {
    const TextureFormat rgb = normalize_texture_format(3, GL_RGB_VALUE,
                                                       GL_UNSIGNED_BYTE_VALUE, true);
    assert(rgb.internal == GL_RGB8_VALUE && rgb.normalized);
    const TextureFormat rgba = normalize_texture_format(4, GL_RGBA_VALUE,
                                                        GL_UNSIGNED_BYTE_VALUE, true);
    assert(rgba.internal == GL_RGBA8_VALUE && rgba.normalized);
    const TextureFormat bgra = normalize_texture_format(GL_BGRA_VALUE, GL_RGBA_VALUE,
                                                        GL_UNSIGNED_BYTE_VALUE, true);
    assert(bgra.internal == GL_RGBA8_VALUE && bgra.normalized);
    const TextureFormat bgr = normalize_texture_format(GL_BGR_VALUE, GL_RGB_VALUE,
                                                       GL_UNSIGNED_BYTE_VALUE, true);
    assert(bgr.internal == GL_RGB8_VALUE && bgr.normalized);
    // Never claim a desktop BGR upload is legal unless the bytes were converted.
    const TextureFormat unsupported = normalize_texture_format(
        GL_BGRA_VALUE, GL_BGRA_VALUE, GL_UNSIGNED_BYTE_VALUE, false);
    assert(unsupported.internal == GL_BGRA_VALUE && !unsupported.normalized);
    // Preserve formats outside the deliberately narrow supported subset.
    const TextureFormat depth = normalize_texture_format(0x81A6, 0x1902,
                                                         GL_UNSIGNED_BYTE_VALUE, true);
    assert(depth.internal == 0x81A6 && !depth.normalized);
    const TextureFormat packed = normalize_texture_format(4, GL_RGBA_VALUE, 0x8367, true);
    assert(packed.internal == 4 && !packed.normalized);
    std::puts("GL4ES-inspired format mapping: 7 cases passed");
}
