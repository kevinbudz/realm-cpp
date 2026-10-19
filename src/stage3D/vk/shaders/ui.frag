#version 450
// Premultiplied output (the pipeline blends ONE, ONE_MINUS_SRC_ALPHA, as GL's ui pass does).
layout(set = 1, binding = 0) uniform sampler2D surfaceImage;

layout(push_constant) uniform Push {
    mat4 transform;
    vec2 translate;
    int textured;
} pc;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;
layout(location = 0) out vec4 finalColor;

void main() {
    vec4 color = fragColor;
    if (pc.textured != 0) {
        color *= texture(surfaceImage, fragTexCoord);
    }
    finalColor = color;
}
