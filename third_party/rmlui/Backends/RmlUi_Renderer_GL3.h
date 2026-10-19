#pragma once

#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/Types.h>
#include <bitset>
#include <cstdint>
#include <vector>

enum class ProgramId;
enum class UniformId;
class RenderLayerStack;
namespace Gfx {
struct ProgramData;
struct FramebufferData;
} // namespace Gfx

class RenderInterface_GL3 : public Rml::RenderInterface {
public:
	RenderInterface_GL3();
	~RenderInterface_GL3();

	// Returns true if the renderer was successfully constructed.
	explicit operator bool() const { return static_cast<bool>(program_data); }

	// The viewport should be updated whenever the window size changes.
	void SetViewport(int viewport_width, int viewport_height, int viewport_offset_x = 0, int viewport_offset_y = 0);

	// Sets up OpenGL states for taking rendering commands from RmlUi.
	void BeginFrame();
	// Draws the result to the backbuffer and restores OpenGL state.
	void EndFrame();

	// Optional, can be used to clear the active framebuffer.
	void Clear();
	// realm: true when the last EndFrame composited the cached layer instead of drawing the frame.
	[[nodiscard]] bool frame_reused() const { return reused_frame; }
	// realm: call before texture contents are written outside the backend. Commands queued for the frame are replayed first, and the
	// frame cache is invalidated.
	void TextureContentChanged();

	// -- Inherited from Rml::RenderInterface --

	Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
	void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture) override;
	void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override;

	Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) override;
	Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source_data, Rml::Vector2i source_dimensions) override;
	void ReleaseTexture(Rml::TextureHandle texture_handle) override;

	void EnableScissorRegion(bool enable) override;
	void SetScissorRegion(Rml::Rectanglei region) override;

	void EnableClipMask(bool enable) override;
	void RenderToClipMask(Rml::ClipMaskOperation mask_operation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation) override;

	void SetTransform(const Rml::Matrix4f* transform) override;

	Rml::LayerHandle PushLayer() override;
	void CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination, Rml::BlendMode blend_mode,
		Rml::Span<const Rml::CompiledFilterHandle> filters) override;
	void PopLayer() override;

	Rml::TextureHandle SaveLayerAsTexture() override;

	Rml::CompiledFilterHandle SaveLayerAsMaskImage() override;

	Rml::CompiledFilterHandle CompileFilter(const Rml::String& name, const Rml::Dictionary& parameters) override;
	void ReleaseFilter(Rml::CompiledFilterHandle filter) override;

	Rml::CompiledShaderHandle CompileShader(const Rml::String& name, const Rml::Dictionary& parameters) override;
	void RenderShader(Rml::CompiledShaderHandle shader_handle, Rml::CompiledGeometryHandle geometry_handle, Rml::Vector2f translation,
		Rml::TextureHandle texture) override;
	void ReleaseShader(Rml::CompiledShaderHandle effect_handle) override;

	// Can be passed to RenderGeometry() to enable texture rendering without changing the bound texture.
	static constexpr Rml::TextureHandle TextureEnableWithoutBinding = Rml::TextureHandle(-1);
	// Can be passed to RenderGeometry() to leave the bound texture and used program unchanged.
	static constexpr Rml::TextureHandle TexturePostprocess = Rml::TextureHandle(-2);

	// -- Utility functions for clients --

	const Rml::Matrix4f& GetTransform() const;
	void ResetProgram();

private:
	void UseProgram(ProgramId program_id);
	int GetUniformLocation(UniformId uniform_id) const;
	void SubmitTransformUniform(Rml::Vector2f translation);

	void BlitLayerToPostprocessPrimary(Rml::LayerHandle layer_handle);
	void RenderFilters(Rml::Span<const Rml::CompiledFilterHandle> filter_handles);

	void SetScissor(Rml::Rectanglei region, bool vertically_flip = false);

	// realm: grows frame_bounds to cover a RenderGeometry call with the given vertex bounds.
	void ExtendFrameBounds(Rml::Vector2f bounds_min, Rml::Vector2f bounds_max, Rml::Vector2f translation);

	void DrawFullscreenQuad();
	void DrawFullscreenQuad(Rml::Vector2f uv_offset, Rml::Vector2f uv_scaling = Rml::Vector2f(1.f));
	// realm: frame command recording. See BeginFrame.
	struct FrameCommand {
		enum class Kind : std::uint8_t { Geometry, Scissor, EnableScissor, Transform };
		Kind kind{};
		bool flag{};  // EnableScissor: enable; Transform: matrix present
		Rml::CompiledGeometryHandle geometry{};
		Rml::TextureHandle texture{};
		Rml::Vector2f translation{};
		Rml::Rectanglei region{};
		Rml::Matrix4f matrix{};
		bool operator==(const FrameCommand& other) const {
			return kind == other.kind && flag == other.flag && geometry == other.geometry && texture == other.texture &&
				translation == other.translation && region == other.region && matrix == other.matrix;
		}
	};
	void EndRecording();
	void ReplayFrameCommands();
	void ClearPreviousBounds();
	void ReleaseGeometryNow(Rml::CompiledGeometryHandle handle);
	void ReleaseTextureNow(Rml::TextureHandle texture);

	void RenderBlur(float sigma, const Gfx::FramebufferData& source_destination, const Gfx::FramebufferData& temp, Rml::Rectanglei window_flipped);

	static constexpr size_t MaxNumPrograms = 32;
	std::bitset<MaxNumPrograms> program_transform_dirty;

	Rml::Matrix4f transform;
	Rml::Matrix4f projection;

	ProgramId active_program = {};
	Rml::Rectanglei scissor_state;

	int viewport_width = 0;
	int viewport_height = 0;
	int viewport_offset_x = 0;
	int viewport_offset_y = 0;

	Rml::CompiledGeometryHandle fullscreen_quad_geometry = {};

	// realm: pixel region of the base layer written this frame, rounded outwards and padded by one pixel (invalid when nothing was drawn).
	// Anything the bounds cannot model (transforms, layers, filters, shaders, ...) sets frame_full_screen instead. The base layer is
	// zero outside previous_bounds at the start of a frame, so BeginFrame only clears that region, and EndFrame only resolves and
	// composites the region drawn.
	Rml::Rectanglei frame_bounds = Rml::Rectanglei::MakeInvalid();
	bool frame_full_screen = false;
	Rml::Rectanglei previous_bounds = Rml::Rectanglei::MakeInvalid();

	Rml::UniquePtr<const Gfx::ProgramData> program_data;

	// realm: frame cache. Between BeginFrame and EndFrame, draw, scissor and transform calls are queued and replayed in order (see
	// EndRecording), so a frame can be decided before any of its GL runs. A frame whose queue and resource generation equal the
	// last replayed frame's reuses that frame's resolved layer (postprocess primary), which is still in place
	// because nothing else writes it: only the layer, filter and shader paths do, and those end recording and disable reuse.
	bool recording = false;
	bool frame_flushed = false;
	bool reused_frame = false;
	std::vector<FrameCommand> frame_commands;
	std::vector<FrameCommand> previous_commands;
	std::vector<Rml::CompiledGeometryHandle> deferred_geometry;
	std::vector<Rml::TextureHandle> deferred_textures;
	// Bumped by any change to the drawn content: texture or geometry creation and release, texture writes and viewport changes.
	std::uint64_t generation = 0;
	bool cache_valid = false;
	std::uint64_t cached_generation = 0;
	Rml::Rectanglei cached_frame_bounds = Rml::Rectanglei::MakeInvalid();
	bool cached_frame_full_screen = false;
	bool cached_scissor_enabled = false;
	int cached_scissor_box[4] = {};

	/*
	    Manages render targets, including the layer stack and postprocessing framebuffers.

	    Layers can be pushed and popped, creating new framebuffers as needed. Typically, geometry is rendered to the top
	    layer. The layer framebuffers may have MSAA enabled.

	    Postprocessing framebuffers are separate from the layers, and are commonly used to apply texture-wide effects
	    such as filters. They are used both as input and output during rendering, and do not use MSAA.
	*/
	class RenderLayerStack {
	public:
		RenderLayerStack();
		~RenderLayerStack();

		// Push a new layer. All references to previously retrieved layers are invalidated.
		Rml::LayerHandle PushLayer();

		// Pop the top layer. All references to previously retrieved layers are invalidated.
		void PopLayer();

		const Gfx::FramebufferData& GetLayer(Rml::LayerHandle layer) const;
		const Gfx::FramebufferData& GetTopLayer() const;
		Rml::LayerHandle GetTopLayerHandle() const;

		const Gfx::FramebufferData& GetPostprocessPrimary() { return EnsureFramebufferPostprocess(0); }
		const Gfx::FramebufferData& GetPostprocessSecondary() { return EnsureFramebufferPostprocess(1); }
		const Gfx::FramebufferData& GetPostprocessTertiary() { return EnsureFramebufferPostprocess(2); }
		const Gfx::FramebufferData& GetBlendMask() { return EnsureFramebufferPostprocess(3); }

		void SwapPostprocessPrimarySecondary();

		// Returns true if the base layer storage was (re)created, so its contents are undefined.
		bool BeginFrame(int new_width, int new_height);
		void EndFrame();

	private:
		void DestroyFramebuffers();
		const Gfx::FramebufferData& EnsureFramebufferPostprocess(int index);

		int width = 0, height = 0;

		// The number of active layers is manually tracked since we re-use the framebuffers stored in the fb_layers stack.
		int layers_size = 0;

		Rml::Vector<Gfx::FramebufferData> fb_layers;
		Rml::Vector<Gfx::FramebufferData> fb_postprocess;
	};

	RenderLayerStack render_layers;

	// realm: the UI pass does not back up GL state. SetFrameState sets everything the pass depends on on entry, and SetBaseState leaves the
	// renderer's baseline on exit, so no frame waits for the driver. The scissor test and box and the stencil reference are tracked
	// here, because reading them back would stall too.
	void SetFrameState();
	void SetBaseState();
	void SetGlScissorTest(bool enable);
	void SetGlScissorBox(int x, int y, int width, int height);
	void SetStencilFunc(unsigned int func, int ref);
	bool gl_scissor_on = false;
	int gl_scissor_box[4] = {};
	int stencil_ref = 0;
};

/**
    Helper functions for the OpenGL 3 renderer.
 */
namespace RmlGL3 {

// Loads OpenGL functions. Optionally, the out message describes the loaded GL version or an error message on failure.
bool Initialize(Rml::String* out_message = nullptr);

// Unloads OpenGL functions.
void Shutdown();

} // namespace RmlGL3
