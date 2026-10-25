"""Unit tests for the fail-closed BoxedWine GL compatibility injection."""
import importlib.util
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest


PATCHER = Path(__file__).with_name("patch_boxedwine_webgl.py")
spec = importlib.util.spec_from_file_location("bw_webgl_patcher", PATCHER)
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)


class WebGLPatchTest(unittest.TestCase):
    def make_source(self, root: Path):
        gl = root / "source/opengl"
        gl.mkdir(parents=True)
        # The patcher must find all 33 generated callbacks and install the
        # corresponding real native/wasm implementations.
        fixed_names = (
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
        fixed_declarations = "".join(
            f'GL_FUNCTION({name}, void, (), (),,,("gl{name}"))\n'
            for name in fixed_names
        )
        (gl / "glfunctions.h").write_text(
            "GL_FUNCTION(TexImage2D, void, (GLenum target), (ARG1), , , (\"glTexImage2D\"))\n"
            "GL_FUNCTION(DrawBuffer, void, (GLenum mode), (ARG1),,,(\"glDrawBuffer\"))\n"
            "GL_FUNCTION(ReadBuffer, void, (GLenum mode), (ARG1),,,(\"glReadBuffer\"))\n"
            + fixed_declarations,
            encoding="utf-8",
        )
        (gl / "glcommon.cpp").write_text(
            "void glcommon_glGetIntegerv(CPU* cpu) {\n"
            "    GLenum pname = ARG1;\n"
            "    if (pname == GL_NUM_EXTENSIONS) {\n"
            "        getFilteredExtensionString(cpu->thread->process.get());\n"
            "        cpu->memory->writed(ARG2, cpu->thread->process->numberOfExtensions);\n"
            "    } else {\n"
            "        MarshalReadWrite<GLint> buffer(cpu, ARG2, getSize(ARG1));\n"
            "        GL_FUNC(pglGetIntegerv)(pname, buffer.getPtr());\n"
            "    }\n"
            "}\n"
            "void glcommon_glGetTexImage(CPU* cpu) {\n"
            "    GL_FUNC(pglGetTexImage)(target, level, format, type, pixels.getPtr());\n"
            "}\n"
            "void glcommon_glTexSubImage2D(CPU* cpu) {\n"
            "    GL_FUNC(pglTexSubImage2D)(target, level, xoffset, "
            "yoffset, width, height, format, type, pixels);\n"
            "}\n"
            "void glcommon_glGetError(CPU* cpu) {\n"
            "    if (cpu->thread->glLastError) {\n"
            "        EAX = cpu->thread->glLastError;\n"
            "        cpu->thread->glLastError = 0;\n"
            "    } else {\n"
            "        EAX = GL_FUNC(pglGetError)();\n"
            "    }\n"
            "}\n"
            "\n#define GLX_USE_GL 1\n"
            "#if defined(BOXEDWINE_OPENGL) && defined(__EMSCRIPTEN__) && defined(BOXEDWINE_WEBGL_COUNTERS)\n"
            "    glCallCounters.record(index);\n"
            "#endif\n"
            "    int99Callback[index](cpu);\n"
            "void callOpenGL(CPU* cpu, U32 index) {\n"
            "#ifdef BOXEDWINE_OPENGL\n"
            "    //KNativeWindow::getNativeWindow()->preOpenGLCall(index);\n",
            encoding="utf-8",
        )
        (gl / "glfunctions_ext1.cpp").write_text(
            "    EAX=GL_FUNC(ext_glCheckFramebufferStatus)(ARG1);\n"
            "    EAX=GL_FUNC(ext_glCheckFramebufferStatusEXT)(ARG1);\n"
            "    GL_FUNC(ext_glFramebufferTexture2D)(ARG1, ARG2, ARG3, ARG4, ARG5);\n"
            "    GL_FUNC(ext_glFramebufferTexture2DEXT)(ARG1, ARG2, ARG3, ARG4, ARG5);\n"
            "    GL_FUNC(ext_glFramebufferRenderbuffer)(ARG1, ARG2, ARG3, ARG4);\n"
            "    GL_FUNC(ext_glFramebufferRenderbufferEXT)(ARG1, ARG2, ARG3, ARG4);\n",
            encoding="utf-8",
        )
        (gl / "glfunctions_ext2.cpp").write_text(
            "void glcommon_glRenderbufferStorage(CPU* cpu) {\n"
            "    GL_FUNC(ext_glRenderbufferStorage)(ARG1, ARG2, ARG3, ARG4);\n"
            "    GL_FUNC(ext_glRenderbufferStorageEXT)(ARG1, ARG2, ARG3, ARG4);\n"
            "    GL_FUNC(ext_glRenderbufferStorageMultisample)(ARG1, ARG2, ARG3, ARG4, ARG5);\n"
            "    GL_FUNC(ext_glRenderbufferStorageMultisampleEXT)(ARG1, ARG2, ARG3, ARG4, ARG5);\n",
            encoding="utf-8",
        )
        (gl / "glCounters.h").write_text(
            "    void record(unsigned index) {\n"
            "        if (index < count) {\n"
            "            values[index].fetch_add(1, std::memory_order_relaxed);\n"
            "        }\n"
            "    }\n",
            encoding="utf-8",
        )
        return gl

    def test_installs_compatibility_and_instrumentation(self):
        with TemporaryDirectory() as temp:
            root = Path(temp)
            gl = self.make_source(root)
            overlay = root / "shim.inc"
            overlay.write_text("// shim fixture\n", encoding="utf-8")
            (root / "emuwasm_gl4es_formats.h").write_text("// mapper fixture\n", encoding="utf-8")
            (root / "emuwasm_gl4es_framebuffer.h").write_text("// fbo fixture\n", encoding="utf-8")
            (root / "emuwasm_gl4es_gl_functions.inc").write_text("// real GL fixture\n", encoding="utf-8")
            (root / "emuwasm_gl4es_gl_functions.h").write_text("// GL helper fixture\n", encoding="utf-8")
            for fixed_name in ("emuwasm_gl4es_fixed_handlers.inc",
                               "emuwasm_gl4es_fixed_pipeline.inc"):
                (root / fixed_name).write_bytes(
                    (PATCHER.parent.parent / "browser_port" / fixed_name).read_bytes()
                )
            patcher.apply(root, overlay)
            self.assertEqual(
                (gl / "emuwasm_webgl_pixel.inc").read_text(encoding="utf-8"),
                "// shim fixture\n",
            )
            self.assertEqual((gl / "emuwasm_gl4es_formats.h").read_text(encoding="utf-8"),
                             "// mapper fixture\n")
            self.assertEqual((gl / "emuwasm_gl4es_framebuffer.h").read_text(encoding="utf-8"),
                             "// fbo fixture\n")
            self.assertEqual((gl / "emuwasm_gl4es_gl_functions.inc").read_text(encoding="utf-8"),
                             "// real GL fixture\n")
            self.assertEqual((gl / "emuwasm_gl4es_gl_functions.h").read_text(encoding="utf-8"),
                             "// GL helper fixture\n")
            callbacks = (gl / "glfunctions.h").read_text(encoding="utf-8")
            self.assertIn("GL_FUNCTION_CUSTOM(TexImage2D", callbacks)
            self.assertIn("GL_FUNCTION_CUSTOM(DrawBuffer", callbacks)
            self.assertIn("GL_FUNCTION_CUSTOM(ReadBuffer", callbacks)
            self.assertEqual(callbacks.count("GL_FUNCTION_CUSTOM("), 38)
            self.assertIn("GL_FUNCTION_CUSTOM(Begin", callbacks)
            self.assertIn("GL_FUNCTION_CUSTOM(End", callbacks)
            self.assertIn("GL_FUNCTION_CUSTOM(LoadMatrixd", callbacks)
            self.assertIn("GL_FUNCTION_CUSTOM(MultMatrixf", callbacks)
            self.assertIn("GL_FUNCTION_CUSTOM(GetFloatv", callbacks)
            self.assertIn("GL_FUNCTION_CUSTOM(GetDoublev", callbacks)
            source = (gl / "glcommon.cpp").read_text(encoding="utf-8")
            self.assertIn('#include "emuwasm_webgl_pixel.inc"', source)
            self.assertIn('#include "emuwasm_gl4es_gl_functions.inc"', source)
            self.assertIn('#include "emuwasm_gl4es_fixed_handlers.inc"', source)
            self.assertIn("emuwasm_gl4es_get_tex_image_rgba8", source)
            self.assertIn("emuwasm_ffp_is_integer_query(pname)", source)
            self.assertIn("emuwasm_ffp_query(pname", source)
            self.assertIn("emuwasm_gl4es::pack_rgba_readback", source)
            self.assertIn("emuwasm_gl4es_set_error(cpu, GL_INVALID_OPERATION)", source)
            self.assertIn("EmuWasmWebGLPixelUpload upload", source)
            self.assertIn("glGetError=0x%x", source)
            self.assertIn("glCallCounters.recordAndIsFirst(index)", source)
            self.assertIn("guest GL first use %s", source)
            self.assertEqual(source.count("glCallCounters.record(index)"), 0)
            self.assertIn("bool recordAndIsFirst(unsigned index)",
                          (gl / "glCounters.h").read_text(encoding="utf-8"))
            fb = (gl / "glfunctions_ext1.cpp").read_text(encoding="utf-8")
            self.assertIn("glCheckFramebufferStatusEXT target=0x%x", fb)
            self.assertIn("emuwasm-webgl: glFramebufferTexture2D target=0x%x", fb)
            self.assertIn("emuwasm-webgl: glFramebufferRenderbuffer target=0x%x", fb)
            storage = (gl / "glfunctions_ext2.cpp").read_text(encoding="utf-8")
            self.assertIn("emuwasm-webgl: glRenderbufferStorage target=0x%x", storage)
            self.assertIn('#include "emuwasm_gl4es_framebuffer.h"', storage)
            self.assertEqual(storage.count("normalize_renderbuffer_format("), 4)
            self.assertIn("static_cast<GLenum>(rbMapped.internal)", storage)
            self.assertIn("emuwasm-gl4es: glRenderbufferStorageMultisample", storage)
            self.assertIn("#else\n    GL_FUNC(ext_glRenderbufferStorage)(ARG1, ARG2, ARG3, ARG4);", storage)
            self.assertIn("glFramebufferTexture2DEXT target=0x%x", fb)
            self.assertIn("glFramebufferRenderbufferEXT target=0x%x", fb)

    def test_repeated_patch_fails_instead_of_silently_replacing(self):
        with TemporaryDirectory() as temp:
            root = Path(temp)
            self.make_source(root)
            overlay = root / "shim.inc"
            overlay.write_text("// shim fixture\n", encoding="utf-8")
            (root / "emuwasm_gl4es_formats.h").write_text("// mapper fixture\n", encoding="utf-8")
            (root / "emuwasm_gl4es_framebuffer.h").write_text("// fbo fixture\n", encoding="utf-8")
            (root / "emuwasm_gl4es_gl_functions.inc").write_text("// real GL fixture\n", encoding="utf-8")
            (root / "emuwasm_gl4es_gl_functions.h").write_text("// GL helper fixture\n", encoding="utf-8")
            for fixed_name in ("emuwasm_gl4es_fixed_handlers.inc",
                               "emuwasm_gl4es_fixed_pipeline.inc"):
                (root / fixed_name).write_bytes(
                    (PATCHER.parent.parent / "browser_port" / fixed_name).read_bytes()
                )
            patcher.apply(root, overlay)
            with self.assertRaisesRegex(RuntimeError, "already patched"):
                patcher.apply(root, overlay)

    def test_missing_source_fails(self):
        with TemporaryDirectory() as temp:
            with self.assertRaisesRegex(RuntimeError, "Not an upstream"):
                patcher.apply(Path(temp), Path(temp) / "missing.inc")


if __name__ == "__main__":
    unittest.main()
