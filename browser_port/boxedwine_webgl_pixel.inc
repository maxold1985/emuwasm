/*
 * EmuWasm compatibility shim for BoxedWine's active OpenGL -> WebGL2 path.
 * Copyright 2026 EmuWasm contributors. GPL-2.0-or-later.
 *
 * The browser's WebGL2 API does NOT accept desktop GL_BGR / GL_BGRA uploads.
 * Convert the byte ordering into RGB / RGBA before the Emscripten GL call.
 * Preserve all guest GL state, pixel pack offsets, and glGetError semantics.
 *
 * This file is inserted inside glcommon.cpp after the GL helper functions.
 */
#ifdef __EMSCRIPTEN__
#include <cstdint>
#include <vector>

struct EmuWasmWebGLPixelUpload {
    GLenum format;
    GLenum type;
    const GLvoid* pixels;
    std::vector<U8> converted;

    bool changedUnpack = false;
    GLint oldAlignment = 4;
    GLint oldRowLength = 0;
    GLint oldSkipRows = 0;
    GLint oldSkipPixels = 0;

    EmuWasmWebGLPixelUpload(GLenum format, GLenum type, const GLvoid* pixels)
        : format(format), type(type), pixels(pixels) {}

    void prepare(GLsizei width, GLsizei height, bool unpackPboBound) {
        constexpr GLenum desktopBGR = 0x80E0;
        constexpr GLenum desktopBGRA = 0x80E1;
        constexpr GLenum packed8888Rev = 0x8367;

        const bool bgra = format == desktopBGRA;
        const bool bgr = format == desktopBGR;
        if (!bgra && !bgr)
            return;

        // PBO pointer arguments are byte offsets into GPU buffers. Reading
        // them as guest CPU pointers would corrupt memory. Do not convert.
        if (unpackPboBound) {
            klog_fmt("emuwasm-webgl: BGR/BGRA PBO upload requires GPU staging: format=0x%x type=0x%x",
                     format, type);
            return;
        }

        // Packed REV is byte-compatible with BGRA on little-endian wasm32.
        // Other packed representations need a dedicated component unpacker.
        if (type != GL_UNSIGNED_BYTE && !(bgra && type == packed8888Rev)) {
            klog_fmt("emuwasm-webgl: unsupported desktop pixel type: format=0x%x type=0x%x",
                     format, type);
            return;
        }

        const unsigned channels = bgra ? 4u : 3u;
        const GLenum outputFormat = bgra ? GL_RGBA : GL_RGB;

        // When allocating texture storage, changing BGRA to RGBA is safe
        // without accessing any pixel data.
        if (!pixels) {
            format = outputFormat;
            type = GL_UNSIGNED_BYTE;
            return;
        }

        if (width <= 0 || height <= 0)
            return;

        GL_FUNC(pglGetIntegerv)(GL_UNPACK_ALIGNMENT, &oldAlignment);
        GL_FUNC(pglGetIntegerv)(GL_UNPACK_ROW_LENGTH, &oldRowLength);
        GL_FUNC(pglGetIntegerv)(GL_UNPACK_SKIP_ROWS, &oldSkipRows);
        GL_FUNC(pglGetIntegerv)(GL_UNPACK_SKIP_PIXELS, &oldSkipPixels);

        if ((oldAlignment != 1 && oldAlignment != 2 &&
             oldAlignment != 4 && oldAlignment != 8) ||
            oldRowLength < 0 || oldSkipRows < 0 || oldSkipPixels < 0)
            return;

        // Limit buffers both for memory safety and browser responsiveness.
        constexpr std::uint64_t maxBytes = 64ull * 1024ull * 1024ull;
        const std::uint64_t rowPixels = oldRowLength
            ? static_cast<std::uint64_t>(oldRowLength)
            : static_cast<std::uint64_t>(width);
        if (rowPixels < static_cast<std::uint64_t>(width))
            return;
        const std::uint64_t rowRaw = rowPixels * channels;
        const std::uint64_t pitch = (rowRaw + oldAlignment - 1) /
                                    oldAlignment * oldAlignment;
        const std::uint64_t outputBytes =
            static_cast<std::uint64_t>(width) * height * channels;
        const std::uint64_t finalSourceByte =
            (static_cast<std::uint64_t>(oldSkipRows) + height - 1) * pitch +
            (static_cast<std::uint64_t>(oldSkipPixels) + width) * channels;
        if (!outputBytes || outputBytes > maxBytes || finalSourceByte > maxBytes)
            return;

        converted.resize(static_cast<size_t>(outputBytes));
        const U8* input = static_cast<const U8*>(pixels);
        const size_t dstPitch = static_cast<size_t>(width) * channels;
        const size_t srcStart =
            static_cast<size_t>(oldSkipRows) * static_cast<size_t>(pitch) +
            static_cast<size_t>(oldSkipPixels) * channels;
        for (GLsizei y = 0; y < height; ++y) {
            const U8* row = input + srcStart + static_cast<size_t>(y) * pitch;
            U8* dst = converted.data() + static_cast<size_t>(y) * dstPitch;
            for (GLsizei x = 0; x < width; ++x) {
                const U8* src = row + static_cast<size_t>(x) * channels;
                U8* pixel = dst + static_cast<size_t>(x) * channels;
                pixel[0] = src[2]; // BGR desktop -> RGB WebGL
                pixel[1] = src[1];
                pixel[2] = src[0];
                if (bgra)
                    pixel[3] = src[3]; // Alpha is unchanged
            }
        }

        // Upload is now tightly packed; save and restore all UNPACK state.
        GL_FUNC(pglPixelStorei)(GL_UNPACK_ALIGNMENT, 1);
        GL_FUNC(pglPixelStorei)(GL_UNPACK_ROW_LENGTH, 0);
        GL_FUNC(pglPixelStorei)(GL_UNPACK_SKIP_ROWS, 0);
        GL_FUNC(pglPixelStorei)(GL_UNPACK_SKIP_PIXELS, 0);
        changedUnpack = true;

        pixels = converted.data();
        format = outputFormat;
        type = GL_UNSIGNED_BYTE;

        thread_local unsigned reported = 0;
        if (reported++ < 48)
            klog_fmt("emuwasm-webgl: converted BGR/BGRA texture %dx%d (%u channels) to WebGL2",
                     width, height, channels);
    }

    void restore() {
        if (!changedUnpack)
            return;
        GL_FUNC(pglPixelStorei)(GL_UNPACK_ALIGNMENT, oldAlignment);
        GL_FUNC(pglPixelStorei)(GL_UNPACK_ROW_LENGTH, oldRowLength);
        GL_FUNC(pglPixelStorei)(GL_UNPACK_SKIP_ROWS, oldSkipRows);
        GL_FUNC(pglPixelStorei)(GL_UNPACK_SKIP_PIXELS, oldSkipPixels);
    }
};
#endif

// This replaces the generated GL_FUNCTION handler on all targets. The
// conversion is compiled only into wasm, so native BoxedWine is unaffected.
void glcommon_glTexImage2D(CPU* cpu) {
    const GLenum target = ARG1;
    const GLint level = ARG2;
    GLint internalFormat = ARG3;
    const GLsizei width = ARG4;
    const GLsizei height = ARG5;
    const GLint border = ARG6;
    GLenum format = ARG7;
    GLenum type = ARG8;
    const bool usingPbo = PIXEL_UNPACK_BUFFER();
    const GLvoid* pixels = usingPbo
        ? reinterpret_cast<const GLvoid*>(static_cast<uintptr_t>(pARG9))
        : marshalPixels(cpu, 2, width, height, 1, format, type, ARG9);

#ifdef __EMSCRIPTEN__
    thread_local unsigned logged = 0;
    if (logged++ < 96) {
        klog_fmt("emuwasm-webgl: glTexImage2D target=0x%x level=%d internal=0x%x "
                 "size=%dx%d border=%d format=0x%x type=0x%x pbo=%d pixels=%d",
                 target, level, internalFormat, width, height, border,
                 format, type, static_cast<int>(usingPbo), pixels != nullptr);
    }
    EmuWasmWebGLPixelUpload upload(format, type, pixels);
    upload.prepare(width, height, usingPbo);
    format = upload.format;
    type = upload.type;
    pixels = upload.pixels;

    // Desktop glTexImage2D may specify BGRA/BGR as *internal* format.
    // WebGL2 does not support either as a storage format.
    if (internalFormat == 0x80E1)
        internalFormat = GL_RGBA;
    else if (internalFormat == 0x80E0)
        internalFormat = GL_RGB;
#endif

    GL_FUNC(pglTexImage2D)(target, level, internalFormat, width, height,
                           border, format, type, pixels);
#ifdef __EMSCRIPTEN__
    upload.restore();
#endif
}
