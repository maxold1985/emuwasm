# GL4ES compatibility integration (experimental)

This is a **small, source-inspired compatibility subset**, **not** a link of
the complete GL4ES runtime. BoxedWine remains responsible for dispatching
guest OpenGL functions and Emscripten remains responsible for WebGL2.

## Source studied

- GL4ES by ptitSeb and contributors: https://github.com/ptitSeb/gl4es
- `src/gl/texture.c`: classification of desktop texture internal formats,
  including GL_RGB8, GL_RGBA8, GL_BGR and GL_BGRA.
- GL4ES is MIT licensed. The original GL4ES source was not copied into this
  module; the mapping is a new, narrow implementation based on the public
  format classification. Retain this attribution when distributing the code.

## Implemented

- `browser_port/emuwasm_gl4es_formats.h` normalizes legacy 3/4-component
  RGB(A) internal formats and desktop BGR(A) storage formats to sized
  `GL_RGB8` / `GL_RGBA8` **only** for supported unsigned-byte uploads.
- `browser_port/boxedwine_webgl_pixel.inc` performs the pixel conversion and
  invokes the mapper before the Emscripten texture upload.
- `scripts/patch_boxedwine_webgl.py` installs the adapter into the active
  upstream BoxedWine OpenGL dispatcher.
- `scripts/test_gl4es_formats.cpp` runs native C++ regression tests in CI.

## Not yet implemented

This does **not** integrate GL4ES fixed-function pipeline emulation, shader
conversion, GLX, compressed textures, depth/stencil conversion, or framebuffer
attachment emulation. No unsupported GL feature is falsely advertised.

A future full GL4ES integration would need to route BoxedWine's generated
OpenGL entry points into GL4ES's context-bound implementation and test that
GL4ES's GLES2 backend works with Emscripten/WebGL2. Simply linking
`libGL` will not intercept BoxedWine's existing GL marshalling layer.

## Testing

Run the workflow `18 Wheels of Steel browser port (WASM WebGL)`, download
the successful browser artifact and retain `.local-game-data` from your
existing installation. Search the log for `emuwasm-gl4es:` to confirm the
mapping was exercised. Passing CI does not establish that Prism3D runs.
