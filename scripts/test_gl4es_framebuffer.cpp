// GL4ES-inspired renderbuffer storage mapping regression tests.
// No WebGL context or game files are needed to test the pure decision logic.
#include "emuwasm_gl4es_framebuffer.h"
#include <cassert>
#include <cstdio>

using namespace emuwasm_gl4es;

static void expect(uint32_t input, uint32_t expected, bool approximation) {
    const RenderbufferFormat mapped = normalize_renderbuffer_format(input);
    assert(mapped.original == input);
    assert(mapped.internal == expected);
    assert(mapped.changed == (input != expected));
    assert(mapped.approximation == approximation);
}

int main() {
    // WebGL2 sized formats for formerly unsized desktop renderbuffers.
    expect(RB_RGB, RB_RGB8, false);
    expect(RB_RGBA, RB_RGBA8, false);
    expect(RB_DEPTH_COMPONENT, RB_DEPTH_COMPONENT16, false);
    expect(RB_DEPTH_STENCIL, RB_DEPTH24_STENCIL8, false);
    expect(RB_STENCIL_INDEX, RB_STENCIL_INDEX8, false);

    // Precision or channel-count differences must remain visible in logs.
    expect(RB_RGB5, RB_RGB565, true);
    expect(RB_RGB332, RB_RGB565, true);
    expect(RB_RGB4, RB_RGBA4, true);
    expect(RB_DEPTH_COMPONENT32, RB_DEPTH_COMPONENT24, true);

    // Existing WebGL2-sized formats and unrecognized formats remain unchanged.
    expect(RB_RGB8, RB_RGB8, false);
    expect(RB_RGBA8, RB_RGBA8, false);
    expect(RB_RGB565, RB_RGB565, false);
    expect(RB_DEPTH_COMPONENT24, RB_DEPTH_COMPONENT24, false);
    expect(RB_DEPTH24_STENCIL8, RB_DEPTH24_STENCIL8, false);
    expect(0x8CAC, 0x8CAC, false); // DEPTH_COMPONENT32F is supported by WebGL2.
    expect(0xDEADBEEF, 0xDEADBEEF, false);

    std::puts("GL4ES-inspired framebuffer mapping: 16 cases passed");
    return 0;
}
