#version 450
// The post-effect fragment shader: the blur (a box of the blur width along axis), the blind grey,
// and the vignette and sidebar overlays. A port of the OpenGL program; gl_FragCoord.xy is the
// screen position
// (the framebuffer's y runs down the screen).
layout(set=2, binding=0) uniform sampler2D scene;

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

layout(location=0) out vec4 color;

vec4 fetch(ivec2 c) {
    return (c.x < 0 || c.y < 0 || c.x >= int(pc.region.x) || c.y >= int(pc.region.y))
               ? vec4(0)
               : texelFetch(scene, c, 0);
}

void main() {
    vec2 screen = gl_FragCoord.xy;
    if (pc.mode == 1) {
        // HurtOverlay: radial gradient ratios 0/155/255, alphas 0/0/.92, ellipse through the
        // corners.
        float r = length((screen - pc.region * 0.5) / (pc.region * 0.70710678));
        float edge = clamp((r - 155.0 / 255.0) / (1.0 - 155.0 / 255.0), 0.0, 1.0) * 0.92;
        color = vec4(1.0, 55.0 / 255.0, 0.0, edge * pc.alpha);
        return;
    }
    if (pc.mode == 2) {
        float t = clamp((screen.x - (pc.region.x - pc.sidebar)) / pc.sidebar, 0.0, 1.0);
        color = vec4(0.0, 0.0, 0.0, t);
        return;
    }
    ivec2 c = ivec2(screen);
    vec4 sum;
    if (pc.blur <= 0.0) {
        sum = fetch(c);
    } else {
        float half_width = pc.blur * 0.5;
        int n = min(int(ceil(half_width)), 160);
        sum = vec4(0);
        // The weights only sum to the blur width from 1 px up; dividing by their total keeps a
        // sub-pixel blur from brightening.
        float total = 0.0;
        for (int i = -n; i <= n; ++i) {
            float w = clamp(half_width - abs(float(i)) + 0.5, 0.0, 1.0);
            sum += w * fetch(c + ivec2(pc.axis) * i);
            total += w;
        }
        sum /= total;
    }
    vec3 rgb = pc.blind != 0 ? vec3(0.05 * (sum.r + sum.g + sum.b)) : sum.rgb;
    color = vec4(rgb, 1.0);
}
