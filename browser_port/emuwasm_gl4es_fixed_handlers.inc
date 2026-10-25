/*
 * C++ x86 BoxedWine GL callback wrappers for real WebGL2 fixed-function
 * emulation. Each handler preserves the existing guest CPU ABI (ARG/fARG/
 * dARG marshalling); non-Emscripten builds retain native desktop GL calls.
 *
 * Generated with explicit signatures to keep native/wasm paths equivalent.
 */
#include "emuwasm_gl4es_fixed_pipeline.inc"

#ifdef __EMSCRIPTEN__
static void emuwasm_ffp_report(CPU* cpu, int result) {
    if (result)
        emuwasm_gl4es_set_error(cpu, static_cast<GLenum>(result));
}
#endif

void glcommon_glBegin(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    emuwasm_ffp_report(cpu, emuwasm_ffp_command(1, ARG1, 0, 0, 0, 0));
#else
    GL_FUNC(pglBegin)(ARG1);
#endif
}

void glcommon_glEnd(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    emuwasm_ffp_report(cpu, emuwasm_ffp_command(2, 0, 0, 0, 0, 0));
#else
    GL_FUNC(pglEnd)();
#endif
}

void glcommon_glVertex2f(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    emuwasm_ffp_report(cpu, emuwasm_ffp_command(3, fARG1, fARG2, 0, 1, 0));
#else
    GL_FUNC(pglVertex2f)(fARG1, fARG2);
#endif
}

void glcommon_glVertex3f(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    emuwasm_ffp_report(cpu, emuwasm_ffp_command(3, fARG1, fARG2, fARG3, 1, 0));
#else
    GL_FUNC(pglVertex3f)(fARG1, fARG2, fARG3);
#endif
}

void glcommon_glVertex4f(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    emuwasm_ffp_report(cpu, emuwasm_ffp_command(3, fARG1, fARG2, fARG3, fARG4, 0));
#else
    GL_FUNC(pglVertex4f)(fARG1, fARG2, fARG3, fARG4);
#endif
}

void glcommon_glVertex2d(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    emuwasm_ffp_report(cpu, emuwasm_ffp_command(3, dARG1, dARG3, 0, 1, 0));
#else
    GL_FUNC(pglVertex2d)(dARG1, dARG3);
#endif
}

void glcommon_glVertex3d(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    emuwasm_ffp_report(cpu, emuwasm_ffp_command(3, dARG1, dARG3, dARG5, 1, 0));
#else
    GL_FUNC(pglVertex3d)(dARG1, dARG3, dARG5);
#endif
}

void glcommon_glVertex2i(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    emuwasm_ffp_report(cpu, emuwasm_ffp_command(3, static_cast<GLint>(ARG1), static_cast<GLint>(ARG2), 0, 1, 0));
#else
    GL_FUNC(pglVertex2i)(ARG1, ARG2);
#endif
}

void glcommon_glVertex3i(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    emuwasm_ffp_report(cpu, emuwasm_ffp_command(3, static_cast<GLint>(ARG1), static_cast<GLint>(ARG2), static_cast<GLint>(ARG3), 1, 0));
#else
    GL_FUNC(pglVertex3i)(ARG1, ARG2, ARG3);
#endif
}

void glcommon_glVertex2s(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    emuwasm_ffp_report(cpu, emuwasm_ffp_command(3, static_cast<GLshort>(ARG1), static_cast<GLshort>(ARG2), 0, 1, 0));
#else
    GL_FUNC(pglVertex2s)(ARG1, ARG2);
#endif
}

void glcommon_glVertex3s(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    emuwasm_ffp_report(cpu, emuwasm_ffp_command(3, static_cast<GLshort>(ARG1), static_cast<GLshort>(ARG2), static_cast<GLshort>(ARG3), 1, 0));
#else
    GL_FUNC(pglVertex3s)(ARG1, ARG2, ARG3);
#endif
}

void glcommon_glColor3f(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    emuwasm_ffp_report(cpu, emuwasm_ffp_command(4, fARG1, fARG2, fARG3, 1, 0));
#else
    GL_FUNC(pglColor3f)(fARG1, fARG2, fARG3);
#endif
}

void glcommon_glColor4f(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    emuwasm_ffp_report(cpu, emuwasm_ffp_command(4, fARG1, fARG2, fARG3, fARG4, 0));
#else
    GL_FUNC(pglColor4f)(fARG1, fARG2, fARG3, fARG4);
#endif
}

void glcommon_glColor3ub(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    emuwasm_ffp_report(cpu, emuwasm_ffp_command(4, static_cast<GLubyte>(ARG1)/255.0, static_cast<GLubyte>(ARG2)/255.0, static_cast<GLubyte>(ARG3)/255.0, 1, 0));
#else
    GL_FUNC(pglColor3ub)(ARG1, ARG2, ARG3);
#endif
}

void glcommon_glColor4ub(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    emuwasm_ffp_report(cpu, emuwasm_ffp_command(4, static_cast<GLubyte>(ARG1)/255.0, static_cast<GLubyte>(ARG2)/255.0, static_cast<GLubyte>(ARG3)/255.0, static_cast<GLubyte>(ARG4)/255.0, 0));
#else
    GL_FUNC(pglColor4ub)(ARG1, ARG2, ARG3, ARG4);
#endif
}

void glcommon_glTexCoord2f(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    emuwasm_ffp_report(cpu, emuwasm_ffp_command(5, fARG1, fARG2, 0, 0, 0));
#else
    GL_FUNC(pglTexCoord2f)(fARG1, fARG2);
#endif
}

void glcommon_glTexCoord2d(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    emuwasm_ffp_report(cpu, emuwasm_ffp_command(5, dARG1, dARG3, 0, 0, 0));
#else
    GL_FUNC(pglTexCoord2d)(dARG1, dARG3);
#endif
}

void glcommon_glMatrixMode(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    emuwasm_ffp_report(cpu, emuwasm_ffp_command(6, ARG1, 0, 0, 0, 0));
#else
    GL_FUNC(pglMatrixMode)(ARG1);
#endif
}

void glcommon_glLoadIdentity(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    emuwasm_ffp_report(cpu, emuwasm_ffp_command(7, 0, 0, 0, 0, 0));
#else
    GL_FUNC(pglLoadIdentity)();
#endif
}

void glcommon_glPushMatrix(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    emuwasm_ffp_report(cpu, emuwasm_ffp_command(8, 0, 0, 0, 0, 0));
#else
    GL_FUNC(pglPushMatrix)();
#endif
}

void glcommon_glPopMatrix(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    emuwasm_ffp_report(cpu, emuwasm_ffp_command(9, 0, 0, 0, 0, 0));
#else
    GL_FUNC(pglPopMatrix)();
#endif
}

void glcommon_glTranslatef(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    emuwasm_ffp_report(cpu, emuwasm_ffp_command(10, fARG1, fARG2, fARG3, 0, 0));
#else
    GL_FUNC(pglTranslatef)(fARG1, fARG2, fARG3);
#endif
}

void glcommon_glScalef(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    emuwasm_ffp_report(cpu, emuwasm_ffp_command(11, fARG1, fARG2, fARG3, 0, 0));
#else
    GL_FUNC(pglScalef)(fARG1, fARG2, fARG3);
#endif
}

void glcommon_glRotatef(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    emuwasm_ffp_report(cpu, emuwasm_ffp_command(12, fARG1, fARG2, fARG3, fARG4, 0));
#else
    GL_FUNC(pglRotatef)(fARG1, fARG2, fARG3, fARG4);
#endif
}

void glcommon_glAlphaFunc(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    emuwasm_ffp_report(cpu, emuwasm_ffp_command(17, ARG1, fARG2, 0, 0, 0));
#else
    GL_FUNC(pglAlphaFunc)(ARG1, fARG2);
#endif
}

void glcommon_glEnable(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    if (ARG1 == GL_TEXTURE_2D || ARG1 == GL_ALPHA_TEST) {
        const int opcode = ARG1 == GL_TEXTURE_2D ? 15 : 16;
        emuwasm_ffp_report(cpu, emuwasm_ffp_command(opcode, 1, 0, 0, 0, 0));
        return;
    }
#endif
    GL_FUNC(pglEnable)(ARG1);
}

void glcommon_glDisable(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    if (ARG1 == GL_TEXTURE_2D || ARG1 == GL_ALPHA_TEST) {
        const int opcode = ARG1 == GL_TEXTURE_2D ? 15 : 16;
        emuwasm_ffp_report(cpu, emuwasm_ffp_command(opcode, 0, 0, 0, 0, 0));
        return;
    }
#endif
    GL_FUNC(pglDisable)(ARG1);
}

void glcommon_glOrtho(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    // GLdouble arguments are passed as 64-bit floats on the x86 guest stack.
    alignas(8) double parameters[6] = {dARG1, dARG3, dARG5, dARG7, dARG9, dARG11};
    emuwasm_ffp_report(cpu,
        emuwasm_ffp_command(13, 0, 0, 0, 0,
            static_cast<int>(reinterpret_cast<uintptr_t>(parameters))));
#else
    GL_FUNC(pglOrtho)(dARG1, dARG3, dARG5, dARG7, dARG9, dARG11);
#endif
}

void glcommon_glFrustum(CPU* cpu) {
#ifdef __EMSCRIPTEN__
    // GLdouble arguments are passed as 64-bit floats on the x86 guest stack.
    alignas(8) double parameters[6] = {dARG1, dARG3, dARG5, dARG7, dARG9, dARG11};
    emuwasm_ffp_report(cpu,
        emuwasm_ffp_command(14, 0, 0, 0, 0,
            static_cast<int>(reinterpret_cast<uintptr_t>(parameters))));
#else
    GL_FUNC(pglFrustum)(dARG1, dARG3, dARG5, dARG7, dARG9, dARG11);
#endif
}

void glcommon_glLoadMatrixf(CPU* cpu) {
    const GLfloat* data = marshalArray<GLfloat>(cpu, ARG1, 16);
#ifdef __EMSCRIPTEN__
    emuwasm_ffp_report(cpu,
        emuwasm_ffp_command(18, 1, 0, 0, 0,
            static_cast<int>(reinterpret_cast<uintptr_t>(data))));
#else
    GL_FUNC(pglLoadMatrixf)(data);
#endif
}

void glcommon_glLoadMatrixd(CPU* cpu) {
    const GLdouble* data = marshalArray<GLdouble>(cpu, ARG1, 16);
#ifdef __EMSCRIPTEN__
    emuwasm_ffp_report(cpu,
        emuwasm_ffp_command(18, 0, 0, 0, 0,
            static_cast<int>(reinterpret_cast<uintptr_t>(data))));
#else
    GL_FUNC(pglLoadMatrixd)(data);
#endif
}

void glcommon_glMultMatrixf(CPU* cpu) {
    const GLfloat* data = marshalArray<GLfloat>(cpu, ARG1, 16);
#ifdef __EMSCRIPTEN__
    emuwasm_ffp_report(cpu,
        emuwasm_ffp_command(19, 1, 0, 0, 0,
            static_cast<int>(reinterpret_cast<uintptr_t>(data))));
#else
    GL_FUNC(pglMultMatrixf)(data);
#endif
}

void glcommon_glMultMatrixd(CPU* cpu) {
    const GLdouble* data = marshalArray<GLdouble>(cpu, ARG1, 16);
#ifdef __EMSCRIPTEN__
    emuwasm_ffp_report(cpu,
        emuwasm_ffp_command(19, 0, 0, 0, 0,
            static_cast<int>(reinterpret_cast<uintptr_t>(data))));
#else
    GL_FUNC(pglMultMatrixd)(data);
#endif
}



// Desktop matrix/color queries must reflect our emulated fixed-function state.
static bool emuwasm_ffp_is_float_query(GLenum pname) {
    return pname == 0x0BA6 || pname == 0x0BA7 || pname == 0x0BA8 ||
           pname == 0x0B00 || pname == 0x0B03 || pname == 0x0BA0 ||
           pname == 0x0BA3 || pname == 0x0BA4 || pname == 0x0BA5;
}

static bool emuwasm_ffp_is_integer_query(GLenum pname) {
    return pname == 0x0BA0 || pname == 0x0BA3 ||
           pname == 0x0BA4 || pname == 0x0BA5;
}

static U32 emuwasm_ffp_query_components(GLenum pname) {
    if (pname == 0x0BA6 || pname == 0x0BA7 || pname == 0x0BA8)
        return 16;
    if (pname == 0x0B00 || pname == 0x0B03)
        return 4;
    return 1;
}

void glcommon_glGetFloatv(CPU* cpu) {
    const GLenum pname = ARG1;
#ifdef __EMSCRIPTEN__
    if (emuwasm_ffp_is_float_query(pname)) {
        emuwasm_ffp_report(cpu, emuwasm_ffp_command(0, 0, 0, 0, 0, 0));
        MarshalReadWrite<GLfloat> data(cpu, ARG2,
                                      emuwasm_ffp_query_components(pname));
        if (!emuwasm_ffp_query(pname,
                static_cast<int>(reinterpret_cast<uintptr_t>(data.getPtr())), 0))
            emuwasm_gl4es_set_error(cpu, GL_INVALID_OPERATION);
        return;
    }
#endif
    MarshalReadWrite<GLfloat> data(cpu, ARG2, getSize(pname));
    GL_FUNC(pglGetFloatv)(pname, data.getPtr());
}

void glcommon_glGetDoublev(CPU* cpu) {
    const GLenum pname = ARG1;
#ifdef __EMSCRIPTEN__
    if (emuwasm_ffp_is_float_query(pname)) {
        emuwasm_ffp_report(cpu, emuwasm_ffp_command(0, 0, 0, 0, 0, 0));
        MarshalReadWrite<GLdouble> data(cpu, ARG2,
                                       emuwasm_ffp_query_components(pname));
        if (!emuwasm_ffp_query(pname,
                static_cast<int>(reinterpret_cast<uintptr_t>(data.getPtr())), 1))
            emuwasm_gl4es_set_error(cpu, GL_INVALID_OPERATION);
        return;
    }
#endif
    MarshalReadWrite<GLdouble> data(cpu, ARG2, getSize(pname));
    GL_FUNC(pglGetDoublev)(pname, data.getPtr());
}
