// Regression tests for actual guest GL compatibility handlers:
// glDrawBuffer, glReadBuffer, glGetTexImage's CPU-side color packing.
#include "emuwasm_gl4es_gl_functions.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace emuwasm_gl4es;

static void test_buffer_routes() {
    const BufferRoute back = route_buffer(BACK_BUFFER, true, false);
    assert(back.valid && !back.approximated && back.hostMode == BACK_BUFFER);
    const BufferRoute front = route_buffer(FRONT_BUFFER, true, false);
    assert(front.valid && front.approximated && front.hostMode == BACK_BUFFER);
    const BufferRoute frontRead = route_buffer(FRONT_BUFFER, true, true);
    assert(frontRead.valid && frontRead.approximated);
    const BufferRoute frontBackRead =
        route_buffer(FRONT_AND_BACK_BUFFER, true, true);
    assert(!frontBackRead.valid);
    const BufferRoute none = route_buffer(NO_BUFFER, true, false);
    assert(none.valid && none.hostMode == NO_BUFFER);
    const BufferRoute attach = route_buffer(COLOR_ATTACHMENT_ZERO + 2, false, false);
    assert(attach.valid && !attach.approximated &&
           attach.hostMode == COLOR_ATTACHMENT_ZERO + 2);
    assert(!route_buffer(BACK_BUFFER, false, true).valid);
    assert(!route_buffer(COLOR_ATTACHMENT_ZERO, true, false).valid);
    assert(!route_buffer(COLOR_ATTACHMENT_ZERO + 16, false, false).valid);
}

static void test_teximage_readback_packing() {
    // gl.readPixels supplies bottom-to-top RGBA8 rows; no vertical flip.
    const std::uint8_t rgba[] = {
        10, 20, 30, 40,  50, 60, 70, 80,
        90, 100, 110, 120,  130, 140, 150, 160,
    };
    std::vector<std::uint8_t> out(64, 0xEE);
    // BGRA output, 3 pixels/row, skip 1 row + 1 pixel.
    assert(pack_rgba_readback(rgba, sizeof(rgba), 2, 2, BGRA_PIXELS,
                              4, 3, 1, 1, out.data(), out.size()));
    const std::size_t first = 3 * 4 + 4;
    assert(out[first + 0] == 30 && out[first + 1] == 20 &&
           out[first + 2] == 10 && out[first + 3] == 40);
    assert(out[first + 4] == 70 && out[first + 5] == 60 &&
           out[first + 6] == 50 && out[first + 7] == 80);
    assert(out[first + 12] == 110 && out[first + 13] == 100 &&
           out[first + 14] == 90 && out[first + 15] == 120);
    assert(out[0] == 0xEE && out[3] == 0xEE &&
           out[first + 8] == 0xEE);

    // RGB rows use 1-byte alignment with no alpha component in output.
    std::vector<std::uint8_t> rgb(12, 0xAA);
    assert(pack_rgba_readback(rgba, sizeof(rgba), 2, 2, RGB_PIXELS,
                              1, 0, 0, 0, rgb.data(), rgb.size()));
    assert(rgb[0] == 10 && rgb[1] == 20 && rgb[2] == 30);
    assert(rgb[3] == 50 && rgb[4] == 60 && rgb[5] == 70);
    assert(rgb[6] == 90 && rgb[7] == 100 && rgb[8] == 110);
    assert(rgb[9] == 130 && rgb[10] == 140 && rgb[11] == 150);

    // PACK_ALIGNMENT = 4 must leave trailing RGB padding untouched.
    std::vector<std::uint8_t> padded(15, 0xCC);
    assert(pack_rgba_readback(rgba, sizeof(rgba), 1, 2, RGB_PIXELS,
                              4, 0, 0, 0, padded.data(), padded.size()));
    assert(padded[0] == 10 && padded[1] == 20 && padded[2] == 30);
    assert(padded[3] == 0xCC);
    assert(padded[4] == 50 && padded[5] == 60 && padded[6] == 70);
    assert(padded[7] == 0xCC);

    // Bad formats, packing state, and small spans fail without writing.
    std::vector<std::uint8_t> invalid(8, 0xFA);
    assert(!pack_rgba_readback(rgba, sizeof(rgba), 2, 2, 0xDEAD,
                               4, 0, 0, 0, invalid.data(), invalid.size()));
    assert(!pack_rgba_readback(rgba, sizeof(rgba), 2, 2, RGBA_PIXELS,
                               3, 0, 0, 0, invalid.data(), invalid.size()));
    assert(!pack_rgba_readback(rgba, sizeof(rgba), 2, 2, RGBA_PIXELS,
                               4, 0, 0, 0, invalid.data(), invalid.size()));
    assert(!pack_rgba_readback(rgba, 1, 2, 2, RGBA_PIXELS,
                               4, 0, 0, 0, invalid.data(), invalid.size()));
    assert(invalid[0] == 0xFA);
}

int main() {
    test_buffer_routes();
    test_teximage_readback_packing();
    std::puts("Real GL functions: draw/read buffer routing and glGetTexImage pixel packing passed");
}
