// Test genuine URL parameters expected by upstream BoxedWine's shell.
// Node builtins only; no browser framework or proprietary game files.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createContext, runInContext } from "node:vm";

const source = readFileSync(new URL("./port.js", import.meta.url), "utf8");
const nodes = new Map([
    ["graphics", { value: "gl" }],
    ["resolution", { value: "1024x768" }],
    ["cpu", { value: "" }],
    ["sound", { checked: false }],
]);
const document = {
    getElementById(id) {
        const result = nodes.get(id);
        if (!result) {
            throw new Error("Missing test element " + id);
        }
        return result;
    },
    addEventListener() {},
};
const context = {
    document,
    location: {
        href: "http://127.0.0.1:8000/index.html",
        hostname: "127.0.0.1",
    },
    URL,
    URLSearchParams,
    WebAssembly,
    XMLHttpRequest: class {},
};
const vmContext = createContext(context);
runInContext(source, vmContext);
const call = (expr) => runInContext(expr, vmContext);

const url = new URL(call("buildBoxedwineURL();"));
assert.equal(url.pathname, "/boxedwine.html");
const q = url.searchParams;
assert.equal(q.get("rootBase"), "/user-content/");
assert.equal(q.get("root"), "boxedwine");
assert.equal(q.get("appBase"), "/user-content/");
assert.equal(q.get("app"), "prism3d");
assert.equal(q.get("p"), "prism3d.exe");
assert.equal(q.get("registry"), "winxp,gl");
assert.equal(q.get("resolution"), "1024x768");
assert.equal(q.get("sound"), "false");
assert.equal(q.get("storage"), "idb");
assert.equal(q.get("auto"), "true");
assert.equal(q.get("rootfs"), "idb");

nodes.get("graphics").value = "gdi";
nodes.get("sound").checked = true;
nodes.get("cpu").value = "p3";
const compatibilityURL = new URL(call("buildBoxedwineURL();"));
assert.equal(compatibilityURL.searchParams.get("registry"), "winxp,gdi");
assert.equal(compatibilityURL.searchParams.get("sound"), "true");
assert.equal(compatibilityURL.searchParams.get("cpu"), "p3");

nodes.get("graphics").value = "vulkan";
assert.throws(() => call("buildBoxedwineURL();"), /Renderizador invalido/);

console.log("Browser launch parameters verified: Wine ZIP, game ZIP, WinXP, WebGL/GDI, CPU and audio.");
