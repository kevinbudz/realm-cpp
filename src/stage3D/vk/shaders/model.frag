#version 450
// The model fragment shader: nearest, repeated sprite times the face shade; or the flat
// object colour.
layout(location=0) in vec2 uv;
layout(location=1) flat in float lit;
layout(location=2) flat in vec4 bounds;
layout(location=3) flat in float layer;
layout(location=4) flat in vec3 flatColor;
layout(location=5) flat in float textured;

layout(set=0, binding=0) uniform sampler2DArray sprites;

layout(location=0) out vec4 color;

void main() {
    if (textured < 0.5) { color = vec4(flatColor * lit, 1.0); return; }
    vec2 page = vec2(textureSize(sprites, 0).xy);
    vec2 size = bounds.zw * page;
    vec2 coords = (bounds.xy * page + mod(floor(uv * size), size) + 0.5) / page;
    vec4 texel = texture(sprites, vec3(coords, layer));
    color = vec4(texel.rgb * lit, texel.a);
}
