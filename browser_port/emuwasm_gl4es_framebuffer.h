/*
 * WebGL2 renderbuffer storage compatibility inspired by GL4ES framebuffers.c
 * https://github.com/ptitSeb/gl4es
 *
 * Only normalize desktop/unsized storage enumerants for which WebGL2 has a
 * matching sized renderbuffer format. Do not alter attachments, framebuffers,
 * sample counts, object identities, or GL error return values.
 *
 * Clean-room, narrow adapter: no GL4ES code is copied into this file.
 */
#ifndef EMUWASM_GL4ES_FRAMEBUFFER_H
#define EMUWASM_GL4ES_FRAMEBUFFER_H

#include <stdint.h>

namespace emuwasm_gl4es {

enum : uint32_t {
    RB_RGB = 0x1907,
    RB_RGBA = 0x1908,
    RB_RGB4 = 0x804F,
    RB_RGB5 = 0x8050,
    RB_RGB8 = 0x8051,
    RB_RGBA4 = 0x8056,
    RB_RGBA8 = 0x8058,
    RB_RGB565 = 0x8D62,
    RB_RGB332 = 0x2A10,
    RB_DEPTH_COMPONENT = 0x1902,
    RB_DEPTH_COMPONENT16 = 0x81A5,
    RB_DEPTH_COMPONENT24 = 0x81A6,
    RB_DEPTH_COMPONENT32 = 0x81A7,
    RB_STENCIL_INDEX = 0x1901,
    RB_STENCIL_INDEX8 = 0x8D48,
    RB_DEPTH_STENCIL = 0x84F9,
    RB_DEPTH24_STENCIL8 = 0x88F0,
};

struct RenderbufferFormat {
    uint32_t original;
    uint32_t internal;
    bool changed;
    bool approximation;
};

/*
 * Some desktop internal formats are not legal for glRenderbufferStorage in
 * WebGL2. GL4ES similarly maps generic/deprecated formats to GLES formats.
 *
 * The approximations are explicitly flagged: RGB5->RGB565, RGB4->RGBA4 and
 * DEPTH_COMPONENT32->DEPTH_COMPONENT24 do not guarantee identical precision.
 * Unknown formats stay unchanged so capability probing continues to work.
 */
inline RenderbufferFormat normalize_renderbuffer_format(uint32_t internal) {
    RenderbufferFormat result = {internal, internal, false, false};
    switch (internal) {
    case RB_RGB:
        result.internal = RB_RGB8;
        break;
    case RB_RGBA:
        result.internal = RB_RGBA8;
        break;
    case RB_RGB5:
    case RB_RGB332:
        result.internal = RB_RGB565;
        result.approximation = true;
        break;
    case RB_RGB4:
        result.internal = RB_RGBA4;
        result.approximation = true;
        break;
    case RB_DEPTH_COMPONENT:
        result.internal = RB_DEPTH_COMPONENT16;
        break;
    case RB_DEPTH_COMPONENT32:
        result.internal = RB_DEPTH_COMPONENT24;
        result.approximation = true;
        break;
    case RB_STENCIL_INDEX:
        result.internal = RB_STENCIL_INDEX8;
        break;
    case RB_DEPTH_STENCIL:
        result.internal = RB_DEPTH24_STENCIL8;
        break;
    default:
        return result;
    }
    result.changed = result.internal != internal;
    return result;
}

} // namespace emuwasm_gl4es

#endif
