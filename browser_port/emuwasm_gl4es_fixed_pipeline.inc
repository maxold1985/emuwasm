/*
 * OpenGL 1.x immediate/fixed-function pipeline -> actual WebGL2 rendering.
 *
 * The guest GL APIs glBegin/glEnd/glVertex*/glMatrix* invoke this runtime
 * through EM_JS, preserving the current WebGL context and its host bindings.
 * State is per-WebGL-context, not shared among unrelated guest contexts.
 *
 * Supported: points, lines, triangle strips/fans, triangles, quads, quad
 * strips and convex polygons; vertex colors, one 2D texture unit, alpha test,
 * modelview/projection/texture matrices. Lighting, fog, multitexturing,
 * polygon stipple and nonconvex polygons are deliberately not emulated yet.
 */
#ifdef __EMSCRIPTEN__
EM_JS(int, emuwasm_ffp_command,
      (int op, double a, double b, double c, double d, int pointer), {
    var gl = GL.currentContext && GL.currentContext.GLctx;
    if (!gl || typeof gl.createVertexArray !== "function")
        return 0x0502; // GL_INVALID_OPERATION

    var contexts = Module["emuwasmFFPContexts"];
    if (!contexts) {
        contexts = new WeakMap();
        Module["emuwasmFFPContexts"] = contexts;
    }
    var state = contexts.get(gl);
    if (!state) {
        function identity() {
            return [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1];
        }
        function multiply(lhs, rhs) {
            var out = new Array(16).fill(0);
            // OpenGL matrices are column-major, post-multiplied.
            for (var column = 0; column < 4; ++column)
                for (var row = 0; row < 4; ++row)
                    for (var k = 0; k < 4; ++k)
                        out[column*4+row] += lhs[k*4+row]*rhs[column*4+k];
            return out;
        }
        function makeShader(type, source) {
            var shader = gl.createShader(type);
            if (!shader)
                throw Error("gl.createShader failed");
            gl.shaderSource(shader, source);
            gl.compileShader(shader);
            if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
                var log = gl.getShaderInfoLog(shader);
                gl.deleteShader(shader);
                throw Error("WebGL2 fixed-function shader: " + log);
            }
            return shader;
        }
        var s = {
            matrices: [identity(), identity(), identity()],
            stacks: [[], [], []],
            matrixMode: 0,
            color: [1,1,1,1],
            texcoord: [0,0],
            textureEnabled: false,
            alphaEnabled: false,
            alphaFunc: 0x0207, // GL_ALWAYS
            alphaRef: 0,
            vertices: [],
            primitive: -1,
            program: null,
            vao: null,
            vbo: null,
        };
        function ensureGpu() {
            if (s.program)
                return;
            var vertexSource = `#version 300 es
                precision highp float;
                layout(location=0) in vec4 aPosition;
                layout(location=1) in vec4 aColor;
                layout(location=2) in vec2 aTexcoord;
                uniform mat4 uModelView, uProjection, uTexture;
                out vec4 vColor;
                out vec2 vTexcoord;
                void main() {
                    gl_Position = uProjection * uModelView * aPosition;
                    gl_PointSize = 1.0;
                    vColor = aColor;
                    vTexcoord = (uTexture * vec4(aTexcoord, 0.0, 1.0)).xy;
                }`;
            var fragmentSource = `#version 300 es
                precision highp float;
                in vec4 vColor;
                in vec2 vTexcoord;
                uniform sampler2D uSampler;
                uniform bool uUseTexture, uAlphaEnabled;
                uniform int uAlphaFunc;
                uniform float uAlphaRef;
                out vec4 outputColor;
                void main() {
                    vec4 pixel = vColor;
                    if (uUseTexture)
                        pixel *= texture(uSampler, vTexcoord);
                    if (uAlphaEnabled) {
                        bool ok = (uAlphaFunc == 0x0200) ? false :
                                  (uAlphaFunc == 0x0201) ? pixel.a < uAlphaRef :
                                  (uAlphaFunc == 0x0202) ? pixel.a == uAlphaRef :
                                  (uAlphaFunc == 0x0203) ? pixel.a <= uAlphaRef :
                                  (uAlphaFunc == 0x0204) ? pixel.a > uAlphaRef :
                                  (uAlphaFunc == 0x0205) ? pixel.a != uAlphaRef :
                                  (uAlphaFunc == 0x0206) ? pixel.a >= uAlphaRef : true;
                        if (!ok) discard;
                    }
                    outputColor = pixel;
                }`;
            var vertex = makeShader(gl.VERTEX_SHADER, vertexSource);
            var fragment = makeShader(gl.FRAGMENT_SHADER, fragmentSource);
            var program = gl.createProgram();
            if (!program)
                throw Error("gl.createProgram failed");
            gl.attachShader(program, vertex);
            gl.attachShader(program, fragment);
            gl.linkProgram(program);
            gl.deleteShader(vertex);
            gl.deleteShader(fragment);
            if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
                var log = gl.getProgramInfoLog(program);
                gl.deleteProgram(program);
                throw Error("WebGL2 fixed-function link: " + log);
            }
            var vao = gl.createVertexArray();
            var vbo = gl.createBuffer();
            if (!vao || !vbo) {
                if (vao) gl.deleteVertexArray(vao);
                if (vbo) gl.deleteBuffer(vbo);
                gl.deleteProgram(program);
                throw Error("WebGL2 fixed-function GPU allocation failed");
            }
            s.program = program;
            s.vao = vao;
            s.vbo = vbo;
            s.uniforms = {
                modelview: gl.getUniformLocation(program, "uModelView"),
                projection: gl.getUniformLocation(program, "uProjection"),
                texture: gl.getUniformLocation(program, "uTexture"),
                sampler: gl.getUniformLocation(program, "uSampler"),
                useTexture: gl.getUniformLocation(program, "uUseTexture"),
                alphaEnabled: gl.getUniformLocation(program, "uAlphaEnabled"),
                alphaFunc: gl.getUniformLocation(program, "uAlphaFunc"),
                alphaRef: gl.getUniformLocation(program, "uAlphaRef"),
            };
            // Attribute pointers live in our VAO. Restore all host bindings.
            var previousVao = gl.getParameter(gl.VERTEX_ARRAY_BINDING);
            var previousBuffer = gl.getParameter(gl.ARRAY_BUFFER_BINDING);
            try {
                gl.bindVertexArray(vao);
                gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
                for (var attr = 0; attr < 3; ++attr) {
                    var components = attr == 2 ? 2 : 4;
                    gl.enableVertexAttribArray(attr);
                    gl.vertexAttribPointer(attr, components, gl.FLOAT, false,
                                           40, attr == 0 ? 0 : attr == 1 ? 16 : 32);
                }
            } finally {
                gl.bindVertexArray(previousVao);
                gl.bindBuffer(gl.ARRAY_BUFFER, previousBuffer);
            }
        }
        function triangles() {
            var src = s.vertices, mode = s.primitive, dst = [];
            function append(index) {
                dst.push.apply(dst, src[index]);
            }
            function triangle(i, j, k) {
                append(i); append(j); append(k);
            }
            if (mode == 0x0007) { // GL_QUADS
                for (var i = 0; i + 3 < src.length; i += 4) {
                    triangle(i,i+1,i+2); triangle(i,i+2,i+3);
                }
            } else if (mode == 0x0008) { // GL_QUAD_STRIP
                for (var i = 0; i + 3 < src.length; i += 2) {
                    triangle(i,i+1,i+3); triangle(i,i+3,i+2);
                }
            } else if (mode == 0x0009) { // GL_POLYGON: convex only
                for (var i = 1; i + 1 < src.length; ++i)
                    triangle(0,i,i+1);
            } else {
                for (var i = 0; i < src.length; ++i)
                    append(i);
            }
            return dst;
        }
        function draw() {
            var src = triangles();
            if (!src.length)
                return 0;
            ensureGpu();
            var prevProgram = gl.getParameter(gl.CURRENT_PROGRAM);
            var prevVAO = gl.getParameter(gl.VERTEX_ARRAY_BINDING);
            var prevBuffer = gl.getParameter(gl.ARRAY_BUFFER_BINDING);
            try {
                gl.useProgram(s.program);
                gl.bindVertexArray(s.vao);
                gl.bindBuffer(gl.ARRAY_BUFFER, s.vbo);
                gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(src), gl.STREAM_DRAW);
                var u = s.uniforms;
                gl.uniformMatrix4fv(u.modelview, false, new Float32Array(s.matrices[0]));
                gl.uniformMatrix4fv(u.projection, false, new Float32Array(s.matrices[1]));
                gl.uniformMatrix4fv(u.texture, false, new Float32Array(s.matrices[2]));
                gl.uniform1i(u.useTexture, s.textureEnabled ? 1 : 0);
                gl.uniform1i(u.alphaEnabled, s.alphaEnabled ? 1 : 0);
                gl.uniform1i(u.alphaFunc, s.alphaFunc);
                gl.uniform1f(u.alphaRef, s.alphaRef);
                var textureUnit = gl.getParameter(gl.ACTIVE_TEXTURE) - gl.TEXTURE0;
                gl.uniform1i(u.sampler, textureUnit);
                var mode = s.primitive;
                if (mode == 0x0007 || mode == 0x0008 || mode == 0x0009)
                    mode = gl.TRIANGLES;
                gl.drawArrays(mode, 0, src.length / 10);
                return 0;
            } finally {
                gl.bindVertexArray(prevVAO);
                gl.bindBuffer(gl.ARRAY_BUFFER, prevBuffer);
                gl.useProgram(prevProgram);
            }
        }
        s.execute = function(op, a, b, c, d, pointer) {
            var values = s.matrices, idx = s.matrixMode, matrix = values[idx];
            var inside = s.primitive >= 0;
            // GL only allows vertex/color/texcoord and glEnd during Begin.
            if (inside && op != 2 && op != 3 && op != 4 && op != 5)
                return 0x0502; // GL_INVALID_OPERATION
            switch (op) {
            case 1: { // glBegin(mode)
                if (inside) return 0x0502;
                var mode = a | 0;
                if (mode < 0 || mode > 9) return 0x0500;
                s.primitive = mode;
                s.vertices = [];
                return 0;
            }
            case 2: { // glEnd
                if (!inside) return 0x0502;
                try { return draw(); }
                finally { s.primitive = -1; s.vertices = []; }
            }
            case 3: // glVertex4f
                if (!inside) return 0x0502;
                if (s.vertices.length >= 32768) return 0x0505; // OUT_OF_MEMORY
                s.vertices.push([a,b,c,d,
                    s.color[0],s.color[1],s.color[2],s.color[3],
                    s.texcoord[0],s.texcoord[1]]);
                return 0;
            case 4: // glColor4f
                s.color = [a,b,c,d]; return 0;
            case 5: // glTexCoord2f
                s.texcoord = [a,b]; return 0;
            case 6: // glMatrixMode
                if (a == 0x1700) idx = 0;
                else if (a == 0x1701) idx = 1;
                else if (a == 0x1702) idx = 2;
                else return 0x0500;
                s.matrixMode = idx;
                return 0;
            case 7: // glLoadIdentity
                values[idx] = identity(); return 0;
            case 8: // glPushMatrix
                if (s.stacks[idx].length >= (idx == 0 ? 31 : 1))
                    return 0x0503; // STACK_OVERFLOW
                s.stacks[idx].push(matrix.slice()); return 0;
            case 9: // glPopMatrix
                if (!s.stacks[idx].length) return 0x0504; // STACK_UNDERFLOW
                values[idx] = s.stacks[idx].pop(); return 0;
            case 10: // glTranslatef
                values[idx] = multiply(matrix,
                    [1,0,0,0, 0,1,0,0, 0,0,1,0, a,b,c,1]);
                return 0;
            case 11: // glScalef
                values[idx] = multiply(matrix,
                    [a,0,0,0, 0,b,0,0, 0,0,c,0, 0,0,0,1]);
                return 0;
            case 12: { // glRotatef(angle, x, y, z)
                var len = Math.hypot(b,c,d);
                if (!Number.isFinite(len) || len == 0) return 0x0501;
                var x=b/len, y=c/len, z=d/len;
                var angle=a*Math.PI/180, co=Math.cos(angle), si=Math.sin(angle), t=1-co;
                values[idx] = multiply(matrix, [
                    t*x*x+co, t*x*y+si*z, t*x*z-si*y, 0,
                    t*x*y-si*z, t*y*y+co, t*y*z+si*x, 0,
                    t*x*z+si*y, t*y*z-si*x, t*z*z+co, 0,
                    0,0,0,1
                ]);
                return 0;
            }
            case 13: // glOrtho
            case 14: { // glFrustum, parameters in guest WASM memory
                if (!pointer || pointer % 8) return 0x0501;
                var p = pointer >>> 3;
                var l=HEAPF64[p],r=HEAPF64[p+1],bt=HEAPF64[p+2],
                    tp=HEAPF64[p+3],n=HEAPF64[p+4],f=HEAPF64[p+5];
                if (![l,r,bt,tp,n,f].every(Number.isFinite) ||
                    l==r || bt==tp || n==f ||
                    (op==14 && (n<=0 || f<=0))) return 0x0501;
                if (op==13) {
                    values[idx] = multiply(matrix, [
                        2/(r-l),0,0,0, 0,2/(tp-bt),0,0,
                        0,0,-2/(f-n),0,
                        -(r+l)/(r-l),-(tp+bt)/(tp-bt),-(f+n)/(f-n),1
                    ]);
                } else {
                    values[idx] = multiply(matrix, [
                        2*n/(r-l),0,0,0, 0,2*n/(tp-bt),0,0,
                        (r+l)/(r-l),(tp+bt)/(tp-bt),-(f+n)/(f-n),-1,
                        0,0,-2*f*n/(f-n),0
                    ]);
                }
                return 0;
            }
            case 15: // glEnable/glDisable(GL_TEXTURE_2D)
                s.textureEnabled = !!a; return 0;
            case 16: // glEnable/glDisable(GL_ALPHA_TEST)
                s.alphaEnabled = !!a; return 0;
            case 17: // glAlphaFunc(compare, ref)
                if (a < 0x0200 || a > 0x0207) return 0x0500;
                s.alphaFunc = a;
                s.alphaRef = Math.min(1, Math.max(0, b));
                return 0;
            default:
                return 0x0500;
            }
        };
        contexts.set(gl, s);
        state = s;
    }
    try {
        return state.execute(op,a,b,c,d,pointer);
    } catch (error) {
        console.warn("emuwasm-ffp: GL WebGL2 pipeline failed", String(error));
        return 0x0502; // GL_INVALID_OPERATION, no success forgery
    }
});
#endif // __EMSCRIPTEN__
