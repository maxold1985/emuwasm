# EmuWasm — executáveis WebAssembly para Node.js

Este pacote contém dois programas WebAssembly compilados a partir do C++
do projeto. Os arquivos JavaScript distribuídos são **formatados com recuo
e quebras de linha pelo Prettier**, facilitando a leitura. São códigos
gerados pelo Emscripten, não o código-fonte original do emulador.

**Não é um emulador completo de jogos Windows** e não executa APIs do
Wine nem DirectX.

## Requisitos

- Node.js 20 ou superior (Windows 10, Linux ou macOS).
- O arquivo `.js` e o `.wasm` de cada programa devem permanecer na
  **mesma pasta**.

## Comandos (Windows CMD ou PowerShell)

Inspecionar um arquivo PE32:

```bat
node emuwasm_pe32_inspect.js "C:\caminho\programa.exe"
```

Carregar DLLs PE32 do Wine e verificar imports:

```bat
node emuwasm_wine_link.js --wine-dir "C:\wine\i386-windows" "C:\caminho\programa.exe"
```

Exibir a ajuda do vinculador:

```bat
node emuwasm_wine_link.js --help
```

Use a pasta `i386-windows` com as DLLs PE32 **32 bits** do Wine.
O suporte a `--wine-dir` utiliza o filesystem local do Node.js
(Emscripten NODERAWFS), não o filesystem de um navegador.

## Arquivos

- `emuwasm_pe32_inspect.js` + `emuwasm_pe32_inspect.wasm`:
  analisador de PE32.
- `emuwasm_wine_link.js` + `emuwasm_wine_link.wasm`:
  carregador/vinculador de DLLs PE32 do Wine.

**Limitações:** ainda não há inicialização completa de ntdll, execução
de APIs Windows, BoxedWine CPU no binário WASM, DirectX/WebGPU ou
interface gráfica. A execução real de um `.exe` Windows não é
suportada por estes dois programas.

Código e CI: https://github.com/maxold1985/emuwasm
