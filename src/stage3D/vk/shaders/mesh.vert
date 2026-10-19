#version 450
// Prepared meshes (MeshDraw owners, drawn from the frame's vertex ring): pixel positions with the
// window viewport, as the OpenGL mesh program.
layout(location=0) in vec2 position;
layout(location=1) in vec2 texcoord;
layout(location=2) in float shade;
layout(location=3) in vec4 spriteUV;
layout(location=4) in float page;
layout(location=5) in vec3 meshColor;
layout(location=6) in float texturedFlag;
layout(location=7) in float repeatFlag;

layout(push_constant) uniform Push {
    vec2 viewport;
    vec2 groundOrigin;
    vec2 groundX;
    vec2 groundY;
    float groundTime;
    float pad0;
    float pad1;
    float pad2;
    vec4 clipRect;
    float clipCut;
    int standalone;
    int triangleList;
    int groundSpace;
    int clipped;
} pc;

layout(location=0) out vec2 uv;
layout(location=1) out float lit;
layout(location=2) flat out vec4 bounds;
layout(location=3) flat out float layer;
layout(location=4) flat out vec3 flatColor;
layout(location=5) flat out float textured;
layout(location=6) flat out float repeatTexture;

void main() {
    gl_Position = vec4(position.x / pc.viewport.x * 2.0 - 1.0,
                       1.0 - position.y / pc.viewport.y * 2.0, 0.0, 1.0);
    uv = texcoord; lit = shade;
    bounds = spriteUV; layer = page; flatColor = meshColor;
    textured = texturedFlag; repeatTexture = repeatFlag;
}
