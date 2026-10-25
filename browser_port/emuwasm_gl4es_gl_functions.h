/*
 * Real guest OpenGL compatibility helpers for the WebGL2 backend.
 *
 * These are pure C++11 helpers used by the BoxedWine GL entry points
 * glGetTexImage, glDrawBuffer and glReadBuffer. WebGL2 does not expose
 * glGetTexImage; its emulation reads the currently bound texture through
 * an actual temporary framebuffer / readPixels operation.
 *
 * The GL4ES framebuffers/texture design informed this narrowly scoped
 * implementation. This is original code, not the GL4ES library.
 */
#ifndef EMUWASM_GL4ES_GL_FUNCTIONS_H
#define EMUWASM_GL4ES_GL_FUNCTIONS_H

#include <cstddef>
#include <cstdint>

namespace emuwasm_gl4es {

enum : std::uint32_t {
    BACK_BUFFER = 0x0405,
    FRONT_BUFFER = 0x0404,
    FRONT_AND_BACK_BUFFER = 0x0408,
    NO_BUFFER = 0,
    COLOR_ATTACHMENT_ZERO = 0x8CE0,
    RGB_PIXELS = 0x1907,
    RGBA_PIXELS = 0x1908,
    BGR_PIXELS = 0x80E0,
    BGRA_PIXELS = 0x80E1
};

struct BufferRoute {
    std::uint32_t hostMode;
    bool valid;
    bool approximated;
};

// WebGL2 exposes only the BACK buffer of its default framebuffer.
// Desktop FRONT maps to BACK with an explicitly reported approximation.
// FBO modes are not silently mapped to the default framebuffer.
inline BufferRoute route_buffer(std::uint32_t guest, bool defaultFramebuffer,
                                bool readOperation) {
    BufferRoute invalid = {guest, false, false};
    if (guest == NO_BUFFER)
        return {NO_BUFFER, true, false};
    if (defaultFramebuffer) {
        if (guest == BACK_BUFFER)
            return {BACK_BUFFER, true, false};
        if (guest == FRONT_BUFFER ||
            (!readOperation && guest == FRONT_AND_BACK_BUFFER))
            return {BACK_BUFFER, true, true};
        return invalid;
    }
    if (guest >= COLOR_ATTACHMENT_ZERO &&
        guest < COLOR_ATTACHMENT_ZERO + 16)
        return {guest, true, false};
    return invalid;
}

inline unsigned pixel_channels(std::uint32_t format) {
    if (format == RGB_PIXELS || format == BGR_PIXELS)
        return 3;
    if (format == RGBA_PIXELS || format == BGRA_PIXELS)
        return 4;
    return 0;
}

/*
 * Pack actual RGBA8 data into a guest OpenGL pixel memory layout.
 * Respect GL_PACK_ALIGNMENT, ROW_LENGTH, SKIP_ROWS and SKIP_PIXELS.
 *
 * The destination must be an already-allocated writable guest buffer
 * supplied through BoxedWine's MarshalReadWritePackedPixels. Padding and
 * skipped bytes remain untouched. No buffer pointer may be read on failure.
 */
inline bool pack_rgba_readback(const std::uint8_t* rgba,
                              std::size_t rgbaBytes,
                              std::uint32_t width,
                              std::uint32_t height,
                              std::uint32_t format,
                              std::uint32_t alignment,
                              std::uint32_t rowLength,
                              std::uint32_t skipRows,
                              std::uint32_t skipPixels,
                              std::uint8_t* guest,
                              std::size_t guestBytes) {
    const unsigned channels = pixel_channels(format);
    if (!channels || !width || !height || !rgba || !guest ||
        (alignment != 1 && alignment != 2 &&
         alignment != 4 && alignment != 8))
        return false;

    const std::uint64_t w = width, h = height;
    const std::uint64_t srcSize = w * h * 4;
    const std::uint64_t rowPixels = rowLength ? rowLength : w;
    if (srcSize > rgbaBytes || rowPixels < w)
        return false;

    const std::uint64_t rowRaw = rowPixels * channels;
    const std::uint64_t pitch =
        ((rowRaw + alignment - 1) / alignment) * alignment;
    const std::uint64_t base =
        static_cast<std::uint64_t>(skipRows) * pitch +
        static_cast<std::uint64_t>(skipPixels) * channels;
    const std::uint64_t last =
        base + (h - 1) * pitch + w * channels;
    if (!pitch || last > guestBytes)
        return false;

    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t* src = rgba + static_cast<std::size_t>(y) * width * 4;
        std::uint8_t* dst = guest + base + static_cast<std::uint64_t>(y) * pitch;
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::uint8_t* p = src + static_cast<std::size_t>(x) * 4;
            std::uint8_t* q = dst + static_cast<std::size_t>(x) * channels;
            const bool bgr = format == BGR_PIXELS || format == BGRA_PIXELS;
            q[0] = p[bgr ? 2 : 0];
            q[1] = p[1];
            q[2] = p[bgr ? 0 : 2];
            if (channels == 4)
                q[3] = p[3];
        }
    }
    return true;
}
}  // namespace emuwasm_gl4es

#endif
