"use strict";

/**
 * Local-only Prism3D game launcher, designed for upstream BoxedWine shell.
 * The actual Windows EXE is executed by BoxedWine/Wine WASM, not this JS.
 * Never send user files to an external service or expose archives publicly.
 */
const byId = (id) => document.getElementById(id);
let status = { archives: {}, runtime: {} };
let requestBusy = false;

function mark(id, ok, text) {
    const node = byId(id);
    node.textContent = text;
    node.classList.remove("ok", "bad");
    node.classList.add(ok ? "ok" : "bad");
}

function hasWebAssembly() {
    return typeof WebAssembly === "object" &&
        typeof WebAssembly.instantiate === "function";
}

function hasWebGL() {
    try {
        const canvas = document.createElement("canvas");
        return Boolean(canvas.getContext("webgl2") ||
                       canvas.getContext("webgl") ||
                       canvas.getContext("experimental-webgl"));
    } catch (_) {
        return false;
    }
}

async function loadStatus() {
    const response = await fetch("./api/status", { cache: "no-store" });
    if (!response.ok) {
        throw new Error("O servidor local nao respondeu ao consultar os arquivos.");
    }
    const result = await response.json();
    status = result;
    const wasmReady = ["boxedwine.html", "boxedwine.js", "boxedwine.wasm"]
        .every((name) => Boolean(status.runtime[name]));
    mark("check-runtime", wasmReady,
         wasmReady ? "BoxedWine WebAssembly: arquivos presentes." :
                     "BoxedWine WebAssembly: faltam os arquivos HTML, JS ou WASM.");
    for (const kind of ["wine", "game"]) {
        const item = status.archives[kind];
        const ready = item && item.ready;
        byId(kind + "-state").textContent = ready
            ? "Importado: " + readableBytes(item.bytes) : "Ainda nao importado.";
    }
    syncLaunch();
}

function readableBytes(size) {
    const units = ["B", "KiB", "MiB", "GiB"];
    let value = size;
    let step = 0;
    while (value >= 1024 && step < units.length - 1) {
        value /= 1024;
        step += 1;
    }
    return value.toFixed(step === 0 ? 0 : 1) + " " + units[step];
}

function syncLaunch() {
    const rootReady = status.archives.wine && status.archives.wine.ready;
    const gameReady = status.archives.game && status.archives.game.ready;
    const runtimeReady = ["boxedwine.html", "boxedwine.js", "boxedwine.wasm"]
        .every((name) => status.runtime[name]);
    const supported = hasWebAssembly() && hasWebGL() &&
        location.hostname === "127.0.0.1";
    byId("launch").disabled = requestBusy || !supported ||
        !rootReady || !gameReady || !runtimeReady;
    if (!requestBusy) {
        byId("launch-state").textContent = !supported
            ? "Use Chrome/Edge em " + location.origin + " com WebGL ativo."
            : (!rootReady || !gameReady)
                ? "Importe o ZIP do Wine e o ZIP do jogo."
                : (!runtimeReady)
                    ? "Ainda nao ha runtime WASM neste pacote."
                    : "Arquivos carregados. Pode tentar iniciar.";
    }
}

function uploadZIP(kind, file) {
    if (!file) {
        throw new Error("Selecione primeiro um arquivo .zip.");
    }
    if (!file.name.toLowerCase().endsWith(".zip")) {
        throw new Error("O arquivo precisa ser .zip.");
    }
    if (!Number.isFinite(file.size) || file.size <= 0 ||
        file.size > 3 * (1024 ** 3)) {
        throw new Error("Arquivo vazio ou maior que o limite de 3 GiB.");
    }
    const progress = byId(kind + "-progress");
    progress.hidden = false;
    progress.value = 0;
    byId(kind + "-state").textContent = "Enviando ao servidor local...";
    requestBusy = true;
    syncLaunch();
    return new Promise((resolve, reject) => {
        const xhr = new XMLHttpRequest();
        xhr.open("POST", "./api/upload/" + kind, true);
        xhr.setRequestHeader("Content-Type", "application/zip");
        xhr.timeout = 0; // Local uploads may be large; let the user cancel a tab.
        xhr.upload.addEventListener("progress", (event) => {
            if (event.lengthComputable) {
                progress.value = Math.round(100 * event.loaded / event.total);
                byId(kind + "-state").textContent =
                    "Importando: " + progress.value + "%";
            }
        });
        xhr.addEventListener("load", () => {
            let result = null;
            try {
                result = JSON.parse(xhr.responseText);
            } catch (_) {
                reject(new Error("Resposta invalida do servidor local."));
                return;
            }
            if (xhr.status !== 200 || !result.ok) {
                reject(new Error(result.error || "Falha ao importar arquivo."));
                return;
            }
            resolve(result);
        });
        xhr.addEventListener("error", () => reject(
            new Error("Erro de rede local ao transferir o ZIP.")
        ));
        xhr.addEventListener("abort", () => reject(
            new Error("Upload cancelado.")
        ));
        xhr.send(file);
    }).finally(() => {
        requestBusy = false;
        progress.hidden = true;
        syncLaunch();
    });
}

function buildBoxedwineURL() {
    // These parameters are consumed by the real upstream boxedwine-shell.js:
    // rootBase / root => Wine root ZIP; appBase / app => game ZIP;
    // registry => Wine XP and renderer settings, p => EXE inside ZIP.
    // storage=memory avoids IndexedDB startup hangs; idb persists save data.
    const graphics = byId("graphics").value;
    const resolution = byId("resolution").value;
    const cpu = byId("cpu").value;
    const storage = byId("storage").value;
    if (!["gl", "gdi"].includes(graphics)) {
        throw new Error("Renderizador invalido.");
    }
    if (!["640x480", "800x600", "1024x768", "1280x720"]
        .includes(resolution)) {
        throw new Error("Resolucao invalida.");
    }
    if (!["", "p2", "p3"].includes(cpu)) {
        throw new Error("CPU invalida.");
    }
    if (!["memory", "idb"].includes(storage)) {
        throw new Error("Modo de armazenamento invalido.");
    }
    const target = new URL("./boxedwine.html", location.href);
    const params = target.searchParams;
    params.set("rootBase", "/user-content/");
    params.set("root", "boxedwine");
    params.set("appBase", "/user-content/");
    params.set("app", "prism3d");
    params.set("p", "prism3d.exe");
    params.set("registry", "winxp," + graphics);
    params.set("resolution", resolution);
    params.set("sound", byId("sound").checked ? "true" : "false");
    params.set("auto", "true");
    params.set("desktop", "false");
    params.set("storage", storage);
    params.set("rootfs", storage);
    params.set("storageKey", "18wos-pedal-to-the-metal");
    if (cpu) {
        params.set("cpu", cpu);
    }
    return target.toString();
}

async function start() {
    byId("launch-state").textContent = "Verificando os arquivos do emulador...";
    try {
        await loadStatus();
        if (byId("launch").disabled) {
            throw new Error("Runtime e arquivos do jogo precisam estar presentes.");
        }
        // Explicit user action opens BoxedWine. It may still fail in guest
        // DirectDraw/Direct3D startup: WebGL is not a substitute for Vulkan.
        location.assign(buildBoxedwineURL());
    } catch (error) {
        byId("launch-state").textContent = error.message;
    }
}

function initialize() {
    mark("check-wasm", hasWebAssembly(),
         hasWebAssembly() ? "WebAssembly: disponivel." : "WebAssembly: indisponivel.");
    mark("check-webgl", hasWebGL(),
         hasWebGL() ? "WebGL: disponivel." : "WebGL: indisponivel ou bloqueado.");
    const local = location.hostname === "127.0.0.1";
    mark("check-secure", local, local
        ? "Servidor: loopback privado 127.0.0.1."
        : "Abra a pagina pelo servidor local 127.0.0.1.");
    for (const kind of ["wine", "game"]) {
        byId(kind + "-upload").addEventListener("click", async () => {
            const file = byId(kind + "-file").files[0];
            try {
                const result = await uploadZIP(kind, file);
                const warn = result.warnings.length
                    ? "\nAvisos: " + result.warnings.join("; ") : "";
                byId(kind + "-state").textContent =
                    "OK: " + result.files + " arquivos (" +
                    readableBytes(result.uncompressedBytes) + ")" + warn;
                await loadStatus();
                if (warn) {
                    byId(kind + "-state").textContent += warn;
                }
            } catch (error) {
                byId(kind + "-state").textContent = "Erro: " + error.message;
            }
        });
    }
    byId("launch").addEventListener("click", start);
    loadStatus().catch((error) => {
        mark("check-runtime", false, "Servidor local indisponivel: " + error.message);
        syncLaunch();
    });
}

document.addEventListener("DOMContentLoaded", initialize);
