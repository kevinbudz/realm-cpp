#version 450
// The world and overlay quad program (instances of Quad, 88 bytes; see DrawTypes.hpp). A port of the
// OpenGL 3.3 vertex shader: the same math. The y convention comes from the negative viewport, so the
// position formulas are unchanged. Walls read their corners from a storage buffer (set 0, binding 1).
layout(location=0) in vec2 origin;
layout(location=1) in vec2 axisX;
layout(location=2) in vec2 axisY;
layout(location=3) in vec4 spriteUV;
layout(location=4) in float page;
layout(location=5) in vec4 multiply;
layout(location=6) in vec4 offset;
layout(location=7) in float kind;
layout(location=8) in vec2 uvOffset;

// One block for both stages: std430 offsets are fixed by the C++ struct PushConstants (VkRenderer).
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

// Wall corners: two RGBA32F texels each (x y z u, then v shade flags pad), as WallGpuVertex.
layout(std430, set=0, binding=1) readonly buffer Walls {
    vec4 texels[];
} walls;

layout(location=0) out vec2 local;
layout(location=1) flat out vec4 bounds;
layout(location=2) flat out float layer;
layout(location=3) flat out vec4 tint;
layout(location=4) flat out vec4 bias;
layout(location=5) flat out float mode;
layout(location=6) flat out vec2 scroll;

const int stripCorner[6] = int[6](0, 1, 2, 2, 1, 3);

void main() {
    bounds = spriteUV; layer = page; tint = multiply; bias = offset; mode = kind; scroll = uvOffset;
    if (kind > 16.5 && kind < 17.5) {
        // Wall instance: multiply = (first corner, corner count, neighbor black mask, unused). A triangle is kept when
        // its screen determinant (b - a) x (c - a) is positive, the winding the software path culls by.
        int vertex = gl_VertexIndex;
        if (vertex >= int(multiply.y)) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }
        int first = (int(multiply.x) + vertex / 3 * 3) * 2;
        vec4 g0 = walls.texels[first], h0 = walls.texels[first + 1];
        vec4 g1 = walls.texels[first + 2], h1 = walls.texels[first + 3];
        vec4 g2 = walls.texels[first + 4], h2 = walls.texels[first + 5];
        vec2 p0 = origin + axisX * g0.x + axisY * g0.y + uvOffset * g0.z;
        vec2 p1 = origin + axisX * g1.x + axisY * g1.y + uvOffset * g1.z;
        vec2 p2 = origin + axisX * g2.x + axisY * g2.y + uvOffset * g2.z;
        float det = (p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x);
        if (det <= 0.0) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }
        int corner = vertex % 3;
        vec4 g = corner == 0 ? g0 : corner == 1 ? g1 : g2;
        vec2 pixel = corner == 0 ? p0 : corner == 1 ? p1 : p2;
        gl_Position = vec4(pixel.x / pc.viewport.x * 2.0 - 1.0, 1.0 - pixel.y / pc.viewport.y * 2.0, 0.0, 1.0);
        // flags = repeat + 2 * slot, slot 4 meaning no neighbor test; the mask says which neighbor slots are black.
        // Every corner of a triangle belongs to one polygon, so corner 0 holds the flags and shade of the triangle.
        int flags = int(h0.z);
        int slot = flags / 2;
        bool solid = slot != 4 && ((int(multiply.z) >> slot) & 1) != 0;
        local = vec2(g.w, corner == 0 ? h0.x : corner == 1 ? h1.x : h2.x);
        tint = vec4(h0.y, 0.0, 0.0, 1.0);
        scroll = vec2(solid ? 0.0 : 1.0, (flags & 1) != 0 ? 1.0 : 0.0);
        bias = vec4(0.0);
        mode = 17.0;
        return;
    }
    if (pc.triangleList != 0) {
        if (gl_VertexIndex >= 6) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }
        int corner = stripCorner[gl_VertexIndex];
        local = vec2(corner & 1, (corner >> 1) & 1);
    } else {
        local = vec2(gl_VertexIndex & 1, (gl_VertexIndex >> 1) & 1);
    }
    vec2 pixel = origin + axisX * local.x + axisY * local.y;
    gl_Position = vec4(pixel.x / pc.viewport.x * 2.0 - 1.0, 1.0 - pixel.y / pc.viewport.y * 2.0, 0.0, 1.0);
    if (pc.groundSpace != 0) {
        pixel = pc.groundOrigin + pc.groundX * pixel.x + pc.groundY * pixel.y;
        gl_Position = vec4(pixel.x / pc.viewport.x * 2.0 - 1.0, 1.0 - pixel.y / pc.viewport.y * 2.0, 0.0, 1.0);
        if (kind > 10.5 && kind < 12.5) {
            scroll = vec2(offset.w) + (kind < 11.5 ? uvOffset * pc.groundTime : sin(uvOffset * pc.groundTime));
            bias = vec4(0); mode = 3.0;
        }
    }
}
