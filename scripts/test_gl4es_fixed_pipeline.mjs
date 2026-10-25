// Execute the actual Emscripten fixed-function EM_JS bridge with a fake
// WebGL2 context. Verify GPU calls, geometry, transforms and state cleanup.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { runInNewContext } from "node:vm";

const src = readFileSync(
    new URL("../browser_port/emuwasm_gl4es_fixed_pipeline.inc", import.meta.url),
    "utf8"
);
const start = src.indexOf("EM_JS(int, emuwasm_ffp_command,");
assert.ok(start >= 0);
const bodyStart = src.indexOf("), {", start);
const bodyEnd = src.lastIndexOf("\n});");
assert.ok(bodyStart > start && bodyEnd > bodyStart);
const memory = new ArrayBuffer(4096);
const HEAPF32 = new Float32Array(memory);
const HEAPF64 = new Float64Array(memory);
const HEAPU8 = new Uint8Array(memory);
const events = [];
let currentProgram = {id:"original-program"};
let currentBuffer = {id:"original-buffer"};
let currentVAO = {id:"original-vao"};
let shaderCount = 0;
const gl = {
    VERTEX_SHADER:0x8B31, FRAGMENT_SHADER:0x8B30,
    COMPILE_STATUS:0x8B81, LINK_STATUS:0x8B82,
    VERTEX_ARRAY_BINDING:0x85B5, ARRAY_BUFFER_BINDING:0x8894,
    ARRAY_BUFFER:0x8892, STREAM_DRAW:0x88E0, FLOAT:0x1406,
    CURRENT_PROGRAM:0x8B8D, ACTIVE_TEXTURE:0x84E0,
    TEXTURE0:0x84C0, TRIANGLES:4,
    createShader(type) { ++shaderCount; return {type}; },
    shaderSource(shader,source) {
        assert.ok(source.includes("#version 300 es"));
        events.push(["shaderSource",shader.type,source]);
    },
    compileShader(shader) { events.push(["compileShader",shader.type]); },
    getShaderParameter() {return true;},
    getShaderInfoLog() {return "";},
    deleteShader() {},
    createProgram() { return {program:42}; },
    attachShader() {},
    linkProgram() {},
    getProgramParameter() {return true;},
    getProgramInfoLog() {return "";},
    deleteProgram() {},
    createVertexArray() {return {vao:4};},
    createBuffer() {return {vbo:7};},
    deleteVertexArray() {},
    deleteBuffer() {},
    getUniformLocation(program,name) {return name;},
    getParameter(pname) {
        if (pname===this.VERTEX_ARRAY_BINDING) return currentVAO;
        if (pname===this.ARRAY_BUFFER_BINDING) return currentBuffer;
        if (pname===this.CURRENT_PROGRAM) return currentProgram;
        if (pname===this.ACTIVE_TEXTURE) return this.TEXTURE0;
        throw Error("unexpected gl.getParameter: "+pname);
    },
    bindVertexArray(vao) {currentVAO=vao;events.push(["vao",vao]);},
    bindBuffer(target,buffer) {
        assert.equal(target,this.ARRAY_BUFFER);
        currentBuffer=buffer;events.push(["buffer",buffer]);
    },
    enableVertexAttribArray(location) {events.push(["attribEnable",location]);},
    vertexAttribPointer(location,components,type,normalized,stride,offset) {
        events.push(["attribPointer",location,components,stride,offset]);
    },
    useProgram(program) {currentProgram=program;events.push(["program",program]);},
    bufferData(target,data,usage) {
        assert.equal(target,this.ARRAY_BUFFER);
        assert.equal(usage,this.STREAM_DRAW);
        events.push(["bufferData",Array.from(data)]);
    },
    uniformMatrix4fv(name,transpose,matrix) {
        assert.equal(transpose,false);
        events.push(["matrix",name,Array.from(matrix)]);
    },
    uniform1i(name,value) {events.push(["int",name,value]);},
    uniform1f(name,value) {events.push(["float",name,value]);},
    drawArrays(mode,first,count) {events.push(["drawArrays",mode,first,count]);},
};
const Module = {};
const warnings = [];
const context = {
    GL:{currentContext:{GLctx:gl}},
    Module, HEAPF64, HEAPF32, HEAPU8,
    console:{warn:(...args)=>warnings.push(args.join(" "))},
};
const executor = runInNewContext(
    "(function(op,a,b,c,d,pointer) {" +
    src.slice(bodyStart+4,bodyEnd) + "})", context
);
function call(op,a=0,b=0,c=0,d=0,pointer=0) {
    return executor(op,a,b,c,d,pointer);
}

assert.equal(call(6,0x1700),0); // MODELVIEW
assert.equal(call(7),0); // identity
assert.equal(call(10,2,3,4),0); // translate
assert.equal(call(8),0); // push
assert.equal(call(11,2,2,2),0); // scale
assert.equal(call(9),0); // pop
assert.equal(call(9),0x0504); // stack underflow
assert.equal(call(6,0x1701),0); // PROJECTION
assert.equal(call(7),0);
const projection = new Float64Array(memory,256,6);
projection.set([-1,1,-1,1,0.1,100]);
assert.equal(call(14,0,0,0,0,256),0); // real frustum
assert.equal(call(6,0x1700),0);
assert.equal(call(15,1),0); // texture enable
assert.equal(call(16,1),0); // alpha test enable
assert.equal(call(17,0x0204,0.25),0); // alpha greater than .25

assert.equal(call(1,4),0); // Begin triangles
assert.equal(call(4,1,0,0,1),0); // Color red
assert.equal(call(5,0.5,0.75),0); // UV
assert.equal(call(3,0,0,0,1),0);
assert.equal(call(3,1,0,0,1),0);
assert.equal(call(3,0,1,0,1),0);
assert.equal(call(6,0x1700),0x0502); // matrix forbidden inside Begin
assert.equal(call(2),0); // End draws
assert.equal(shaderCount,2);
assert.ok(events.some(e=>e[0]==="shaderSource" && e[1]===gl.FRAGMENT_SHADER
                    && e[2].includes("discard")));
assert.ok(events.some(e=>e[0]==="drawArrays" && e[1]===4 && e[3]===3));
const vertexData = events.find(e=>e[0]==="bufferData")[1];
assert.equal(vertexData.length,30);
assert.deepEqual(vertexData.slice(0,10),[0,0,0,1, 1,0,0,1, 0.5,0.75]);
const modelview = events.find(e=>e[0]==="matrix" && e[1]==="uModelView")[2];
assert.equal(modelview[12],2);
assert.equal(modelview[13],3);
assert.equal(modelview[14],4);
assert.equal(currentProgram.id,"original-program");
assert.equal(currentVAO.id,"original-vao");
assert.equal(currentBuffer.id,"original-buffer");

// Quads are really triangulated and sent as six GL_TRIANGLES vertices.
const before = events.length;
assert.equal(call(1,7),0); // QUADS
for (let i=0;i<4;i++) assert.equal(call(3,i,0,0,1),0);
assert.equal(call(2),0);
const subsequent = events.slice(before);
assert.ok(subsequent.some(e=>e[0]==="drawArrays"&&e[1]===4&&e[3]===6));
assert.equal(subsequent.find(e=>e[0]==="bufferData")[1].length,60);

// glLoadMatrixf/glMultMatrixf operate on 16 guest floats.
const matrix = new Float32Array(memory,512,16);
matrix.set([1,0,0,0,0,1,0,0,0,0,1,0,9,8,7,1]);
assert.equal(call(18,1,0,0,0,512),0);
assert.equal(Module.emuwasmFFPContexts.get(gl).matrices[0][12],9);
assert.equal(call(19,1,0,0,0,512),0);
assert.equal(Module.emuwasmFFPContexts.get(gl).matrices[0][12],18);
assert.equal(call(1,99),0x0500); // Invalid mode
assert.equal(call(2),0x0502); // End without Begin
assert.equal(warnings.length,0);
console.log("OpenGL fixed pipeline: real shader compile, triangles/quads, VBO, matrix operations and state restoration verified.");
