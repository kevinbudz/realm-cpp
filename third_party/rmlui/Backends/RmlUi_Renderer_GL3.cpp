#include "RmlUi_Renderer_GL3.h"
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/DecorationTypes.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/Platform.h>
#include <RmlUi/Core/SystemInterface.h>
#include <algorithm>
#include <cmath>
#include <string.h>

#if defined RMLUI_PLATFORM_WIN32_NATIVE
	// function call missing argument list
	#pragma warning(disable : 4551)
	// unreferenced local function has been removed
	#pragma warning(disable : 4505)
#endif

#if defined RMLUI_PLATFORM_EMSCRIPTEN
	#define RMLUI_SHADER_HEADER_VERSION "#version 300 es\nprecision highp float;\n"
	#include <GLES3/gl3.h>
#elif defined __ANDROID__
	#define RMLUI_SHADER_HEADER_VERSION "#version 320 es\nprecision highp float;\n"
	#include <GLES3/gl32.h>
#elif defined RMLUI_GL3_CUSTOM_LOADER
	#define RMLUI_SHADER_HEADER_VERSION "#version 330\n"
	#include RMLUI_GL3_CUSTOM_LOADER
#else
	#define RMLUI_SHADER_HEADER_VERSION "#version 330\n"
	#define GLAD_GL_IMPLEMENTATION
	#include "RmlUi_Include_GL3.h"
#endif

// Determines the anti-aliasing quality when creating layers. Enables better-looking visuals, especially when transforms are applied.
#ifndef RMLUI_NUM_MSAA_SAMPLES
	#define RMLUI_NUM_MSAA_SAMPLES 2
#endif

#define MAX_NUM_STOPS 16
#define BLUR_SIZE 7
#define BLUR_NUM_WEIGHTS ((BLUR_SIZE + 1) / 2)

#define RMLUI_STRINGIFY_IMPL(x) #x
#define RMLUI_STRINGIFY(x) RMLUI_STRINGIFY_IMPL(x)

#define RMLUI_SHADER_HEADER \
	RMLUI_SHADER_HEADER_VERSION "#define MAX_NUM_STOPS " RMLUI_STRINGIFY(MAX_NUM_STOPS) "\n#line " RMLUI_STRINGIFY(__LINE__) "\n"

static const char* shader_vert_main = RMLUI_SHADER_HEADER R"(
uniform vec2 _translate;
uniform mat4 _transform;

in vec2 inPosition;
in vec4 inColor0;
in vec2 inTexCoord0;

out vec2 fragTexCoord;
out vec4 fragColor;

void main() {
	fragTexCoord = inTexCoord0;
	fragColor = inColor0;

	vec2 translatedPos = inPosition + _translate;
	vec4 outPos = _transform * vec4(translatedPos, 0.0, 1.0);

    gl_Position = outPos;
}
)";
static const char* shader_frag_texture = RMLUI_SHADER_HEADER R"(
uniform sampler2D _tex;
in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

void main() {
	vec4 texColor = texture(_tex, fragTexCoord);
	finalColor = fragColor * texColor;
}
)";
static const char* shader_frag_color = RMLUI_SHADER_HEADER R"(
in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

void main() {
	finalColor = fragColor;
}
)";

enum class ShaderGradientFunction { Linear, Radial, Conic, RepeatingLinear, RepeatingRadial, RepeatingConic }; // Must match shader definitions below.

static const char* shader_frag_gradient = RMLUI_SHADER_HEADER R"(
#define LINEAR 0
#define RADIAL 1
#define CONIC 2
#define REPEATING_LINEAR 3
#define REPEATING_RADIAL 4
#define REPEATING_CONIC 5
#define PI 3.14159265

uniform int _func; // one of the above definitions
uniform vec2 _p;   // linear: starting point,         radial: center,                        conic: center
uniform vec2 _v;   // linear: vector to ending point, radial: 2d curvature (inverse radius), conic: angled unit vector
uniform vec4 _stop_colors[MAX_NUM_STOPS];
uniform float _stop_positions[MAX_NUM_STOPS]; // normalized, 0 -> starting point, 1 -> ending point
uniform int _num_stops;

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

vec4 mix_stop_colors(float t) {
	vec4 color = _stop_colors[0];

	for (int i = 1; i < _num_stops; i++)
		color = mix(color, _stop_colors[i], smoothstep(_stop_positions[i-1], _stop_positions[i], t));

	return color;
}

void main() {
	float t = 0.0;

	if (_func == LINEAR || _func == REPEATING_LINEAR)
	{
		float dist_square = dot(_v, _v);
		vec2 V = fragTexCoord - _p;
		t = dot(_v, V) / dist_square;
	}
	else if (_func == RADIAL || _func == REPEATING_RADIAL)
	{
		vec2 V = fragTexCoord - _p;
		t = length(_v * V);
	}
	else if (_func == CONIC || _func == REPEATING_CONIC)
	{
		mat2 R = mat2(_v.x, -_v.y, _v.y, _v.x);
		vec2 V = R * (fragTexCoord - _p);
		t = 0.5 + atan(-V.x, V.y) / (2.0 * PI);
	}

	if (_func == REPEATING_LINEAR || _func == REPEATING_RADIAL || _func == REPEATING_CONIC)
	{
		float t0 = _stop_positions[0];
		float t1 = _stop_positions[_num_stops - 1];
		t = t0 + mod(t - t0, t1 - t0);
	}

	finalColor = fragColor * mix_stop_colors(t);
}
)";

// "Creation" by Danilo Guanabara, based on: https://www.shadertoy.com/view/XsXXDn
static const char* shader_frag_creation = RMLUI_SHADER_HEADER R"(
uniform float _value;
uniform vec2 _dimensions;

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

void main() {
	float t = _value;
	vec3 c;
	float l;
	for (int i = 0; i < 3; i++) {
		vec2 p = fragTexCoord;
		vec2 uv = p;
		p -= .5;
		p.x *= _dimensions.x / _dimensions.y;
		float z = t + float(i) * .07;
		l = length(p);
		uv += p / l * (sin(z) + 1.) * abs(sin(l * 9. - z - z));
		c[i] = .01 / length(mod(uv, 1.) - .5);
	}
	finalColor = vec4(c / l, fragColor.a);
}
)";

static const char* shader_vert_passthrough = RMLUI_SHADER_HEADER R"(
in vec2 inPosition;
in vec2 inTexCoord0;

out vec2 fragTexCoord;

void main() {
	fragTexCoord = inTexCoord0;
    gl_Position = vec4(inPosition, 0.0, 1.0);
}
)";
static const char* shader_frag_passthrough = RMLUI_SHADER_HEADER R"(
uniform sampler2D _tex;
in vec2 fragTexCoord;
out vec4 finalColor;

void main() {
	finalColor = texture(_tex, fragTexCoord);
}
)";
static const char* shader_frag_color_matrix = RMLUI_SHADER_HEADER R"(
uniform sampler2D _tex;
uniform mat4 _color_matrix;

in vec2 fragTexCoord;
out vec4 finalColor;

void main() {
	// The general case uses a 4x5 color matrix for full rgba transformation, plus a constant term with the last column.
	// However, we only consider the case of rgb transformations. Thus, we could in principle use a 3x4 matrix, but we
	// keep the alpha row for simplicity.
	// In the general case we should do the matrix transformation in non-premultiplied space. However, without alpha
	// transformations, we can do it directly in premultiplied space to avoid the extra division and multiplication
	// steps. In this space, the constant term needs to be multiplied by the alpha value, instead of unity.
	vec4 texColor = texture(_tex, fragTexCoord);
	vec3 transformedColor = vec3(_color_matrix * texColor);
	finalColor = vec4(transformedColor, texColor.a);
}
)";
static const char* shader_frag_blend_mask = RMLUI_SHADER_HEADER R"(
uniform sampler2D _tex;
uniform sampler2D _texMask;

in vec2 fragTexCoord;
out vec4 finalColor;

void main() {
	vec4 texColor = texture(_tex, fragTexCoord);
	float maskAlpha = texture(_texMask, fragTexCoord).a;
	finalColor = texColor * maskAlpha;
}
)";

#define RMLUI_SHADER_BLUR_HEADER \
	RMLUI_SHADER_HEADER "\n#define BLUR_SIZE " RMLUI_STRINGIFY(BLUR_SIZE) "\n#define BLUR_NUM_WEIGHTS " RMLUI_STRINGIFY(BLUR_NUM_WEIGHTS)

static const char* shader_vert_blur = RMLUI_SHADER_BLUR_HEADER R"(
uniform vec2 _texelOffset;

in vec3 inPosition;
in vec2 inTexCoord0;

out vec2 fragTexCoord[BLUR_SIZE];

void main() {
	for(int i = 0; i < BLUR_SIZE; i++)
		fragTexCoord[i] = inTexCoord0 - float(i - BLUR_NUM_WEIGHTS + 1) * _texelOffset;
    gl_Position = vec4(inPosition, 1.0);
}
)";
static const char* shader_frag_blur = RMLUI_SHADER_BLUR_HEADER R"(
uniform sampler2D _tex;
uniform float _weights[BLUR_NUM_WEIGHTS];
uniform vec2 _texCoordMin;
uniform vec2 _texCoordMax;

in vec2 fragTexCoord[BLUR_SIZE];
out vec4 finalColor;

void main() {
	vec4 color = vec4(0.0, 0.0, 0.0, 0.0);
	for(int i = 0; i < BLUR_SIZE; i++)
	{
		vec2 in_region = step(_texCoordMin, fragTexCoord[i]) * step(fragTexCoord[i], _texCoordMax);
		color += texture(_tex, fragTexCoord[i]) * in_region.x * in_region.y * _weights[abs(i - BLUR_NUM_WEIGHTS + 1)];
	}
	finalColor = color;
}
)";
static const char* shader_frag_drop_shadow = RMLUI_SHADER_HEADER R"(
uniform sampler2D _tex;
uniform vec2 _texCoordMin;
uniform vec2 _texCoordMax;
uniform vec4 _color;

in vec2 fragTexCoord;
out vec4 finalColor;

void main() {
	vec2 in_region = step(_texCoordMin, fragTexCoord) * step(fragTexCoord, _texCoordMax);
	finalColor = texture(_tex, fragTexCoord).a * in_region.x * in_region.y * _color;
}
)";

enum class ProgramId {
	None,
	Color,
	Texture,
	Gradient,
	Creation,
	Passthrough,
	ColorMatrix,
	BlendMask,
	Blur,
	DropShadow,
	Count,
};
enum class VertShaderId {
	Main,
	Passthrough,
	Blur,
	Count,
};
enum class FragShaderId {
	Color,
	Texture,
	Gradient,
	Creation,
	Passthrough,
	ColorMatrix,
	BlendMask,
	Blur,
	DropShadow,
	Count,
};
enum class UniformId {
	Translate,
	Transform,
	Tex,
	Color,
	ColorMatrix,
	TexelOffset,
	TexCoordMin,
	TexCoordMax,
	TexMask,
	Weights,
	Func,
	P,
	V,
	StopColors,
	StopPositions,
	NumStops,
	Value,
	Dimensions,
	Count,
};

namespace Gfx {

static const char* const program_uniform_names[(size_t)UniformId::Count] = {"_translate", "_transform", "_tex", "_color", "_color_matrix",
	"_texelOffset", "_texCoordMin", "_texCoordMax", "_texMask", "_weights[0]", "_func", "_p", "_v", "_stop_colors[0]", "_stop_positions[0]",
	"_num_stops", "_value", "_dimensions"};

enum class VertexAttribute { Position, Color0, TexCoord0, Count };
static const char* const vertex_attribute_names[(size_t)VertexAttribute::Count] = {"inPosition", "inColor0", "inTexCoord0"};

struct VertShaderDefinition {
	VertShaderId id;
	const char* name_str;
	const char* code_str;
};
struct FragShaderDefinition {
	FragShaderId id;
	const char* name_str;
	const char* code_str;
};
struct ProgramDefinition {
	ProgramId id;
	const char* name_str;
	VertShaderId vert_shader;
	FragShaderId frag_shader;
};

// clang-format off
static const VertShaderDefinition vert_shader_definitions[] = {
	{VertShaderId::Main,        "main",         shader_vert_main},
	{VertShaderId::Passthrough, "passthrough",  shader_vert_passthrough},
	{VertShaderId::Blur,        "blur",         shader_vert_blur},
};
static const FragShaderDefinition frag_shader_definitions[] = {
	{FragShaderId::Color,       "color",        shader_frag_color},
	{FragShaderId::Texture,     "texture",      shader_frag_texture},
	{FragShaderId::Gradient,    "gradient",     shader_frag_gradient},
	{FragShaderId::Creation,    "creation",     shader_frag_creation},
	{FragShaderId::Passthrough, "passthrough",  shader_frag_passthrough},
	{FragShaderId::ColorMatrix, "color_matrix", shader_frag_color_matrix},
	{FragShaderId::BlendMask,   "blend_mask",   shader_frag_blend_mask},
	{FragShaderId::Blur,        "blur",         shader_frag_blur},
	{FragShaderId::DropShadow,  "drop_shadow",  shader_frag_drop_shadow},
};
static const ProgramDefinition program_definitions[] = {
	{ProgramId::Color,       "color",        VertShaderId::Main,        FragShaderId::Color},
	{ProgramId::Texture,     "texture",      VertShaderId::Main,        FragShaderId::Texture},
	{ProgramId::Gradient,    "gradient",     VertShaderId::Main,        FragShaderId::Gradient},
	{ProgramId::Creation,    "creation",     VertShaderId::Main,        FragShaderId::Creation},
	{ProgramId::Passthrough, "passthrough",  VertShaderId::Passthrough, FragShaderId::Passthrough},
	{ProgramId::ColorMatrix, "color_matrix", VertShaderId::Passthrough, FragShaderId::ColorMatrix},
	{ProgramId::BlendMask,   "blend_mask",   VertShaderId::Passthrough, FragShaderId::BlendMask},
	{ProgramId::Blur,        "blur",         VertShaderId::Blur,        FragShaderId::Blur},
	{ProgramId::DropShadow,  "drop_shadow",  VertShaderId::Passthrough, FragShaderId::DropShadow},
};
// clang-format on

template <typename T, typename Enum>
class EnumArray {
public:
	const T& operator[](Enum id) const
	{
		RMLUI_ASSERT((size_t)id < (size_t)Enum::Count);
		return ids[size_t(id)];
	}
	T& operator[](Enum id)
	{
		RMLUI_ASSERT((size_t)id < (size_t)Enum::Count);
		return ids[size_t(id)];
	}
	auto begin() const { return ids.begin(); }
	auto end() const { return ids.end(); }

private:
	Rml::Array<T, (size_t)Enum::Count> ids = {};
};

using Programs = EnumArray<GLuint, ProgramId>;
using VertShaders = EnumArray<GLuint, VertShaderId>;
using FragShaders = EnumArray<GLuint, FragShaderId>;

class Uniforms {
public:
	GLint Get(ProgramId id, UniformId uniform) const
	{
		auto it = map.find(ToKey(id, uniform));
		if (it != map.end())
			return it->second;
		return -1;
	}
	void Insert(ProgramId id, UniformId uniform, GLint location) { map[ToKey(id, uniform)] = location; }

private:
	using Key = uint64_t;
	Key ToKey(ProgramId id, UniformId uniform) const { return (static_cast<Key>(id) << 32) | static_cast<Key>(uniform); }
	Rml::UnorderedMap<Key, GLint> map;
};

struct ProgramData {
	Programs programs;
	VertShaders vert_shaders;
	FragShaders frag_shaders;
	Uniforms uniforms;
};

struct CompiledGeometryData {
	GLuint vao;
	GLuint vbo;
	GLuint ibo;
	GLsizei draw_count;
	Rml::Vector2f bounds_min; // realm: vertex bounds, used to limit the frame's written region.
	Rml::Vector2f bounds_max;
};

struct FramebufferData {
	int width, height;
	GLuint framebuffer;
	GLuint color_tex_buffer;
	GLuint color_render_buffer;
	GLuint depth_stencil_buffer;
	bool owns_depth_stencil_buffer;
};

enum class FramebufferAttachment { None, DepthStencil };

static void CheckGLError(const char* operation_name)
{
#ifndef NDEBUG // realm: glGetError synchronises with the driver, so release builds never call it.
	GLenum error_code = glGetError();
	if (error_code != GL_NO_ERROR)
	{
		static const Rml::Pair<GLenum, const char*> error_names[] = {{GL_INVALID_ENUM, "GL_INVALID_ENUM"}, {GL_INVALID_VALUE, "GL_INVALID_VALUE"},
			{GL_INVALID_OPERATION, "GL_INVALID_OPERATION"}, {GL_OUT_OF_MEMORY, "GL_OUT_OF_MEMORY"}};
		const char* error_str = "''";
		for (auto& err : error_names)
		{
			if (err.first == error_code)
			{
				error_str = err.second;
				break;
			}
		}
		Rml::Log::Message(Rml::Log::LT_ERROR, "OpenGL error during %s. Error code 0x%x (%s).", operation_name, error_code, error_str);
	}
#endif
	(void)operation_name;
}

// Create the shader, 'shader_type' is either GL_VERTEX_SHADER or GL_FRAGMENT_SHADER.
static bool CreateShader(GLuint& out_shader_id, GLenum shader_type, const char* code_string)
{
	RMLUI_ASSERT(shader_type == GL_VERTEX_SHADER || shader_type == GL_FRAGMENT_SHADER);

	GLuint id = glCreateShader(shader_type);
	glShaderSource(id, 1, (const GLchar**)&code_string, NULL);
	glCompileShader(id);

	GLint status = 0;
	glGetShaderiv(id, GL_COMPILE_STATUS, &status);
	if (status == GL_FALSE)
	{
		GLint info_log_length = 0;
		glGetShaderiv(id, GL_INFO_LOG_LENGTH, &info_log_length);
		char* info_log_string = new char[info_log_length + 1];
		glGetShaderInfoLog(id, info_log_length, NULL, info_log_string);

		Rml::Log::Message(Rml::Log::LT_ERROR, "Compile failure in OpenGL shader: %s", info_log_string);
		delete[] info_log_string;
		glDeleteShader(id);
		return false;
	}

	CheckGLError("CreateShader");

	out_shader_id = id;
	return true;
}

static bool CreateProgram(GLuint& out_program, Uniforms& inout_uniform_map, ProgramId program_id, GLuint vertex_shader, GLuint fragment_shader)
{
	GLuint id = glCreateProgram();
	RMLUI_ASSERT(id);

	for (GLuint i = 0; i < (GLuint)VertexAttribute::Count; i++)
		glBindAttribLocation(id, i, vertex_attribute_names[i]);

	CheckGLError("BindAttribLocations");

	glAttachShader(id, vertex_shader);
	glAttachShader(id, fragment_shader);

	glLinkProgram(id);

	glDetachShader(id, vertex_shader);
	glDetachShader(id, fragment_shader);

	GLint status = 0;
	glGetProgramiv(id, GL_LINK_STATUS, &status);
	if (status == GL_FALSE)
	{
		GLint info_log_length = 0;
		glGetProgramiv(id, GL_INFO_LOG_LENGTH, &info_log_length);
		char* info_log_string = new char[info_log_length + 1];
		glGetProgramInfoLog(id, info_log_length, NULL, info_log_string);

		Rml::Log::Message(Rml::Log::LT_ERROR, "OpenGL program linking failure: %s", info_log_string);
		delete[] info_log_string;
		glDeleteProgram(id);
		return false;
	}

	out_program = id;

	// Make a lookup table for the uniform locations.
	GLint num_active_uniforms = 0;
	glGetProgramiv(id, GL_ACTIVE_UNIFORMS, &num_active_uniforms);

	constexpr size_t name_size = 64;
	GLchar name_buf[name_size] = "";
	for (int unif = 0; unif < num_active_uniforms; ++unif)
	{
		GLint array_size = 0;
		GLenum type = 0;
		GLsizei actual_length = 0;
		glGetActiveUniform(id, unif, name_size, &actual_length, &array_size, &type, name_buf);
		GLint location = glGetUniformLocation(id, name_buf);

		// See if we have the name in our pre-defined name list.
		UniformId program_uniform = UniformId::Count;
		for (int i = 0; i < (int)UniformId::Count; i++)
		{
			const char* uniform_name = program_uniform_names[i];
			if (strcmp(name_buf, uniform_name) == 0)
			{
				program_uniform = (UniformId)i;
				break;
			}
		}

		if ((size_t)program_uniform < (size_t)UniformId::Count)
		{
			inout_uniform_map.Insert(program_id, program_uniform, location);
		}
		else
		{
			Rml::Log::Message(Rml::Log::LT_ERROR, "OpenGL program uses unknown uniform '%s'.", name_buf);
			return false;
		}
	}

	CheckGLError("CreateProgram");

	return true;
}

static bool CreateFramebuffer(FramebufferData& out_fb, int width, int height, int samples, FramebufferAttachment attachment,
	GLuint shared_depth_stencil_buffer)
{
#if defined(RMLUI_PLATFORM_EMSCRIPTEN) || defined(__ANDROID__)
	constexpr GLint wrap_mode = GL_CLAMP_TO_EDGE;
#else
	constexpr GLint wrap_mode = GL_CLAMP_TO_BORDER; // GL_REPEAT GL_MIRRORED_REPEAT GL_CLAMP_TO_EDGE
#endif

	constexpr GLenum color_format = GL_RGBA8;   // GL_RGBA8 GL_SRGB8_ALPHA8 GL_RGBA16F
	constexpr GLint min_mag_filter = GL_LINEAR; // GL_NEAREST
	const Rml::Colourf border_color(0.f, 0.f);

	GLuint framebuffer = 0;
	glGenFramebuffers(1, &framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

	GLuint color_tex_buffer = 0;
	GLuint color_render_buffer = 0;
	if (samples > 0)
	{
		glGenRenderbuffers(1, &color_render_buffer);
		glBindRenderbuffer(GL_RENDERBUFFER, color_render_buffer);
		glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, color_format, width, height);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color_render_buffer);
	}
	else
	{
		glGenTextures(1, &color_tex_buffer);
		glBindTexture(GL_TEXTURE_2D, color_tex_buffer);
		glTexImage2D(GL_TEXTURE_2D, 0, color_format, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_mag_filter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, min_mag_filter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_mode);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_mode);
#if !defined(RMLUI_PLATFORM_EMSCRIPTEN) && !defined(__ANDROID__)
		glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, &border_color[0]);
#endif

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_tex_buffer, 0);
	}

	// Create depth/stencil buffer storage attachment.
	GLuint depth_stencil_buffer = 0;
	if (attachment != FramebufferAttachment::None)
	{
		if (shared_depth_stencil_buffer)
		{
			// Share depth/stencil buffer
			depth_stencil_buffer = shared_depth_stencil_buffer;
		}
		else
		{
			// Create new depth/stencil buffer
			glGenRenderbuffers(1, &depth_stencil_buffer);
			glBindRenderbuffer(GL_RENDERBUFFER, depth_stencil_buffer);

			glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, width, height);
		}

		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth_stencil_buffer);
	}

	const GLuint framebuffer_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (framebuffer_status != GL_FRAMEBUFFER_COMPLETE)
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "OpenGL framebuffer could not be generated. Error code %x.", framebuffer_status);
		return false;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);

	CheckGLError("CreateFramebuffer");

	out_fb = {};
	out_fb.width = width;
	out_fb.height = height;
	out_fb.framebuffer = framebuffer;
	out_fb.color_tex_buffer = color_tex_buffer;
	out_fb.color_render_buffer = color_render_buffer;
	out_fb.depth_stencil_buffer = depth_stencil_buffer;
	out_fb.owns_depth_stencil_buffer = !shared_depth_stencil_buffer;

	return true;
}

static void DestroyFramebuffer(FramebufferData& fb)
{
	if (fb.framebuffer)
		glDeleteFramebuffers(1, &fb.framebuffer);
	if (fb.color_tex_buffer)
		glDeleteTextures(1, &fb.color_tex_buffer);
	if (fb.color_render_buffer)
		glDeleteRenderbuffers(1, &fb.color_render_buffer);
	if (fb.owns_depth_stencil_buffer && fb.depth_stencil_buffer)
		glDeleteRenderbuffers(1, &fb.depth_stencil_buffer);
	fb = {};
}

static GLuint CreateTexture(Rml::Span<const Rml::byte> source_data, Rml::Vector2i source_dimensions)
{
	GLuint texture_id = 0;
	glGenTextures(1, &texture_id);
	if (texture_id == 0)
		return 0;

	glBindTexture(GL_TEXTURE_2D, texture_id);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, source_dimensions.x, source_dimensions.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, source_data.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	glBindTexture(GL_TEXTURE_2D, 0);

	return texture_id;
}

static void BindTexture(const FramebufferData& fb)
{
	if (!fb.color_tex_buffer)
	{
		RMLUI_ERRORMSG("Only framebuffers with color textures can be bound as textures. This framebuffer probably uses multisampling which needs a "
					   "blit step first.");
	}

	glBindTexture(GL_TEXTURE_2D, fb.color_tex_buffer);
}

static bool CreateShaders(ProgramData& data)
{
	RMLUI_ASSERT(std::all_of(data.vert_shaders.begin(), data.vert_shaders.end(), [](auto&& value) { return value == 0; }));
	RMLUI_ASSERT(std::all_of(data.frag_shaders.begin(), data.frag_shaders.end(), [](auto&& value) { return value == 0; }));
	RMLUI_ASSERT(std::all_of(data.programs.begin(), data.programs.end(), [](auto&& value) { return value == 0; }));
	auto ReportError = [](const char* type, const char* name) {
		Rml::Log::Message(Rml::Log::LT_ERROR, "Could not create OpenGL %s: '%s'.", type, name);
		return false;
	};

	for (const VertShaderDefinition& def : vert_shader_definitions)
	{
		if (!CreateShader(data.vert_shaders[def.id], GL_VERTEX_SHADER, def.code_str))
			return ReportError("vertex shader", def.name_str);
	}

	for (const FragShaderDefinition& def : frag_shader_definitions)
	{
		if (!CreateShader(data.frag_shaders[def.id], GL_FRAGMENT_SHADER, def.code_str))
			return ReportError("fragment shader", def.name_str);
	}

	for (const ProgramDefinition& def : program_definitions)
	{
		if (!CreateProgram(data.programs[def.id], data.uniforms, def.id, data.vert_shaders[def.vert_shader], data.frag_shaders[def.frag_shader]))
			return ReportError("program", def.name_str);
	}

	glUseProgram(data.programs[ProgramId::BlendMask]);
	glUniform1i(data.uniforms.Get(ProgramId::BlendMask, UniformId::TexMask), 1);

	glUseProgram(0);

	return true;
}

static void DestroyShaders(const ProgramData& data)
{
	for (GLuint id : data.programs)
		glDeleteProgram(id);

	for (GLuint id : data.vert_shaders)
		glDeleteShader(id);

	for (GLuint id : data.frag_shaders)
		glDeleteShader(id);
}

} // namespace Gfx

RenderInterface_GL3::RenderInterface_GL3()
{
	auto mut_program_data = Rml::MakeUnique<Gfx::ProgramData>();
	if (Gfx::CreateShaders(*mut_program_data))
	{
		program_data = std::move(mut_program_data);
		Rml::Mesh mesh;
		Rml::MeshUtilities::GenerateQuad(mesh, Rml::Vector2f(-1), Rml::Vector2f(2), {});
		fullscreen_quad_geometry = RenderInterface_GL3::CompileGeometry(mesh.vertices, mesh.indices);
	}
}

RenderInterface_GL3::~RenderInterface_GL3()
{
	if (fullscreen_quad_geometry)
	{
		RenderInterface_GL3::ReleaseGeometry(fullscreen_quad_geometry);
		fullscreen_quad_geometry = {};
	}

	if (program_data)
	{
		Gfx::DestroyShaders(*program_data);
		program_data.reset();
	}
}

void RenderInterface_GL3::SetViewport(int width, int height, int offset_x, int offset_y)
{
	EndRecording(); // realm: queued commands use the projection they were recorded with.
	const int new_width = Rml::Math::Max(width, 1);
	const int new_height = Rml::Math::Max(height, 1);
	if (new_width != viewport_width || new_height != viewport_height || offset_x != viewport_offset_x || offset_y != viewport_offset_y)
		++generation; // realm: projection changes; a cached layer is no longer valid.
	viewport_width = new_width;
	viewport_height = new_height;
	viewport_offset_x = offset_x;
	viewport_offset_y = offset_y;
	projection = Rml::Matrix4f::ProjectOrtho(0, (float)viewport_width, (float)viewport_height, 0, -10000, 10000);
}

void RenderInterface_GL3::BeginFrame()
{
	RMLUI_ASSERT(viewport_width >= 1 && viewport_height >= 1);

	SetFrameState();
	SetTransform(nullptr);

	// realm: clear only the region the previous frame could have written. New layer storage is undefined, so clear all of it.
	if (render_layers.BeginFrame(viewport_width, viewport_height))
	{
		previous_bounds = Rml::Rectanglei::FromSize({viewport_width, viewport_height});
		cache_valid = false;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, render_layers.GetTopLayer().framebuffer);

	UseProgram(ProgramId::None);
	program_transform_dirty.set();
	scissor_state = Rml::Rectanglei::MakeInvalid();
	frame_bounds = Rml::Rectanglei::MakeInvalid();
	frame_full_screen = false;

	// realm: draw, scissor and transform calls from here on are queued (see frame_commands). The clear of previous_bounds and the
	// queue run in EndFrame, or earlier if a call cannot be queued. A frame reusing the cached layer runs neither.
	frame_commands.clear();
	frame_flushed = false;
	reused_frame = false;
	recording = true;

	Gfx::CheckGLError("BeginFrame");
}

void RenderInterface_GL3::EndFrame()
{
	const Gfx::FramebufferData& fb_active = render_layers.GetTopLayer();
	const Gfx::FramebufferData& fb_postprocess = render_layers.GetPostprocessPrimary();

	// realm: a frame whose queue and generation equal the last replayed frame's reuses that frame's resolved layer.
	// Otherwise the queue is replayed now. Frames flushed early (see EndRecording) are never reused.
	const bool eligible = recording && !frame_flushed;
	recording = false;
	reused_frame = false;
	if (eligible)
	{
		reused_frame = cache_valid && cached_generation == generation && frame_commands == previous_commands;
		if (reused_frame)
		{
			frame_bounds = cached_frame_bounds;
			frame_full_screen = cached_frame_full_screen;
		}
		else
		{
			ReplayFrameCommands();
		}
	}

	// realm: resolve and composite only the region written this frame. A scissor test left enabled would clip the resolve and composite in
	// ways the bounds do not model, so that case keeps the full-size path.
	const Rml::Rectanglei full_viewport = Rml::Rectanglei::FromSize({viewport_width, viewport_height});
	// The scissor test and box are tracked (see SetGlScissorTest), not read back. A reused frame applies the state its replayed frame left.
	bool scissor_enabled = gl_scissor_on;
	int scissor_box[4] = {gl_scissor_box[0], gl_scissor_box[1], gl_scissor_box[2], gl_scissor_box[3]};
	if (reused_frame)
	{
		scissor_enabled = cached_scissor_enabled;
		for (int i = 0; i < 4; ++i)
			scissor_box[i] = cached_scissor_box[i];
		SetGlScissorTest(scissor_enabled);
		if (scissor_enabled)
			SetGlScissorBox(scissor_box[0], scissor_box[1], scissor_box[2], scissor_box[3]);
	}
	const bool bounded = !frame_full_screen && !scissor_enabled;
	const bool skip = bounded && !frame_bounds.Valid();
	const Rml::Rectanglei region = bounded ? frame_bounds : full_viewport;
	if (!reused_frame)
		previous_bounds = frame_full_screen ? full_viewport : frame_bounds;

	// Resolve MSAA to postprocess framebuffer. A reused frame keeps the resolved layer from the frame that drew it.
	if (!skip && !reused_frame)
	{
		glBindFramebuffer(GL_READ_FRAMEBUFFER, fb_active.framebuffer);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb_postprocess.framebuffer);

		glBlitFramebuffer(region.Left(), viewport_height - region.Bottom(), region.Right(), viewport_height - region.Top(), region.Left(),
			viewport_height - region.Bottom(), region.Right(), viewport_height - region.Top(), GL_COLOR_BUFFER_BIT, GL_NEAREST);
	}

	// Draw to backbuffer
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(viewport_offset_x, viewport_offset_y, viewport_width, viewport_height);

	if (!skip)
	{
		// realm: limit the composite to the region written this frame. Outside it the layer is zero, which leaves the backbuffer unchanged.
		if (bounded)
		{
			SetGlScissorTest(true);
			SetGlScissorBox(viewport_offset_x + region.Left(), viewport_offset_y + viewport_height - region.Bottom(), region.Width(), region.Height());
		}

		// Assuming we have an opaque background, we can just write to it with the premultiplied alpha blend mode and we'll get the correct result.
		// Instead, if we had a transparent destination that didn't use premultiplied alpha, we would need to perform a manual un-premultiplication step.
		glActiveTexture(GL_TEXTURE0);
		Gfx::BindTexture(fb_postprocess);
		UseProgram(ProgramId::Passthrough);
		DrawFullscreenQuad();
	}

	render_layers.EndFrame();

	// realm: remember a replayed frame so the next one can reuse it. A reused frame keeps the record of the frame it reuses.
	if (eligible && !reused_frame)
	{
		cache_valid = true;
		cached_generation = generation;
		cached_frame_bounds = frame_bounds;
		cached_frame_full_screen = frame_full_screen;
		cached_scissor_enabled = scissor_enabled;
		for (int i = 0; i < 4; ++i)
			cached_scissor_box[i] = scissor_box[i];
		previous_commands.swap(frame_commands);
	}
	else if (!eligible)
	{
		cache_valid = false;
	}
	frame_commands.clear();

	SetBaseState();

	Gfx::CheckGLError("EndFrame");

	// realm: resources released during the frame are freed only now, after the queue that may name them has run.
	for (const Rml::CompiledGeometryHandle handle : deferred_geometry)
		ReleaseGeometryNow(handle);
	deferred_geometry.clear();
	for (const Rml::TextureHandle texture : deferred_textures)
		ReleaseTextureNow(texture);
	deferred_textures.clear();
}

void RenderInterface_GL3::EndRecording()
{
	if (!recording)
		return;
	// Replays what is queued, so the caller's GL call lands after it, and records that the frame may not be reused.
	recording = false;
	frame_flushed = true;
	ReplayFrameCommands();
	frame_commands.clear();
}

void RenderInterface_GL3::ClearPreviousBounds()
{
	glBindFramebuffer(GL_FRAMEBUFFER, render_layers.GetTopLayer().framebuffer);
	if (previous_bounds.Valid())
	{
		SetGlScissorBox(previous_bounds.Left(), viewport_height - previous_bounds.Bottom(), previous_bounds.Width(), previous_bounds.Height());
		SetGlScissorTest(true);
		glClear(GL_COLOR_BUFFER_BIT);
		SetGlScissorTest(false);
	}
}

void RenderInterface_GL3::ReplayFrameCommands()
{
	// Runs with recording off, so each base call below executes immediately.
	ClearPreviousBounds();
	for (const FrameCommand& command : frame_commands)
	{
		switch (command.kind)
		{
		case FrameCommand::Kind::Geometry: RenderInterface_GL3::RenderGeometry(command.geometry, command.translation, command.texture); break;
		case FrameCommand::Kind::Scissor: RenderInterface_GL3::SetScissorRegion(command.region); break;
		case FrameCommand::Kind::EnableScissor: RenderInterface_GL3::EnableScissorRegion(command.flag); break;
		case FrameCommand::Kind::Transform: RenderInterface_GL3::SetTransform(command.flag ? &command.matrix : nullptr); break;
		}
	}
}

void RenderInterface_GL3::TextureContentChanged()
{
	EndRecording();
	++generation;
}

void RenderInterface_GL3::SetFrameState()
{
	// Entry state of the UI pass. The UI sets every state it depends on, so nothing is read back from the driver. The colour mask is
	// set because the renderer leaves it all-true, and glClear and the draws depend on it.
	glViewport(0, 0, viewport_width, viewport_height);

	glClearStencil(0);
	glClearColor(0, 0, 0, 0);

	glActiveTexture(GL_TEXTURE0);

	SetGlScissorTest(false);
	glDisable(GL_CULL_FACE);

	// Set blending function for premultiplied alpha.
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

#if !defined(RMLUI_PLATFORM_EMSCRIPTEN) && !defined(__ANDROID__)
	// We do blending in nonlinear sRGB space because that is the common practice and gives results that we are used to.
	glDisable(GL_FRAMEBUFFER_SRGB);
#endif

	glEnable(GL_STENCIL_TEST);
	SetStencilFunc(GL_ALWAYS, 1);
	glStencilMask(GLuint(-1));
	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

	glDisable(GL_DEPTH_TEST);

	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

void RenderInterface_GL3::SetBaseState()
{
	// Exit state: the renderer's own baseline (Renderer.cpp draw path and overlay pass), so the next frame starts from what it expects.
	// Blend on with the renderer's functions, depth, cull, stencil and scissor off, the full drawable viewport, colour mask all, texture
	// unit 0, and the default stencil function. Texture bindings and the clear colour are not part of it: the renderer binds its own
	// textures and sets the clear colour before each clear.
	glViewport(0, 0, viewport_width, viewport_height);

	SetGlScissorTest(false);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_STENCIL_TEST);
	SetStencilFunc(GL_ALWAYS, 0);
	glStencilMask(GLuint(-1));
	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glActiveTexture(GL_TEXTURE0);
}

void RenderInterface_GL3::SetGlScissorTest(bool enable)
{
	// The test is only toggled by this backend within a frame, so the tracked value is exact once SetFrameState has disabled it.
	if (enable == gl_scissor_on)
		return;
	gl_scissor_on = enable;
	if (enable)
		glEnable(GL_SCISSOR_TEST);
	else
		glDisable(GL_SCISSOR_TEST);
}

void RenderInterface_GL3::SetGlScissorBox(int x, int y, int width, int height)
{
	// Always issued: the driver's box is not known until this frame sets it.
	gl_scissor_box[0] = x;
	gl_scissor_box[1] = y;
	gl_scissor_box[2] = width;
	gl_scissor_box[3] = height;
	glScissor(x, y, width, height);
}

void RenderInterface_GL3::SetStencilFunc(unsigned int func, int ref)
{
	stencil_ref = ref;
	glStencilFunc(func, ref, GLuint(-1));
}

void RenderInterface_GL3::Clear()
{
	EndRecording(); // realm: not queued.
	frame_full_screen = true; // realm: see frame_bounds.
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
}

Rml::CompiledGeometryHandle RenderInterface_GL3::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
{
	++generation; // realm: see generation.
	constexpr GLenum draw_usage = GL_STATIC_DRAW;

	GLuint vao = 0;
	GLuint vbo = 0;
	GLuint ibo = 0;

	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);
	glGenBuffers(1, &ibo);
	glBindVertexArray(vao);

	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(Rml::Vertex) * vertices.size(), (const void*)vertices.data(), draw_usage);

	glEnableVertexAttribArray((GLuint)Gfx::VertexAttribute::Position);
	glVertexAttribPointer((GLuint)Gfx::VertexAttribute::Position, 2, GL_FLOAT, GL_FALSE, sizeof(Rml::Vertex),
		(const GLvoid*)(offsetof(Rml::Vertex, position)));

	glEnableVertexAttribArray((GLuint)Gfx::VertexAttribute::Color0);
	glVertexAttribPointer((GLuint)Gfx::VertexAttribute::Color0, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Rml::Vertex),
		(const GLvoid*)(offsetof(Rml::Vertex, colour)));

	glEnableVertexAttribArray((GLuint)Gfx::VertexAttribute::TexCoord0);
	glVertexAttribPointer((GLuint)Gfx::VertexAttribute::TexCoord0, 2, GL_FLOAT, GL_FALSE, sizeof(Rml::Vertex),
		(const GLvoid*)(offsetof(Rml::Vertex, tex_coord)));

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(int) * indices.size(), (const void*)indices.data(), draw_usage);

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	Gfx::CheckGLError("CompileGeometry");

	Gfx::CompiledGeometryData* geometry = new Gfx::CompiledGeometryData;
	geometry->vao = vao;
	geometry->vbo = vbo;
	geometry->ibo = ibo;
	geometry->draw_count = (GLsizei)indices.size();

	// realm: store the vertex bounds, used to limit the frame's written region.
	geometry->bounds_min = geometry->bounds_max = vertices.empty() ? Rml::Vector2f(0.f) : vertices[0].position;
	for (const Rml::Vertex& vertex : vertices)
	{
		geometry->bounds_min = {std::min(geometry->bounds_min.x, vertex.position.x), std::min(geometry->bounds_min.y, vertex.position.y)};
		geometry->bounds_max = {std::max(geometry->bounds_max.x, vertex.position.x), std::max(geometry->bounds_max.y, vertex.position.y)};
	}

	return (Rml::CompiledGeometryHandle)geometry;
}

void RenderInterface_GL3::RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture)
{
	Gfx::CompiledGeometryData* geometry = (Gfx::CompiledGeometryData*)handle;

	// realm: queued for EndFrame. The post-process pass is not queued: it runs only inside the layer and filter paths, which end recording.
	if (recording)
	{
		if (texture != TexturePostprocess)
		{
			FrameCommand command;
			command.kind = FrameCommand::Kind::Geometry;
			command.geometry = handle;
			command.texture = texture;
			command.translation = translation;
			frame_commands.push_back(command);
			return;
		}
		EndRecording();
	}

	// realm: post-process passes only run inside the full-screen paths, so they are not tracked here.
	if (texture != TexturePostprocess && geometry->draw_count > 0 && !frame_full_screen)
		ExtendFrameBounds(geometry->bounds_min, geometry->bounds_max, translation);

	if (texture == TexturePostprocess)
	{
		// Do nothing.
	}
	else if (texture)
	{
		UseProgram(ProgramId::Texture);
		SubmitTransformUniform(translation);
		if (texture != TextureEnableWithoutBinding)
			glBindTexture(GL_TEXTURE_2D, (GLuint)texture);
	}
	else
	{
		UseProgram(ProgramId::Color);
		glBindTexture(GL_TEXTURE_2D, 0);
		SubmitTransformUniform(translation);
	}

	glBindVertexArray(geometry->vao);
	glDrawElements(GL_TRIANGLES, geometry->draw_count, GL_UNSIGNED_INT, (const GLvoid*)0);

	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);

	Gfx::CheckGLError("RenderCompiledGeometry");
}

void RenderInterface_GL3::ReleaseGeometry(Rml::CompiledGeometryHandle handle)
{
	++generation; // realm: see generation.
	if (recording)
	{
		deferred_geometry.push_back(handle); // realm: a queued command may still name it; freed in EndFrame.
		return;
	}
	ReleaseGeometryNow(handle);
}

void RenderInterface_GL3::ReleaseGeometryNow(Rml::CompiledGeometryHandle handle)
{
	Gfx::CompiledGeometryData* geometry = (Gfx::CompiledGeometryData*)handle;

	glDeleteVertexArrays(1, &geometry->vao);
	glDeleteBuffers(1, &geometry->vbo);
	glDeleteBuffers(1, &geometry->ibo);

	delete geometry;
}

/// Flip the vertical axis of the rectangle, and move its origin to the vertically opposite side of the viewport.
/// @note Changes the coordinate system from RmlUi to OpenGL, or equivalently in reverse.
/// @note The Rectangle::Top and Rectangle::Bottom members will have reverse meaning in the returned rectangle.
static Rml::Rectanglei VerticallyFlipped(Rml::Rectanglei rect, int viewport_height)
{
	RMLUI_ASSERT(rect.Valid());
	Rml::Rectanglei flipped_rect = rect;
	flipped_rect.p0.y = viewport_height - rect.p1.y;
	flipped_rect.p1.y = viewport_height - rect.p0.y;
	return flipped_rect;
}

void RenderInterface_GL3::SetScissor(Rml::Rectanglei region, bool vertically_flip)
{
	SetGlScissorTest(region.Valid());

	if (region.Valid() && vertically_flip)
		region = VerticallyFlipped(region, viewport_height);

	if (region.Valid() && region != scissor_state)
	{
		// Some render APIs don't like offscreen positions (WebGL in particular), so clamp them to the viewport.
		const int x = Rml::Math::Clamp(region.Left(), 0, viewport_width);
		const int y = Rml::Math::Clamp(viewport_height - region.Bottom(), 0, viewport_height);

		SetGlScissorBox(x, y, region.Width(), region.Height());
	}

	Gfx::CheckGLError("SetScissorRegion");
	scissor_state = region;
}

void RenderInterface_GL3::EnableScissorRegion(bool enable)
{
	if (recording) // realm: queued, see RenderGeometry.
	{
		FrameCommand command;
		command.kind = FrameCommand::Kind::EnableScissor;
		command.flag = enable;
		frame_commands.push_back(command);
		return;
	}
	// Assume enable is immediately followed by a SetScissorRegion() call, and ignore it here.
	if (!enable)
		SetScissor(Rml::Rectanglei::MakeInvalid(), false);
}

void RenderInterface_GL3::ExtendFrameBounds(Rml::Vector2f bounds_min, Rml::Vector2f bounds_max, Rml::Vector2f translation)
{
	// realm: the pixel bounds of the geometry, rounded outwards and padded by one pixel, clamped to the viewport.
	const float x0 = bounds_min.x + translation.x;
	const float y0 = bounds_min.y + translation.y;
	const float x1 = bounds_max.x + translation.x;
	const float y1 = bounds_max.y + translation.y;
	constexpr float limit = 1.0e6f;
	if (!(x0 > -limit && x0 < limit && y0 > -limit && y0 < limit && x1 > -limit && x1 < limit && y1 > -limit && y1 < limit))
	{
		frame_full_screen = true;
		return;
	}

	const float width = float(viewport_width);
	const float height = float(viewport_height);
	const int left = int(Rml::Math::Clamp(std::floor(x0) - 1.f, 0.f, width));
	const int top = int(Rml::Math::Clamp(std::floor(y0) - 1.f, 0.f, height));
	const int right = int(Rml::Math::Clamp(std::ceil(x1) + 1.f, 0.f, width));
	const int bottom = int(Rml::Math::Clamp(std::ceil(y1) + 1.f, 0.f, height));
	if (right <= left || bottom <= top)
		return;

	if (!frame_bounds.Valid())
		frame_bounds = Rml::Rectanglei::FromCorners(Rml::Vector2i(left, top), Rml::Vector2i(right, bottom));
	else
		frame_bounds = Rml::Rectanglei::FromCorners(Rml::Vector2i(std::min(frame_bounds.Left(), left), std::min(frame_bounds.Top(), top)),
			Rml::Vector2i(std::max(frame_bounds.Right(), right), std::max(frame_bounds.Bottom(), bottom)));
}

void RenderInterface_GL3::SetScissorRegion(Rml::Rectanglei region)
{
	if (recording) // realm: queued, see RenderGeometry.
	{
		FrameCommand command;
		command.kind = FrameCommand::Kind::Scissor;
		command.region = region;
		frame_commands.push_back(command);
		return;
	}
	SetScissor(region);
}

void RenderInterface_GL3::EnableClipMask(bool enable)
{
	EndRecording(); // realm: stencil state is not part of the cache key.
	if (enable)
		glEnable(GL_STENCIL_TEST);
	else
		glDisable(GL_STENCIL_TEST);
}

void RenderInterface_GL3::RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation)
{
	EndRecording(); // realm: writes the stencil buffer, which is not part of the cache key.
	RMLUI_ASSERT(glIsEnabled(GL_STENCIL_TEST));
	using Rml::ClipMaskOperation;

	GLint stencil_write_value = 1;
	GLint stencil_test_value = 1;
	switch (operation)
	{
	case ClipMaskOperation::Set:
	{
		// @performance Increment the reference value instead of clearing each time.
		glClear(GL_STENCIL_BUFFER_BIT);
		glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
	}
	break;
	case ClipMaskOperation::SetInverse:
	{
		glClearStencil(1);
		glClear(GL_STENCIL_BUFFER_BIT);
		glClearStencil(0);
		glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
		stencil_write_value = 0;
	}
	break;
	case ClipMaskOperation::Intersect:
	{
		glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
		stencil_test_value = stencil_ref; // realm: tracked, not glGetIntegerv (a driver sync).
		stencil_test_value += 1;
	}
	break;
	}

	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	SetStencilFunc(GL_ALWAYS, stencil_write_value);

	RenderGeometry(geometry, translation, {});

	// Restore state
	// @performance Cache state so we don't toggle it unnecessarily.
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
	SetStencilFunc(GL_EQUAL, stencil_test_value);
}

// Set to byte packing, or the compiler will expand our struct, which means it won't read correctly from file
#pragma pack(1)
struct TGAHeader {
	char idLength;
	char colourMapType;
	char dataType;
	short int colourMapOrigin;
	short int colourMapLength;
	char colourMapDepth;
	short int xOrigin;
	short int yOrigin;
	short int width;
	short int height;
	char bitsPerPixel;
	char imageDescriptor;
};
// Restore packing
#pragma pack()

Rml::TextureHandle RenderInterface_GL3::LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source)
{
	++generation; // realm: see generation.
	Rml::FileInterface* file_interface = Rml::GetFileInterface();
	Rml::FileHandle file_handle = file_interface->Open(source);
	if (!file_handle)
	{
		return false;
	}

	file_interface->Seek(file_handle, 0, SEEK_END);
	size_t buffer_size = file_interface->Tell(file_handle);
	file_interface->Seek(file_handle, 0, SEEK_SET);

	if (buffer_size <= sizeof(TGAHeader))
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "Texture file size is smaller than TGAHeader, file is not a valid TGA image.");
		file_interface->Close(file_handle);
		return false;
	}

	using Rml::byte;
	Rml::UniquePtr<byte[]> buffer(new byte[buffer_size]);
	file_interface->Read(buffer.get(), buffer_size, file_handle);
	file_interface->Close(file_handle);

	TGAHeader header;
	memcpy(&header, buffer.get(), sizeof(TGAHeader));

	int color_mode = header.bitsPerPixel / 8;
	const size_t image_size = header.width * header.height * 4; // We always make 32bit textures

	if (header.dataType != 2)
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "Only 24/32bit uncompressed TGAs are supported.");
		return false;
	}

	// Ensure we have at least 3 colors
	if (color_mode < 3)
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "Only 24 and 32bit textures are supported.");
		return false;
	}

	const byte* image_src = buffer.get() + sizeof(TGAHeader);
	Rml::UniquePtr<byte[]> image_dest_buffer(new byte[image_size]);
	byte* image_dest = image_dest_buffer.get();
	const bool top_to_bottom_order = ((header.imageDescriptor & 32) != 0);

	// Targa is BGR, swap to RGB, flip Y axis as necessary, and convert to premultiplied alpha.
	for (long y = 0; y < header.height; y++)
	{
		long read_index = y * header.width * color_mode;
		long write_index = top_to_bottom_order ? (y * header.width * 4) : (header.height - y - 1) * header.width * 4;
		for (long x = 0; x < header.width; x++)
		{
			image_dest[write_index] = image_src[read_index + 2];
			image_dest[write_index + 1] = image_src[read_index + 1];
			image_dest[write_index + 2] = image_src[read_index];
			if (color_mode == 4)
			{
				const byte alpha = image_src[read_index + 3];
				for (size_t j = 0; j < 3; j++)
					image_dest[write_index + j] = byte((image_dest[write_index + j] * alpha) / 255);
				image_dest[write_index + 3] = alpha;
			}
			else
				image_dest[write_index + 3] = 255;

			write_index += 4;
			read_index += color_mode;
		}
	}

	texture_dimensions.x = header.width;
	texture_dimensions.y = header.height;

	return GenerateTexture({image_dest, image_size}, texture_dimensions);
}

Rml::TextureHandle RenderInterface_GL3::GenerateTexture(Rml::Span<const Rml::byte> source_data, Rml::Vector2i source_dimensions)
{
	++generation; // realm: see generation.
	RMLUI_ASSERT(source_data.data() && source_data.size() == size_t(source_dimensions.x * source_dimensions.y * 4));

	GLuint texture_id = Gfx::CreateTexture(source_data, source_dimensions);
	if (texture_id == 0)
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to generate texture.");
		return {};
	}
	return (Rml::TextureHandle)texture_id;
}

void RenderInterface_GL3::DrawFullscreenQuad()
{
	RenderGeometry(fullscreen_quad_geometry, {}, RenderInterface_GL3::TexturePostprocess);
}

void RenderInterface_GL3::DrawFullscreenQuad(Rml::Vector2f uv_offset, Rml::Vector2f uv_scaling)
{
	Rml::Mesh mesh;
	Rml::MeshUtilities::GenerateQuad(mesh, Rml::Vector2f(-1), Rml::Vector2f(2), {});
	if (uv_offset != Rml::Vector2f() || uv_scaling != Rml::Vector2f(1.f))
	{
		for (Rml::Vertex& vertex : mesh.vertices)
			vertex.tex_coord = (vertex.tex_coord * uv_scaling) + uv_offset;
	}
	const Rml::CompiledGeometryHandle geometry = CompileGeometry(mesh.vertices, mesh.indices);
	RenderGeometry(geometry, {}, RenderInterface_GL3::TexturePostprocess);
	ReleaseGeometry(geometry);
}

static Rml::Colourf ConvertToColorf(Rml::ColourbPremultiplied c0)
{
	Rml::Colourf result;
	for (int i = 0; i < 4; i++)
		result[i] = (1.f / 255.f) * float(c0[i]);
	return result;
}

static void SigmaToParameters(const float desired_sigma, int& out_pass_level, float& out_sigma)
{
	constexpr int max_num_passes = 10;
	static_assert(max_num_passes < 31, "");
	constexpr float max_single_pass_sigma = 3.0f;
	out_pass_level = Rml::Math::Clamp(Rml::Math::Log2(int(desired_sigma * (2.f / max_single_pass_sigma))), 0, max_num_passes);
	out_sigma = Rml::Math::Clamp(desired_sigma / float(1 << out_pass_level), 0.0f, max_single_pass_sigma);
}

static void SetTexCoordLimits(GLint tex_coord_min_location, GLint tex_coord_max_location, Rml::Rectanglei rectangle_flipped,
	Rml::Vector2i framebuffer_size)
{
	// Offset by half-texel values so that texture lookups are clamped to fragment centers, thereby avoiding color
	// bleeding from neighboring texels due to bilinear interpolation.
	const Rml::Vector2f min = (Rml::Vector2f(rectangle_flipped.p0) + Rml::Vector2f(0.5f)) / Rml::Vector2f(framebuffer_size);
	const Rml::Vector2f max = (Rml::Vector2f(rectangle_flipped.p1) - Rml::Vector2f(0.5f)) / Rml::Vector2f(framebuffer_size);

	glUniform2f(tex_coord_min_location, min.x, min.y);
	glUniform2f(tex_coord_max_location, max.x, max.y);
}

static void SetBlurWeights(GLint weights_location, float sigma)
{
	constexpr int num_weights = BLUR_NUM_WEIGHTS;
	float weights[num_weights];
	float normalization = 0.0f;
	for (int i = 0; i < num_weights; i++)
	{
		if (Rml::Math::Absolute(sigma) < 0.1f)
			weights[i] = float(i == 0);
		else
			weights[i] = Rml::Math::Exp(-float(i * i) / (2.0f * sigma * sigma)) / (Rml::Math::SquareRoot(2.f * Rml::Math::RMLUI_PI) * sigma);

		normalization += (i == 0 ? 1.f : 2.0f) * weights[i];
	}
	for (int i = 0; i < num_weights; i++)
		weights[i] /= normalization;

	glUniform1fv(weights_location, (GLsizei)num_weights, &weights[0]);
}

void RenderInterface_GL3::RenderBlur(float sigma, const Gfx::FramebufferData& source_destination, const Gfx::FramebufferData& temp,
	const Rml::Rectanglei window_flipped)
{
	RMLUI_ASSERT(&source_destination != &temp && source_destination.width == temp.width && source_destination.height == temp.height);
	RMLUI_ASSERT(window_flipped.Valid());

	int pass_level = 0;
	SigmaToParameters(sigma, pass_level, sigma);

	const Rml::Rectanglei original_scissor = scissor_state;

	// Begin by downscaling so that the blur pass can be done at a reduced resolution for large sigma.
	Rml::Rectanglei scissor = window_flipped;

	UseProgram(ProgramId::Passthrough);
	SetScissor(scissor, true);

	// Downscale by iterative half-scaling with bilinear filtering, to reduce aliasing.
	glViewport(0, 0, source_destination.width / 2, source_destination.height / 2);

	// Scale UVs if we have even dimensions, such that texture fetches align perfectly between texels, thereby producing a 50% blend of
	// neighboring texels.
	const Rml::Vector2f uv_scaling = {(source_destination.width % 2 == 1) ? (1.f - 1.f / float(source_destination.width)) : 1.f,
		(source_destination.height % 2 == 1) ? (1.f - 1.f / float(source_destination.height)) : 1.f};

	for (int i = 0; i < pass_level; i++)
	{
		scissor.p0 = (scissor.p0 + Rml::Vector2i(1)) / 2;
		scissor.p1 = Rml::Math::Max(scissor.p1 / 2, scissor.p0);
		const bool from_source = (i % 2 == 0);
		Gfx::BindTexture(from_source ? source_destination : temp);
		glBindFramebuffer(GL_FRAMEBUFFER, (from_source ? temp : source_destination).framebuffer);
		SetScissor(scissor, true);

		DrawFullscreenQuad({}, uv_scaling);
	}

	glViewport(0, 0, source_destination.width, source_destination.height);

	// Ensure texture data end up in the temp buffer. Depending on the last downscaling, we might need to move it from the source_destination buffer.
	const bool transfer_to_temp_buffer = (pass_level % 2 == 0);
	if (transfer_to_temp_buffer)
	{
		Gfx::BindTexture(source_destination);
		glBindFramebuffer(GL_FRAMEBUFFER, temp.framebuffer);
		DrawFullscreenQuad();
	}

	// Set up uniforms.
	UseProgram(ProgramId::Blur);
	SetBlurWeights(GetUniformLocation(UniformId::Weights), sigma);
	SetTexCoordLimits(GetUniformLocation(UniformId::TexCoordMin), GetUniformLocation(UniformId::TexCoordMax), scissor,
		{source_destination.width, source_destination.height});

	const GLint texel_offset_location = GetUniformLocation(UniformId::TexelOffset);
	auto SetTexelOffset = [texel_offset_location](Rml::Vector2f blur_direction, int texture_dimension) {
		const Rml::Vector2f texel_offset = blur_direction * (1.0f / float(texture_dimension));
		glUniform2f(texel_offset_location, texel_offset.x, texel_offset.y);
	};

	// Blur render pass - vertical.
	Gfx::BindTexture(temp);
	glBindFramebuffer(GL_FRAMEBUFFER, source_destination.framebuffer);

	SetTexelOffset({0.f, 1.f}, temp.height);
	DrawFullscreenQuad();

	// Blur render pass - horizontal.
	Gfx::BindTexture(source_destination);
	glBindFramebuffer(GL_FRAMEBUFFER, temp.framebuffer);

	// Add a 1px transparent border around the blur region by first clearing with a padded scissor. This helps prevent
	// artifacts when upscaling the blur result in the later step. On Intel and AMD, we have observed that during
	// blitting with linear filtering, pixels outside the 'src' region can be blended into the output. On the other
	// hand, it looks like Nvidia clamps the pixels to the source edge, which is what we really want. Regardless, we
	// work around the issue with this extra step.
	SetScissor(scissor.Extend(1), true);
	glClear(GL_COLOR_BUFFER_BIT);
	SetScissor(scissor, true);

	SetTexelOffset({1.f, 0.f}, source_destination.width);
	DrawFullscreenQuad();

	// Blit the blurred image to the scissor region with upscaling.
	SetScissor(window_flipped, true);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, temp.framebuffer);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, source_destination.framebuffer);

	const Rml::Vector2i src_min = scissor.p0;
	const Rml::Vector2i src_max = scissor.p1;
	const Rml::Vector2i dst_min = window_flipped.p0;
	const Rml::Vector2i dst_max = window_flipped.p1;
	glBlitFramebuffer(src_min.x, src_min.y, src_max.x, src_max.y, dst_min.x, dst_min.y, dst_max.x, dst_max.y, GL_COLOR_BUFFER_BIT, GL_LINEAR);

	// The above upscale blit might be jittery at low resolutions (large pass levels). This is especially noticeable when moving an element with
	// backdrop blur around or when trying to click/hover an element within a blurred region since it may be rendered at an offset. For more stable
	// and accurate rendering we next upscale the blur image by an exact power-of-two. However, this may not fill the edges completely so we need to
	// do the above first. Note that this strategy may sometimes result in visible seams. Alternatively, we could try to enlarge the window to the
	// next power-of-two size and then downsample and blur that.
	const Rml::Vector2i target_min = src_min * (1 << pass_level);
	const Rml::Vector2i target_max = src_max * (1 << pass_level);
	if (target_min != dst_min || target_max != dst_max)
	{
		glBlitFramebuffer(src_min.x, src_min.y, src_max.x, src_max.y, target_min.x, target_min.y, target_max.x, target_max.y, GL_COLOR_BUFFER_BIT,
			GL_LINEAR);
	}

	// Restore render state.
	SetScissor(original_scissor);

	Gfx::CheckGLError("Blur");
}

void RenderInterface_GL3::ReleaseTexture(Rml::TextureHandle texture_handle)
{
	++generation; // realm: see generation.
	if (recording)
	{
		deferred_textures.push_back(texture_handle); // realm: a queued command may still name it; freed in EndFrame.
		return;
	}
	ReleaseTextureNow(texture_handle);
}

void RenderInterface_GL3::ReleaseTextureNow(Rml::TextureHandle texture_handle)
{
	glDeleteTextures(1, (GLuint*)&texture_handle);
}

void RenderInterface_GL3::SetTransform(const Rml::Matrix4f* new_transform)
{
	if (recording) // realm: queued, see RenderGeometry. The matrix is copied because RmlUi may reuse the pointer.
	{
		FrameCommand command;
		command.kind = FrameCommand::Kind::Transform;
		command.flag = new_transform != nullptr;
		if (new_transform)
			command.matrix = *new_transform;
		frame_commands.push_back(command);
		return;
	}
	// realm: a transformed geometry can land anywhere, so the frame is treated as full screen.
	if (new_transform)
		frame_full_screen = true;

	transform = (new_transform ? (projection * (*new_transform)) : projection);
	program_transform_dirty.set();
}

enum class FilterType { Invalid = 0, Passthrough, Blur, DropShadow, ColorMatrix, MaskImage };
struct CompiledFilter {
	FilterType type;

	// Passthrough
	float blend_factor;

	// Blur
	float sigma;

	// Drop shadow
	Rml::Vector2f offset;
	Rml::ColourbPremultiplied color;

	// ColorMatrix
	Rml::Matrix4f color_matrix;
};

Rml::CompiledFilterHandle RenderInterface_GL3::CompileFilter(const Rml::String& name, const Rml::Dictionary& parameters)
{
	CompiledFilter filter = {};

	if (name == "opacity")
	{
		filter.type = FilterType::Passthrough;
		filter.blend_factor = Rml::Get(parameters, "value", 1.0f);
	}
	else if (name == "blur")
	{
		filter.type = FilterType::Blur;
		filter.sigma = Rml::Get(parameters, "sigma", 1.0f);
	}
	else if (name == "drop-shadow")
	{
		filter.type = FilterType::DropShadow;
		filter.sigma = Rml::Get(parameters, "sigma", 0.f);
		filter.color = Rml::Get(parameters, "color", Rml::Colourb()).ToPremultiplied();
		filter.offset = Rml::Get(parameters, "offset", Rml::Vector2f(0.f));
	}
	else if (name == "brightness")
	{
		filter.type = FilterType::ColorMatrix;
		const float value = Rml::Get(parameters, "value", 1.0f);
		filter.color_matrix = Rml::Matrix4f::Diag(value, value, value, 1.f);
	}
	else if (name == "contrast")
	{
		filter.type = FilterType::ColorMatrix;
		const float value = Rml::Get(parameters, "value", 1.0f);
		const float grayness = 0.5f - 0.5f * value;
		filter.color_matrix = Rml::Matrix4f::Diag(value, value, value, 1.f);
		filter.color_matrix.SetColumn(3, Rml::Vector4f(grayness, grayness, grayness, 1.f));
	}
	else if (name == "invert")
	{
		filter.type = FilterType::ColorMatrix;
		const float value = Rml::Math::Clamp(Rml::Get(parameters, "value", 1.0f), 0.f, 1.f);
		const float inverted = 1.f - 2.f * value;
		filter.color_matrix = Rml::Matrix4f::Diag(inverted, inverted, inverted, 1.f);
		filter.color_matrix.SetColumn(3, Rml::Vector4f(value, value, value, 1.f));
	}
	else if (name == "grayscale")
	{
		filter.type = FilterType::ColorMatrix;
		const float value = Rml::Get(parameters, "value", 1.0f);
		const float rev_value = 1.f - value;
		const Rml::Vector3f gray = value * Rml::Vector3f(0.2126f, 0.7152f, 0.0722f);
		// clang-format off
		filter.color_matrix = Rml::Matrix4f::FromRows(
			{gray.x + rev_value, gray.y,             gray.z,             0.f},
			{gray.x,             gray.y + rev_value, gray.z,             0.f},
			{gray.x,             gray.y,             gray.z + rev_value, 0.f},
			{0.f,                0.f,                0.f,                1.f}
		);
		// clang-format on
	}
	else if (name == "sepia")
	{
		filter.type = FilterType::ColorMatrix;
		const float value = Rml::Get(parameters, "value", 1.0f);
		const float rev_value = 1.f - value;
		const Rml::Vector3f r_mix = value * Rml::Vector3f(0.393f, 0.769f, 0.189f);
		const Rml::Vector3f g_mix = value * Rml::Vector3f(0.349f, 0.686f, 0.168f);
		const Rml::Vector3f b_mix = value * Rml::Vector3f(0.272f, 0.534f, 0.131f);
		// clang-format off
		filter.color_matrix = Rml::Matrix4f::FromRows(
			{r_mix.x + rev_value, r_mix.y,             r_mix.z,             0.f},
			{g_mix.x,             g_mix.y + rev_value, g_mix.z,             0.f},
			{b_mix.x,             b_mix.y,             b_mix.z + rev_value, 0.f},
			{0.f,                 0.f,                 0.f,                 1.f}
		);
		// clang-format on
	}
	else if (name == "hue-rotate")
	{
		// Hue-rotation and saturation values based on: https://www.w3.org/TR/filter-effects-1/#attr-valuedef-type-huerotate
		filter.type = FilterType::ColorMatrix;
		const float value = Rml::Get(parameters, "value", 1.0f);
		const float s = Rml::Math::Sin(value);
		const float c = Rml::Math::Cos(value);
		// clang-format off
		filter.color_matrix = Rml::Matrix4f::FromRows(
			{0.213f + 0.787f * c - 0.213f * s,  0.715f - 0.715f * c - 0.715f * s,  0.072f - 0.072f * c + 0.928f * s,  0.f},
			{0.213f - 0.213f * c + 0.143f * s,  0.715f + 0.285f * c + 0.140f * s,  0.072f - 0.072f * c - 0.283f * s,  0.f},
			{0.213f - 0.213f * c - 0.787f * s,  0.715f - 0.715f * c + 0.715f * s,  0.072f + 0.928f * c + 0.072f * s,  0.f},
			{0.f,                               0.f,                               0.f,                               1.f}
		);
		// clang-format on
	}
	else if (name == "saturate")
	{
		filter.type = FilterType::ColorMatrix;
		const float value = Rml::Get(parameters, "value", 1.0f);
		// clang-format off
		filter.color_matrix = Rml::Matrix4f::FromRows(
			{0.213f + 0.787f * value,  0.715f - 0.715f * value,  0.072f - 0.072f * value,  0.f},
			{0.213f - 0.213f * value,  0.715f + 0.285f * value,  0.072f - 0.072f * value,  0.f},
			{0.213f - 0.213f * value,  0.715f - 0.715f * value,  0.072f + 0.928f * value,  0.f},
			{0.f,                      0.f,                      0.f,                      1.f}
		);
		// clang-format on
	}

	if (filter.type != FilterType::Invalid)
		return reinterpret_cast<Rml::CompiledFilterHandle>(new CompiledFilter(std::move(filter)));

	Rml::Log::Message(Rml::Log::LT_WARNING, "Unsupported filter type '%s'.", name.c_str());
	return {};
}

void RenderInterface_GL3::ReleaseFilter(Rml::CompiledFilterHandle filter)
{
	delete reinterpret_cast<CompiledFilter*>(filter);
}

enum class CompiledShaderType { Invalid = 0, Gradient, Creation };
struct CompiledShader {
	CompiledShaderType type;

	// Gradient
	ShaderGradientFunction gradient_function;
	Rml::Vector2f p;
	Rml::Vector2f v;
	Rml::Vector<float> stop_positions;
	Rml::Vector<Rml::Colourf> stop_colors;

	// Shader
	Rml::Vector2f dimensions;
};

Rml::CompiledShaderHandle RenderInterface_GL3::CompileShader(const Rml::String& name, const Rml::Dictionary& parameters)
{
	auto ApplyColorStopList = [](CompiledShader& shader, const Rml::Dictionary& shader_parameters) {
		auto it = shader_parameters.find("color_stop_list");
		RMLUI_ASSERT(it != shader_parameters.end() && it->second.GetType() == Rml::Variant::COLORSTOPLIST);
		const Rml::ColorStopList& color_stop_list = it->second.GetReference<Rml::ColorStopList>();
		const int num_stops = Rml::Math::Min((int)color_stop_list.size(), MAX_NUM_STOPS);

		shader.stop_positions.resize(num_stops);
		shader.stop_colors.resize(num_stops);
		for (int i = 0; i < num_stops; i++)
		{
			const Rml::ColorStop& stop = color_stop_list[i];
			RMLUI_ASSERT(stop.position.unit == Rml::Unit::NUMBER);
			shader.stop_positions[i] = stop.position.number;
			shader.stop_colors[i] = ConvertToColorf(stop.color);
		}
	};

	CompiledShader shader = {};

	if (name == "linear-gradient")
	{
		shader.type = CompiledShaderType::Gradient;
		const bool repeating = Rml::Get(parameters, "repeating", false);
		shader.gradient_function = (repeating ? ShaderGradientFunction::RepeatingLinear : ShaderGradientFunction::Linear);
		shader.p = Rml::Get(parameters, "p0", Rml::Vector2f(0.f));
		shader.v = Rml::Get(parameters, "p1", Rml::Vector2f(0.f)) - shader.p;
		ApplyColorStopList(shader, parameters);
	}
	else if (name == "radial-gradient")
	{
		shader.type = CompiledShaderType::Gradient;
		const bool repeating = Rml::Get(parameters, "repeating", false);
		shader.gradient_function = (repeating ? ShaderGradientFunction::RepeatingRadial : ShaderGradientFunction::Radial);
		shader.p = Rml::Get(parameters, "center", Rml::Vector2f(0.f));
		shader.v = Rml::Vector2f(1.f) / Rml::Get(parameters, "radius", Rml::Vector2f(1.f));
		ApplyColorStopList(shader, parameters);
	}
	else if (name == "conic-gradient")
	{
		shader.type = CompiledShaderType::Gradient;
		const bool repeating = Rml::Get(parameters, "repeating", false);
		shader.gradient_function = (repeating ? ShaderGradientFunction::RepeatingConic : ShaderGradientFunction::Conic);
		shader.p = Rml::Get(parameters, "center", Rml::Vector2f(0.f));
		const float angle = Rml::Get(parameters, "angle", 0.f);
		shader.v = {Rml::Math::Cos(angle), Rml::Math::Sin(angle)};
		ApplyColorStopList(shader, parameters);
	}
	else if (name == "shader")
	{
		const Rml::String value = Rml::Get(parameters, "value", Rml::String());
		if (value == "creation")
		{
			shader.type = CompiledShaderType::Creation;
			shader.dimensions = Rml::Get(parameters, "dimensions", Rml::Vector2f(0.f));
		}
	}

	if (shader.type != CompiledShaderType::Invalid)
		return reinterpret_cast<Rml::CompiledShaderHandle>(new CompiledShader(std::move(shader)));

	Rml::Log::Message(Rml::Log::LT_WARNING, "Unsupported shader type '%s'.", name.c_str());
	return {};
}

void RenderInterface_GL3::RenderShader(Rml::CompiledShaderHandle shader_handle, Rml::CompiledGeometryHandle geometry_handle,
	Rml::Vector2f translation, Rml::TextureHandle /*texture*/)
{
	EndRecording(); // realm: layer, filter and shader paths are not queued.
	frame_full_screen = true; // realm: see frame_bounds.
	RMLUI_ASSERT(shader_handle && geometry_handle);
	const CompiledShader& shader = *reinterpret_cast<CompiledShader*>(shader_handle);
	const CompiledShaderType type = shader.type;
	const Gfx::CompiledGeometryData& geometry = *reinterpret_cast<Gfx::CompiledGeometryData*>(geometry_handle);

	switch (type)
	{
	case CompiledShaderType::Gradient:
	{
		RMLUI_ASSERT(shader.stop_positions.size() == shader.stop_colors.size());
		const int num_stops = (int)shader.stop_positions.size();

		UseProgram(ProgramId::Gradient);
		glUniform1i(GetUniformLocation(UniformId::Func), static_cast<int>(shader.gradient_function));
		glUniform2f(GetUniformLocation(UniformId::P), shader.p.x, shader.p.y);
		glUniform2f(GetUniformLocation(UniformId::V), shader.v.x, shader.v.y);
		glUniform1i(GetUniformLocation(UniformId::NumStops), num_stops);
		glUniform1fv(GetUniformLocation(UniformId::StopPositions), num_stops, shader.stop_positions.data());
		glUniform4fv(GetUniformLocation(UniformId::StopColors), num_stops, shader.stop_colors[0]);

		SubmitTransformUniform(translation);
		glBindVertexArray(geometry.vao);
		glDrawElements(GL_TRIANGLES, geometry.draw_count, GL_UNSIGNED_INT, (const GLvoid*)0);
		glBindVertexArray(0);
	}
	break;
	case CompiledShaderType::Creation:
	{
		const double time = Rml::GetSystemInterface()->GetElapsedTime();

		UseProgram(ProgramId::Creation);
		glUniform1f(GetUniformLocation(UniformId::Value), (float)time);
		glUniform2f(GetUniformLocation(UniformId::Dimensions), shader.dimensions.x, shader.dimensions.y);

		SubmitTransformUniform(translation);
		glBindVertexArray(geometry.vao);
		glDrawElements(GL_TRIANGLES, geometry.draw_count, GL_UNSIGNED_INT, (const GLvoid*)0);
		glBindVertexArray(0);
	}
	break;
	case CompiledShaderType::Invalid:
	{
		Rml::Log::Message(Rml::Log::LT_WARNING, "Unhandled render shader %d.", (int)type);
	}
	break;
	}

	Gfx::CheckGLError("RenderShader");
}

void RenderInterface_GL3::ReleaseShader(Rml::CompiledShaderHandle shader_handle)
{
	delete reinterpret_cast<CompiledShader*>(shader_handle);
}

void RenderInterface_GL3::BlitLayerToPostprocessPrimary(Rml::LayerHandle layer_handle)
{
	const Gfx::FramebufferData& source = render_layers.GetLayer(layer_handle);
	const Gfx::FramebufferData& destination = render_layers.GetPostprocessPrimary();
	glBindFramebuffer(GL_READ_FRAMEBUFFER, source.framebuffer);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination.framebuffer);

	// Blit and resolve MSAA. Any active scissor state will restrict the size of the blit region.
	glBlitFramebuffer(0, 0, source.width, source.height, 0, 0, destination.width, destination.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

void RenderInterface_GL3::RenderFilters(Rml::Span<const Rml::CompiledFilterHandle> filter_handles)
{
	frame_full_screen = true; // realm: see frame_bounds.

	for (const Rml::CompiledFilterHandle filter_handle : filter_handles)
	{
		const CompiledFilter& filter = *reinterpret_cast<const CompiledFilter*>(filter_handle);
		const FilterType type = filter.type;

		switch (type)
		{
		case FilterType::Passthrough:
		{
			UseProgram(ProgramId::Passthrough);
			glBlendFunc(GL_CONSTANT_COLOR, GL_ZERO);
			glBlendColor(filter.blend_factor, filter.blend_factor, filter.blend_factor, filter.blend_factor);

			const Gfx::FramebufferData& source = render_layers.GetPostprocessPrimary();
			const Gfx::FramebufferData& destination = render_layers.GetPostprocessSecondary();
			Gfx::BindTexture(source);
			glBindFramebuffer(GL_FRAMEBUFFER, destination.framebuffer);

			DrawFullscreenQuad();

			render_layers.SwapPostprocessPrimarySecondary();
			glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		}
		break;
		case FilterType::Blur:
		{
			glDisable(GL_BLEND);

			const Gfx::FramebufferData& source_destination = render_layers.GetPostprocessPrimary();
			const Gfx::FramebufferData& temp = render_layers.GetPostprocessSecondary();

			const Rml::Rectanglei window_flipped = VerticallyFlipped(scissor_state, viewport_height);
			RenderBlur(filter.sigma, source_destination, temp, window_flipped);

			glEnable(GL_BLEND);
		}
		break;
		case FilterType::DropShadow:
		{
			UseProgram(ProgramId::DropShadow);
			glDisable(GL_BLEND);

			Rml::Colourf color = ConvertToColorf(filter.color);
			glUniform4fv(GetUniformLocation(UniformId::Color), 1, &color[0]);

			const Gfx::FramebufferData& primary = render_layers.GetPostprocessPrimary();
			const Gfx::FramebufferData& secondary = render_layers.GetPostprocessSecondary();
			Gfx::BindTexture(primary);
			glBindFramebuffer(GL_FRAMEBUFFER, secondary.framebuffer);

			const Rml::Rectanglei window_flipped = VerticallyFlipped(scissor_state, viewport_height);
			SetTexCoordLimits(GetUniformLocation(UniformId::TexCoordMin), GetUniformLocation(UniformId::TexCoordMax), window_flipped,
				{primary.width, primary.height});

			const Rml::Vector2f uv_offset = filter.offset / Rml::Vector2f(-(float)viewport_width, (float)viewport_height);
			DrawFullscreenQuad(uv_offset);

			if (filter.sigma >= 0.5f)
			{
				const Gfx::FramebufferData& tertiary = render_layers.GetPostprocessTertiary();
				RenderBlur(filter.sigma, secondary, tertiary, window_flipped);
			}

			UseProgram(ProgramId::Passthrough);
			BindTexture(primary);
			glEnable(GL_BLEND);
			DrawFullscreenQuad();

			render_layers.SwapPostprocessPrimarySecondary();
		}
		break;
		case FilterType::ColorMatrix:
		{
			UseProgram(ProgramId::ColorMatrix);
			glDisable(GL_BLEND);

			const GLint uniform_location = program_data->uniforms.Get(ProgramId::ColorMatrix, UniformId::ColorMatrix);
			constexpr bool transpose = std::is_same_v<decltype(filter.color_matrix), Rml::RowMajorMatrix4f>;
			glUniformMatrix4fv(uniform_location, 1, transpose, filter.color_matrix.data());

			const Gfx::FramebufferData& source = render_layers.GetPostprocessPrimary();
			const Gfx::FramebufferData& destination = render_layers.GetPostprocessSecondary();
			Gfx::BindTexture(source);
			glBindFramebuffer(GL_FRAMEBUFFER, destination.framebuffer);

			DrawFullscreenQuad();

			render_layers.SwapPostprocessPrimarySecondary();
			glEnable(GL_BLEND);
		}
		break;
		case FilterType::MaskImage:
		{
			UseProgram(ProgramId::BlendMask);
			glDisable(GL_BLEND);

			const Gfx::FramebufferData& source = render_layers.GetPostprocessPrimary();
			const Gfx::FramebufferData& blend_mask = render_layers.GetBlendMask();
			const Gfx::FramebufferData& destination = render_layers.GetPostprocessSecondary();

			Gfx::BindTexture(source);
			glActiveTexture(GL_TEXTURE1);
			Gfx::BindTexture(blend_mask);
			glActiveTexture(GL_TEXTURE0);

			glBindFramebuffer(GL_FRAMEBUFFER, destination.framebuffer);

			DrawFullscreenQuad();

			render_layers.SwapPostprocessPrimarySecondary();
			glEnable(GL_BLEND);
		}
		break;
		case FilterType::Invalid:
		{
			Rml::Log::Message(Rml::Log::LT_WARNING, "Unhandled render filter %d.", (int)type);
		}
		break;
		}
	}

	Gfx::CheckGLError("RenderFilter");
}

Rml::LayerHandle RenderInterface_GL3::PushLayer()
{
	EndRecording(); // realm: layer, filter and shader paths are not queued.
	frame_full_screen = true; // realm: see frame_bounds.

	const Rml::LayerHandle layer_handle = render_layers.PushLayer();

	glBindFramebuffer(GL_FRAMEBUFFER, render_layers.GetLayer(layer_handle).framebuffer);
	glClear(GL_COLOR_BUFFER_BIT);

	return layer_handle;
}

void RenderInterface_GL3::CompositeLayers(Rml::LayerHandle source_handle, Rml::LayerHandle destination_handle, Rml::BlendMode blend_mode,
	Rml::Span<const Rml::CompiledFilterHandle> filters)
{
	using Rml::BlendMode;
	EndRecording(); // realm: layer, filter and shader paths are not queued.
	frame_full_screen = true; // realm: see frame_bounds.

	// Blit source layer to postprocessing buffer. Do this regardless of whether we actually have any filters to be
	// applied, because we need to resolve the multi-sampled framebuffer in any case.
	// @performance If we have BlendMode::Replace and no filters or mask then we can just blit directly to the destination.
	BlitLayerToPostprocessPrimary(source_handle);

	// Render the filters, the PostprocessPrimary framebuffer is used for both input and output.
	RenderFilters(filters);

	// Render to the destination layer.
	glBindFramebuffer(GL_FRAMEBUFFER, render_layers.GetLayer(destination_handle).framebuffer);
	Gfx::BindTexture(render_layers.GetPostprocessPrimary());

	UseProgram(ProgramId::Passthrough);

	if (blend_mode == BlendMode::Replace)
		glDisable(GL_BLEND);

	DrawFullscreenQuad();

	if (blend_mode == BlendMode::Replace)
		glEnable(GL_BLEND);

	if (destination_handle != render_layers.GetTopLayerHandle())
		glBindFramebuffer(GL_FRAMEBUFFER, render_layers.GetTopLayer().framebuffer);

	Gfx::CheckGLError("CompositeLayers");
}

void RenderInterface_GL3::PopLayer()
{
	EndRecording(); // realm: layer, filter and shader paths are not queued.
	render_layers.PopLayer();
	glBindFramebuffer(GL_FRAMEBUFFER, render_layers.GetTopLayer().framebuffer);
}

Rml::TextureHandle RenderInterface_GL3::SaveLayerAsTexture()
{
	EndRecording(); // realm: layer, filter and shader paths are not queued.
	frame_full_screen = true; // realm: see frame_bounds.
	RMLUI_ASSERT(scissor_state.Valid());
	const Rml::Rectanglei bounds = scissor_state;

	GLuint render_texture = Gfx::CreateTexture({}, bounds.Size());
	if (render_texture == 0)
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to create render texture.");
		return {};
	}

	BlitLayerToPostprocessPrimary(render_layers.GetTopLayerHandle());

	EnableScissorRegion(false);

	const Gfx::FramebufferData& source = render_layers.GetPostprocessPrimary();
	const Gfx::FramebufferData& destination = render_layers.GetPostprocessSecondary();
	glBindFramebuffer(GL_READ_FRAMEBUFFER, source.framebuffer);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination.framebuffer);

	// Flip the image vertically, as that convention is used for textures, and move to origin.
	glBlitFramebuffer(                                  //
		bounds.Left(), source.height - bounds.Bottom(), // src0
		bounds.Right(), source.height - bounds.Top(),   // src1
		0, bounds.Height(),                             // dst0
		bounds.Width(), 0,                              // dst1
		GL_COLOR_BUFFER_BIT, GL_NEAREST                 //
	);

	glBindTexture(GL_TEXTURE_2D, render_texture);

	const Gfx::FramebufferData& texture_source = destination;
	glBindFramebuffer(GL_READ_FRAMEBUFFER, texture_source.framebuffer);
	glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, bounds.Width(), bounds.Height());

	SetScissor(bounds);
	glBindFramebuffer(GL_FRAMEBUFFER, render_layers.GetTopLayer().framebuffer);
	Gfx::CheckGLError("SaveLayerAsTexture");

	return (Rml::TextureHandle)render_texture;
}

Rml::CompiledFilterHandle RenderInterface_GL3::SaveLayerAsMaskImage()
{
	EndRecording(); // realm: layer, filter and shader paths are not queued.
	frame_full_screen = true; // realm: see frame_bounds.
	BlitLayerToPostprocessPrimary(render_layers.GetTopLayerHandle());

	const Gfx::FramebufferData& source = render_layers.GetPostprocessPrimary();
	const Gfx::FramebufferData& destination = render_layers.GetBlendMask();

	glBindFramebuffer(GL_FRAMEBUFFER, destination.framebuffer);
	BindTexture(source);
	UseProgram(ProgramId::Passthrough);
	glDisable(GL_BLEND);

	DrawFullscreenQuad();

	glEnable(GL_BLEND);
	glBindFramebuffer(GL_FRAMEBUFFER, render_layers.GetTopLayer().framebuffer);
	Gfx::CheckGLError("SaveLayerAsMaskImage");

	CompiledFilter filter = {};
	filter.type = FilterType::MaskImage;
	return reinterpret_cast<Rml::CompiledFilterHandle>(new CompiledFilter(std::move(filter)));
}

void RenderInterface_GL3::UseProgram(ProgramId program_id)
{
	RMLUI_ASSERT(program_data);
	if (active_program != program_id)
	{
		if (program_id != ProgramId::None)
			glUseProgram(program_data->programs[program_id]);
		active_program = program_id;
	}
}

int RenderInterface_GL3::GetUniformLocation(UniformId uniform_id) const
{
	return program_data->uniforms.Get(active_program, uniform_id);
}

void RenderInterface_GL3::SubmitTransformUniform(Rml::Vector2f translation)
{
	static_assert((size_t)ProgramId::Count < MaxNumPrograms, "Maximum number of programs exceeded.");
	const size_t program_index = (size_t)active_program;

	if (program_transform_dirty.test(program_index))
	{
		glUniformMatrix4fv(GetUniformLocation(UniformId::Transform), 1, false, transform.data());
		program_transform_dirty.set(program_index, false);
	}

	glUniform2fv(GetUniformLocation(UniformId::Translate), 1, &translation.x);

	Gfx::CheckGLError("SubmitTransformUniform");
}

RenderInterface_GL3::RenderLayerStack::RenderLayerStack()
{
	fb_postprocess.resize(4);
}

RenderInterface_GL3::RenderLayerStack::~RenderLayerStack()
{
	DestroyFramebuffers();
}

Rml::LayerHandle RenderInterface_GL3::RenderLayerStack::PushLayer()
{
	RMLUI_ASSERT(layers_size <= (int)fb_layers.size());

	if (layers_size == (int)fb_layers.size())
	{
		// All framebuffers should share a single stencil buffer.
		GLuint shared_depth_stencil = (fb_layers.empty() ? 0 : fb_layers.front().depth_stencil_buffer);

		fb_layers.push_back(Gfx::FramebufferData{});
		Gfx::CreateFramebuffer(fb_layers.back(), width, height, RMLUI_NUM_MSAA_SAMPLES, Gfx::FramebufferAttachment::DepthStencil,
			shared_depth_stencil);
	}

	layers_size += 1;
	return GetTopLayerHandle();
}

void RenderInterface_GL3::RenderLayerStack::PopLayer()
{
	RMLUI_ASSERT(layers_size > 0);
	layers_size -= 1;
}

const Gfx::FramebufferData& RenderInterface_GL3::RenderLayerStack::GetLayer(Rml::LayerHandle layer) const
{
	RMLUI_ASSERT((size_t)layer < (size_t)layers_size);
	return fb_layers[layer];
}

const Gfx::FramebufferData& RenderInterface_GL3::RenderLayerStack::GetTopLayer() const
{
	return GetLayer(GetTopLayerHandle());
}

Rml::LayerHandle RenderInterface_GL3::RenderLayerStack::GetTopLayerHandle() const
{
	RMLUI_ASSERT(layers_size > 0);
	return static_cast<Rml::LayerHandle>(layers_size - 1);
}

void RenderInterface_GL3::RenderLayerStack::SwapPostprocessPrimarySecondary()
{
	std::swap(fb_postprocess[0], fb_postprocess[1]);
}

bool RenderInterface_GL3::RenderLayerStack::BeginFrame(int new_width, int new_height)
{
	RMLUI_ASSERT(layers_size == 0);

	if (new_width != width || new_height != height)
	{
		width = new_width;
		height = new_height;

		DestroyFramebuffers();
	}

	// realm: the base layer storage is new if no framebuffers survived the resize.
	const bool base_layer_created = fb_layers.empty();
	PushLayer();
	return base_layer_created;
}

void RenderInterface_GL3::RenderLayerStack::EndFrame()
{
	RMLUI_ASSERT(layers_size == 1);
	PopLayer();
}

void RenderInterface_GL3::RenderLayerStack::DestroyFramebuffers()
{
	RMLUI_ASSERTMSG(layers_size == 0, "Do not call this during frame rendering, that is, between BeginFrame() and EndFrame().");

	for (Gfx::FramebufferData& fb : fb_layers)
		Gfx::DestroyFramebuffer(fb);

	fb_layers.clear();

	for (Gfx::FramebufferData& fb : fb_postprocess)
		Gfx::DestroyFramebuffer(fb);
}

const Gfx::FramebufferData& RenderInterface_GL3::RenderLayerStack::EnsureFramebufferPostprocess(int index)
{
	RMLUI_ASSERT(index < (int)fb_postprocess.size())
	Gfx::FramebufferData& fb = fb_postprocess[index];
	if (!fb.framebuffer)
		Gfx::CreateFramebuffer(fb, width, height, 0, Gfx::FramebufferAttachment::None, 0);
	return fb;
}

const Rml::Matrix4f& RenderInterface_GL3::GetTransform() const
{
	return transform;
}

void RenderInterface_GL3::ResetProgram()
{
	EndRecording(); // realm: layer, filter and shader paths are not queued.
	UseProgram(ProgramId::None);
}

bool RmlGL3::Initialize(Rml::String* out_message)
{
#if defined(RMLUI_PLATFORM_EMSCRIPTEN)
	if (out_message)
		*out_message = "Started Emscripten WebGL renderer.";
#elif defined(__ANDROID__)
	if (out_message)
		*out_message = "Started OpenGL ES 3 renderer.";
#elif !defined RMLUI_GL3_CUSTOM_LOADER
	const int gl_version = gladLoaderLoadGL();
	if (gl_version == 0)
	{
		if (out_message)
			*out_message = "Failed to initialize OpenGL context.";
		return false;
	}

	if (out_message)
		*out_message = Rml::CreateString("Loaded OpenGL %d.%d.", GLAD_VERSION_MAJOR(gl_version), GLAD_VERSION_MINOR(gl_version));
#endif

	return true;
}

void RmlGL3::Shutdown()
{
#if !defined(RMLUI_PLATFORM_EMSCRIPTEN) && !defined(__ANDROID__) && !defined(RMLUI_GL3_CUSTOM_LOADER)
	gladLoaderUnloadGL();
#endif
}
