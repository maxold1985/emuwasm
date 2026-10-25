// Tests the real browser GL counter exporter without loading game files or WebGL.
// Run with: node browser_port/test_gl_usage.mjs
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createContext, runInContext } from "node:vm";

const source = readFileSync(new URL("./gl_usage.js", import.meta.url), "utf8");
const recorded = [
    ["glTexImage2D", 8n],
    ["glGetError", 120n],
    ["glBindFramebuffer", 5n],
    ["unused", 0n],
    ["hostContextChanges", 2n],
    ["mainThreadDispatches", 0n],
];
const module = {
    _boxedwine_gl_counter_version: () => 1,
    _boxedwine_gl_counter_count: () => recorded.length,
    _boxedwine_gl_counter_get: (i) => recorded[i][1],
    ccall(name, type, sig, args) {
        assert.equal(name, "boxedwine_gl_counter_name");
        assert.equal(type, "string");
        assert.equal(sig[0], "number");
        return recorded[args[0]][0];
    },
};
const downloads = [];
const window = {
    Module: module,
    location: { pathname: "/boxedwine.html" },
    setTimeout(callback) { callback(); },
};
const a = {
    href: "",
    download: "",
    click() {
        downloads.push({name: this.download, contents: null});
    },
    remove() {},
};
const document = {
    body: {appendChild() {}},
    createElement(tag) {
        assert.equal(tag, "a");
        return a;
    },
};
const mockedURL = {
    createObjectURL() { return "blob:fake"; },
    revokeObjectURL() {},
};
const vm = createContext({window, document, URL: mockedURL, Blob});
runInContext(source, vm);
const api = window.EmuWasmGLUsage;
assert.ok(api);
const result = api.readCounters();
assert.equal(result.schema, "emuwasm-gl-usage-v1");
assert.equal(result.usedFunctions, 3);
assert.equal(result.totalGuestCalls, "133");
assert.equal(result.functions.length, 4); // includes non-zero hostContextChanges
assert.equal(result.functions[0].name, "glGetError");
assert.equal(result.functions[0].calls, "120");
assert.equal(result.functions[1].name, "glTexImage2D");
assert.ok(api.formatText(result).includes("glBindFramebuffer"));
assert.ok(api.formatText(result).includes("133"));
assert.equal(result.procQueries.requests, 0);

// The exported file must now identify which actual GL function names Wine
// queried and which were rejected, not just glProcAddressAvailable=912.
module.emuwasmGLProcQueries = {
    glBegin: { found: 3, missing: 0 },
    glUnimplementedTextureFeature: { found: 0, missing: 5 },
    glShaderSource: { found: 2, missing: 0 },
    glChangingResult: { found: 1, missing: 1 },
};
const withQueries = api.readCounters();
assert.equal(withQueries.procQueries.requests, 12);
assert.equal(withQueries.procQueries.found, 6);
assert.equal(withQueries.procQueries.missing, 6);
assert.equal(withQueries.procQueries.names, 4);
assert.equal(withQueries.procQueries.entries[0].name, "glUnimplementedTextureFeature");
const probeDump = api.formatText(withQueries);
assert.ok(probeDump.includes("glUnimplementedTextureFeature"));
assert.ok(probeDump.includes("INDISPONIVEL"));
assert.ok(probeDump.includes("glBegin"));
assert.ok(probeDump.includes("MISTO"));
assert.ok(probeDump.includes("Consultas identificadas: 12"));

assert.equal(api.download().usedFunctions, 3);
assert.equal(downloads.length, 1);
assert.equal(downloads[0].name, "emuwasm-gl-functions.txt");

const legacyModule = {
    ...module,
    _boxedwine_gl_counter_get: (i) => i === 1 ? -1 : 0,
};
const legacy = api.readCounters(legacyModule);
assert.equal(legacy.totalGuestCalls, "4294967295");

assert.throws(
    () => api.readCounters({_boxedwine_gl_counter_version() {return 1;}}),
    /WASM sem contadores GL/
);
assert.throws(
    () => api.readCounters({...module, _boxedwine_gl_counter_count: () => 90000}),
    /quantidade invalida/
);
delete window.Module;
assert.throws(() => api.readCounters(), /ainda nao criou o modulo/);
console.log("GL dump verified: every used callback name, frequencies, host counters, file export and missing-runtime errors.");
