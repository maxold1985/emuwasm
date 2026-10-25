#!/usr/bin/env python3
"""Apply the EmuWasm WebGL2 compatibility shim to an upstream BoxedWine checkout.

The shim operates on the active Emscripten OpenGL marshaller, NOT on the
obsolete BOXEDWINE_ES stub. It never changes guest binaries or Wine archives.
All anchors must match exactly; fail closed when upstream code has changed.
"""
from __future__ import annotations

import argparse
from pathlib import Path


def substitute_once(path: Path, before: str, after: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(before)
    if count != 1:
        raise RuntimeError(f"{path}: expected one anchor, found {count}: {before[:80]!r}")
    path.write_text(text.replace(before, after, 1), encoding="utf-8")


def apply(root: Path, overlay: Path) -> None:
    gl = root / "source/opengl"
    glfunctions = gl / "glfunctions.h"
    glcommon = gl / "glcommon.cpp"
    gl_ext = gl / "glfunctions_ext1.cpp"
    gl_ext2 = gl / "glfunctions_ext2.cpp"
    gl_counters = gl / "glCounters.h"
    if not all(p.is_file() for p in (glfunctions, glcommon, gl_ext, gl_ext2, gl_counters)):
        raise RuntimeError(f"Not an upstream BoxedWine source checkout: {root}")
    if not overlay.is_file():
        raise RuntimeError(f"Missing EmuWasm shim source: {overlay}")

    # Generated GL_FUNCTION invokes glTexImage2D without interception.
    # CUSTOM routes the same guest int99 function to our real C++ handler.
    original = next(
        (line for line in glfunctions.read_text(encoding="utf-8").splitlines()
         if line.startswith("GL_FUNCTION(TexImage2D, void,")),
        None,
    )
    if original is None:
        raise RuntimeError("TexImage2D declaration missing or already patched")
    custom = ("GL_FUNCTION_CUSTOM(TexImage2D, void, "
              "(GLenum target, GLint level, GLint internalFormat, "
              "GLsizei width, GLsizei height, GLint border, GLenum format, "
              "GLenum type, const GLvoid* pixels))")
    substitute_once(glfunctions, original, custom)

    # Ensure this header is compiled at the native and WebAssembly targets.
    # Its compatibility operations are guarded by __EMSCRIPTEN__.
    include = '#include "emuwasm_webgl_pixel.inc"\n\n'
    substitute_once(glcommon, "void glcommon_glTexSubImage2D(CPU* cpu) {",
                    include + "void glcommon_glTexSubImage2D(CPU* cpu) {")

    # Existing TexSubImage2D already repacks non-contiguous rows. The shim
    # runs after that logic, then restores the temporary unpack state.
    original_call = ("    GL_FUNC(pglTexSubImage2D)(target, level, xoffset, "
                     "yoffset, width, height, format, type, pixels);")
    wrapped_call = """#ifdef __EMSCRIPTEN__
    EmuWasmWebGLPixelUpload upload(format, type, pixels);
    upload.prepare(width, height, PIXEL_UNPACK_BUFFER());
    format = upload.format;
    type = upload.type;
    pixels = upload.pixels;
#endif
""" + original_call + """
#ifdef __EMSCRIPTEN__
    upload.restore();
#endif"""
    substitute_once(glcommon, original_call, wrapped_call)

    # Preserve the guest's GL error queue. Only log a non-zero error that
    # glGetError has already consumed, never make extra glGetError calls.
    needle = """    } else {
        EAX = GL_FUNC(pglGetError)();
    }
}

#define GLX_USE_GL"""
    replacement = """    } else {
        EAX = GL_FUNC(pglGetError)();
    }
#ifdef __EMSCRIPTEN__
    if (EAX) {
        static unsigned errorCount = 0;
        if (errorCount++ < 80)
            klog_fmt("emuwasm-webgl: glGetError=0x%x", EAX);
    }
#endif
}

#define GLX_USE_GL"""
    substitute_once(glcommon, needle, replacement)

    needle = "    EAX=GL_FUNC(ext_glCheckFramebufferStatus)(ARG1);"
    replacement = (needle + '\n#ifdef __EMSCRIPTEN__\n'
                   '    if (EAX != 0x8CD5)\n'
                   '        klog_fmt("emuwasm-webgl: glCheckFramebufferStatus '
                   'target=0x%x result=0x%x", ARG1, EAX);\n'
                   '#endif')
    substitute_once(gl_ext, needle, replacement)
    needle = "    EAX=GL_FUNC(ext_glCheckFramebufferStatusEXT)(ARG1);"
    replacement = (needle + '\n#ifdef __EMSCRIPTEN__\n'
                   '    if (EAX != 0x8CD5)\n'
                   '        klog_fmt("emuwasm-webgl: glCheckFramebufferStatusEXT '
                   'target=0x%x result=0x%x", ARG1, EAX);\n'
                   '#endif')
    substitute_once(gl_ext, needle, replacement)

    # The WineD3D framebuffer probes deliberately try unsupported formats.
    # Log the real attachment/storage operations; never fake "complete" status
    # or swallow OpenGL errors, because that would break feature detection.
    def trace_call(file: Path, entrypoint: str, fmt: str, arguments: str) -> None:
        old = f"    GL_FUNC(ext_gl{entrypoint})({arguments});"
        # Arguments are already read from the emulated x86 CPU stack.
        arg_names = arguments
        new = (old + "\n#ifdef __EMSCRIPTEN__\n"
               "    thread_local static unsigned emuwasm_calls = 0;\n"
               "    if (emuwasm_calls++ < 80)\n"
               f'        klog_fmt("emuwasm-webgl: gl{entrypoint} {fmt}", {arg_names});\n'
               "#endif")
        substitute_once(file, old, new)

    trace_call(gl_ext, "FramebufferTexture2D",
               "target=0x%x attach=0x%x texTarget=0x%x texture=%u level=%d",
               "ARG1, ARG2, ARG3, ARG4, ARG5")
    trace_call(gl_ext, "FramebufferRenderbuffer",
               "target=0x%x attach=0x%x rbTarget=0x%x renderbuffer=%u",
               "ARG1, ARG2, ARG3, ARG4")
    trace_call(gl_ext2, "RenderbufferStorage",
               "target=0x%x format=0x%x size=%dx%d",
               "ARG1, ARG2, ARG3, ARG4")

    # Register EVERY guest GL request at the central dispatcher, including
    # requests rejected by WebGL validation or lacking a current GL context.
    # The old hook counted only successfully dispatched callbacks.
    original_record = """#if defined(BOXEDWINE_OPENGL) && defined(__EMSCRIPTEN__) && defined(BOXEDWINE_WEBGL_COUNTERS)
    glCallCounters.record(index);
#endif
    int99Callback[index](cpu);"""
    substitute_once(glcommon, original_record, "    int99Callback[index](cpu);")

    # Atomic fetch_add returns the previous value: exactly one thread reports
    # the first use of a function even in multi-threaded browser builds.
    counter_record = """    void record(unsigned index) {
        if (index < count) {
            values[index].fetch_add(1, std::memory_order_relaxed);
        }
    }"""
    counter_replacement = counter_record + """

    bool recordAndIsFirst(unsigned index) {
        return index < count &&
            values[index].fetch_add(1, std::memory_order_relaxed) == 0;
    }"""
    substitute_once(gl_counters, counter_record, counter_replacement)

    dispatcher_start = """void callOpenGL(CPU* cpu, U32 index) {
#ifdef BOXEDWINE_OPENGL
    //KNativeWindow::getNativeWindow()->preOpenGLCall(index);"""
    dispatcher_replacement = """void callOpenGL(CPU* cpu, U32 index) {
#ifdef BOXEDWINE_OPENGL
#if defined(__EMSCRIPTEN__) && defined(BOXEDWINE_WEBGL_COUNTERS)
    if (index < int99CallbackSize && int99Callback[index]) {
        const bool first = glCallCounters.recordAndIsFirst(index);
        if (first) {
            const char* name = GLCallCounters::name(index);
            klog_fmt("emuwasm-webgl: guest GL first use %s [index=%u]",
                     name ? name : "(unknown)", index);
        }
    }
#endif
    //KNativeWindow::getNativeWindow()->preOpenGLCall(index);"""
    substitute_once(glcommon, dispatcher_start, dispatcher_replacement)

    (gl / "emuwasm_webgl_pixel.inc").write_bytes(overlay.read_bytes())
    print("EmuWasm: OpenGL BGRA/BGR -> WebGL2 pixel shim and GL diagnostics applied")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("boxedwine", type=Path, help="upstream BoxedWine source root")
    parser.add_argument("--overlay", type=Path, default=Path(__file__).resolve().parents[1] /
                        "browser_port/boxedwine_webgl_pixel.inc")
    args = parser.parse_args()
    apply(args.boxedwine.resolve(), args.overlay.resolve())
