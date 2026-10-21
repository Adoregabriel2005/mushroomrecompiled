#version 450
layout(binding = 0) uniform sampler2D frame_tex;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform Push {
    float brightness;
    float scanline;
} pc;

void main() {
    vec3 color = texture(frame_tex, v_uv).rgb;
    float shade = 1.0;
    if (pc.scanline > 0.0) {
        float row = floor(v_uv.y * 240.0);
        shade = mod(row, 2.0) < 1.0 ? 1.0 : (1.0 - pc.scanline);
    }
    out_color = vec4(color * pc.brightness * shade, 1.0);
}
