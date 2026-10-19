#version 450
// RmlUi geometry (RenderInterface_GL3's main program). Vertices are in window pixels with a top-left
// origin; the translation is applied first, then transform (RmlUi's transform times the projection).
layout(push_constant) uniform Push {
    mat4 transform;
    vec2 translate;
    int textured;
} pc;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;

void main() {
    fragTexCoord = inTexCoord;
    fragColor = inColor;
    gl_Position = pc.transform * vec4(inPosition + pc.translate, 0.0, 1.0);
}
