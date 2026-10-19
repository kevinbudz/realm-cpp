#version 450
// Instanced registered models (a port of the OpenGL model program). Vertices come from the model
// geometry (binding 0); instances are quads (binding 1, 88 bytes). Same math as the GL program.
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 texcoord;
layout(location=3) in float solid;
layout(location=4) in vec2 origin;
layout(location=5) in vec2 axisX;
layout(location=6) in vec2 axisY;
layout(location=7) in vec2 axisZ;
layout(location=8) in vec4 spriteUV;
layout(location=9) in float page;
layout(location=10) in vec4 multiply;
layout(location=11) in vec4 rotation;

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
layout(location=1) flat out float lit;
layout(location=2) flat out vec4 bounds;
layout(location=3) flat out float layer;
layout(location=4) flat out vec3 flatColor;
layout(location=5) flat out float textured;

void main() {
    vec2 pixel = origin + axisX * position.x + axisY * position.y + axisZ * position.z;
    gl_Position = vec4(pixel.x / pc.viewport.x * 2.0 - 1.0,
                       1.0 - pixel.y / pc.viewport.y * 2.0, 0.0, 1.0);
    // Lighting3D.LIGHT_VECTOR = normalize(1, 3, 2), in world space after the object rotation.
    vec3 n = vec3(normal.x * rotation.x - normal.y * rotation.y,
                  normal.x * rotation.y + normal.y * rotation.x, normal.z);
    lit = 0.75 + 0.25 * max(0.0, dot(n, vec3(1.0, 3.0, 2.0) / 3.7416573867739413));
    uv = texcoord; bounds = spriteUV; layer = page; flatColor = multiply.rgb;
    textured = solid < 0.5 && multiply.a > 0.5 ? 1.0 : 0.0;
}
