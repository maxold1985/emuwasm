/* Dump all guest GL callback names and counts from BoxedWine WEBGL_COUNTERS.
 * Does not create a WebGL context or alter renderer state.
 */
(function (root) {
    "use strict";
    const MAX_COUNTERS = 65536;
    const MAX_U64 = (1n << 64n) - 1n;

    function getModule() {
        const module = root.Module;
        if (!module) throw new Error("BoxedWine ainda nao criou o modulo WASM.");
        return module;
    }

    function readCounters(module = getModule()) {
        if (!module || typeof module !== "object")
            throw new Error("Modulo BoxedWine invalido.");
        for (const name of [
            "_boxedwine_gl_counter_version",
            "_boxedwine_gl_counter_count",
            "_boxedwine_gl_counter_get",
            "ccall",
        ]) {
            if (typeof module[name] !== "function")
                throw new Error("WASM sem contadores GL: falta " + name +
                    ". Compile com WEBGL_COUNTERS=1.");
        }
        const version = module._boxedwine_gl_counter_version();
        const count = module._boxedwine_gl_counter_count();
        if (version !== 1 || !Number.isInteger(count) ||
                count < 1 || count > MAX_COUNTERS)
            throw new Error("Versao ou quantidade invalida de contadores GL.");

        const functions = [];
        let totalCalls = 0n;
        for (let index = 0; index < count; index++) {
            const raw = module._boxedwine_gl_counter_get(index);
            let calls;
            if (typeof raw === "bigint") {
                calls = BigInt.asUintN(64, raw);
            } else if (typeof raw === "number" && Number.isSafeInteger(raw)) {
                // A legacy wasm32 i64 result may be truncated to 32 bits.
                calls = BigInt(raw < 0 ? raw >>> 0 : raw);
            } else {
                throw new Error("Formato de contador GL nao suportado: indice " + index);
            }
            if (calls === 0n) continue;
            if (calls > MAX_U64) throw new Error("Contador GL fora do uint64.");
            const returnedName = module.ccall(
                "boxedwine_gl_counter_name", "string", ["number"], [index]
            );
            if (returnedName !== null && typeof returnedName !== "string")
                throw new Error("Nome GL invalido no indice " + index);
            const name = returnedName || "int99_" + index;
            functions.push({ index, name, calls: calls.toString() });
            if (name !== "hostContextChanges" && name !== "mainThreadDispatches")
                totalCalls += calls;
        }
        functions.sort((a, b) => {
            const aa = BigInt(a.calls);
            const bb = BigInt(b.calls);
            return aa === bb ? a.name.localeCompare(b.name) : aa > bb ? -1 : 1;
        });
        return {
            schema: "emuwasm-gl-usage-v1",
            generatedAt: new Date().toISOString(),
            page: root.location && root.location.pathname || "",
            scope: "Chamadas recebidas pelo dispatcher OpenGL/EGL/GLX do BoxedWine. " +
                "Nao inclui chamadas internas WebGL do Emscripten nem da GPU. " +
                "Contadores cumulativos medem tentativas, nao conclusoes GPU.",
            counterSlots: count,
            usedFunctions: functions.filter(
                f => f.name !== "hostContextChanges" &&
                     f.name !== "mainThreadDispatches"
            ).length,
            totalGuestCalls: totalCalls.toString(),
            functions,
        };
    }

    function formatText(snapshot) {
        return [
            "EmuWasm - todas as funcoes GL utilizadas",
            "Gerado: " + snapshot.generatedAt,
            "Total de chamadas convidado: " + snapshot.totalGuestCalls,
            "Funcoes utilizadas: " + snapshot.usedFunctions,
            "Escopo: " + snapshot.scope,
            "",
            "Chamadas       | Funcao OpenGL",
            "---------------+------------------------------------------",
            ...snapshot.functions.map(
                f => f.calls.padStart(14) + " | " + f.name + " [id=" + f.index + "]"
            ),
            "",
        ].join("\n");
    }

    function downloadText(text) {
        const blob = new Blob([text], {type: "text/plain;charset=utf-8"});
        const url = URL.createObjectURL(blob);
        try {
            const a = document.createElement("a");
            a.href = url;
            a.download = "emuwasm-gl-functions.txt";
            document.body.appendChild(a);
            a.click();
            a.remove();
        } finally {
            root.setTimeout(() => URL.revokeObjectURL(url), 5000);
        }
    }

    root.EmuWasmGLUsage = Object.freeze({
        readCounters,
        formatText,
        download() {
            const snapshot = readCounters();
            downloadText(formatText(snapshot));
            return snapshot;
        },
    });
})(window);
