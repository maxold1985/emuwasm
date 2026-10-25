/*
 * GL4ES-inspired OpenGL texture internal-format compatibility for WebGL2.
 *
 * Reference: ptitSeb/gl4es, src/gl/texture.c (internal-format classification).
 * https://github.com/ptitSeb/gl4es
 *
 * This is a clean implementation of a narrow compatibility subset, not the
 * GL4ES library itself. The host OpenGL dispatcher remains BoxedWine.
 * Only normalize combinations that WebGL2 accepts; do not lie about formats.
 *
 * GL4ES uses the MIT license. See docs/gl4es-integration.md for attribution.
 */
#ifndef EMUWASM_GL4ES_FORMATS_H
#define EMUWASM_GL4ES_FORMATS_H

#include <stdint.h>

namespace emuwasm_gl4es {
enum : uint32_t {
    GL_RGB_VALUE = 0x1907,
    GL_RGBA_VALUE = 0x1908,
    GL_BGR_VALUE = 0x80E0,
    GL_BGRA_VALUE = 0x80E1,
    GL_RGB8_VALUE = 0x8051,
    GL_RGBA8_VALUE = 0x8058,
    GL_RGB5_VALUE = 0x8050,
    GL_RGB5_A1_VALUE = 0x8057,
    GL_RGBA4_VALUE = 0x8056,
    GL_RGB565_VALUE = 0x8D62,
    GL_UNSIGNED_BYTE_VALUE = 0x1401
};

struct TextureFormat {
    uint32_t internal;
    uint32_t external;
    uint32_t type;
    bool normalized;
};

/* Use WebGL2-sized storage formats for byte-based RGB(A) uploads.
 * WebGL2 does not permit desktop GL_BGR / GL_BGRA external formats.
 * The caller must first convert the bytes to RGB(A); this routine does NOT
 * rearrange pixel bytes and must not run on an unconverted upload.
 */
inline TextureFormat normalize_texture_format(uint32_t internal,
                                              uint32_t external,
                                              uint32_t type,
                                              bool pixels_converted) {
    TextureFormat result = {internal, external, type, false};
    if ((external == GL_BGR_VALUE || external == GL_BGRA_VALUE) &&
        !pixels_converted)
        return result;

    if (type != GL_UNSIGNED_BYTE_VALUE)
        return result;

    if (external == GL_RGB_VALUE &&
        (internal == 3 || internal == GL_BGR_VALUE ||
         internal == GL_RGB_VALUE || internal == GL_RGB8_VALUE)) {
        result.internal = GL_RGB8_VALUE;
        result.normalized = (internal != result.internal);
    } else if (external == GL_RGBA_VALUE &&
               (internal == 4 || internal == GL_BGRA_VALUE ||
                internal == GL_RGBA_VALUE || internal == GL_RGBA8_VALUE)) {
        result.internal = GL_RGBA8_VALUE;
        result.normalized = (internal != result.internal);
    }
    return result;
}
} // namespace emuwasm_gl4es
#endif
