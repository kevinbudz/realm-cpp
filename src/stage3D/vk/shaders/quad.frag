#version 450
// The quad fragment shader: a port of the OpenGL 3.3 program. The one change is the screen position:
// the Vulkan framebuffer's y already runs down the screen (negative viewport), so gl_FragCoord.xy is
// the screen position the GL program rebuilt from its bottom-left origin.
layout(location=0) in vec2 local;
layout(location=1) flat in vec4 bounds;
layout(location=2) flat in float layer;
layout(location=3) flat in vec4 tint;
layout(location=4) flat in vec4 bias;
layout(location=5) flat in float mode;
layout(location=6) flat in vec2 scroll;

layout(set=0, binding=0) uniform sampler2DArray sprites;
layout(set=1, binding=0) uniform sampler2D surface;

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

layout(location=0) out vec4 color;

void main() {
    vec2 screen = gl_FragCoord.xy;
    if (pc.clipped != 0 && (any(lessThan(screen,pc.clipRect.xy)) || any(greaterThanEqual(screen,pc.clipRect.xy+pc.clipRect.zw)))) discard;
    // Cut corners: each corner of the clip square loses a clipCut-sized triangle (device px).
    if (pc.clipped != 0) {
        vec2 inset = min(screen - pc.clipRect.xy, pc.clipRect.xy + pc.clipRect.zw - screen);
        if (inset.x + inset.y < pc.clipCut) discard;
    }
    if (mode > 16.5 && mode < 17.5) {
        // Wall corner (quad.vert): solid faces are black; textured faces sample their sprite as the mesh program does.
        if (scroll.x < 0.5) { color = vec4(0.0, 0.0, 0.0, 1.0); return; }
        vec2 page = vec2(textureSize(sprites, 0).xy);
        vec2 size = bounds.zw * page;
        vec2 pixels = floor(local * size);
        pixels = scroll.y > 0.5 ? mod(pixels, size) : clamp(pixels, vec2(0.0), size - 1.0);
        vec2 coords = (bounds.xy * page + pixels + 0.5) / page;
        vec4 texel = texture(sprites, vec3(coords, layer));
        color = vec4(texel.rgb * tint.x, texel.a);
        return;
    }
    if (mode > 12.5 && mode < 13.5) {
        vec2 uv = bounds.xy + local * bounds.zw;
        color = any(lessThan(uv,vec2(0))) || any(greaterThanEqual(uv,vec2(1)))
            ? vec4(27.0/255.0,27.0/255.0,27.0/255.0,1) : texture(surface,uv);
        return;
    }
    if (mode > 14.5 && mode < 15.5) {
        // Marker coverage at one logical pixel per drawable pixel uses four subpixel steps.
        vec2 p = screen;
        vec2 coverage = clamp(min(bias.xy+vec2(4)*scroll.x,floor(p)+vec2(1))-max(bias.xy,floor(p)),0.0,1.0);
        if (scroll.x == 1.0) coverage = floor(coverage*4.0+0.5)/4.0;
        color = vec4(tint.rgb,coverage.x*coverage.y); return;
    }
    if (mode > 15.5 && mode < 16.5) {
        vec2 d=(local-0.5)*32.0;
        vec2 uv=vec2((scroll.x*d.x+scroll.y*d.y)/8.0+0.5,(-scroll.y*d.x+scroll.x*d.y)/32.0+0.625);
        if(any(lessThan(uv,vec2(0)))||any(greaterThanEqual(uv,vec2(1)))) { color=vec4(0);return; }
        color=texture(surface,uv);if(color.a>0.0)color.rgb/=color.a;return;
    }
    if(mode > 6.5 && mode < 7.5) {
        vec2 pixel=floor(local*(scroll+8.0));
        vec2 d=max(vec2(4)-pixel,pixel-(scroll+3.0));
        float edge=max(d.x,d.y);
        color=edge<=0.0 ? tint : vec4(0,0,0,edge<=1.0 ? 0.8 : 0.0);return;
    }
    if (mode > 1.5 && mode < 2.5) { color = tint; return; }
    if (mode > 0.5 && mode < 1.5) {
        float r = min(1.0, length((local - 0.5) * 2.0));
        color = vec4(tint.rgb, mix(tint.a, bias.a, r)); return;
    }
    vec2 coords = mode > 2.5 && mode < 3.5 ? fract(local + scroll) : local;
    if(mode > 7.5 && mode < 8.5) {
        vec2 p=local-.5;coords=vec2(p.x*scroll.x+p.y*scroll.y,-p.x*scroll.y+p.y*scroll.x)+.5;
        if(any(lessThan(coords,vec2(0)))||any(greaterThan(coords,vec2(1)))) { color=vec4(0);return; }
    }
    vec2 uv = bounds.xy + coords * bounds.zw;
    vec2 halfTexel = 0.5 / vec2(pc.standalone != 0 ? textureSize(surface, 0) : textureSize(sprites, 0).xy);
    // Bitmaps in the atlas' dynamic pages clamp to their own region like sprites (the neighbouring slot must never show);
    // mode 4 stays unclamped, and a standalone surface is bounded by the [0,1] test instead.
    if (mode < 3.5 || (mode > 4.5 && pc.standalone == 0)) uv = clamp(uv, bounds.xy + halfTexel, bounds.xy + bounds.zw - halfTexel);
    if(pc.standalone != 0 && mode > 5.5 && (any(lessThan(uv,vec2(0)))||any(greaterThan(uv,vec2(1))))) { color=vec4(0);return; }
    vec4 texel = pc.standalone != 0 ? texture(surface, uv) : texture(sprites, vec3(uv, layer));
    // UI is composited by the regular display list, which uses the
    // premultiplied bitmap color once. World Stage3D uses SOURCE_ALPHA again.
    if (mode > 4.5 && mode < 5.5 && texel.a > 0.0) texel.rgb /= texel.a;
    // Stasis: ColorMatrixFilter(greyscaleFilterMatrix) on ordinary sprites.
    if (scroll.x > 0.5 && (mode < 0.5 || (mode > 5.5 && mode < 6.5))) texel.rgb = vec3(dot(texel.rgb, vec3(0.3, 0.59, 0.11)));
    color = clamp(texel * tint + bias, 0.0, 1.0);
}
