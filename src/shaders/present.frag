#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D frame;

// Source rectangle of the picture inside the frame texture, in texture pixels:
// the whole 426x240 widescreen picture, or the full 256x240 4:3 frame (no
// overscan crop). scanlines is 1 to darken every other picture row.
layout(push_constant) uniform PresentRect {
    vec4 rect;       // x, y, width, height
    float scanlines;
} pc;

void main() {
    // Position in texture pixels, so the scan lines follow the NES rows at any window size.
    vec2 pixel = pc.rect.xy + uv * pc.rect.zw;
    vec3 c = texture(frame, pixel / vec2(textureSize(frame, 0))).rgb;
    if (pc.scanlines > 0.5 && mod(floor(pixel.y), 2.0) == 1.0) c *= 0.72;
    outColor = vec4(c, 1.0);
}
