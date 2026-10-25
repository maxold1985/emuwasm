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
- `browser_port/emuwasm_gl4es_framebuffer.h` normalizes a limited set of
  legacy renderbuffer internal formats to WebGL2-sized RGB(A), depth, stencil
  and depth-stencil formats. It is inspired by GL4ES `src/gl/framebuffers.c`,
  specifically `gl4es_glRenderbufferStorage`.
- `scripts/patch_boxedwine_webgl.py` intercepts real BoxedWine
  `glRenderbufferStorage`, `glRenderbufferStorageEXT`,
  `glRenderbufferStorageMultisample`, and their EXT variant. It does not
  change the sample count or force framebuffer completeness.
- `scripts/test_gl4es_framebuffer.cpp` verifies 16 format conversions and
  no-op cases, including flagging approximations.

## Renderbuffer compatibility

| Guest internal format | WebGL2 renderbuffer format | Notes |
| --- | --- | --- |
| GL_RGB / GL_RGBA | GL_RGB8 / GL_RGBA8 | Sized equivalents |
| GL_DEPTH_COMPONENT | GL_DEPTH_COMPONENT16 | Explicit depth storage |
| GL_DEPTH_STENCIL | GL_DEPTH24_STENCIL8 | Combined depth/stencil storage |
| GL_STENCIL_INDEX | GL_STENCIL_INDEX8 | Explicit stencil storage |
| GL_RGB5 / GL_R3_G3_B2 | GL_RGB565 | Precision approximation |
| GL_RGB4 | GL_RGBA4 | Adds an alpha channel |
| GL_DEPTH_COMPONENT32 | GL_DEPTH_COMPONENT24 | Precision approximation |

Unsupported and already-sized formats pass through unchanged. Approximate
mappings are logged with `approximate=1`. These decisions do not create
secondary renderbuffers or emulate depth/stencil attachment combinations.
A `GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT` status still comes from the host,
including when WineD3D deliberately probes an unsupported combination.

## Not yet implemented

This does **not** integrate GL4ES fixed-function pipeline emulation, shader
conversion, GLX, compressed textures, general depth/stencil data conversion,
or framebuffer attachment emulation. No unsupported GL feature is falsely advertised.

A future full GL4ES integration would need to route BoxedWine's generated
OpenGL entry points into GL4ES's context-bound implementation and test that
GL4ES's GLES2 backend works with Emscripten/WebGL2. Simply linking
`libGL` will not intercept BoxedWine's existing GL marshalling layer.

## Testing

Run the workflow `18 Wheels of Steel browser port (WASM WebGL)`, download
the successful browser artifact and retain `.local-game-data` from your
existing installation. Search the log for `emuwasm-gl4es:` to confirm the
mapping was exercised. Passing CI does not establish that Prism3D runs.
