/*
 * Real OpenGL entry points missing or incompatible with WebGL2:
 *
 *  - glDrawBuffer: desktop single-buffer selector -> WebGL2 drawBuffers
 *  - glReadBuffer: desktop read selector -> WebGL2 readBuffer
 *  - glGetTexImage: real texture readback through a temporary WebGL2 FBO,
 *    then pack RGBA pixels into the requested guest format.
 *
 * This file is inserted into glcommon.cpp above glcommon_glGetTexImage.
 * BOXEDWINE_OPENGL native builds keep their real desktop GL calls.
 */
#include "emuwasm_gl4es_gl_functions.h"

#ifdef __EMSCRIPTEN__

EM_JS(int, emuwasm_gl4es_framebuffer_is_default, (int read), {
    var gl = GL.currentContext && GL.currentContext.GLctx;
    if (!gl || typeof gl.readBuffer !== "function")
        return -1;
    try {
        var framebuffer = gl.getParameter(
            read ? gl.READ_FRAMEBUFFER_BINDING : gl.DRAW_FRAMEBUFFER_BINDING);
        return framebuffer ? 0 : 1;
    } catch (e) {
        return -1;
    }
});

EM_JS(int, emuwasm_gl4es_draw_buffer, (int mode), {
    var gl = GL.currentContext && GL.currentContext.GLctx;
    if (!gl || typeof gl.readBuffer !== "function" ||
        typeof gl.drawBuffers !== "function")
        return 0;
    try {
        // Actually updates WebGL2 draw-buffer state, not just a cached enum.
        // Validity was checked using the real guest framebuffer binding.
        gl.drawBuffers([mode]);
        return 1;
    } catch (e) {
        console.warn("emuwasm-gl4es: glDrawBuffer host error", String(e));
        return 0;
    }
});

EM_JS(int, emuwasm_gl4es_read_buffer, (int mode), {
    var gl = GL.currentContext && GL.currentContext.GLctx;
    if (!gl || typeof gl.readBuffer !== "function")
        return 0;
    try {
        gl.readBuffer(mode);
        return 1;
    } catch (e) {
        console.warn("emuwasm-gl4es: glReadBuffer host error", String(e));
        return 0;
    }
});

// WebGL2 has no getTexImage. Attach the currently bound 2D texture to
// a temporary read framebuffer, perform a genuine GPU-to-WASM readPixels,
// then restore the original READ/DRAW FBOs, read buffer and PACK parameters.
//
// This supports color-renderable texture levels. Incomplete/non-color
// renderable levels return false; we never manufacture pixel contents.
EM_JS(int, emuwasm_gl4es_get_tex_image_rgba8,
      (int level, int width, int height, int out, int size), {
    var gl = GL.currentContext && GL.currentContext.GLctx;
    if (!gl || typeof gl.readBuffer !== "function" ||
        !Number.isSafeInteger(width) || !Number.isSafeInteger(height) ||
        width <= 0 || height <= 0 || size !== width * height * 4)
        return 0;
    var texture = gl.getParameter(gl.TEXTURE_BINDING_2D);
    if (!texture)
        return 0;
    // A client typed array cannot be used as a readPixels target while
    // PIXEL_PACK_BUFFER is bound; that path is deliberately unsupported.
    if (gl.getParameter(gl.PIXEL_PACK_BUFFER_BINDING))
        return 0;
    var readFbo = gl.getParameter(gl.READ_FRAMEBUFFER_BINDING);
    var drawFbo = gl.getParameter(gl.DRAW_FRAMEBUFFER_BINDING);
    var oldReadBuffer = gl.getParameter(gl.READ_BUFFER);
    var oldAlign = gl.getParameter(gl.PACK_ALIGNMENT);
    var oldRowLength = gl.getParameter(gl.PACK_ROW_LENGTH);
    var oldSkipRows = gl.getParameter(gl.PACK_SKIP_ROWS);
    var oldSkipPixels = gl.getParameter(gl.PACK_SKIP_PIXELS);
    var staging = null;
    try {
        staging = gl.createFramebuffer();
        if (!staging)
            return 0;
        gl.bindFramebuffer(gl.READ_FRAMEBUFFER, staging);
        gl.framebufferTexture2D(gl.READ_FRAMEBUFFER, gl.COLOR_ATTACHMENT0,
                                gl.TEXTURE_2D, texture, level);
        if (gl.checkFramebufferStatus(gl.READ_FRAMEBUFFER) !==
                gl.FRAMEBUFFER_COMPLETE)
            return 0;
        gl.readBuffer(gl.COLOR_ATTACHMENT0);
        gl.pixelStorei(gl.PACK_ALIGNMENT, 1);
        gl.pixelStorei(gl.PACK_ROW_LENGTH, 0);
        gl.pixelStorei(gl.PACK_SKIP_ROWS, 0);
        gl.pixelStorei(gl.PACK_SKIP_PIXELS, 0);
        gl.readPixels(0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE,
                      HEAPU8.subarray(out, out + size));
        return 1;
    } catch (error) {
        console.warn("emuwasm-gl4es: glGetTexImage readPixels error",
                     String(error));
        return 0;
    } finally {
        // Both FBO binding points are independent in WebGL2.
        gl.bindFramebuffer(gl.READ_FRAMEBUFFER, readFbo);
        gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, drawFbo);
        gl.readBuffer(oldReadBuffer);
        gl.pixelStorei(gl.PACK_ALIGNMENT, oldAlign);
        gl.pixelStorei(gl.PACK_ROW_LENGTH, oldRowLength);
        gl.pixelStorei(gl.PACK_SKIP_ROWS, oldSkipRows);
        gl.pixelStorei(gl.PACK_SKIP_PIXELS, oldSkipPixels);
        if (staging)
            gl.deleteFramebuffer(staging);
    }
});

static void emuwasm_gl4es_set_error(CPU* cpu, GLenum error) {
    // BoxedWine reports synthetic guest errors before querying host GL.
    if (!cpu->thread->glLastError)
        cpu->thread->glLastError = error;
}

#endif // __EMSCRIPTEN__

void glcommon_glDrawBuffer(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    const int isDefault = emuwasm_gl4es_framebuffer_is_default(0);
    const emuwasm_gl4es::BufferRoute route =
        emuwasm_gl4es::route_buffer(ARG1, isDefault == 1, false);
    if (isDefault < 0 || !route.valid ||
        !emuwasm_gl4es_draw_buffer(static_cast<int>(route.hostMode))) {
        emuwasm_gl4es_set_error(cpu, GL_INVALID_ENUM);
        return;
    }
    if (route.approximated) {
        thread_local unsigned n = 0;
        if (n++ < 30)
            klog_fmt("emuwasm-gl4es: glDrawBuffer FRONT -> BACK (WebGL2 default FBO)");
    }
#else
    GL_FUNC(pglDrawBuffer)(ARG1);
#endif
}

void glcommon_glReadBuffer(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    const int isDefault = emuwasm_gl4es_framebuffer_is_default(1);
    const emuwasm_gl4es::BufferRoute route =
        emuwasm_gl4es::route_buffer(ARG1, isDefault == 1, true);
    if (isDefault < 0 || !route.valid ||
        !emuwasm_gl4es_read_buffer(static_cast<int>(route.hostMode))) {
        emuwasm_gl4es_set_error(cpu, GL_INVALID_ENUM);
        return;
    }
    if (route.approximated) {
        thread_local unsigned n = 0;
        if (n++ < 30)
            klog_fmt("emuwasm-gl4es: glReadBuffer FRONT -> BACK (WebGL2 default FBO)");
    }
#else
    GL_FUNC(pglReadBuffer)(ARG1);
#endif
}
