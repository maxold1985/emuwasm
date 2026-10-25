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

// WebGL2 supports getBufferSubData, while GLES3 intentionally has no matching
// native GL entry point. Read the existing PIXEL_UNPACK_BUFFER without mapping
// it as an x86 / wasm CPU pointer. Return false for WebGL1 or invalid ranges.
EM_JS(int, emuwasm_webgl_read_unpack_pbo,
      (unsigned byteOffset, unsigned out, unsigned size), {
    var gl = GL.currentContext && GL.currentContext.GLctx;
    if (!gl || typeof gl.getBufferSubData !== "function" || !size)
        return 0;
    try {
        var buffer = gl.getParameter(0x88EF); // PIXEL_UNPACK_BUFFER_BINDING
        if (!buffer) return 0;
        var capacity = gl.getBufferParameter(0x88EC, 0x8764); // BUFFER_SIZE
        if (!Number.isSafeInteger(capacity) ||
                byteOffset > capacity || size > capacity - byteOffset)
            return 0;
        // The current context owns the binding. Emscripten provides the
        // wasm memory view, so this does not expose guest files or buffers.
        gl.getBufferSubData(0x88EC, byteOffset,
                            HEAPU8.subarray(out, out + size));
        return 1;
    } catch (error) {
        console.warn("emuwasm-webgl: PBO readback failed", String(error));
        return 0;
    }
});

struct EmuWasmWebGLPixelUpload {
    GLenum format;
    GLenum type;
    const GLvoid* pixels;
    std::vector<U8> converted;
    std::vector<U8> pboReadback;

    bool changedUnpack = false;
    bool unboundPbo = false;
    GLint previousPbo = 0;
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

        // Packed REV is byte-compatible with BGRA on little-endian wasm32.
        // Other packed representations need a dedicated component unpacker.
        if (type != GL_UNSIGNED_BYTE && !(bgra && type == packed8888Rev)) {
            klog_fmt("emuwasm-webgl: unsupported desktop pixel type: format=0x%x type=0x%x",
                     format, type);
            return;
        }

        const unsigned channels = bgra ? 4u : 3u;
        const GLenum outputFormat = bgra ? GL_RGBA : GL_RGB;

        // No GPU buffer bound and no pixels: allocate texture storage only.
        // With a bound PBO, pointer 0 is a *valid buffer offset*, not NULL.
        if (!pixels && !unpackPboBound) {
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
        if (rowRaw > maxBytes || pitch > maxBytes ||
            (static_cast<std::uint64_t>(oldSkipRows) + height - 1) > maxBytes / pitch ||
            (static_cast<std::uint64_t>(oldSkipPixels) + width) > maxBytes / channels)
            return;
        const std::uint64_t finalSourceByte =
            (static_cast<std::uint64_t>(oldSkipRows) + height - 1) * pitch +
            (static_cast<std::uint64_t>(oldSkipPixels) + width) * channels;
        if (!outputBytes || outputBytes > maxBytes ||
            pitch > maxBytes || finalSourceByte > maxBytes)
            return;

        // For PBO uploads the client pointer is a byte offset, often zero.
        // We cannot dereference that pointer directly, even if it is nonzero.
        const U8* input = static_cast<const U8*>(pixels);
        if (unpackPboBound) {
            const std::uint64_t byteOffset =
                static_cast<std::uint64_t>(reinterpret_cast<uintptr_t>(pixels));
            if (byteOffset > UINT32_MAX ||
                    finalSourceByte > UINT32_MAX) {
                klog_fmt("emuwasm-webgl: PBO size/offset outside wasm32 range");
                return;
            }
            pboReadback.resize(static_cast<size_t>(finalSourceByte));
            if (!emuwasm_webgl_read_unpack_pbo(
                    static_cast<unsigned>(byteOffset),
                    static_cast<unsigned>(reinterpret_cast<uintptr_t>(pboReadback.data())),
                    static_cast<unsigned>(finalSourceByte))) {
                klog_fmt("emuwasm-webgl: PBO readback unsupported or out of range "
                         "offset=%u len=%u format=0x%x type=0x%x",
                         static_cast<unsigned>(byteOffset),
                         static_cast<unsigned>(finalSourceByte), format, type);
                return;
            }
            input = pboReadback.data();
        }
        converted.resize(static_cast<size_t>(outputBytes));
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

        // A CPU pointer is legal only with PIXEL_UNPACK_BUFFER unbound.
        // Keep Emscripten's GL binding cache and the original guest binding.
        if (unpackPboBound) {
            GL_FUNC(pglGetIntegerv)(0x88EF, &previousPbo);
            if (previousPbo <= 0) {
                klog_fmt("emuwasm-webgl: PBO binding disappeared during readback");
                return;
            }
            glBindBuffer(0x88EC, 0); // GL_PIXEL_UNPACK_BUFFER
            unboundPbo = true;
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
            klog_fmt("emuwasm-webgl: converted BGR/BGRA %dx%d channels=%u "
                     "pboReadback=%d pixels=%u", width, height, channels,
                     static_cast<int>(unboundPbo), static_cast<unsigned>(outputBytes));
    }

    void restore() {
        if (changedUnpack) {
            GL_FUNC(pglPixelStorei)(GL_UNPACK_ALIGNMENT, oldAlignment);
            GL_FUNC(pglPixelStorei)(GL_UNPACK_ROW_LENGTH, oldRowLength);
            GL_FUNC(pglPixelStorei)(GL_UNPACK_SKIP_ROWS, oldSkipRows);
            GL_FUNC(pglPixelStorei)(GL_UNPACK_SKIP_PIXELS, oldSkipPixels);
        }
        if (unboundPbo) {
            glBindBuffer(0x88EC, static_cast<GLuint>(previousPbo));
        }
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
        ? (GLvoid*)pARG9
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
    else if (internalFormat == 4)
        internalFormat = GL_RGBA; // Legacy GL component-count internal format
    else if (internalFormat == 3)
        internalFormat = GL_RGB;
#endif

    GL_FUNC(pglTexImage2D)(target, level, internalFormat, width, height,
                           border, format, type, pixels);
#ifdef __EMSCRIPTEN__
    upload.restore();
#endif
}
