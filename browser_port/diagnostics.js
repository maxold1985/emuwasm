/* EmuWasm diagnostic bridge for the upstream BoxedWine HTML/WASM page.
 *
 * Loaded BEFORE fsfs.js, boxedwine-shell.js and Emscripten boxedwine.js.
 * Does not modify the emulated CPU, Wine, filesystem or graphics code.
 * Keeps diagnostics private to the localhost server and the current browser.
 */
(function () {
    "use strict";

    const nativeFetch = window.fetch.bind(window);
    const ring = [];
    const queue = [];
    const MAX_LINES = 300;
    const MAX_PENDING = 64;
    const MAX_ENTRY_CHARS = 950;
    let flushHandle = null;
    let panel = null;
    let output = null;
    let observedGameGet = false;
    let observedWineGet = false;
    let observedInitialSetup = false;

    function asText(value) {
        try {
            if (value instanceof Error) {
                return value.stack || value.message || String(value);
            }
            if (typeof value === "string") {
                return value;
            }
            return JSON.stringify(value);
        } catch (_) {
            return String(value);
        }
    }

    function display() {
        if (!output) return;
        output.textContent = ring.map((x) => x.time + " " + x.level + " " + x.message).join("\n");
        output.scrollTop = output.scrollHeight;
    }

    function flush() {
        flushHandle = null;
        if (queue.length === 0) return;
        const batch = queue.splice(0, 24);
        nativeFetch("/api/browser-log", {
            method: "POST",
            credentials: "same-origin",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ events: batch }),
            cache: "no-store",
            keepalive: false
        }).catch(() => {
            // Keep the in-browser diagnostic panel available if localhost logging fails.
        });
        if (queue.length > 0) {
            flushHandle = window.setTimeout(flush, 500);
        }
    }

    function append(level, message) {
        message = String(message).slice(0, MAX_ENTRY_CHARS);
        const entry = {
            time: new Date().toISOString().slice(11, 23),
            level: String(level),
            message: message
        };
        ring.push(entry);
        if (ring.length > MAX_LINES) ring.shift();
        // Keep bounded logs. Preserve critical failures even when noisy Wine
        // tracing reaches the cap; browser's F12 Console still has full output.
        if (queue.length < MAX_PENDING || level === "error") {
            queue.push(entry);
        }
        if (!flushHandle) flushHandle = window.setTimeout(flush, 450);
        if (message.includes("running initial setup")) observedInitialSetup = true;
        display();
    }

    const originalConsole = {};
    for (const method of ["log", "warn", "error"]) {
        originalConsole[method] = console[method].bind(console);
        console[method] = function (...args) {
            originalConsole[method](...args);
            const message = args.map(asText).join(" ");
            // Do not fill the ring buffer with every low-level x86 instruction.
            if (message.startsWith("trace:") || message.includes("trace:virtual:")) return;
            append(method === "log" ? "info" : method, message);
        };
    }

    window.addEventListener("error", function (event) {
        const loc = event.filename ? (" at " + event.filename + ":" + event.lineno) : "";
        append("error", "JS error: " + (event.message || asText(event.error)) + loc);
    });

    window.addEventListener("unhandledrejection", function (event) {
        append("error", "Promise rejected: " + asText(event.reason));
    });

    function requestLabel(request) {
        const url = typeof request === "string" ? request : (request && request.url || "");
        try {
            const parsed = new URL(url, location.href);
            return parsed.origin === location.origin ? parsed.pathname : "[external URL]";
        } catch (_) {
            return "[invalid URL]";
        }
    }

    function isImportant(path) {
        return path.endsWith(".wasm") || path.endsWith(".zip") ||
            path.includes("/user-content/") || path.includes("/api/status");
    }

    function networkEvent(path, method, status, failure) {
        if (!isImportant(path)) return;
        append(failure ? "error" : "net",
               method + " " + path + ": " + (failure || ("HTTP " + status)));
        if (method === "GET" && path === "/user-content/prism3d.zip") observedGameGet = true;
        if (method === "GET" && path === "/user-content/boxedwine.zip") observedWineGet = true;
    }

    // Instrument fetch requests used to load guest files without modifying
    // their bodies or replacing the Promise returned to upstream BoxedWine.
    window.fetch = function (...args) {
        const path = requestLabel(args[0]);
        const method = (args[1] && args[1].method) ||
            (args[0] && args[0].method) || "GET";
        let promise;
        try {
            promise = nativeFetch(...args);
        } catch (error) {
            networkEvent(path, method, 0, asText(error));
            throw error;
        }
        return promise.then(function (response) {
            networkEvent(path, method, response.status, response.ok ? "" : "HTTP " + response.status);
            return response;
        }, function (error) {
            networkEvent(path, method, 0, asText(error));
            throw error;
        });
    };

    // Older Emscripten versions can use XMLHttpRequest instead of fetch.
    const xhrPrototype = XMLHttpRequest.prototype;
    const originalOpen = xhrPrototype.open;
    const originalSend = xhrPrototype.send;
    xhrPrototype.open = function (method, url, ...rest) {
        this.__emuwasm_diag_request = {
            method: String(method).toUpperCase(),
            path: requestLabel(url)
        };
        return originalOpen.call(this, method, url, ...rest);
    };
    xhrPrototype.send = function (...args) {
        const req = this.__emuwasm_diag_request;
        if (req && isImportant(req.path)) {
            this.addEventListener("loadend", () => {
                networkEvent(req.path, req.method, this.status,
                    this.status >= 200 && this.status < 400 ? "" : "HTTP " + this.status);
            }, { once: true });
        }
        return originalSend.apply(this, args);
    };

    function downloadLogs() {
        const data = [
            "EmuWasm Browser diagnostics",
            "Page: " + location.pathname,
            "Browser: " + navigator.userAgent,
            "Cross-origin isolated: " + String(window.crossOriginIsolated),
            "Game ZIP requested by BoxedWine: " + observedGameGet,
            "Wine ZIP requested by BoxedWine: " + observedWineGet,
            "",
            ...ring.map(x => x.time + " " + x.level + " " + x.message)
        ].join("\n");
        const blob = new Blob([data], { type: "text/plain;charset=utf-8" });
        const url = URL.createObjectURL(blob);
        const anchor = document.createElement("a");
        anchor.href = url;
        anchor.download = "emuwasm-browser-diagnostics.txt";
        document.body.appendChild(anchor);
        anchor.click();
        anchor.remove();
        window.setTimeout(() => URL.revokeObjectURL(url), 5000);
    }

    function setupPanel() {
        if (panel) return;
        const style = document.createElement("style");
        style.textContent = [
            "#emuwasm-debug-panel{position:fixed;right:10px;bottom:10px;z-index:2147483647;",
            "width:min(520px,calc(100vw - 20px));max-height:65vh;box-shadow:0 10px 35px #0009;",
            "background:#101d2a;color:#e4edf7;border:1px solid #52758b;border-radius:8px;",
            "font:12px/1.5 system-ui,sans-serif}",
            "#emuwasm-debug-panel summary{padding:9px 12px;cursor:pointer;font-weight:700;",
            "background:#203c52;border-radius:8px}",
            "#emuwasm-debug-panel .body{padding:10px}",
            "#emuwasm-debug-panel pre{white-space:pre-wrap;word-break:break-word;",
            "overflow:auto;height:180px;max-height:40vh;background:#081421;",
            "color:#b8d5ed;padding:8px;font:11px/1.45 Consolas,monospace}",
            "#emuwasm-debug-panel button,#emuwasm-debug-panel a{display:inline-block;margin-right:8px;",
            "padding:6px 9px;background:#286782;color:white;border:0;border-radius:4px;cursor:pointer}",
            "#emuwasm-debug-panel .hint{margin:8px 0;color:#d0deea}"
        ].join("");
        document.head.appendChild(style);
        panel = document.createElement("details");
        panel.id = "emuwasm-debug-panel";
        panel.open = true;
        const summary = document.createElement("summary");
        summary.textContent = "EmuWasm · diagnóstico de inicialização";
        const body = document.createElement("div");
        body.className = "body";
        const intro = document.createElement("p");
        intro.className = "hint";
        intro.textContent =
            "Se a tela estiver preta ou parada, baixe o diagnóstico. " +
            "O terminal do servidor mostra somente requisições HTTP.";
        const button = document.createElement("button");
        button.type = "button";
        button.textContent = "Baixar log";
        button.addEventListener("click", downloadLogs);
        const retryRAM = document.createElement("button");
        retryRAM.type = "button";
        retryRAM.textContent = "Reiniciar sem IndexedDB";
        retryRAM.title = "Usa apenas RAM; alteracoes e saves nao ficam persistidos";
        retryRAM.addEventListener("click", function () {
            const destination = new URL(location.href);
            destination.searchParams.set("storage", "memory");
            destination.searchParams.set("rootfs", "memory");
            location.assign(destination.toString());
        });
        const back = document.createElement("a");
        back.href = "/";
        back.textContent = "Voltar ao lançador";
        output = document.createElement("pre");
        output.setAttribute("aria-live", "polite");
        body.append(intro, button, retryRAM, back, output);
        panel.append(summary, body);
        document.body.appendChild(panel);
        display();
    }

    async function checkZIP(kind, path) {
        try {
            const response = await nativeFetch(path, {
                method: "HEAD", cache: "no-store", credentials: "same-origin"
            });
            append(response.ok ? "info" : "error",
                   kind + " ZIP: HEAD " + response.status +
                   (response.ok ? " (" + (response.headers.get("Content-Length") || "?") + " bytes)" : ""));
        } catch (error) {
            append("error", kind + " ZIP: " + asText(error));
        }
    }

    function onReady() {
        setupPanel();
        append("info", "Browser: " + navigator.userAgent.slice(0, 180));
        append("info", "crossOriginIsolated=" + window.crossOriginIsolated +
               ", SharedArrayBuffer=" + (typeof SharedArrayBuffer !== "undefined") +
               ", WebAssembly=" + (typeof WebAssembly !== "undefined"));
        // HEAD only: these do not load the full multi-hundred-megabyte game.
        checkZIP("Wine", "/user-content/boxedwine.zip");
        checkZIP("Prism3D", "/user-content/prism3d.zip");
        window.setTimeout(function () {
            if (!observedGameGet && !observedWineGet) {
                append("warn",
                    "Sem GET dos arquivos ZIP apos 15 s. " +
                    (observedInitialSetup
                        ? "O BoxedWine chegou ao setup inicial, mas nao iniciou o carregamento dos ZIPs."
                        : "O setup inicial nao apareceu; confira erros JS/WASM acima."));
            }
        }, 15000);
    }
    if (document.readyState === "loading") {
        document.addEventListener("DOMContentLoaded", onReady, { once: true });
    } else {
        onReady();
    }
    append("info", "Ponte de diagnostico do EmuWasm instalada antes do BoxedWine.");
}());
