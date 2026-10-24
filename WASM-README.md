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

Carregar as DLLs do Wine incluídas no pacote `emuwasm-wasm-with-wine`:

```bat
node emuwasm_wine_link.js "C:\\caminho\\programa.exe"
```

Execute esse comando na pasta do aplicativo, onde fica
`wine/i386-windows`. O carregador encontra as DLLs automaticamente.
Para utilizar uma pasta externa de DLLs:

```bat
node emuwasm_wine_link.js --wine-dir "C:\\wine\\i386-windows" "C:\\caminho\\programa.exe"
```

Exibir a ajuda do vinculador:

```bat
node emuwasm_wine_link.js --help
```

O workflow `wine-bundle.yml` compila DLLs PE32 i386 a partir do
código-fonte Wine 10.0 e publica um ZIP com a pasta
`wine/i386-windows`. O arquivo `wine/manifest.json` contém a
origem, os nomes e hashes SHA-256 das DLLs.
O pacote antigo `emuwasm-wasm-node` não contém essas DLLs.
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
