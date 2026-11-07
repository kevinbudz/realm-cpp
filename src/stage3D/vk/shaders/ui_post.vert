#version 450
// RmlUi layer composites: a rectangle of the target (window pixels, top-left origin) filled from a
// rectangle of the source texture (uv). Positions are computed from the vertex index (a strip of four).
// The push block is std430 with scalar arrays, matching UiPostPush (84 bytes).
layout(push_constant) uniform Post {
    float dst[4];      // x0 y0 x1 y1, pixels
    float src[4];      // u0 v0 u1 v1
    float offset[2];   // uv step of one blur tap
    float clampUv[4];  // blur region: min x, min y, max x, max y
    float weights[4];  // blur weights for tap distance 0..3
    float window[2];   // window size in pixels
    int blur;          // 0 copy, 1 blur
} pc;

layout(location = 0) out vec2 fragUv;

void main() {
    vec2 corner = vec2(float(gl_VertexIndex & 1), float(gl_VertexIndex >> 1));
    vec2 px = mix(vec2(pc.dst[0], pc.dst[1]), vec2(pc.dst[2], pc.dst[3]), corner);
    fragUv = mix(vec2(pc.src[0], pc.src[1]), vec2(pc.src[2], pc.src[3]), corner);
    gl_Position = vec4(2.0 * px.x / pc.window[0] - 1.0, 1.0 - 2.0 * px.y / pc.window[1], 0.5, 1.0);
}
