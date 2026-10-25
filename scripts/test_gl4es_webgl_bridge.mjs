// Test actual Emscripten EM_JS OpenGL implementations using a WebGL2 mock.
// No native OpenGL driver, Emscripten SDK, or game files are required.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { runInNewContext } from "node:vm";

const source = readFileSync(
    new URL("../browser_port/emuwasm_gl4es_gl_functions.inc", import.meta.url),
    "utf8"
);

function extractEMJS(name, args, context) {
    const anchor = "EM_JS(int, " + name + ",";
    const position = source.indexOf(anchor);
    assert.ok(position >= 0, "missing EM_JS: " + name);
    const opening = source.indexOf("), {", position);
    assert.ok(opening >= position);
    const end = source.indexOf("\n});", opening);
    assert.ok(end > opening);
    const body = source.slice(opening + 4, end);
    return runInNewContext(
        "(function(" + args.join(",") + ") {" + body + "})",
        context
    );
}

function newMockGL() {
    const state = {
        read: null, draw: null, readBuffer: 0x0405,
        texture: {texture: "example"},
        packBuffer: null,
        packAlign: 4, rowLength: 2, skipRows: 1, skipPixels: 1,
        complete: true,
    };
    const events = [];
    const gl = {
        BACK: 0x0405,
        COLOR_ATTACHMENT0: 0x8CE0,
        TEXTURE_2D: 0x0DE1,
        TEXTURE_BINDING_2D: 0x8069,
        FRAMEBUFFER_COMPLETE: 0x8CD5,
        READ_FRAMEBUFFER: 0x8CA8,
        DRAW_FRAMEBUFFER: 0x8CA9,
        READ_FRAMEBUFFER_BINDING: 0x8CAA,
        DRAW_FRAMEBUFFER_BINDING: 0x8CA6,
        READ_BUFFER: 0x0C02,
        PACK_ALIGNMENT: 0x0D05,
        PACK_ROW_LENGTH: 0x0D02,
        PACK_SKIP_ROWS: 0x0D03,
        PACK_SKIP_PIXELS: 0x0D04,
        PIXEL_PACK_BUFFER_BINDING: 0x88ED,
        RGBA: 0x1908,
        UNSIGNED_BYTE: 0x1401,
        getParameter(param) {
            const entries = new Map([
                [this.READ_FRAMEBUFFER_BINDING, state.read],
                [this.DRAW_FRAMEBUFFER_BINDING, state.draw],
                [this.READ_BUFFER, state.readBuffer],
                [this.TEXTURE_BINDING_2D, state.texture],
                [this.PIXEL_PACK_BUFFER_BINDING, state.packBuffer],
                [this.PACK_ALIGNMENT, state.packAlign],
                [this.PACK_ROW_LENGTH, state.rowLength],
                [this.PACK_SKIP_ROWS, state.skipRows],
                [this.PACK_SKIP_PIXELS, state.skipPixels],
            ]);
            assert.ok(entries.has(param), "unexpected getParameter 0x" + param.toString(16));
            return entries.get(param);
        },
        drawBuffers(modes) { events.push(["drawBuffers", ...modes]); },
        readBuffer(mode) {
            state.readBuffer = mode;
            events.push(["readBuffer", mode]);
        },
        createFramebuffer() { events.push(["createFramebuffer"]); return { fbo: 7 }; },
        deleteFramebuffer(fbo) { events.push(["deleteFramebuffer", fbo]); },
        bindFramebuffer(target, fbo) {
            events.push(["bindFramebuffer", target, fbo]);
            if (target === this.READ_FRAMEBUFFER) state.read = fbo;
            else if (target === this.DRAW_FRAMEBUFFER) state.draw = fbo;
            else throw Error("unknown framebuffer target");
        },
        framebufferTexture2D(target, attachment, textureTarget, texture, level) {
            events.push(["framebufferTexture2D", target, attachment, textureTarget, texture, level]);
        },
        checkFramebufferStatus() {
            return state.complete ? this.FRAMEBUFFER_COMPLETE : 0x8CD6;
        },
        pixelStorei(param, value) {
            events.push(["pixelStorei", param, value]);
            const destination = new Map([
                [this.PACK_ALIGNMENT, "packAlign"],
                [this.PACK_ROW_LENGTH, "rowLength"],
                [this.PACK_SKIP_ROWS, "skipRows"],
                [this.PACK_SKIP_PIXELS, "skipPixels"],
            ]).get(param);
            assert.ok(destination);
            state[destination] = value;
        },
        readPixels(x, y, width, height, format, type, output) {
            events.push(["readPixels", width, height, format, type]);
            assert.equal(width, 1);
            assert.equal(height, 1);
            assert.equal(output.length, 4);
            output.set([7, 11, 23, 255]);
        },
    };
    return {gl, state, events};
}

const mock = newMockGL();
const heap = new Uint8Array(128);
const warnings = [];
const context = {
    GL: {currentContext: {GLctx: mock.gl}},
    HEAPU8: heap,
    console: {warn: (...args) => warnings.push(args.map(String).join(" "))},
};

const current = extractEMJS(
    "emuwasm_gl4es_framebuffer_is_default", ["read"], context
);
const draw = extractEMJS("emuwasm_gl4es_draw_buffer", ["mode"], context);
const read = extractEMJS("emuwasm_gl4es_read_buffer", ["mode"], context);
const getImage = extractEMJS(
    "emuwasm_gl4es_get_tex_image_rgba8",
    ["level", "width", "height", "out", "size"], context
);

// Real GPU-bound operations must be invoked; a no-op converter is insufficient.
assert.equal(current(0), 1);
assert.equal(current(1), 1);
assert.equal(draw(0x0405), 1);
assert.equal(read(0x0405), 1);
assert.ok(mock.events.some(e => e[0] === "drawBuffers" && e[1] === 0x0405));
assert.ok(mock.events.some(e => e[0] === "readBuffer" && e[1] === 0x0405));

// Actual framebuffer attachment and GPU->WASM readPixels.
assert.equal(getImage(0, 1, 1, 32, 4), 1);
assert.deepEqual([...heap.subarray(32, 36)], [7, 11, 23, 255]);
assert.ok(mock.events.some(e => e[0] === "framebufferTexture2D"));
assert.ok(mock.events.some(e => e[0] === "readPixels"));
assert.ok(mock.events.some(e => e[0] === "deleteFramebuffer"));
assert.equal(mock.state.read, null);
assert.equal(mock.state.draw, null);
assert.equal(mock.state.readBuffer, mock.gl.BACK);
assert.equal(mock.state.packAlign, 4);
assert.equal(mock.state.rowLength, 2);
assert.equal(mock.state.skipRows, 1);
assert.equal(mock.state.skipPixels, 1);

// An incomplete framebuffer never produces a fabricated readback.
mock.state.complete = false;
const before = mock.events.filter(e => e[0] === "readPixels").length;
assert.equal(getImage(0, 1, 1, 36, 4), 0);
assert.equal(mock.events.filter(e => e[0] === "readPixels").length, before);
assert.equal(mock.state.read, null);
assert.equal(mock.state.draw, null);

// PBO readback is unsupported and must not mutate framebuffer state.
mock.state.complete = true;
mock.state.packBuffer = {pbo: 1};
assert.equal(getImage(0, 1, 1, 40, 4), 0);
assert.equal(mock.state.read, null);
assert.equal(warnings.length, 0);
console.log("WebGL2 bridge: real drawBuffers/readBuffer and texture framebuffer readPixels/restore passed.");
