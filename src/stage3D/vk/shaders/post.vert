#version 450
// The post passes: a full-screen rectangle (a strip of four corners) in pixels, as the GL program.
layout(push_constant) uniform Post {
    vec2 viewport;
    vec2 pad;
    vec4 rect;
    vec2 region;
    vec2 axis;
    float blur;
    int mode;
    int blind;
    float alpha;
    float sidebar;
} pc;

void main() {
    vec2 local = vec2(gl_VertexIndex & 1, (gl_VertexIndex >> 1) & 1);
    vec2 pixel = pc.rect.xy + pc.rect.zw * local;
    gl_Position = vec4(pixel.x / pc.viewport.x * 2.0 - 1.0,
                       1.0 - pixel.y / pc.viewport.y * 2.0, 0.0, 1.0);
}
