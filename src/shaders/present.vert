#version 450

// One oversized triangle covers the whole viewport; no vertex buffer needed.
// UV runs 0..1 across the visible square.
layout(location = 0) out vec2 uv;

void main() {
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
