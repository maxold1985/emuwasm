#!/usr/bin/env python3
"""Apply the EmuWasm WebGL2 compatibility shim to an upstream BoxedWine checkout.

The shim operates on the active Emscripten OpenGL marshaller, NOT on the
obsolete BOXEDWINE_ES stub. It never changes guest binaries or Wine archives.
All anchors must match exactly; fail closed when upstream code has changed.
"""
from __future__ import annotations

import argparse
import re
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
    # Fail before modifying upstream files when either compatibility header is
    # missing. Both modules are independent C++11 headers, not GL4ES binaries.
    mapper = overlay.with_name("emuwasm_gl4es_formats.h")
    fbo_mapper = overlay.with_name("emuwasm_gl4es_framebuffer.h")
    gl_functions = overlay.with_name("emuwasm_gl4es_gl_functions.inc")
    gl_functions_header = overlay.with_name("emuwasm_gl4es_gl_functions.h")
    fixed_handlers = overlay.with_name("emuwasm_gl4es_fixed_handlers.inc")
    fixed_runtime = overlay.with_name("emuwasm_gl4es_fixed_pipeline.inc")
    for required in (mapper, fbo_mapper, gl_functions, gl_functions_header,
                     fixed_handlers, fixed_runtime):
        if not required.is_file():
            raise RuntimeError(f"Missing GL4ES-inspired compatibility header: {required}")

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

    # These are real OpenGL guest handlers, not format translators. The
    # desktop counterparts are absent from WebGL2, so dispatch directly to
    # our WebGL2 drawBuffers/readBuffer/texture-FBO readback implementations.
    for function_name in ("DrawBuffer", "ReadBuffer"):
        original_line = next(
            (line for line in glfunctions.read_text(encoding="utf-8").splitlines()
             if line.startswith(f"GL_FUNCTION({function_name}, void,")),
            None,
        )
        if original_line is None:
            raise RuntimeError(f"{function_name} declaration missing/already patched")
        substitute_once(
            glfunctions, original_line,
            f"GL_FUNCTION_CUSTOM({function_name}, void, (GLenum mode))")

    # Complete first batch of real immediate-mode / fixed-function OpenGL
    # entrypoints. Each custom x86 callback runs a WebGL2 operation or
    # updates state consumed by a genuine WebGL2 drawArrays call.
    # The GL4ES-inspired bridge is intentionally limited; GL lighting/fog
    # are not claimed as implemented.
    fixed_entrypoints = (
        "Begin", "End", "Vertex2f", "Vertex3f", "Vertex4f",
        "Vertex2d", "Vertex3d", "Vertex2i", "Vertex3i",
        "Vertex2s", "Vertex3s", "Color3f", "Color4f",
        "Color3ub", "Color4ub", "TexCoord2f", "TexCoord2d",
        "MatrixMode", "LoadIdentity", "PushMatrix", "PopMatrix",
        "Translatef", "Scalef", "Rotatef", "Ortho", "Frustum",
        "Enable", "Disable", "AlphaFunc",
        "LoadMatrixf", "LoadMatrixd", "MultMatrixf", "MultMatrixd",
        "GetFloatv", "GetDoublev",
    )
    original_declarations = glfunctions.read_text(encoding="utf-8").splitlines()
    for name in fixed_entrypoints:
        original_line = next(
            (line for line in original_declarations
             if line.startswith(f"GL_FUNCTION({name}, void,")),
            None)
        if original_line is None:
            raise RuntimeError(f"Expected GL_FUNCTION({name}) declaration")
        declaration = re.match(
            rf"GL_FUNCTION\({re.escape(name)}, void, (\([^)]*\))",
            original_line)
        if declaration is None:
            raise RuntimeError(f"Cannot parse OpenGL guest ABI for {name}")
        if f"void glcommon_gl{name}(CPU* cpu)" not in fixed_handlers.read_text(
                encoding="utf-8"):
            raise RuntimeError(f"Missing fixed-function implementation for {name}")
        substitute_once(
            glfunctions, original_line,
            f"GL_FUNCTION_CUSTOM({name}, void, {declaration.group(1)})")

    # WineD3D probes hundreds of GL names. For CUSTOM functions the original
    # lookup consults pglFoo, but the new handlers are actual int99 callbacks.
    # Advertise only specifically implemented functions with installed callbacks.
    patched_proc_names = fixed_entrypoints + (
        "DrawBuffer", "ReadBuffer", "GetTexImage",
    )
    proc_checks = "\n".join(
        f'    if (!strcmp(name, "gl{func}")) '
        f'return int99Callback && {func} < int99CallbackSize && '
        f'int99Callback[{func}] != nullptr;'
        for func in patched_proc_names
    )
    proc_source = """#if defined(__EMSCRIPTEN__)
static bool emuwasmHasPatchedGLProc(const char* name) {
""" + proc_checks + """
    return false;
}

#if defined(BOXEDWINE_WEBGL_COUNTERS)
EM_JS(void, emuwasmRecordProcQuery, (const char* requested, int supported), {
    if (!requested || typeof Module === 'undefined')
        return;
    var name = UTF8ToString(requested);
    if (!/^gl[A-Za-z0-9_]{1,96}$/.test(name))
        return;
    var table = Module['emuwasmGLProcQueries'];
    if (!table) {
        table = Object.create(null);
        Module['emuwasmGLProcQueries'] = table;
        Module['emuwasmGLProcQueryUnique'] = 0;
    }
    var row = table[name];
    if (!row) {
        if (Module['emuwasmGLProcQueryUnique'] >= 4096)
            return;
        row = table[name] = {found:0, missing:0};
        Module['emuwasmGLProcQueryUnique']++;
    }
    if (supported)
        row.found++;
    else
        row.missing++;
});
#endif
#endif

"""
    substitute_once(glcommon,
                    "static bool isOpenGLProcAddressAvailable(const char* name) {",
                    proc_source +
                    "static bool isOpenGLProcAddressAvailable(const char* name) {")

    # Do not bypass upstream's unsupported-signature safeguards. The special
    # handling is enabled only when the int99 handler exists in the runtime.
    substitute_once(
        glcommon,
        """    if (boxedwineIsUnsupportedEmscriptenGLProcAddress(name)) {
        return false;
    }
    // This core entry uses our typed WebGL wrapper""",
        """    if (boxedwineIsUnsupportedEmscriptenGLProcAddress(name)) {
        return false;
    }
    if (emuwasmHasPatchedGLProc(name))
        return true;
    // This core entry uses our typed WebGL wrapper"""
    )

    # Keep a bounded count of distinct function names and found/missing
    # results for the existing browser export; no per-call console spam.
    substitute_once(
        glcommon,
        "    EAX = isOpenGLProcAddressAvailable(marshalsz(cpu, ARG1)) ? 1 : 0;",
        """    const char* name = marshalsz(cpu, ARG1);
    EAX = isOpenGLProcAddressAvailable(name) ? 1 : 0;
#if defined(__EMSCRIPTEN__) && defined(BOXEDWINE_WEBGL_COUNTERS)
    emuwasmRecordProcQuery(name, EAX);
#endif"""
    )

    # Put the GL helpers ahead of glGetIntegerv: the desktop matrix-mode
    # query can then consult exactly the same state that our WebGL2 shader
    # pipeline uses, without asking WebGL for unsupported desktop enums.
    substitute_once(glcommon,
                    "void glcommon_glGetIntegerv(CPU* cpu) {",
                    '#include "emuwasm_gl4es_gl_functions.inc"\n'
                    '#include "emuwasm_gl4es_fixed_handlers.inc"\n\n'
                    "void glcommon_glGetIntegerv(CPU* cpu) {")

    get_integer = """    } else {
        MarshalReadWrite<GLint> buffer(cpu, ARG2, getSize(ARG1));
        GL_FUNC(pglGetIntegerv)(pname, buffer.getPtr());
    }
}"""
    get_integer_with_ffp = """#ifdef __EMSCRIPTEN__
    } else if (emuwasm_ffp_is_integer_query(pname)) {
        emuwasm_ffp_report(cpu, emuwasm_ffp_command(0, 0, 0, 0, 0, 0));
        MarshalReadWrite<GLint> buffer(cpu, ARG2, 1);
        if (!emuwasm_ffp_query(pname,
                static_cast<int>(reinterpret_cast<uintptr_t>(buffer.getPtr())), 2))
            emuwasm_gl4es_set_error(cpu, GL_INVALID_OPERATION);
#endif
    } else {
        MarshalReadWrite<GLint> buffer(cpu, ARG2, getSize(ARG1));
        GL_FUNC(pglGetIntegerv)(pname, buffer.getPtr());
    }
}"""
    substitute_once(glcommon, get_integer, get_integer_with_ffp)

    # Ensure this header is compiled at the native and WebAssembly targets.
    # Its compatibility operations are guarded by __EMSCRIPTEN__.
    include = '#include "emuwasm_webgl_pixel.inc"\n\n'
    substitute_once(glcommon, "void glcommon_glTexSubImage2D(CPU* cpu) {",
                    include + "void glcommon_glTexSubImage2D(CPU* cpu) {")

    # glGetTexImage is absent from WebGL2. Read a bound 2D color texture via
    # a temporary framebuffer (real gl.readPixels), then convert / pack data
    # into guest memory using the same BoxedWine marshalling contract.
    teximage_read = "    GL_FUNC(pglGetTexImage)(target, level, format, type, pixels.getPtr());"
    teximage_emulated = """#ifdef __EMSCRIPTEN__
    // Only 2D, unsigned-byte, color-renderable textures are supported here.
    // Unsupported paths must report an error, not pretend that a read worked.
    const std::uint64_t needed = static_cast<std::uint64_t>(width) *
                                 static_cast<std::uint64_t>(height) * 4ull;
    if (target == GL_TEXTURE_2D && level >= 0 &&
        width > 0 && height > 0 && !PIXEL_PACK_BUFFER() &&
        type == GL_UNSIGNED_BYTE &&
        emuwasm_gl4es::pixel_channels(format) != 0 &&
        needed <= 64ull * 1024 * 1024) {
        GLint alignment = 4, rowLength = 0, skipRows = 0, skipPixels = 0;
        GL_FUNC(pglGetIntegerv)(GL_PACK_ALIGNMENT, &alignment);
        GL_FUNC(pglGetIntegerv)(GL_PACK_ROW_LENGTH, &rowLength);
        GL_FUNC(pglGetIntegerv)(GL_PACK_SKIP_ROWS, &skipRows);
        GL_FUNC(pglGetIntegerv)(GL_PACK_SKIP_PIXELS, &skipPixels);
        if ((alignment == 1 || alignment == 2 ||
             alignment == 4 || alignment == 8) &&
            rowLength >= 0 && skipRows >= 0 && skipPixels >= 0) {
            std::vector<U8> rgba(static_cast<size_t>(needed));
            if (emuwasm_gl4es_get_tex_image_rgba8(
                    level, width, height,
                    static_cast<int>(reinterpret_cast<uintptr_t>(rgba.data())),
                    static_cast<int>(needed))) {
                U8* destination = static_cast<U8*>(pixels.getPtr());
                if (emuwasm_gl4es::pack_rgba_readback(
                        rgba.data(), rgba.size(),
                        static_cast<std::uint32_t>(width),
                        static_cast<std::uint32_t>(height),
                        format, static_cast<std::uint32_t>(alignment),
                        static_cast<std::uint32_t>(rowLength),
                        static_cast<std::uint32_t>(skipRows),
                        static_cast<std::uint32_t>(skipPixels),
                        destination, pixels.getByteLength())) {
                    thread_local unsigned readReports = 0;
                    if (readReports++ < 32)
                        klog_fmt("emuwasm-gl4es: glGetTexImage texture2D "
                                 "level=%d size=%dx%d format=0x%x data=%u",
                                 level, width, height, format,
                                 pixels.getByteLength());
                    return;
                }
            }
        }
    }
    emuwasm_gl4es_set_error(cpu, GL_INVALID_OPERATION);
    thread_local unsigned unsupportedReads = 0;
    if (unsupportedReads++ < 32)
        klog_fmt("emuwasm-gl4es: glGetTexImage unsupported target=0x%x "
                 "level=%d size=%dx%d format=0x%x type=0x%x pbo=%d",
                 target, level, width, height, format, type,
                 static_cast<int>(PIXEL_PACK_BUFFER()));
#else
    GL_FUNC(pglGetTexImage)(target, level, format, type, pixels.getPtr());
#endif"""
    substitute_once(glcommon, teximage_read, teximage_emulated)

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
    trace_call(gl_ext2, "RenderbufferStorageEXT",
               "target=0x%x format=0x%x size=%dx%d",
               "ARG1, ARG2, ARG3, ARG4")
    trace_call(gl_ext2, "RenderbufferStorageMultisample",
               "target=0x%x samples=%d format=0x%x size=%dx%d",
               "ARG1, ARG2, ARG3, ARG4, ARG5")
    trace_call(gl_ext2, "RenderbufferStorageMultisampleEXT",
               "target=0x%x samples=%d format=0x%x size=%dx%d",
               "ARG1, ARG2, ARG3, ARG4, ARG5")
    trace_call(gl_ext, "FramebufferTexture2DEXT",
               "target=0x%x attach=0x%x texTarget=0x%x texture=%u level=%d",
               "ARG1, ARG2, ARG3, ARG4, ARG5")
    trace_call(gl_ext, "FramebufferRenderbufferEXT",
               "target=0x%x attach=0x%x rbTarget=0x%x renderbuffer=%u",
               "ARG1, ARG2, ARG3, ARG4")

    # Integrate the GL4ES-inspired renderbuffer format adapter at the four
    # real BoxedWine entry points, rather than faking framebuffer status.
    # WebGL2 accepts explicitly sized color/depth/stencil renderbuffers.
    substitute_once(gl_ext2, "void glcommon_glRenderbufferStorage(CPU* cpu) {",
                    '#ifdef __EMSCRIPTEN__\n'
                    '#include "emuwasm_gl4es_framebuffer.h"\n'
                    '#endif\n\n'
                    "void glcommon_glRenderbufferStorage(CPU* cpu) {")

    for name, call_args in (
        ("RenderbufferStorage", "ARG1, ARG2, ARG3, ARG4"),
        ("RenderbufferStorageEXT", "ARG1, ARG2, ARG3, ARG4"),
        ("RenderbufferStorageMultisample", "ARG1, ARG2, ARG3, ARG4, ARG5"),
        ("RenderbufferStorageMultisampleEXT", "ARG1, ARG2, ARG3, ARG4, ARG5"),
    ):
        original_call = f"    GL_FUNC(ext_gl{name})({call_args});"
        if "Multisample" in name:
            translated_call = (f"    GL_FUNC(ext_gl{name})(ARG1, ARG2, "
                               "static_cast<GLenum>(rbMapped.internal), ARG4, ARG5);")
            input_format = "ARG3"
        else:
            translated_call = (f"    GL_FUNC(ext_gl{name})(ARG1, "
                               "static_cast<GLenum>(rbMapped.internal), ARG3, ARG4);")
            input_format = "ARG2"
        translated = f"""#ifdef __EMSCRIPTEN__
    const emuwasm_gl4es::RenderbufferFormat rbMapped =
        emuwasm_gl4es::normalize_renderbuffer_format({input_format});
    if (rbMapped.changed) {{
        thread_local static unsigned emuwasm_rb_format_logs = 0;
        if (emuwasm_rb_format_logs++ < 80)
            klog_fmt("emuwasm-gl4es: gl{name} format=0x%x -> 0x%x approximate=%d",
                     rbMapped.original, rbMapped.internal,
                     static_cast<int>(rbMapped.approximation));
    }}
{translated_call}
#else
{original_call}
#endif"""
        substitute_once(gl_ext2, original_call, translated)

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
    (gl / "emuwasm_gl4es_formats.h").write_bytes(mapper.read_bytes())
    (gl / "emuwasm_gl4es_framebuffer.h").write_bytes(fbo_mapper.read_bytes())
    (gl / "emuwasm_gl4es_gl_functions.inc").write_bytes(gl_functions.read_bytes())
    (gl / "emuwasm_gl4es_gl_functions.h").write_bytes(gl_functions_header.read_bytes())
    (gl / "emuwasm_gl4es_fixed_handlers.inc").write_bytes(fixed_handlers.read_bytes())
    (gl / "emuwasm_gl4es_fixed_pipeline.inc").write_bytes(fixed_runtime.read_bytes())
    print("EmuWasm: GL4ES-inspired WebGL2 converters, readback and "
          f"{len(fixed_entrypoints)} real fixed-function guest GL callbacks applied")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("boxedwine", type=Path, help="upstream BoxedWine source root")
    parser.add_argument("--overlay", type=Path, default=Path(__file__).resolve().parents[1] /
                        "browser_port/boxedwine_webgl_pixel.inc")
    args = parser.parse_args()
    apply(args.boxedwine.resolve(), args.overlay.resolve())
