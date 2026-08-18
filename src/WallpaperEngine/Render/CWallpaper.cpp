#include "CWallpaper.h"
#include "WallpaperEngine/Application/WallpaperApplication.h"
#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/Render/Wallpapers/CScene.h"
#include "WallpaperEngine/Render/Wallpapers/CVideo.h"
#include "WallpaperEngine/Render/Wallpapers/CWeb.h"

#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Data/Model/Wallpaper.h"

using namespace WallpaperEngine::Render;

CWallpaper::CWallpaper (
    const Wallpaper& wallpaperData, RenderContext& context, AudioContext& audioContext,
    const WallpaperState::TextureUVsScaling& scalingMode, const uint32_t& clampMode
) :
    ContextAware (context), FBOProvider (nullptr), m_wallpaperData (wallpaperData), m_audioContext (audioContext),
    m_state (scalingMode, clampMode) {
    // generate the VAO to stop opengl from complaining
    glGenVertexArrays (1, &this->m_vaoBuffer);
    glBindVertexArray (this->m_vaoBuffer);

    this->setupShaders ();

    constexpr GLfloat texCoords[] = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f };

    // inverted positions so the final texture is rendered properly
    constexpr GLfloat position[] = { -1.0f, 1.0f,  0.0f, 1.0,  1.0f, 0.0f, -1.0f, -1.0f, 0.0f,
				     -1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,  -1.0f, 0.0f };

    glGenBuffers (1, &this->m_texCoordBuffer);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_texCoordBuffer);
    glBufferData (GL_ARRAY_BUFFER, sizeof (texCoords), texCoords, GL_STATIC_DRAW);

    glGenBuffers (1, &this->m_positionBuffer);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_positionBuffer);
    glBufferData (GL_ARRAY_BUFFER, sizeof (position), position, GL_STATIC_DRAW);
}

CWallpaper::~CWallpaper () {
    // destroy shader programs
    GLuint attachedShaders[2];
    GLsizei attachedCount = 0;

    // destroy shaders (we only attach 2 to each program)
    glGetAttachedShaders (this->m_shader, 2, &attachedCount, attachedShaders);

    for (auto i = 0; i < attachedCount; i++) {
	glDeleteShader (attachedShaders[i]);
    }

    glDeleteProgram (this->m_shader);

    // destroy used buffers
    glDeleteBuffers (1, &this->m_texCoordBuffer);
    glDeleteBuffers (1, &this->m_positionBuffer);
    glDeleteVertexArrays (1, &this->m_vaoBuffer);
}

const AssetLocator& CWallpaper::getAssetLocator () const { return *this->m_wallpaperData.project.assetLocator; }

const Wallpaper& CWallpaper::getWallpaperData () const { return this->m_wallpaperData; }

GLuint CWallpaper::getWallpaperFramebuffer () const { return this->m_sceneFBO->getFramebuffer (); }

GLuint CWallpaper::getWallpaperTexture () const { return this->m_sceneFBO->getTextureID (0); }

void CWallpaper::setupShaders () {
    // reserve shaders in OpenGL
    const GLuint vertexShaderID = glCreateShader (GL_VERTEX_SHADER);

    // give shader's source code to OpenGL to be compiled
    const char* sourcePointer = "#version 330\n"
				"precision highp float;\n"
				"in vec3 a_Position;\n"
				"in vec2 a_TexCoord;\n"
				"out vec2 v_TexCoord;\n"
				"void main () {\n"
				"gl_Position = vec4 (a_Position, 1.0);\n"
				"v_TexCoord = a_TexCoord;\n"
				"}";

    glShaderSource (vertexShaderID, 1, &sourcePointer, nullptr);
    glCompileShader (vertexShaderID);

    GLint result = GL_FALSE;
    int infoLogLength = 0;

    // ensure the vertex shader was correctly compiled
    glGetShaderiv (vertexShaderID, GL_COMPILE_STATUS, &result);
    glGetShaderiv (vertexShaderID, GL_INFO_LOG_LENGTH, &infoLogLength);

    if (infoLogLength > 0) {
	const auto logBuffer = new char[infoLogLength + 1];
	// ensure logBuffer ends with a \0
	memset (logBuffer, 0, infoLogLength + 1);
	// get information about the error
	glGetShaderInfoLog (vertexShaderID, infoLogLength, nullptr, logBuffer);
	// throw an exception about the issue
	const std::string message = logBuffer;
	// free the buffer
	delete[] logBuffer;
	// throw an exception
	sLog.exception (message);
    }

    // reserve shaders in OpenGL
    const GLuint fragmentShaderID = glCreateShader (GL_FRAGMENT_SHADER);

    // give shader's source code to OpenGL to be compiled. When the caller
    // sets a non-default background mode (e.g. --background-mode color=#…),
    // discard fragments whose UVs fall outside [0,1] so the pillarbox
    // keeps whatever the default framebuffer was cleared to, instead of
    // the opaque edge-clamped scene pixel.
    sourcePointer = "#version 330\n"
		    "precision highp float;\n"
		    "uniform sampler2D g_Texture0;\n"
		    "uniform bool g_DiscardOutside;\n"
		    "in vec2 v_TexCoord;\n"
		    "out vec4 out_FragColor;\n"
		    "void main () {\n"
		    "if (g_DiscardOutside) {\n"
		    "  if (v_TexCoord.x < 0.0 || v_TexCoord.x > 1.0\n"
		    "      || v_TexCoord.y < 0.0 || v_TexCoord.y > 1.0) discard;\n"
		    "}\n"
		    "out_FragColor = texture (g_Texture0, v_TexCoord);\n"
		    "}";

    glShaderSource (fragmentShaderID, 1, &sourcePointer, nullptr);
    glCompileShader (fragmentShaderID);

    result = GL_FALSE;
    infoLogLength = 0;

    // ensure the vertex shader was correctly compiled
    glGetShaderiv (fragmentShaderID, GL_COMPILE_STATUS, &result);
    glGetShaderiv (fragmentShaderID, GL_INFO_LOG_LENGTH, &infoLogLength);

    if (infoLogLength > 0) {
	const auto logBuffer = new char[infoLogLength + 1];
	// ensure logBuffer ends with a \0
	memset (logBuffer, 0, infoLogLength + 1);
	// get information about the error
	glGetShaderInfoLog (fragmentShaderID, infoLogLength, nullptr, logBuffer);
	// throw an exception about the issue
	const std::string message = logBuffer;
	// free the buffer
	delete[] logBuffer;
	// throw an exception
	sLog.exception (message);
    }

    // create the final program
    this->m_shader = glCreateProgram ();
    // link the shaders together
    glAttachShader (this->m_shader, vertexShaderID);
    glAttachShader (this->m_shader, fragmentShaderID);
    glLinkProgram (this->m_shader);
    // check that the shader was properly linked
    result = GL_FALSE;
    infoLogLength = 0;

    glGetProgramiv (this->m_shader, GL_LINK_STATUS, &result);
    glGetProgramiv (this->m_shader, GL_INFO_LOG_LENGTH, &infoLogLength);

    if (infoLogLength > 0) {
	const auto logBuffer = new char[infoLogLength + 1];
	// ensure logBuffer ends with a \0
	memset (logBuffer, 0, infoLogLength + 1);
	// get information about the error
	glGetProgramInfoLog (this->m_shader, infoLogLength, nullptr, logBuffer);
	// throw an exception about the issue
	const std::string message = logBuffer;
	// free the buffer
	delete[] logBuffer;
	// throw an exception
	sLog.exception (message);
    }

    // after being liked shaders can be dettached and deleted
    glDetachShader (this->m_shader, vertexShaderID);
    glDetachShader (this->m_shader, fragmentShaderID);

    glDeleteShader (vertexShaderID);
    glDeleteShader (fragmentShaderID);

    // get textures
    this->g_Texture0 = glGetUniformLocation (this->m_shader, "g_Texture0");
    this->g_DiscardOutside = glGetUniformLocation (this->m_shader, "g_DiscardOutside");
    this->a_Position = glGetAttribLocation (this->m_shader, "a_Position");
    this->a_TexCoord = glGetAttribLocation (this->m_shader, "a_TexCoord");
}

void CWallpaper::setDestinationFramebuffer (GLuint framebuffer) { this->m_destFramebuffer = framebuffer; }

void CWallpaper::setSpanInfo (const SpanInfo& spanInfo) { this->m_spanInfo = spanInfo; }

const CWallpaper::SpanInfo* CWallpaper::getSpanInfo () const {
    return this->m_spanInfo.has_value () ? &this->m_spanInfo.value () : nullptr;
}

void CWallpaper::updateUVs (const glm::ivec4& viewport, const bool vflip) {
    // update UVs if something has changed, otherwise use old values
    if (this->m_state.hasChanged (viewport, vflip, this->getWidth (), this->getHeight ())) {
	// Update wallpaper state
	this->m_state.updateState (viewport, vflip, this->getWidth (), this->getHeight ());
    }
}

void CWallpaper::render (
    const glm::ivec4& viewport, const bool vflip, const glm::ivec2& globalPosition, const glm::ivec2& logicalSize
) {
    // Get current frame counter from the driver to avoid redundant scene renders
    const uint32_t currentFrame = this->getContext ().getDriver ().getFrameCounter ();
    const bool needsSceneRender = (currentFrame != this->m_lastRenderedFrame);
    const glm::ivec4 sceneViewport = this->m_spanInfo.has_value ()
	? glm::ivec4 { 0, 0, this->m_spanInfo->totalBounds.z, this->m_spanInfo->totalBounds.w }
	: viewport;

#if !NDEBUG
    glPushDebugGroup (GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Rendering scene");
#endif /* !NDEBUG */
    if (needsSceneRender) {
	this->renderFrame (sceneViewport);
	this->m_lastRenderedFrame = currentFrame;
    }
#if !NDEBUG
    glPopDebugGroup ();
#endif /* !NDEBUG */

    // Backdrop-blur pass. Runs BEFORE the fit blit so the final scene draw
    // sits on top of the blurred backdrop. The fit blit's g_DiscardOutside
    // uniform is set later; for pillarbox/letterbox fragments the scene
    // discard lets this blurred layer show through.
    const auto& bgMode =
	this->getContext ().getApp ().getContext ().settings.general.backgroundMode;
    if (bgMode.kind == Application::ApplicationContext::BackgroundMode::Blur) {
#if !NDEBUG
	glPushDebugGroup (GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Rendering backdrop blur");
#endif
	this->renderBackdropBlur (viewport);
#if !NDEBUG
	glPopDebugGroup ();
#endif
    }

#if !NDEBUG
    glPushDebugGroup (GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Rendering scene to output");
#endif /* !NDEBUG */

    float ustart, uend, vstart, vend;

    if (this->m_spanInfo.has_value ()) {
	// Span mode: treat bounding box as virtual viewport, scale wallpaper using
	// the normal scaling rules (fill/fit/stretch/default), then slice per monitor.
	const auto& span = this->m_spanInfo.value ();
	const float spanW = static_cast<float> (span.totalBounds.z);
	const float spanH = static_cast<float> (span.totalBounds.w);
	const float spanX = static_cast<float> (span.totalBounds.x);
	const float spanY = static_cast<float> (span.totalBounds.y);

	// Compute base UVs for the wallpaper scaled to the bounding box
	this->updateUVs (span.totalBounds, vflip);
	auto [baseUstart, baseUend, baseVstart, baseVend] = this->m_state.getTextureUVs ();

	// This viewport's relative position within the bounding box [0..1]
	// Use logicalSize (same coordinate space as globalPosition and totalBounds)
	const float relLeft = (static_cast<float> (globalPosition.x) - spanX) / spanW;
	const float relRight = (static_cast<float> (globalPosition.x + logicalSize.x) - spanX) / spanW;
	const float relTop = (static_cast<float> (globalPosition.y) - spanY) / spanH;
	const float relBottom = (static_cast<float> (globalPosition.y + logicalSize.y) - spanY) / spanH;

	// Interpolate within the base UVs to get this viewport's slice
	const float baseURange = baseUend - baseUstart;
	const float baseVRange = baseVend - baseVstart;

	ustart = baseUstart + relLeft * baseURange;
	uend = baseUstart + relRight * baseURange;
	vstart = baseVstart + relTop * baseVRange;
	vend = baseVstart + relBottom * baseVRange;

	// Log span debug info only on first few frames
	if (this->m_lastRenderedFrame < 5) {
	    sLog.debug (
		"SPAN DEBUG: viewport=", viewport.z, "x", viewport.w, " globalPos=(", globalPosition.x, ",",
		globalPosition.y, ")", " span=(", span.totalBounds.x, ",", span.totalBounds.y, ",", span.totalBounds.z,
		",", span.totalBounds.w, ")", " rel=[", relLeft, ",", relRight, "]x[", relTop, ",", relBottom, "]",
		" baseUV=[", baseUstart, ",", baseUend, "]x[", baseVstart, ",", baseVend, "]", " finalUV=[", ustart,
		",", uend, "]x[", vstart, ",", vend, "]"
	    );
	}
    } else {
	// Normal mode: compute UVs based on viewport dimensions and wallpaper resolution
	updateUVs (viewport, vflip);
	auto uvs = this->m_state.getTextureUVs ();
	ustart = uvs.ustart;
	uend = uvs.uend;
	vstart = uvs.vstart;
	vend = uvs.vend;
    }

    const GLfloat texCoords[] = {
	ustart, vstart, uend, vstart, ustart, vend, ustart, vend, uend, vstart, uend, vend,
    };

    glViewport (viewport.x, viewport.y, viewport.z, viewport.w);

    glBindFramebuffer (GL_FRAMEBUFFER, this->m_destFramebuffer);

    glBindVertexArray (this->m_vaoBuffer);

    glDisable (GL_BLEND);
    glDisable (GL_DEPTH_TEST);
    glDisable (GL_CULL_FACE);
    // do not use any shader
    glUseProgram (this->m_shader);
    // activate scene texture
    glActiveTexture (GL_TEXTURE0);
    glBindTexture (GL_TEXTURE_2D, this->getWallpaperTexture ());
    // set uniforms and attribs
    glEnableVertexAttribArray (this->a_TexCoord);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_texCoordBuffer);
    glBufferData (GL_ARRAY_BUFFER, sizeof (texCoords), texCoords, GL_STATIC_DRAW);
    glVertexAttribPointer (this->a_TexCoord, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

    glEnableVertexAttribArray (this->a_Position);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_positionBuffer);
    glVertexAttribPointer (this->a_Position, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

    glUniform1i (this->g_Texture0, 0);
    // Tell the blit shader to discard pillarbox/letterbox fragments when a
    // non-default background mode is active, so the default framebuffer's
    // pre-painted background (e.g. cleared to --background-mode color) shows
    // through instead of the opaque edge-clamped scene pixel.
    glUniform1i (this->g_DiscardOutside, bgMode.kind != Application::ApplicationContext::BackgroundMode::None);
    // write the framebuffer as is to the screen
    glBindBuffer (GL_ARRAY_BUFFER, this->m_texCoordBuffer);
    glDrawArrays (GL_TRIANGLES, 0, 6);

#if !NDEBUG
    glPopDebugGroup ();
#endif /* !NDEBUG */
}

void CWallpaper::setPause (bool newState) { }

void CWallpaper::setupFramebuffers () {
    const uint32_t width = this->getWidth ();
    const uint32_t height = this->getHeight ();
    const uint32_t clamp = this->m_state.getClampingMode ();

    // create framebuffer for the scene
    this->m_sceneFBO = this->create (
	"_rt_FullFrameBuffer", TextureFormat_ARGB8888, clamp, 1.0, { width, height }, { width, height }
    );

    this->alias ("_rt_MipMappedFrameBuffer", "_rt_FullFrameBuffer");

    // Always allocate the backdrop-blur pipeline so IPC can switch to Blur
    // mode at runtime without reinitializing GL state mid-frame. Costs a
    // few MB of FBOs that go unused under non-Blur modes; worth it for a
    // branch-free live-tweak path.
    this->setupBackdropBlur ();
}

void CWallpaper::setupBackdropBlur () {
    // Half-res downsample + separable gaussian blur chain. Sized off the
    // scene FBO so the blur stays stable across viewport resizes; the blit
    // pass upscales via the sampler.
    const uint32_t width = this->getWidth ();
    const uint32_t height = this->getHeight ();
    const uint32_t halfW = std::max<uint32_t> (1, width / 2);
    const uint32_t halfH = std::max<uint32_t> (1, height / 2);

    this->m_backdropHalf = this->create (
	"_rt_Kuro_BackdropHalf", TextureFormat_ARGB8888, TextureFlags_ClampUVs, 1.0,
	{ halfW, halfH }, { halfW, halfH }
    );
    this->m_backdropBlurH = this->create (
	"_rt_Kuro_BackdropBlurH", TextureFormat_ARGB8888, TextureFlags_ClampUVs, 1.0,
	{ halfW, halfH }, { halfW, halfH }
    );
    this->m_backdropBlurV = this->create (
	"_rt_Kuro_BackdropBlurV", TextureFormat_ARGB8888, TextureFlags_ClampUVs, 1.0,
	{ halfW, halfH }, { halfW, halfH }
    );

    // Separable gaussian shader: 9-tap, direction toggled via uniform.
    // texelSize lets us size the kernel off the target FBO's dimensions.
    const GLuint vs = glCreateShader (GL_VERTEX_SHADER);
    const char* vsSrc =
	"#version 330\n"
	"in vec3 a_Position;\n"
	"in vec2 a_TexCoord;\n"
	"out vec2 v_TexCoord;\n"
	"void main () {\n"
	"  gl_Position = vec4 (a_Position, 1.0);\n"
	"  v_TexCoord = a_TexCoord;\n"
	"}";
    glShaderSource (vs, 1, &vsSrc, nullptr);
    glCompileShader (vs);

    const GLuint fs = glCreateShader (GL_FRAGMENT_SHADER);
    const char* fsSrc =
	"#version 330\n"
	"precision highp float;\n"
	"uniform sampler2D g_Texture0;\n"
	"uniform vec2 g_Direction;\n"
	"uniform vec2 g_TexelSize;\n"
	"in vec2 v_TexCoord;\n"
	"out vec4 out_FragColor;\n"
	// 9-tap gaussian, weights from a sigma~4 kernel normalized.
	"const float w[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);\n"
	"void main () {\n"
	"  vec2 step = g_Direction * g_TexelSize;\n"
	"  vec4 sum = texture (g_Texture0, v_TexCoord) * w[0];\n"
	"  for (int i = 1; i < 5; ++i) {\n"
	"    sum += texture (g_Texture0, v_TexCoord + step * float(i)) * w[i];\n"
	"    sum += texture (g_Texture0, v_TexCoord - step * float(i)) * w[i];\n"
	"  }\n"
	"  out_FragColor = sum;\n"
	"}";
    glShaderSource (fs, 1, &fsSrc, nullptr);
    glCompileShader (fs);

    this->m_backdropBlurShader = glCreateProgram ();
    glAttachShader (this->m_backdropBlurShader, vs);
    glAttachShader (this->m_backdropBlurShader, fs);
    glLinkProgram (this->m_backdropBlurShader);
    glDetachShader (this->m_backdropBlurShader, vs);
    glDetachShader (this->m_backdropBlurShader, fs);
    glDeleteShader (vs);
    glDeleteShader (fs);

    this->m_backdropBlurTex = glGetUniformLocation (this->m_backdropBlurShader, "g_Texture0");
    this->m_backdropBlurDirection = glGetUniformLocation (this->m_backdropBlurShader, "g_Direction");
    this->m_backdropBlurTexelSize = glGetUniformLocation (this->m_backdropBlurShader, "g_TexelSize");
}

void CWallpaper::renderBackdropBlur (const glm::ivec4& viewport) {
    if (this->m_backdropBlurShader == GL_NONE) return;

    // Common state for all blur passes.
    glDisable (GL_BLEND);
    glDisable (GL_DEPTH_TEST);
    glDisable (GL_CULL_FACE);
    glBindVertexArray (this->m_vaoBuffer);

    // Full-texture UV quad for all passes. V is flipped on the initial
    // downsample read so the blurred backdrop matches the scene's
    // orientation (the scene FBO is stored V-flipped; see the "inverted
    // positions" comment in setupShaders above).
    constexpr GLfloat flippedUVs[] = {
	0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f,
    };
    constexpr GLfloat fullUVs[] = {
	0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
    };

    glUseProgram (this->m_backdropBlurShader);
    glActiveTexture (GL_TEXTURE0);
    glUniform1i (this->m_backdropBlurTex, 0);

    const GLint posLoc = glGetAttribLocation (this->m_backdropBlurShader, "a_Position");
    const GLint uvLoc = glGetAttribLocation (this->m_backdropBlurShader, "a_TexCoord");

    glEnableVertexAttribArray (posLoc);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_positionBuffer);
    glVertexAttribPointer (posLoc, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

    glEnableVertexAttribArray (uvLoc);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_texCoordBuffer);

    const auto halfW = this->m_backdropHalf->getRealWidth ();
    const auto halfH = this->m_backdropHalf->getRealHeight ();

    // Pass 1: downsample scene → _rt_Kuro_BackdropHalf. V-flipped UVs so
    // the scene FBO (stored upside-down) lands upright in the backdrop
    // texture. Direction is zero so the shader sum collapses to the center
    // tap, and the sampler's linear filter does the prefilter.
    glBindFramebuffer (GL_FRAMEBUFFER, this->m_backdropHalf->getFramebuffer ());
    glViewport (0, 0, halfW, halfH);
    glBindTexture (GL_TEXTURE_2D, this->m_sceneFBO->getTextureID (0));
    glBufferData (GL_ARRAY_BUFFER, sizeof (flippedUVs), flippedUVs, GL_STATIC_DRAW);
    glVertexAttribPointer (uvLoc, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glUniform2f (this->m_backdropBlurDirection, 0.0f, 0.0f);
    glUniform2f (this->m_backdropBlurTexelSize, 0.0f, 0.0f);
    glDrawArrays (GL_TRIANGLES, 0, 6);

    // Subsequent passes read from already-upright FBOs, so use the
    // non-flipped UV coords.
    glBufferData (GL_ARRAY_BUFFER, sizeof (fullUVs), fullUVs, GL_STATIC_DRAW);
    glVertexAttribPointer (uvLoc, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

    // Radius multiplier — scales the blur kernel's texel stride for a
    // stronger, softer blur without adding tap count. With 9 taps and this
    // multiplier the kernel reaches ~±24 half-res pixels, which at typical
    // resolutions reads as a roughly CSS-blur(32-40px) look on the scene.
    constexpr float radius = 6.0f;

    // Pass 2: horizontal blur → _rt_Kuro_BackdropBlurH.
    glBindFramebuffer (GL_FRAMEBUFFER, this->m_backdropBlurH->getFramebuffer ());
    glViewport (0, 0, halfW, halfH);
    glBindTexture (GL_TEXTURE_2D, this->m_backdropHalf->getTextureID (0));
    glUniform2f (this->m_backdropBlurDirection, 1.0f, 0.0f);
    glUniform2f (this->m_backdropBlurTexelSize, radius / static_cast<float> (halfW), 0.0f);
    glDrawArrays (GL_TRIANGLES, 0, 6);

    // Pass 3: vertical blur → _rt_Kuro_BackdropBlurV.
    glBindFramebuffer (GL_FRAMEBUFFER, this->m_backdropBlurV->getFramebuffer ());
    glViewport (0, 0, halfW, halfH);
    glBindTexture (GL_TEXTURE_2D, this->m_backdropBlurH->getTextureID (0));
    glUniform2f (this->m_backdropBlurDirection, 0.0f, 1.0f);
    glUniform2f (this->m_backdropBlurTexelSize, 0.0f, radius / static_cast<float> (halfH));
    glDrawArrays (GL_TRIANGLES, 0, 6);

    // Pass 4: draw the blurred texture full-viewport into the destination
    // framebuffer. Reuse CWallpaper's own blit shader since it does exactly
    // what we need (textured quad). Disable the discard guard for this pass.
    glBindFramebuffer (GL_FRAMEBUFFER, this->m_destFramebuffer);
    glViewport (viewport.x, viewport.y, viewport.z, viewport.w);
    glUseProgram (this->m_shader);
    glBindTexture (GL_TEXTURE_2D, this->m_backdropBlurV->getTextureID (0));
    glUniform1i (this->g_Texture0, 0);
    glUniform1i (this->g_DiscardOutside, 0);
    glEnableVertexAttribArray (this->a_TexCoord);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_texCoordBuffer);
    glBufferData (GL_ARRAY_BUFFER, sizeof (fullUVs), fullUVs, GL_STATIC_DRAW);
    glVertexAttribPointer (this->a_TexCoord, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray (this->a_Position);
    glBindBuffer (GL_ARRAY_BUFFER, this->m_positionBuffer);
    glVertexAttribPointer (this->a_Position, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDrawArrays (GL_TRIANGLES, 0, 6);
}

AudioContext& CWallpaper::getAudioContext () const { return this->m_audioContext; }

const WallpaperState& CWallpaper::getState () const { return this->m_state; }

std::shared_ptr<const CFBO> CWallpaper::findFBO (const std::string& name) const {
    const auto fbo = this->find (name);

    if (fbo == nullptr) {
	sLog.exception ("Cannot find FBO ", name);
    }

    return fbo;
}

std::shared_ptr<const CFBO> CWallpaper::getFBO () const { return this->m_sceneFBO; }

std::unique_ptr<CWallpaper> CWallpaper::fromWallpaper (
    const Wallpaper& wallpaper, RenderContext& context, AudioContext& audioContext,
    WebBrowser::WebBrowserContext* browserContext, const WallpaperState::TextureUVsScaling& scalingMode,
    const uint32_t& clampMode
) {
    if (wallpaper.is<Scene> ()) {
	return std::make_unique<WallpaperEngine::Render::Wallpapers::CScene> (
	    wallpaper, context, audioContext, scalingMode, clampMode
	);
    }

    if (wallpaper.is<Video> ()) {
	return std::make_unique<WallpaperEngine::Render::Wallpapers::CVideo> (
	    wallpaper, context, audioContext, scalingMode, clampMode
	);
    }

    if (wallpaper.is<Web> ()) {
	return std::make_unique<WallpaperEngine::Render::Wallpapers::CWeb> (
	    wallpaper, context, audioContext, *browserContext, scalingMode, clampMode
	);
    }

    sLog.exception ("Unsupported wallpaper type");
}
