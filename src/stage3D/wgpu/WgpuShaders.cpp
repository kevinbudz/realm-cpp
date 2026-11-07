#include "WgpuShaders.hpp"

// The programs are WGSL ports of src/stage3D/vk/shaders/*.{vert,frag}; the math and the locations are
// the GLSL's. Conventions shared by every module:
//  - Framebuffers run top-down, as the Vulkan negative-height viewport made them, so the clip-space
//    formulas are the GLSL ones: y = 1 - 2 * pixel_y / viewport_y. Texture rows are top-down too.
//  - gl_FragCoord.xy is @builtin(position).xy (pixel centres, top-down).
//  - textureSample calls that the GLSL made with implicit derivatives use textureSampleLevel(..., 0.0):
//    every texture here has one mip, so level 0 is the same texel, and WGSL's uniformity rules no
//    longer constrain the branches.
//  - Uniform blocks are 80 bytes (see QuadUniform / PostUniform in WgpuPipelines.hpp); the RmlUi program
//    (ui) reads a 176-byte UiBlock (WgpuUi.hpp).

namespace realm::gfx::wgpu::shaders {
const std::string_view quad = R"WGSL(
// Quad program. Group 0: atlas array, nearest sampler, wall corner texels. Group 1: one surface and
// its sampler. Group 2: the frame's uniform block (dynamic offset).
struct Params {
  viewport: vec2<f32>,
  ground_origin: vec2<f32>,
  ground_x: vec2<f32>,
  ground_y: vec2<f32>,
  ground_time: f32,
  standalone: i32,
  triangle_list: i32,
  ground_space: i32,
  clip_rect: vec4<f32>,
  clipped: i32,
};

@group(0) @binding(0) var sprites: texture_2d_array<f32>;
@group(0) @binding(1) var sprite_sampler: sampler;
@group(0) @binding(2) var walls: texture_2d<f32>;
@group(1) @binding(0) var surface: texture_2d<f32>;
@group(1) @binding(1) var surface_sampler: sampler;
@group(2) @binding(0) var<uniform> pc: Params;

struct VertexOut {
  @builtin(position) position: vec4<f32>,
  @location(0) local: vec2<f32>,
  @location(1) @interpolate(flat) bounds: vec4<f32>,
  @location(2) @interpolate(flat) layer: f32,
  @location(3) @interpolate(flat) tint: vec4<f32>,
  @location(4) @interpolate(flat) bias: vec4<f32>,
  @location(5) @interpolate(flat) mode: f32,
  @location(6) @interpolate(flat) scroll: vec2<f32>,
};

// The corner of the two triangles of a quad, in the order the GL list draws them: 0 1 2, 2 1 3.
fn strip_corner(index: u32) -> u32 {
  if (index == 0u) {
    return 0u;
  }
  if (index == 1u || index == 4u) {
    return 1u;
  }
  if (index == 3u) {
    return 2u;
  }
  if (index == 5u) {
    return 3u;
  }
  return 2u;
}

// One wall texel: texel i is at column i % 1024, row i / 1024 of the wall texture.
fn wall_texel(index: u32) -> vec4<f32> {
  return textureLoad(walls, vec2<i32>(i32(index % 1024u), i32(index / 1024u)), 0);
}

// GLSL mod(): x - y * floor(x / y).
fn glsl_mod(x: vec2<f32>, y: vec2<f32>) -> vec2<f32> {
  return x - y * floor(x / y);
}

@vertex
fn vs_main(
  @location(0) origin: vec2<f32>,
  @location(1) axis_x: vec2<f32>,
  @location(2) axis_y: vec2<f32>,
  @location(3) sprite_uv: vec4<f32>,
  @location(4) page: f32,
  @location(5) multiply: vec4<f32>,
  @location(6) bias_in: vec4<f32>,
  @location(7) kind: f32,
  @location(8) uv_offset: vec2<f32>,
  @builtin(vertex_index) vertex: u32
) -> VertexOut {
  var out: VertexOut;
  out.position = vec4<f32>(2.0, 2.0, 2.0, 1.0);
  out.local = vec2<f32>(0.0, 0.0);
  out.bounds = sprite_uv;
  out.layer = page;
  out.tint = multiply;
  out.bias = bias_in;
  out.mode = kind;
  out.scroll = uv_offset;

  if (kind > 16.5 && kind < 17.5) {
    // Wall instance: multiply = (first corner, corner count, neighbor black mask, unused). A triangle is
    // kept when its screen determinant is positive, the winding the software path culls by.
    if (vertex >= u32(multiply.y)) {
      return out;
    }
    let first = (u32(multiply.x) + vertex / 3u * 3u) * 2u;
    let g0 = wall_texel(first);
    let h0 = wall_texel(first + 1u);
    let g1 = wall_texel(first + 2u);
    let h1 = wall_texel(first + 3u);
    let g2 = wall_texel(first + 4u);
    let h2 = wall_texel(first + 5u);
    let p0 = origin + axis_x * g0.x + axis_y * g0.y + uv_offset * g0.z;
    let p1 = origin + axis_x * g1.x + axis_y * g1.y + uv_offset * g1.z;
    let p2 = origin + axis_x * g2.x + axis_y * g2.y + uv_offset * g2.z;
    let det = (p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x);
    if (det <= 0.0) {
      return out;
    }
    let corner = vertex % 3u;
    var g = g0;
    var pixel = p0;
    var shade_v = h0.x;
    if (corner == 1u) {
      g = g1;
      pixel = p1;
      shade_v = h1.x;
    } else if (corner == 2u) {
      g = g2;
      pixel = p2;
      shade_v = h2.x;
    }
    out.position = vec4<f32>(pixel.x / pc.viewport.x * 2.0 - 1.0, 1.0 - pixel.y / pc.viewport.y * 2.0, 0.0, 1.0);
    // flags = repeat + 2 * slot, slot 4 meaning no neighbour test; the mask says which neighbour
    // slots are black. Corner 0 holds the flags and the shade of the whole triangle.
    let flags = i32(h0.z);
    let slot = flags / 2;
    let solid = slot != 4 && ((i32(multiply.z) >> u32(slot)) & 1) != 0;
    out.local = vec2<f32>(g.w, shade_v);
    out.tint = vec4<f32>(h0.y, 0.0, 0.0, 1.0);
    out.scroll = vec2<f32>(select(1.0, 0.0, solid), select(0.0, 1.0, (flags & 1) != 0));
    out.bias = vec4<f32>(0.0, 0.0, 0.0, 0.0);
    out.mode = 17.0;
    return out;
  }

  var lc = vec2<f32>(0.0, 0.0);
  if (pc.triangle_list != 0) {
    if (vertex >= 6u) {
      return out;
    }
    let corner = strip_corner(vertex);
    lc = vec2<f32>(f32(corner & 1u), f32((corner >> 1u) & 1u));
  } else {
    lc = vec2<f32>(f32(vertex & 1u), f32((vertex >> 1u) & 1u));
  }
  out.local = lc;
  var pixel = origin + axis_x * lc.x + axis_y * lc.y;
  out.position = vec4<f32>(pixel.x / pc.viewport.x * 2.0 - 1.0, 1.0 - pixel.y / pc.viewport.y * 2.0, 0.0, 1.0);
  if (pc.ground_space != 0) {
    pixel = pc.ground_origin + pc.ground_x * pixel.x + pc.ground_y * pixel.y;
    out.position = vec4<f32>(pixel.x / pc.viewport.x * 2.0 - 1.0, 1.0 - pixel.y / pc.viewport.y * 2.0, 0.0, 1.0);
    if (kind > 10.5 && kind < 12.5) {
      var wave = sin(uv_offset * pc.ground_time);
      if (kind < 11.5) {
        wave = uv_offset * pc.ground_time;
      }
      out.scroll = vec2<f32>(bias_in.w) + wave;
      out.bias = vec4<f32>(0.0, 0.0, 0.0, 0.0);
      out.mode = 3.0;
    }
  }
  return out;
}

@fragment
fn fs_main(v: VertexOut) -> @location(0) vec4<f32> {
  let screen = v.position.xy;
  if (pc.clipped != 0 && (any(screen < pc.clip_rect.xy) || any(screen >= pc.clip_rect.xy + pc.clip_rect.zw))) {
    discard;
  }
  let mode = v.mode;

  if (mode > 16.5 && mode < 17.5) {
    // Wall corner: solid faces are black; textured faces sample their sprite as the mesh program does.
    if (v.scroll.x < 0.5) {
      return vec4<f32>(0.0, 0.0, 0.0, 1.0);
    }
    let page = vec2<f32>(textureDimensions(sprites, 0));
    let size = v.bounds.zw * page;
    var pixels = floor(v.local * size);
    if (v.scroll.y > 0.5) {
      pixels = glsl_mod(pixels, size);
    } else {
      pixels = clamp(pixels, vec2<f32>(0.0, 0.0), size - vec2<f32>(1.0, 1.0));
    }
    let coords = (v.bounds.xy * page + pixels + 0.5) / page;
    let sampled = textureSampleLevel(sprites, sprite_sampler, coords, i32(v.layer), 0.0);
    return vec4<f32>(sampled.rgb * v.tint.x, sampled.a);
  }

  if (mode > 12.5 && mode < 13.5) {
    // A surface map: the sampled surface, grey outside its rectangle.
    let uv = v.bounds.xy + v.local * v.bounds.zw;
    if (any(uv < vec2<f32>(0.0, 0.0)) || any(uv >= vec2<f32>(1.0, 1.0))) {
      return vec4<f32>(27.0 / 255.0, 27.0 / 255.0, 27.0 / 255.0, 1.0);
    }
    return textureSampleLevel(surface, surface_sampler, uv, 0.0);
  }

  if (mode > 14.5 && mode < 15.5) {
    // Marker coverage at one logical pixel per drawable pixel uses four subpixel steps.
    let p = screen;
    var coverage = clamp(min(v.bias.xy + vec2<f32>(4.0, 4.0) * v.scroll.x, floor(p) + vec2<f32>(1.0, 1.0)) - max(v.bias.xy, floor(p)), vec2<f32>(0.0, 0.0), vec2<f32>(1.0, 1.0));
    if (v.scroll.x == 1.0) {
      coverage = floor(coverage * 4.0 + 0.5) / 4.0;
    }
    return vec4<f32>(v.tint.rgb, coverage.x * coverage.y);
  }

  if (mode > 15.5 && mode < 16.5) {
    // The minimap arrow: a rotated sample of its surface, un-premultiplied.
    let d = (v.local - 0.5) * 32.0;
    let uv = vec2<f32>((v.scroll.x * d.x + v.scroll.y * d.y) / 8.0 + 0.5, (-v.scroll.y * d.x + v.scroll.x * d.y) / 32.0 + 0.625);
    if (any(uv < vec2<f32>(0.0, 0.0)) || any(uv >= vec2<f32>(1.0, 1.0))) {
      return vec4<f32>(0.0, 0.0, 0.0, 0.0);
    }
    var arrow = textureSampleLevel(surface, surface_sampler, uv, 0.0);
    if (arrow.a > 0.0) {
      arrow = vec4<f32>(arrow.rgb / arrow.a, arrow.a);
    }
    return arrow;
  }

  if (mode > 6.5 && mode < 7.5) {
    // A bordered box: the tint inside, a black one-pixel edge with 0.8 alpha outside.
    let pixel = floor(v.local * (v.scroll + 8.0));
    let d = max(vec2<f32>(4.0, 4.0) - pixel, pixel - (v.scroll + 3.0));
    let edge = max(d.x, d.y);
    if (edge <= 0.0) {
      return v.tint;
    }
    return vec4<f32>(0.0, 0.0, 0.0, select(0.0, 0.8, edge <= 1.0));
  }

  if (mode > 1.5 && mode < 2.5) {
    return v.tint;
  }

  if (mode > 0.5 && mode < 1.5) {
    // A radial gradient from the tint to the bias alpha.
    let r = min(1.0, length((v.local - 0.5) * 2.0));
    return vec4<f32>(v.tint.rgb, mix(v.tint.a, v.bias.a, r));
  }

  var coords = v.local;
  if (mode > 2.5 && mode < 3.5) {
    coords = fract(v.local + v.scroll);
  }
  if (mode > 7.5 && mode < 8.5) {
    let p = v.local - 0.5;
    coords = vec2<f32>(p.x * v.scroll.x + p.y * v.scroll.y, -p.x * v.scroll.y + p.y * v.scroll.x) + 0.5;
    if (any(coords < vec2<f32>(0.0, 0.0)) || any(coords > vec2<f32>(1.0, 1.0))) {
      return vec4<f32>(0.0, 0.0, 0.0, 0.0);
    }
  }
  var uv = v.bounds.xy + coords * v.bounds.zw;
  let standalone = pc.standalone != 0;
  var dims = vec2<f32>(textureDimensions(sprites, 0));
  if (standalone) {
    dims = vec2<f32>(textureDimensions(surface, 0));
  }
  let half_texel = vec2<f32>(0.5, 0.5) / dims;
  // Bitmaps in the atlas' dynamic pages clamp to their own region like sprites (the neighbouring slot
  // must never show); mode 4 stays unclamped, and a standalone surface is bounded by the [0,1] test.
  if (mode < 3.5 || (mode > 4.5 && !standalone)) {
    uv = clamp(uv, v.bounds.xy + half_texel, v.bounds.xy + v.bounds.zw - half_texel);
  }
  if (standalone && mode > 5.5 && (any(uv < vec2<f32>(0.0, 0.0)) || any(uv > vec2<f32>(1.0, 1.0)))) {
    return vec4<f32>(0.0, 0.0, 0.0, 0.0);
  }
  var texel: vec4<f32>;
  if (standalone) {
    texel = textureSampleLevel(surface, surface_sampler, uv, 0.0);
  } else {
    texel = textureSampleLevel(sprites, sprite_sampler, uv, i32(v.layer), 0.0);
  }
  // UI is composited by the regular display list, which uses the premultiplied bitmap colour once.
  if (mode > 4.5 && mode < 5.5 && texel.a > 0.0) {
    texel = vec4<f32>(texel.rgb / texel.a, texel.a);
  }
  // Stasis: a greyscale of ordinary sprites.
  if (v.scroll.x > 0.5 && (mode < 0.5 || (mode > 5.5 && mode < 6.5))) {
    texel = vec4<f32>(vec3<f32>(dot(texel.rgb, vec3<f32>(0.3, 0.59, 0.11))), texel.a);
  }
  return clamp(texel * v.tint + v.bias, vec4<f32>(0.0, 0.0, 0.0, 0.0), vec4<f32>(1.0, 1.0, 1.0, 1.0));
}
)WGSL";

const std::string_view mesh = R"WGSL(
// Prepared meshes: pixel positions with the window viewport, sampled from the atlas (group 0) with the
// uniform block (group 2) for the viewport.
struct Params {
  viewport: vec2<f32>,
  ground_origin: vec2<f32>,
  ground_x: vec2<f32>,
  ground_y: vec2<f32>,
  ground_time: f32,
  standalone: i32,
  triangle_list: i32,
  ground_space: i32,
  clip_rect: vec4<f32>,
  clipped: i32,
};

@group(0) @binding(0) var sprites: texture_2d_array<f32>;
@group(0) @binding(1) var sprite_sampler: sampler;
@group(2) @binding(0) var<uniform> pc: Params;

struct VertexOut {
  @builtin(position) position: vec4<f32>,
  @location(0) uv: vec2<f32>,
  @location(1) lit: f32,
  @location(2) @interpolate(flat) bounds: vec4<f32>,
  @location(3) @interpolate(flat) layer: f32,
  @location(4) @interpolate(flat) flat_color: vec3<f32>,
  @location(5) @interpolate(flat) textured: f32,
  @location(6) @interpolate(flat) repeat_texture: f32,
};

@vertex
fn vs_main(
  @location(0) position: vec2<f32>,
  @location(1) texcoord: vec2<f32>,
  @location(2) shade: f32,
  @location(3) sprite_uv: vec4<f32>,
  @location(4) page: f32,
  @location(5) mesh_color: vec3<f32>,
  @location(6) textured_flag: f32,
  @location(7) repeat_flag: f32
) -> VertexOut {
  var out: VertexOut;
  out.position = vec4<f32>(position.x / pc.viewport.x * 2.0 - 1.0, 1.0 - position.y / pc.viewport.y * 2.0, 0.0, 1.0);
  out.uv = texcoord;
  out.lit = shade;
  out.bounds = sprite_uv;
  out.layer = page;
  out.flat_color = mesh_color;
  out.textured = textured_flag;
  out.repeat_texture = repeat_flag;
  return out;
}

@fragment
fn fs_main(v: VertexOut) -> @location(0) vec4<f32> {
  if (!(v.textured > 0.5)) {
    return vec4<f32>(v.flat_color * v.lit, 1.0);
  }
  let page = vec2<f32>(textureDimensions(sprites, 0));
  let size = v.bounds.zw * page;
  let pixels_raw = floor(v.uv * size);
  var pixels = clamp(pixels_raw, vec2<f32>(0.0, 0.0), size - vec2<f32>(1.0, 1.0));
  if (v.repeat_texture > 0.5) {
    pixels = pixels_raw - size * floor(pixels_raw / size);
  }
  let coords = (v.bounds.xy * page + pixels + 0.5) / page;
  let texel = textureSampleLevel(sprites, sprite_sampler, coords, i32(v.layer), 0.0);
  return vec4<f32>(texel.rgb * v.lit, texel.a);
}
)WGSL";

const std::string_view model = R"WGSL(
// Registered models: each instance is a quad (group-free instance attributes 4 to 11), the geometry is
// vertex buffer 0. The math is the GLSL model program's.
struct Params {
  viewport: vec2<f32>,
  ground_origin: vec2<f32>,
  ground_x: vec2<f32>,
  ground_y: vec2<f32>,
  ground_time: f32,
  standalone: i32,
  triangle_list: i32,
  ground_space: i32,
  clip_rect: vec4<f32>,
  clipped: i32,
};

@group(0) @binding(0) var sprites: texture_2d_array<f32>;
@group(0) @binding(1) var sprite_sampler: sampler;
@group(2) @binding(0) var<uniform> pc: Params;

struct VertexOut {
  @builtin(position) position: vec4<f32>,
  @location(0) uv: vec2<f32>,
  @location(1) @interpolate(flat) lit: f32,
  @location(2) @interpolate(flat) bounds: vec4<f32>,
  @location(3) @interpolate(flat) layer: f32,
  @location(4) @interpolate(flat) flat_color: vec3<f32>,
  @location(5) @interpolate(flat) textured: f32,
};

@vertex
fn vs_main(
  @location(0) position: vec3<f32>,
  @location(1) normal: vec3<f32>,
  @location(2) texcoord: vec2<f32>,
  @location(3) solid: f32,
  @location(4) origin: vec2<f32>,
  @location(5) axis_x: vec2<f32>,
  @location(6) axis_y: vec2<f32>,
  @location(7) axis_z: vec2<f32>,
  @location(8) sprite_uv: vec4<f32>,
  @location(9) page: f32,
  @location(10) multiply: vec4<f32>,
  @location(11) rotation: vec4<f32>
) -> VertexOut {
  var out: VertexOut;
  let pixel = origin + axis_x * position.x + axis_y * position.y + axis_z * position.z;
  out.position = vec4<f32>(pixel.x / pc.viewport.x * 2.0 - 1.0, 1.0 - pixel.y / pc.viewport.y * 2.0, 0.0, 1.0);
  // Lighting3D.LIGHT_VECTOR = normalize(1, 3, 2), in world space after the object rotation.
  let n = vec3<f32>(normal.x * rotation.x - normal.y * rotation.y, normal.x * rotation.y + normal.y * rotation.x, normal.z);
  out.lit = 0.75 + 0.25 * max(0.0, dot(n, vec3<f32>(1.0, 3.0, 2.0) / 3.7416573867739413));
  out.uv = texcoord;
  out.bounds = sprite_uv;
  out.layer = page;
  out.flat_color = multiply.rgb;
  out.textured = select(0.0, 1.0, solid < 0.5 && multiply.a > 0.5);
  return out;
}

@fragment
fn fs_main(v: VertexOut) -> @location(0) vec4<f32> {
  if (v.textured < 0.5) {
    return vec4<f32>(v.flat_color * v.lit, 1.0);
  }
  let page = vec2<f32>(textureDimensions(sprites, 0));
  let size = v.bounds.zw * page;
  let wrapped = floor(v.uv * size);
  let coords = (v.bounds.xy * page + (wrapped - size * floor(wrapped / size)) + 0.5) / page;
  let texel = textureSampleLevel(sprites, sprite_sampler, coords, i32(v.layer), 0.0);
  return vec4<f32>(texel.rgb * v.lit, texel.a);
}
)WGSL";

const std::string_view post = R"WGSL(
// Post effects, drawn over a full-screen strip. Group 0: the scene (texelFetch-style loads), group 1:
// the uniform block. Modes: 0 filter (blur, blind), 1 vignette, 2 sidebar.
struct Post {
  viewport: vec2<f32>,
  pad: vec2<f32>,
  rect: vec4<f32>,
  region: vec2<f32>,
  axis: vec2<f32>,
  blur: f32,
  mode: i32,
  blind: i32,
  alpha: f32,
  sidebar: f32,
};

@group(0) @binding(0) var scene: texture_2d<f32>;
@group(1) @binding(0) var<uniform> pc: Post;

@vertex
fn vs_main(@builtin(vertex_index) vertex: u32) -> @builtin(position) vec4<f32> {
  let local = vec2<f32>(f32(vertex & 1u), f32((vertex >> 1u) & 1u));
  let pixel = pc.rect.xy + pc.rect.zw * local;
  return vec4<f32>(pixel.x / pc.viewport.x * 2.0 - 1.0, 1.0 - pixel.y / pc.viewport.y * 2.0, 0.0, 1.0);
}

// A scene texel, or transparent outside the region (texelFetch with the GLSL bounds test).
fn fetch_scene(c: vec2<i32>) -> vec4<f32> {
  if (c.x < 0 || c.y < 0 || f32(c.x) >= pc.region.x || f32(c.y) >= pc.region.y) {
    return vec4<f32>(0.0, 0.0, 0.0, 0.0);
  }
  return textureLoad(scene, c, 0);
}

@fragment
fn fs_main(@builtin(position) frag: vec4<f32>) -> @location(0) vec4<f32> {
  let screen = frag.xy;
  if (pc.mode == 1) {
    // HurtOverlay: radial gradient ratios 0/155/255, alphas 0/0/.92, ellipse through the corners.
    let r = length((screen - pc.region * 0.5) / (pc.region * 0.70710678));
    let edge = clamp((r - 155.0 / 255.0) / (1.0 - 155.0 / 255.0), 0.0, 1.0) * 0.92;
    return vec4<f32>(1.0, 55.0 / 255.0, 0.0, edge * pc.alpha);
  }
  if (pc.mode == 2) {
    let t = clamp((screen.x - (pc.region.x - pc.sidebar)) / pc.sidebar, 0.0, 1.0);
    return vec4<f32>(0.0, 0.0, 0.0, t);
  }
  let c = vec2<i32>(screen);
  var sum = vec4<f32>(0.0, 0.0, 0.0, 0.0);
  if (pc.blur <= 0.0) {
    sum = fetch_scene(c);
  } else {
    // The weights only sum to the blur width from 1 px up; dividing by their total keeps a sub-pixel
    // blur from brightening.
    let half_width = pc.blur * 0.5;
    let n = min(i32(ceil(half_width)), 160);
    let tap_step = vec2<i32>(pc.axis);
    var total = 0.0;
    for (var i = -n; i <= n; i = i + 1) {
      let w = clamp(half_width - abs(f32(i)) + 0.5, 0.0, 1.0);
      sum = sum + w * fetch_scene(c + tap_step * i);
      total = total + w;
    }
    sum = sum / total;
  }
  var rgb = sum.rgb;
  if (pc.blind != 0) {
    rgb = vec3<f32>(0.05 * (sum.r + sum.g + sum.b));
  }
  return vec4<f32>(rgb, 1.0);
}
)WGSL";
const std::string_view ui = R"WGSL(
// RmlUi programs. Group 0: the surface and its sampler (the surface layout of WgpuPipelines.cpp). Group 1: the
// UiBlock uniform (dynamic offset). The geometry is in window pixels with a top-left origin; the clip transform
// is the Vulkan projection (y = 1 - 2 y / height), so the positions need no flip.
struct UiBlock {
  transform: mat4x4<f32>,
  translate: vec2<f32>,
  textured: i32,
  opacity: f32,
  dst_rect: vec4<f32>,
  src_rect: vec4<f32>,
  step_uv: vec2<f32>,
  pad0: vec2<f32>,
  clamp_rect: vec4<f32>,
  weights: vec4<f32>,
  window_size: vec2<f32>,
  blur: i32,
  pad1: i32,
};

@group(0) @binding(0) var surface: texture_2d<f32>;
@group(0) @binding(1) var surface_sampler: sampler;
@group(1) @binding(0) var<uniform> ui: UiBlock;

struct GeometryIn {
  @location(0) position: vec2<f32>,
  @location(1) color: vec4<f32>,
  @location(2) uv: vec2<f32>,
};

struct GeometryOut {
  @builtin(position) clip: vec4<f32>,
  @location(0) color: vec4<f32>,
  @location(1) uv: vec2<f32>,
};

@vertex
fn geometry_vs(vin: GeometryIn) -> GeometryOut {
  var vout: GeometryOut;
  vout.clip = ui.transform * vec4<f32>(vin.position + ui.translate, 0.0, 1.0);
  vout.color = vin.color;
  vout.uv = vin.uv;
  return vout;
}

// Premultiplied output: the pipeline blends ONE, ONE_MINUS_SRC_ALPHA.
@fragment
fn geometry_fs(fin: GeometryOut) -> @location(0) vec4<f32> {
  let sampled = textureSampleLevel(surface, surface_sampler, fin.uv, 0.0);
  let factor = select(vec4<f32>(1.0, 1.0, 1.0, 1.0), sampled, ui.textured != 0);
  return fin.color * factor;
}

struct PostOut {
  @builtin(position) clip: vec4<f32>,
  @location(0) uv: vec2<f32>,
};

// A strip of four corners: dst_rect is the target rectangle, src_rect the uv rectangle, both in mixing order.
@vertex
fn post_vs(@builtin(vertex_index) corner_index: u32) -> PostOut {
  let corner = vec2<f32>(f32(corner_index & 1u), f32((corner_index >> 1u) & 1u));
  let pixel = mix(ui.dst_rect.xy, ui.dst_rect.zw, corner);
  var vout: PostOut;
  vout.clip = vec4<f32>(2.0 * pixel.x / ui.window_size.x - 1.0, 1.0 - 2.0 * pixel.y / ui.window_size.y, 0.0, 1.0);
  vout.uv = mix(ui.src_rect.xy, ui.src_rect.zw, corner);
  return vout;
}

// Copies (blur 0) or the seven-tap blur along step_uv, each tap inside the clamp region, with symmetric weights.
// Every output is multiplied by opacity.
@fragment
fn post_fs(fin: PostOut) -> @location(0) vec4<f32> {
  if (ui.blur == 0) {
    return textureSampleLevel(surface, surface_sampler, fin.uv, 0.0) * ui.opacity;
  }
  var total = vec4<f32>(0.0, 0.0, 0.0, 0.0);
  for (var i: i32 = 0; i < 7; i = i + 1) {
    let steps = i - 3;
    let tap_uv = fin.uv - f32(steps) * ui.step_uv;
    let inside = step(ui.clamp_rect.xy, tap_uv) * step(tap_uv, ui.clamp_rect.zw);
    let weight = ui.weights[u32(abs(steps))];
    total = total + textureSampleLevel(surface, surface_sampler, tap_uv, 0.0) * (inside.x * inside.y * weight);
  }
  return total * ui.opacity;
}
)WGSL";
}  // namespace realm::gfx::wgpu::shaders
