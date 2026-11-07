#version 450
// RmlUi's blur (RenderInterface_GL3 blur program): seven taps along offset, each inside the clamp
// region, with the symmetric weights. Copies sample the source once.
layout(set = 2, binding = 0) uniform sampler2D scene;

layout(push_constant) uniform Post {
    float dst[4];
    float src[4];
    float offset[2];
    float clampUv[4];
    float weights[4];
    float window[2];
    int blur;
} pc;

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 finalColor;

void main() {
    if (pc.blur == 0) {
        finalColor = texture(scene, fragUv);
        return;
    }
    vec2 clampMin = vec2(pc.clampUv[0], pc.clampUv[1]);
    vec2 clampMax = vec2(pc.clampUv[2], pc.clampUv[3]);
    vec2 texelOffset = vec2(pc.offset[0], pc.offset[1]);
    vec4 color = vec4(0.0);
    for (int i = 0; i < 7; i++) {
        int distance = i - 3;
        vec2 uv = fragUv - float(distance) * texelOffset;
        vec2 inRegion = step(clampMin, uv) * step(uv, clampMax);
        color += texture(scene, uv) * inRegion.x * inRegion.y * pc.weights[abs(distance)];
    }
    finalColor = color;
}
