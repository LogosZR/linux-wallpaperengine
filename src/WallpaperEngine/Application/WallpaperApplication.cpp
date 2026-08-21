#include "WallpaperApplication.h"

#include <cstdio>
#include <sstream>
#include <vector>

#include "IPCServer.h"
#include "Steam/FileSystem/FileSystem.h"
#include "WallpaperEngine/Application/ApplicationState.h"
#include "WallpaperEngine/Assets/AssetLoadException.h"
#include "WallpaperEngine/Audio/Drivers/Detectors/PulseAudioPlayingDetector.h"
#include "WallpaperEngine/FileSystem/Container.h"
#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/Render/Drivers/VideoFactories.h"
#include "WallpaperEngine/Render/RenderContext.h"

#include "WallpaperEngine/Data/Dumpers/StringPrinter.h"
#include "WallpaperEngine/Data/Parsers/ProjectParser.h"

#include "WallpaperEngine/Data/Model/Property.h"
#include "WallpaperEngine/Data/Model/Wallpaper.h"
#include "WallpaperEngine/Debugging/CallStack.h"
#include "WallpaperEngine/FileSystem/Adapters/MediaCover.h"
#include "WallpaperEngine/Media/DBusMediaSource.h"

#if DEMOMODE
#include "recording.h"
#endif /* DEMOMODE */

#include <algorithm>
#include <climits>
#include <numeric>
#include <unistd.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <thread>

#define FULLSCREEN_CHECK_WAIT_TIME 250

float g_Time;
float g_TimeLast;
float g_Daytime;

using namespace WallpaperEngine::Assets;
using namespace WallpaperEngine::Application;
using namespace WallpaperEngine::Data::Model;
using namespace WallpaperEngine::FileSystem;

void CustomGLDebugCallback (
    GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam
) {
    if (severity != GL_DEBUG_SEVERITY_HIGH) {
	return;
    }

    sLog.error ("OpenGL error: ", message, ", type: ", type, ", id: ", id);

    std::vector<WallpaperEngine::Debugging::CallStack::CallInfo> callInfo;

    WallpaperEngine::Debugging::CallStack::GetCalls (callInfo);

    for (std::vector<WallpaperEngine::Debugging::CallStack::CallInfo>::size_type i = 0; i < callInfo.size (); ++i) {
	fprintf (
	    stderr, "[%3lu] %15lu: %s in %s\n", callInfo.size () - i, callInfo[i].offset, callInfo[i].function.c_str (),
	    callInfo[i].module.c_str ()
	);
    }
}

WallpaperApplication::WallpaperApplication (ApplicationContext& context) : m_context (context) {
    this->initializeSubsystems ();
    this->loadBackgrounds ();
    this->setupProperties ();
    this->setupBrowser ();
    this->initializePlaylists ();
}

void WallpaperApplication::initializeSubsystems () {
    // initialize player dbus (update every 2 seconds)
    m_mediaSource = std::make_unique<WallpaperEngine::Media::DBusMediaSource> (std::chrono::milliseconds (2000));
}
WallpaperApplication::~WallpaperApplication () = default;

AssetLocatorUniquePtr WallpaperApplication::setupAssetLocator (const std::string& bg) const {
    auto container = std::make_unique<Container> ();

    const std::filesystem::path path = bg;

    container->registerAdapterFactory (std::make_unique<MediaCoverFactory> (*this->m_mediaSource));
    container->mount ("$mediaThumbnail", "$mediaThumbnail");
    container->mount (path, "/");

    try {
	container->mount (path / "scene.pkg", "/");
    } catch (std::runtime_error&) { }

    try {
	container->mount (path / "gifscene.pkg", "/");
    } catch (std::runtime_error&) { }

    try {
	container->mount (this->m_context.settings.general.assets, "/");
    } catch (std::runtime_error&) {
	sLog.exception ("Cannot find a valid assets folder, resolved to ", this->m_context.settings.general.assets);
    }

    // mount the current directory as root
    try {
	container->mount (std::filesystem::current_path (), "/");
    } catch (std::runtime_error&) { }

    auto& vfs = container->getVFS ();

    //
    // Had to get a little creative with the effects to achieve the same bloom effect without any custom code
    // these virtual files are loaded by an image in the scene that takes current _rt_FullFrameBuffer and
    // applies the bloom effect to render it out to the screen
    //

    // add the effect file for screen bloom

    // add some model for the image element even if it's going to waste rendering cycles
    vfs.add (
	"effects/wpenginelinux/bloomeffect.json",
	{ { "name", "camerabloom_wpengine_linux" },
	  { "group", "wpengine_linux_camera" },
	  { "dependencies", JSON::array () },
	  {
	      "passes",
	      JSON::array (
		  { { { "material", "materials/util/downsample_quarter_bloom.json" },
		      { "target", "_rt_4FrameBuffer" },
		      { "bind", JSON::array ({ { { "name", "_rt_FullFrameBuffer" }, { "index", 0 } } }) } },
		    { { "material", "materials/util/downsample_eighth_blur_v.json" },
		      { "target", "_rt_8FrameBuffer" },
		      { "bind", JSON::array ({ { { "name", "_rt_4FrameBuffer" }, { "index", 0 } } }) } },
		    { { "material", "materials/util/blur_h_bloom.json" },
		      { "target", "_rt_Bloom" },
		      { "bind", JSON::array ({ { { "name", "_rt_8FrameBuffer" }, { "index", 0 } } }) } },
		    { { "material", "materials/util/combine.json" },
		      { "target", "_rt_FullFrameBuffer" },
		      { "bind",
			JSON::array (
			    { { { "name", "_rt_imageLayerComposite_-1_a" }, { "index", 0 } },
			      { { "name", "_rt_Bloom" }, { "index", 1 } } }
			) } } }
	      ),
	  } }
    );

    vfs.add ("models/wpenginelinux.json", { { "material", "materials/wpenginelinux.json" } });

    vfs.add (
	"materials/wpenginelinux.json",
	{ { "passes",
	    JSON::array (
		{ { { "blending", "normal" },
		    { "cullmode", "nocull" },
		    { "depthtest", "disabled" },
		    { "depthwrite", "disabled" },
		    { "shader", "genericimage2" },
		    { "textures", JSON::array ({ "_rt_FullFrameBuffer" }) } } }
	    ) } }
    );

    vfs.add (
	"shaders/commands/copy.frag",
	"uniform sampler2D g_Texture0;\n"
	"in vec2 v_TexCoord;\n"
	"void main () {\n"
	"out_FragColor = texture (g_Texture0, v_TexCoord);\n"
	"}"
    );
    vfs.add (
	"shaders/commands/copy.vert",
	"in vec3 a_Position;\n"
	"in vec2 a_TexCoord;\n"
	"out vec2 v_TexCoord;\n"
	"void main () {\n"
	"gl_Position = vec4 (a_Position, 1.0);\n"
	"v_TexCoord = a_TexCoord;\n"
	"}"
    );

    return std::make_unique<AssetLocator> (std::move (container));
}

void WallpaperApplication::loadBackgrounds () {
    if (this->m_context.settings.render.mode == ApplicationContext::NORMAL_WINDOW
	|| this->m_context.settings.render.mode == ApplicationContext::EXPLICIT_WINDOW) {
	auto path = this->m_context.settings.general.defaultBackground;

	if (this->m_context.settings.general.defaultPlaylist.has_value ()
	    && !this->m_context.settings.general.defaultPlaylist->items.empty ()) {
	    path = this->m_context.settings.general.defaultPlaylist->items.front ();
	}

	this->m_backgrounds["default"] = this->loadBackground (path);
	return;
    }

    for (const auto& [screen, path] : this->m_context.settings.general.screenBackgrounds) {
	// skip span group synthetic keys here, they're handled below
	if (screen.rfind ("span:", 0) == 0) {
	    continue;
	}
	// screens with no path should use the default
	if (path.empty ()) {
	    this->m_backgrounds[screen] = this->loadBackground (this->m_context.settings.general.defaultBackground);
	} else {
	    this->m_backgrounds[screen] = this->loadBackground (path);
	}
    }

    // Load one background per span group
    for (const auto& spanGroup : this->m_context.settings.general.spanGroups) {
	if (spanGroup.screens.empty ()) {
	    continue;
	}

	std::filesystem::path bgPath = spanGroup.background;
	if (bgPath.empty ()) {
	    bgPath = this->m_context.settings.general.defaultBackground;
	}

	// use the first screen's name as the group key for the loaded project
	const std::string groupKey = "span:" + spanGroup.screens.front ();
	this->m_backgrounds[groupKey] = this->loadBackground (bgPath);
    }
}

ProjectUniquePtr WallpaperApplication::loadBackground (const std::string& bg) {
    try {
	auto container = this->setupAssetLocator (bg);
	auto json = WallpaperEngine::Data::JSON::JSON::parse (container->readString ("project.json"));

    // when a background is loaded, reset the screenshot variables
    // this allows taking screenshots after a background changes
    // useful for playlists
    if (this->m_context.settings.screenshot.take) {
	this->m_nextFrameScreenshot = this->m_context.settings.screenshot.delay;

	if (this->m_videoDriver != nullptr) {
	    this->m_nextFrameScreenshot += this->m_videoDriver->getFrameCounter ();
	}

	this->m_screenShotTaken = false;
    }

    return WallpaperEngine::Data::Parsers::ProjectParser::parse (json, std::move (container));
    } catch (const std::exception& e) {
	// Re-throw with the wallpaper path prefixed so the top-level catch
	// in main.cpp can tell us WHICH wallpaper killed the parser. Without
	// this we just see 'type must be number, but is string' and have
	// to guess the bg from process arg history.
	throw std::runtime_error (
	    std::string ("loadBackground failed for bg=") + bg + ": " + e.what ()
	);
    }
}

std::vector<std::size_t>
WallpaperApplication::buildPlaylistOrder (const ApplicationContext::PlaylistDefinition& definition) {
    std::vector<std::size_t> order (definition.items.size ());
    std::iota (order.begin (), order.end (), 0);

    if (definition.settings.order == "random") {
	std::shuffle (order.begin (), order.end (), this->m_playlistRng);
    }

    return order;
}

void WallpaperApplication::initializePlaylists () {
    const bool hasDefaultPlaylist = this->m_context.settings.general.defaultPlaylist.has_value ();
    const bool hasScreenPlaylists = !this->m_context.settings.general.screenPlaylists.empty ();

    if (!hasDefaultPlaylist && !hasScreenPlaylists) {
	return;
    }

    const auto now = std::chrono::steady_clock::now ();

    auto registerPlaylist = [this, now] (
				const std::string& key, const ApplicationContext::PlaylistDefinition& playlist,
				std::optional<std::filesystem::path> currentPath
			    ) {
	if (playlist.items.empty ()) {
	    return;
	}

	ActivePlaylist state;

	state.definition = playlist;
	state.order = this->buildPlaylistOrder (playlist);

	if (state.order.empty ()) {
	    return;
	}

	if (currentPath.has_value ()) {
	    state.orderIndex = 0;

	    for (std::size_t i = 0; i < state.order.size (); i++) {
		if (playlist.items[state.order[i]] == currentPath.value ()) {
		    state.orderIndex = i;
		    break;
		}
	    }
	}

	const uint32_t delayMinutes = std::max<uint32_t> (1, state.definition.settings.delayMinutes);
	state.nextSwitch = now + std::chrono::minutes (delayMinutes);
	state.lastUpdate = now;

	this->m_activePlaylists.insert_or_assign (key, std::move (state));
    };

    if (hasDefaultPlaylist
	&& (this->m_context.settings.render.mode == ApplicationContext::NORMAL_WINDOW
	    || this->m_context.settings.render.mode == ApplicationContext::EXPLICIT_WINDOW)) {
	const auto& playlist = this->m_context.settings.general.defaultPlaylist.value ();
	const auto currentPath = playlist.items.empty ()
	    ? std::optional<std::filesystem::path> { this->m_context.settings.general.defaultBackground }
	    : std::optional<std::filesystem::path> { playlist.items.front () };
	registerPlaylist ("default", playlist, currentPath);
    }

    for (const auto& [screen, playlist] : this->m_context.settings.general.screenPlaylists) {
	const auto current = this->m_context.settings.general.screenBackgrounds.find (screen);
	const auto currentPath = current != this->m_context.settings.general.screenBackgrounds.end ()
	    ? std::optional<std::filesystem::path> { current->second }
	    : std::nullopt;
	registerPlaylist (screen, playlist, currentPath);
    }
}

void WallpaperApplication::ensureBrowserForProject (const Project& project) {
    if (!project.wallpaper->is<Web> ()) {
	return;
    }

    if (!this->m_browserContext) {
	this->m_browserContext = std::make_unique<WebBrowser::WebBrowserContext> (*this);
    }
}

bool WallpaperApplication::makeAnyViewportCurrent () const {
    if (!this->m_renderContext) {
	return false;
    }

    const auto& viewports = this->m_renderContext->getOutput ().getViewports ();

    if (viewports.empty ()) {
	return false;
    }

    viewports.begin ()->second->makeCurrent ();
    return true;
}

bool WallpaperApplication::preflightWallpaper (const std::string& path) {
    try {
	// avoid mutating state, just ensure project.json parses
	auto container = this->setupAssetLocator (path);
	const auto json = WallpaperEngine::Data::JSON::JSON::parse (container->readString ("project.json"));
	if (!json.contains ("type") || !json.contains ("file")) {
	    sLog.error ("Preflight failed for ", path, ": missing required fields");
	    return false;
	}
	return true;
    } catch (const std::exception& e) {
	sLog.error ("Preflight failed for ", path, ": ", e.what ());
	return false;
    }
}

bool WallpaperApplication::selectNextCandidate (ActivePlaylist& playlist, std::size_t& outOrderIndex) {
    if (playlist.order.empty ()) {
	return false;
    }

    std::size_t attempts = 0;
    std::size_t candidateOrderIndex = outOrderIndex;

    while (attempts < playlist.order.size ()) {
	const auto candidateIndex = playlist.order[candidateOrderIndex];

	if (!playlist.failedIndices.contains (candidateIndex)) {
	    outOrderIndex = candidateOrderIndex;
	    return true;
	}

	attempts++;
	candidateOrderIndex = (candidateOrderIndex + 1) % playlist.order.size ();
    }

    return false;
}

void WallpaperApplication::advancePlaylist (
    const std::string& screen, ActivePlaylist& playlist, const std::chrono::steady_clock::time_point& now
) {
    if (playlist.order.empty ()) {
	return;
    }

    playlist.orderIndex = (playlist.orderIndex + 1) % playlist.order.size ();

    if (playlist.orderIndex == 0 && playlist.definition.settings.order == "random") {
	std::shuffle (playlist.order.begin (), playlist.order.end (), this->m_playlistRng);
    }

    std::size_t candidateOrderIndex = playlist.orderIndex;

    if (!this->selectNextCandidate (playlist, candidateOrderIndex)) {
	sLog.error ("All playlist items failed for ", screen, ", keeping current wallpaper");
	const uint32_t delayMinutes = std::max<uint32_t> (1, playlist.definition.settings.delayMinutes);
	playlist.nextSwitch = now + std::chrono::minutes (delayMinutes);
	return;
    }

    const auto candidateIndex = playlist.order[candidateOrderIndex];
    const auto& candidatePath = playlist.definition.items[candidateIndex];

    if (!this->preflightWallpaper (candidatePath.string ())) {
	playlist.failedIndices.insert (candidateIndex);

	if (!this->selectNextCandidate (playlist, candidateOrderIndex)) {
	    sLog.error ("All playlist items failed for ", screen, ", keeping current wallpaper");
	    const uint32_t delayMinutes = std::max<uint32_t> (1, playlist.definition.settings.delayMinutes);
	    playlist.nextSwitch = now + std::chrono::minutes (delayMinutes);
	    return;
	}
    }

    playlist.orderIndex = candidateOrderIndex;
    const auto& nextPath = playlist.definition.items[playlist.order[playlist.orderIndex]];

    bool loaded = false;

    try {
	if (!this->makeAnyViewportCurrent ()) {
	    sLog.error ("Cannot switch playlist on ", screen, ": no active viewport");
	    throw std::runtime_error ("No viewport available");
	}

	auto project = this->loadBackground (nextPath.string ());

	this->setupPropertiesForProject (*project);
	this->ensureBrowserForProject (*project);

	this->m_backgrounds[screen] = std::move (project);

	const auto scalingIt = this->m_context.settings.general.screenScalings.find (screen);
	const auto clampIt = this->m_context.settings.general.screenClamps.find (screen);
	const auto scaling = scalingIt != this->m_context.settings.general.screenScalings.end ()
	    ? scalingIt->second
	    : this->m_context.settings.render.window.scalingMode;
	const auto clamp = clampIt != this->m_context.settings.general.screenClamps.end ()
	    ? clampIt->second
	    : this->m_context.settings.render.window.clamp;

	if (this->m_renderContext) {
	    this->m_renderContext->setWallpaper (
		screen,
		WallpaperEngine::Render::CWallpaper::fromWallpaper (
		    *this->m_backgrounds[screen]->wallpaper, *this->m_renderContext, *this->m_audioContext,
		    this->m_browserContext.get (), scaling, clamp
		)
	    );
	}

	this->m_context.settings.general.screenBackgrounds[screen] = nextPath;
	loaded = true;
    } catch (const std::exception& e) {
	sLog.error ("Failed to advance playlist on ", screen, ": ", e.what ());
    }

    if (!loaded) {
	playlist.failedIndices.insert (playlist.order[playlist.orderIndex]);

	// Keep current position; next timer tick will retry advancement
	sLog.error ("Failed to load wallpaper for ", screen, ", will retry on next cycle");
    }

    const uint32_t delayMinutes = std::max<uint32_t> (1, playlist.definition.settings.delayMinutes);
    playlist.nextSwitch = now + std::chrono::minutes (delayMinutes);
}

void WallpaperApplication::updatePlaylists () {
    if (this->m_activePlaylists.empty ()) {
	return;
    }

    const auto now = std::chrono::steady_clock::now ();

    for (auto& [screen, playlist] : this->m_activePlaylists) {
	playlist.lastUpdate = now;

	if (playlist.definition.settings.mode != "timer") {
	    continue;
	}

	if (playlist.definition.items.size () <= 1) {
	    continue;
	}

	if (now < playlist.nextSwitch) {
	    continue;
	}

	this->advancePlaylist (screen, playlist, now);
    }
}

void WallpaperApplication::setupPropertiesForProject (const Project& project) {
    // show properties if required
    for (const auto& [key, cur] : project.properties) {
	// update the value of the property
	auto override = this->m_context.settings.general.properties.find (key);

	if (override != this->m_context.settings.general.properties.end ()) {
	    sLog.out ("Applying override value for ", key);

	    cur->update (override->second, DynamicValue::UpdateSource::User);
	}

	if (this->m_context.settings.general.onlyListProperties) {
	    sLog.out (cur->dump ());
	}
    }
}

void WallpaperApplication::setupProperties () {
    for (const auto& [background, info] : this->m_backgrounds) {
	this->setupPropertiesForProject (*info);
    }
}

void WallpaperApplication::ipcReposition (glm::ivec4 geometry) {
    if (this->m_context.settings.render.mode
        != ApplicationContext::EXPLICIT_WINDOW) {
	sLog.error ("ipcReposition ignored: not in EXPLICIT_WINDOW mode");
	return;
    }
    this->m_context.settings.render.window.geometry = geometry;
    this->m_videoDriver->resizeWindow (geometry);
}

void WallpaperApplication::ipcSetProperty (
    const std::string& key, const std::string& value
) {
    bool found = false;
    for (const auto& [name, info] : this->m_backgrounds) {
	auto it = info->properties.find (key);
	if (it != info->properties.end ()) {
	    // Upstream's rewrite added an UpdateSource parameter so the scripting
	    // engine can tell script-driven changes from user-driven ones. An IPC
	    // set_property is the user reaching in at runtime (via wpe's Tweak
	    // drawer), so User is the correct provenance -- not Script, which
	    // would let a wallpaper's own script clobber a deliberate user choice.
	    it->second->update (value, WallpaperEngine::Data::Model::DynamicValue::UpdateSource::User);
	    found = true;
	}
    }
    if (!found) {
	sLog.error ("ipcSetProperty: unknown key: ", key);
    }
}

bool WallpaperApplication::ipcSetBackgroundMode (const std::string& value) {
    auto parsed = ApplicationContext::BackgroundMode::parse (value);
    if (!parsed.has_value ()) {
	sLog.error ("ipcSetBackgroundMode: invalid mode: ", value);
	return false;
    }
    // The render loop reads settings.general.backgroundMode each frame so
    // the next render picks up the new value automatically. The backdrop-
    // blur pipeline was allocated at setup time for this exact reason.
    this->m_context.settings.general.backgroundMode = *parsed;
    return true;
}

bool WallpaperApplication::ipcLoadScene (const std::string& path, const std::string& screen, std::string& outError) {
    // Resolve target screen — caller passes empty for "default" (single-window
    // mode like Jumbo) or a screen name (apply mode).
    const std::string target = screen.empty () ? std::string ("default") : screen;

    // Make a viewport current before any GL work — same precondition the
    // playlist path enforces. Without an active context, asset uploads in
    // CWallpaper::fromWallpaper crash hard.
    if (!this->makeAnyViewportCurrent ()) {
	outError = "no active viewport";
	return false;
    }

    ProjectUniquePtr project;
    try {
	project = this->loadBackground (path);
    } catch (const std::exception& e) {
	outError = std::string ("loadBackground: ") + e.what ();
	return false;
    }

    if (!project) {
	outError = "loadBackground returned null";
	return false;
    }

    try {
	this->setupPropertiesForProject (*project);
	this->ensureBrowserForProject (*project);
    } catch (const std::exception& e) {
	outError = std::string ("setupProperties: ") + e.what ();
	return false;
    }

    this->m_backgrounds[target] = std::move (project);

    // Resolve scaling/clamp the same way advancePlaylist does — per-screen
    // override falls back to the global render-window default.
    const auto scalingIt = this->m_context.settings.general.screenScalings.find (target);
    const auto clampIt = this->m_context.settings.general.screenClamps.find (target);
    const auto scaling = scalingIt != this->m_context.settings.general.screenScalings.end ()
	? scalingIt->second
	: this->m_context.settings.render.window.scalingMode;
    const auto clamp = clampIt != this->m_context.settings.general.screenClamps.end ()
	? clampIt->second
	: this->m_context.settings.render.window.clamp;

    if (!this->m_renderContext) {
	outError = "render context not initialized";
	return false;
    }

    try {
	this->m_renderContext->setWallpaper (
	    target,
	    Render::CWallpaper::fromWallpaper (
		*this->m_backgrounds[target]->wallpaper, *this->m_renderContext, *this->m_audioContext,
		this->m_browserContext.get (), scaling, clamp
	    )
	);
    } catch (const std::exception& e) {
	outError = std::string ("setWallpaper: ") + e.what ();
	return false;
    }

    // Track the new path so subsequent --background-mode/--set-property/etc.
    // serializations reflect reality (matches advancePlaylist behavior).
    this->m_context.settings.general.screenBackgrounds[target] = path;
    sLog.out ("ipcLoadScene: swapped ", target, " → ", path);
    return true;
}

bool WallpaperApplication::ipcSamplePixel (int x, int y, std::string& outHex) {
    if (!this->m_videoDriver) return false;
    const glm::ivec2 fbSize = this->m_videoDriver->getFramebufferSize ();
    if (x < 0 || y < 0 || x >= fbSize.x || y >= fbSize.y) return false;

    // glReadPixels uses OpenGL coords (Y=0 at bottom); flip from window coords
    // (Y=0 at top) that the IPC client natively thinks in.
    const int glY = fbSize.y - 1 - y;
    unsigned char rgb[3] = {0, 0, 0};
    glBindFramebuffer (GL_FRAMEBUFFER, 0);
    glReadPixels (x, glY, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    const GLenum err = glGetError ();
    if (err != GL_NO_ERROR) {
	sLog.error ("ipcSamplePixel: glReadPixels error: ", err);
	return false;
    }

    char buf[8];
    std::snprintf (buf, sizeof (buf), "#%02x%02x%02x", rgb[0], rgb[1], rgb[2]);
    outHex = buf;
    return true;
}

bool WallpaperApplication::ipcSampleRegion (int x, int y, int w, int h, std::string& outHex) {
    if (!this->m_videoDriver) return false;
    // Cap block size to bound payload; 64*64*8 chars is about 32KB, fine over
    // a unix socket but we never actually need more than ~16x16 for a loupe.
    if (w < 1 || h < 1 || w > 64 || h > 64) return false;

    const glm::ivec2 fbSize = this->m_videoDriver->getFramebufferSize ();

    // Clamp the requested rect to the framebuffer. We read whatever falls
    // inside, then pad out-of-bounds cells with #000000 so the caller
    // always gets exactly w*h values.
    const int clampedX = std::max (0, x);
    const int clampedY = std::max (0, y);
    const int clampedRight = std::min (fbSize.x, x + w);
    const int clampedBottom = std::min (fbSize.y, y + h);
    const int clampedW = std::max (0, clampedRight - clampedX);
    const int clampedH = std::max (0, clampedBottom - clampedY);

    std::vector<unsigned char> pixels (static_cast<size_t> (clampedW) * clampedH * 3, 0);
    if (clampedW > 0 && clampedH > 0) {
	// glReadPixels origin is bottom-left; convert the rect's top window
	// coord to GL coord for its bottom row.
	const int glY = fbSize.y - clampedBottom;
	glBindFramebuffer (GL_FRAMEBUFFER, 0);
	glPixelStorei (GL_PACK_ALIGNMENT, 1);
	glReadPixels (clampedX, glY, clampedW, clampedH, GL_RGB, GL_UNSIGNED_BYTE, pixels.data ());
	const GLenum err = glGetError ();
	if (err != GL_NO_ERROR) {
	    sLog.error ("ipcSampleRegion: glReadPixels error: ", err);
	    return false;
	}
    }

    // glReadPixels returns rows bottom-to-top; we want top-to-bottom so the
    // hex string matches the window's visible orientation.
    outHex.clear ();
    outHex.reserve (static_cast<size_t> (w) * h * 8);
    for (int row = 0; row < h; ++row) {
	for (int col = 0; col < w; ++col) {
	    const int wx = x + col;
	    const int wy = y + row;
	    char buf[9];
	    if (wx < clampedX || wx >= clampedRight || wy < clampedY || wy >= clampedBottom) {
		std::snprintf (buf, sizeof (buf), "#000000");
	    } else {
		// pixels[] is indexed by (glRow, col) where glRow 0 is the
		// bottom of the clamped block.
		const int localCol = wx - clampedX;
		const int localRow = wy - clampedY;
		const int glRow = clampedH - 1 - localRow;
		const size_t off = static_cast<size_t> (glRow) * clampedW * 3 + static_cast<size_t> (localCol) * 3;
		std::snprintf (buf, sizeof (buf), "#%02x%02x%02x", pixels[off], pixels[off + 1], pixels[off + 2]);
	    }
	    if (!outHex.empty ()) outHex += ' ';
	    outHex += buf;
	}
    }
    return true;
}

glm::vec3 WallpaperApplication::getSceneClearColor () const {
    // Walk the first loaded wallpaper. Prefer the `schemecolor` user
    // property (what WE scenes expose to the user as the tweakable brand
    // color) over the scene's internal `clearcolor` binding — they're
    // usually different properties on the same scene.
    for (const auto& [name, info] : this->m_backgrounds) {
	if (!info) continue;
	auto it = info->properties.find ("schemecolor");
	if (it != info->properties.end () && it->second) {
	    return it->second->getVec3 ();
	}
    }
    // Fallback: the scene-internal clear color (whichever prop drives it).
    for (const auto& [name, info] : this->m_backgrounds) {
	if (info && info->wallpaper && info->wallpaper->is<Data::Model::Scene> ()) {
	    const auto* scene = info->wallpaper->as<Data::Model::Scene> ();
	    if (scene->colors.clear && scene->colors.clear->value) {
		return scene->colors.clear->value->getVec3 ();
	    }
	}
    }
    return glm::vec3 (0.0f, 0.0f, 0.0f);
}

void WallpaperApplication::ipcSetPointer (const double nx, const double ny, const int left, const int right) {
    // Clamp rather than reject: a pointer a few pixels outside the surface during a
    // drag is normal, and dropping those updates makes effects stick at the edge.
    this->m_injectedPointer = {
        nx < 0.0 ? 0.0 : (nx > 1.0 ? 1.0 : nx),
        ny < 0.0 ? 0.0 : (ny > 1.0 ? 1.0 : ny),
    };
    this->m_injectedPointerLeft = left ? 1 : 0;
    this->m_injectedPointerRight = right ? 1 : 0;
    this->m_injectedPointerValid = true;
}

void WallpaperApplication::ipcSetEyedropperActive (bool active) {
    this->m_eyedropperActive = active;
    this->m_lastEyedropperPos = { -1, -1 };
    this->m_lastEyedropperClick = 0;
    this->m_lastEyedropperEmitTime = 0.0f;
}

void WallpaperApplication::pollEyedropper () {
    if (!this->m_eyedropperActive || !this->m_ipcServer || !this->m_videoDriver) return;

    // Pull current mouse position + click state from the input context.
    // Position is in framebuffer-space with Y=0 at the bottom (see
    // GLFWMouseInput::update).
    const auto& mouse = this->m_videoDriver->getInputContext ().getMouseInput ();
    const glm::dvec2 rawPos = mouse.position ();
    const glm::ivec2 fbSize = this->m_videoDriver->getFramebufferSize ();
    const int fx = static_cast<int> (rawPos.x);
    const int fy = fbSize.y - 1 - static_cast<int> (rawPos.y); // convert back to window coords
    const glm::ivec2 winPos { fx, fy };

    const bool inBounds =
	fx >= 0 && fy >= 0 && fx < fbSize.x && fy < fbSize.y;

    // Cursor event — fires on position change OR a low-rate tick so the
    // loupe keeps refreshing over an animated scene when the cursor is
    // stationary. 100ms feels responsive enough to read as "live" and is
    // well below our per-key IPC debounce.
    const float now = this->m_videoDriver->getRenderTime ();
    const bool moved = winPos != this->m_lastEyedropperPos;
    const bool idleTick = (now - this->m_lastEyedropperEmitTime) >= 0.1f;
    if (inBounds && (moved || idleTick)) {
	std::string hex;
	if (this->ipcSamplePixel (fx, fy, hex)) {
	    std::ostringstream data;
	    data << fx << ' ' << fy << ' ' << hex;
	    this->m_ipcServer->emitEvent ("cursor", data.str ());
	    this->m_lastEyedropperEmitTime = now;
	}
	this->m_lastEyedropperPos = winPos;
    }

    // Click event — rising edge on left-click.
    const int click = mouse.leftClick () == WallpaperEngine::Input::MouseClickStatus::Clicked ? 1 : 0;
    if (click && !this->m_lastEyedropperClick && inBounds) {
	std::string hex;
	if (this->ipcSamplePixel (fx, fy, hex)) {
	    std::ostringstream data;
	    data << fx << ' ' << fy << ' ' << hex;
	    this->m_ipcServer->emitEvent ("click", data.str ());
	}
    }
    this->m_lastEyedropperClick = click;
}

void WallpaperApplication::pollClickForFocus () {
    // Skip while eyedropper is active — its own !click event carries the
    // signal and we don't want Kuro refocusing mid-pick. Also skip if
    // the mouse input or IPC server isn't wired up.
    if (this->m_eyedropperActive || !this->m_ipcServer || !this->m_videoDriver) return;

    const auto& mouse = this->m_videoDriver->getInputContext ().getMouseInput ();
    const int click = mouse.leftClick () == WallpaperEngine::Input::MouseClickStatus::Clicked ? 1 : 0;
    if (click && !this->m_lastFocusClick) {
	this->m_ipcServer->emitEvent ("focus_click", "");
    }
    this->m_lastFocusClick = click;
}

void WallpaperApplication::pollKeyboardForwarding () {
    if (!this->m_ipcServer || !this->m_videoDriver) return;

    // GLFW key → DOM KeyboardEvent.key name. We forward a curated set of
    // hotkeys rather than every key so the IPC traffic stays bounded and
    // we don't intercept text input on the host side. Letters map to the
    // lowercase form; the host's handler normalizes case.
    static const std::pair<int, const char*> kForwarded[] = {
	{ 256, "Escape" },     // GLFW_KEY_ESCAPE
	{ 257, "Enter" },      // GLFW_KEY_ENTER
	{ 262, "ArrowRight" }, // GLFW_KEY_RIGHT
	{ 263, "ArrowLeft" },  // GLFW_KEY_LEFT
	{ 264, "ArrowDown" },  // GLFW_KEY_DOWN
	{ 265, "ArrowUp" },    // GLFW_KEY_UP
	{ 65,  "a" },          // GLFW_KEY_A
	{ 66,  "b" },
	{ 68,  "d" },
	{ 70,  "f" },
	{ 80,  "p" },          // GLFW_KEY_P
	{ 83,  "s" },          // GLFW_KEY_S
	{ 84,  "t" },          // GLFW_KEY_T
    };

    for (const auto& [keycode, name] : kForwarded) {
	const int now = this->m_videoDriver->isKeyPressed (keycode) ? 1 : 0;
	const int last = this->m_lastKeyState[keycode];
	if (now && !last) {
	    this->m_ipcServer->emitEvent ("key", name);
	}
	this->m_lastKeyState[keycode] = now;
    }
}

void WallpaperApplication::setupBrowser () {
    bool anyWebProject = std::any_of (
	this->m_backgrounds.begin (), this->m_backgrounds.end (),
	[] (const std::pair<const std::string, ProjectUniquePtr>& pair) -> bool {
	    return pair.second->wallpaper->is<Web> ();
	}
    );

    // do not perform any initialization if no web background is present
    if (!anyWebProject || this->m_browserContext) {
	return;
    }

    this->m_browserContext = std::make_unique<WebBrowser::WebBrowserContext> (*this);
}

void WallpaperApplication::takeScreenshot (const std::filesystem::path& filename) const {
    const int width = this->m_renderContext->getOutput ().getFullWidth ();
    const int height = this->m_renderContext->getOutput ().getFullHeight ();
    const bool vflip = this->m_renderContext->getOutput ().renderVFlip ();
    const auto& wallpapers = this->m_renderContext->getWallpapers ();

    struct ViewportCapture {
	uint8_t* buffer;
	int readWidth;
	int readHeight;
	int vpWidth;
	int vpHeight;
	int xoffset;
	float ustart, uend, vstart, vend;
    };

    std::vector<ViewportCapture> captures;
    int currentXOffset = 0;

    for (const auto& [screen, viewport] : this->m_renderContext->getOutput ().getViewports ()) {
	// activate opengl context so we can read from the framebuffer
	viewport->makeCurrent ();

	// find the wallpaper for this screen to read from its FBO
	const auto wallpaperIt = wallpapers.find (screen);
	if (wallpaperIt == wallpapers.end ()) {
	    sLog.error ("Cannot find wallpaper for screen ", screen);
	    continue;
	}

	const auto& wallpaper = wallpaperIt->second;
	const int vpWidth = viewport->viewport.z - viewport->viewport.x;
	const int vpHeight = viewport->viewport.w - viewport->viewport.y;

	// bind the wallpaper's FBO to read from it directly
	// this is more reliable than the default framebuffer on some drivers (NVIDIA/Wayland)
	glBindFramebuffer (GL_FRAMEBUFFER, wallpaper->getWallpaperFramebuffer ());

	// ensure rendering is complete before reading
	glFinish ();

	// make room for storing the pixel of this viewport
	const int readWidth = wallpaper->getWidth ();
	const int readHeight = wallpaper->getHeight ();
	const auto bufferSize = readWidth * readHeight * 3;
	auto* buffer = new uint8_t[bufferSize];

	// read the FBO data into the pixel buffer
	glPixelStorei (GL_PACK_ALIGNMENT, 1);
	if (GLEW_VERSION_4_5) {
	    glReadnPixels (0, 0, readWidth, readHeight, GL_RGB, GL_UNSIGNED_BYTE, bufferSize, buffer);
	} else {
	    glReadPixels (0, 0, readWidth, readHeight, GL_RGB, GL_UNSIGNED_BYTE, buffer);
	}

	// restore default framebuffer
	glBindFramebuffer (GL_FRAMEBUFFER, 0);

	if (const GLenum error = glGetError (); error != GL_NO_ERROR) {
	    sLog.error ("Cannot obtain pixel data for screen ", screen, ". OpenGL error: ", error);
	    delete[] buffer;
	    continue;
	}

	// Get the UV coordinates which define the visible portion based on scaling mode
	const auto [ustart, uend, vstart, vend] = wallpaper->getState ().getTextureUVs ();

	captures.push_back (
	    { buffer, readWidth, readHeight, vpWidth, vpHeight, currentXOffset, ustart, uend, vstart, vend }
	);

	if (viewport->single) {
	    currentXOffset += vpWidth;
	}
    }

    const auto extension = filename.extension ();
    const std::string extStr = extension.string ();

    // Offload pixel processing and saving to a background thread to avoid hitches
    std::thread ([captures, width, height, vflip, extStr, filename] () {
	auto* bitmap = new uint8_t[width * height * 3] { 0 };

	for (const auto& capture : captures) {
	    // copy pixels to bitmap, sampling from the UV-defined region
	    for (int y = 0; y < capture.vpHeight; y++) {
		for (int x = 0; x < capture.vpWidth; x++) {
		    // interpolate within the UV range to get source coordinates
		    const float u
			= capture.ustart + (static_cast<float> (x) / capture.vpWidth) * (capture.uend - capture.ustart);
		    const float v = capture.vstart
			+ (static_cast<float> (y) / capture.vpHeight) * (capture.vend - capture.vstart);

		    // convert UV to pixel coordinates in the source buffer
		    const int srcX = std::clamp (static_cast<int> (u * capture.readWidth), 0, capture.readWidth - 1);
		    const int srcY = std::clamp (static_cast<int> (v * capture.readHeight), 0, capture.readHeight - 1);
		    const int srcIdx = (srcY * capture.readWidth + srcX) * 3;

		    const int xfinal = x + capture.xoffset;
		    // FBO content is not flipped like default framebuffer, so invert vflip logic
		    const int yfinal = vflip ? y : (capture.vpHeight - y - 1);

		    if (yfinal >= 0 && yfinal < height && xfinal >= 0 && xfinal < width) {
			bitmap[yfinal * width * 3 + xfinal * 3] = capture.buffer[srcIdx];
			bitmap[yfinal * width * 3 + xfinal * 3 + 1] = capture.buffer[srcIdx + 1];
			bitmap[yfinal * width * 3 + xfinal * 3 + 2] = capture.buffer[srcIdx + 2];
		    }
		}
	    }
	    delete[] capture.buffer;
	}

	if (extStr == ".bmp") {
	    stbi_write_bmp (filename.c_str (), width, height, 3, bitmap);
	} else if (extStr == ".png") {
	    stbi_write_png (filename.c_str (), width, height, 3, bitmap, width * 3);
	} else if (extStr == ".jpg" || extStr == ".jpeg") {
	    stbi_write_jpg (filename.c_str (), width, height, 3, bitmap, 100);
	}

	delete[] bitmap;
    }).detach ();
}

void WallpaperApplication::setupOutput () {
    const char* XDG_SESSION_TYPE = getenv ("XDG_SESSION_TYPE");

    if (!XDG_SESSION_TYPE) {
	sLog.exception (
	    "Cannot read environment variable XDG_SESSION_TYPE, window server detection failed. Please ensure proper "
	    "values are set"
	);
    }

    sLog.debug ("Checking for window servers: ");

    for (const auto& windowServer : sVideoFactories.getRegisteredDrivers ()) {
	sLog.debug ("\t", windowServer);
    }

    this->m_videoDriver = sVideoFactories.createVideoDriver (
	this->m_context.settings.render.mode, XDG_SESSION_TYPE, this->m_context, *this
    );
    this->m_fullScreenDetector
	= sVideoFactories.createFullscreenDetector (XDG_SESSION_TYPE, this->m_context, *this->m_videoDriver);
}

void WallpaperApplication::setupAudio () {
    // ensure audioprocessing is required by any background, and we have it enabled
    const bool audioProcessingRequired = std::ranges::any_of (
	this->m_backgrounds, [] (const std::pair<const std::string, ProjectUniquePtr>& pair) -> bool {
	    return pair.second->supportsAudioProcessing;
	}
    );

    if (audioProcessingRequired && this->m_context.settings.audio.audioprocessing) {
	this->m_audioRecorder
	    = std::make_unique<WallpaperEngine::Audio::Drivers::Recorders::PulseAudioPlaybackRecorder> ();
    } else {
	this->m_audioRecorder = std::make_unique<WallpaperEngine::Audio::Drivers::Recorders::PlaybackRecorder> ();
    }

    if (this->m_context.settings.audio.automute) {
	m_audioDetector = std::make_unique<WallpaperEngine::Audio::Drivers::Detectors::PulseAudioPlayingDetector> (
	    this->m_context, *this->m_fullScreenDetector
	);
    } else {
	m_audioDetector = std::make_unique<WallpaperEngine::Audio::Drivers::Detectors::AudioPlayingDetector> (
	    this->m_context, *this->m_fullScreenDetector
	);
    }

    // initialize sdl audio driver
    m_audioDriver = std::make_unique<WallpaperEngine::Audio::Drivers::SDLAudioDriver> (
	this->m_context, *this->m_audioDetector, *this->m_audioRecorder
    );
    // initialize audio context
    m_audioContext = std::make_unique<WallpaperEngine::Audio::AudioContext> (*m_audioDriver);
}

void WallpaperApplication::prepareOutputs () {
    // initialize render context
    m_renderContext
	= std::make_unique<WallpaperEngine::Render::RenderContext> (*m_videoDriver, *this, *this->m_mediaSource);
    // create a new background for each screen

    // set all the specific wallpapers required (skip span group synthetic keys)
    for (const auto& [background, info] : this->m_backgrounds) {
	if (background.rfind ("span:", 0) == 0) {
	    continue;
	}
	const auto scalingIt = this->m_context.settings.general.screenScalings.find (background);
	const auto clampIt = this->m_context.settings.general.screenClamps.find (background);
	const auto scaling = scalingIt != this->m_context.settings.general.screenScalings.end ()
	    ? scalingIt->second
	    : this->m_context.settings.render.window.scalingMode;
	const auto clamp = clampIt != this->m_context.settings.general.screenClamps.end ()
	    ? clampIt->second
	    : this->m_context.settings.render.window.clamp;

	m_renderContext->setWallpaper (
	    background,
	    WallpaperEngine::Render::CWallpaper::fromWallpaper (
		*info->wallpaper, *m_renderContext, *m_audioContext, m_browserContext.get (), scaling, clamp
	    )
	);
    }

    // Set up span groups: one shared wallpaper per group, registered for each viewport
    for (const auto& spanGroup : this->m_context.settings.general.spanGroups) {
	if (spanGroup.screens.empty ()) {
	    continue;
	}

	const std::string groupKey = "span:" + spanGroup.screens.front ();
	const auto bgIt = this->m_backgrounds.find (groupKey);
	if (bgIt == this->m_backgrounds.end ()) {
	    continue;
	}

	// Compute the bounding box of all viewports in this span group
	const auto& viewports = m_renderContext->getOutput ().getViewports ();
	int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;
	bool anyFound = false;

	for (const auto& screenName : spanGroup.screens) {
	    const auto vpIt = viewports.find (screenName);
	    if (vpIt == viewports.end ()) {
		sLog.error ("Span group screen not found: ", screenName);
		continue;
	    }
	    anyFound = true;
	    const auto& vp = vpIt->second;
	    const int x = vp->globalPosition.x;
	    const int y = vp->globalPosition.y;
	    const int w = vp->logicalSize.x;
	    const int h = vp->logicalSize.y;
	    sLog.debug (
		"SPAN DEBUG prepareOutputs: screen '", screenName, "' globalPos=(", x, ",", y, ") logicalSize=", w, "x",
		h
	    );
	    minX = std::min (minX, x);
	    minY = std::min (minY, y);
	    maxX = std::max (maxX, x + w);
	    maxY = std::max (maxY, y + h);
	}

	if (!anyFound) {
	    sLog.error ("No viewports found for span group, skipping");
	    continue;
	}

	sLog.debug (
	    "SPAN DEBUG prepareOutputs: bounding box=(", minX, ",", minY, ",", maxX - minX, ",", maxY - minY, ")"
	);

	WallpaperEngine::Render::CWallpaper::SpanInfo spanInfo;
	spanInfo.totalBounds = { minX, minY, maxX - minX, maxY - minY };

	// Create one shared wallpaper with the span group's scaling mode
	auto sharedWallpaper = WallpaperEngine::Render::CWallpaper::fromWallpaper (
	    *bgIt->second->wallpaper, *m_renderContext, *m_audioContext, m_browserContext.get (), spanGroup.scaling,
	    spanGroup.clamp
	);

	// Convert to shared_ptr so it can be registered for multiple viewports
	std::shared_ptr<WallpaperEngine::Render::CWallpaper> shared (std::move (sharedWallpaper));
	shared->setSpanInfo (spanInfo);

	// Register the same wallpaper for each screen in the span group
	for (const auto& screenName : spanGroup.screens) {
	    m_renderContext->setWallpaper (screenName, shared);
	}
    }
}

void WallpaperApplication::setupOpenGLDebugging () {
#if !NDEBUG
    glDebugMessageCallback (CustomGLDebugCallback, nullptr);
    glEnable (GL_DEBUG_OUTPUT_SYNCHRONOUS);
#endif
}

void WallpaperApplication::setup () {
    this->setupOutput ();
    this->setupAudio ();
    this->prepareOutputs ();
    this->setupOpenGLDebugging ();

    if (!this->m_context.settings.general.ipcSocketPath.empty ()) {
	this->m_ipcServer = std::make_unique<IPCServer> (
	    this->m_context.settings.general.ipcSocketPath, *this
	);
    }

    if (this->m_context.settings.general.dumpStructure) {
	auto prettyPrinter = Data::Dumpers::StringPrinter ();

	for (const auto& [background, info] : this->m_renderContext->getWallpapers ()) {
	    prettyPrinter.printWallpaper (info->getWallpaperData ());
	}

	std::cout << prettyPrinter.str () << std::endl;
    }

#if DEMOMODE
    // ensure only one background is running so everything can be properly caught
    if (this->m_renderContext->getWallpapers ().size () > 1) {
	sLog.exception ("Demo mode only supports one background");
    }

    int width = this->m_renderContext->getWallpapers ().begin ()->second->getWidth ();
    int height = this->m_renderContext->getWallpapers ().begin ()->second->getHeight ();
    std::vector<uint8_t> pixels (width * height * 3);
    bool initialized = false;
    int frame = 0;
#endif /* DEMOMODE */
}

void WallpaperApplication::render () {
    static time_t seconds;
    static struct tm* timeinfo;

    if (this->m_isPaused) {
	usleep (FULLSCREEN_CHECK_WAIT_TIME);
	if (this->m_fullScreenDetector->anythingFullscreen () && this->m_context.state.general.keepRunning) {
	    return;
	}
	m_renderContext->setPause (false);

	// account for paused duration in playlist timers
	const auto pausedNow = std::chrono::steady_clock::now ();
	const auto pausedDuration = pausedNow - this->m_pauseStart;

	for (auto& [_, playlist] : this->m_activePlaylists) {
	    if (!playlist.definition.settings.updateOnPause) {
		playlist.nextSwitch += pausedDuration;
		playlist.lastUpdate += pausedDuration;
	    }
	}

	this->m_isPaused = false;
    } else {
	// update g_Daytime
	time (&seconds);
	timeinfo = localtime (&seconds);
	g_Daytime = static_cast<float> ((timeinfo->tm_hour * 60) + timeinfo->tm_min) / (24.0f * 60.0f);

	// keep track of the previous frame's time
	g_TimeLast = g_Time;
	// calculate the current time value
	g_Time = m_videoDriver->getRenderTime ();
	// update audio recorder
	m_audioDriver->update ();
	// update the media source
	m_mediaSource->update ();
	// update input information
	m_videoDriver->getInputContext ().update ();
	// process driver events
	m_videoDriver->dispatchEventQueue ();

	if (m_videoDriver->closeRequested ()) {
	    sLog.out ("Stop requested by driver");
	    this->m_context.state.general.keepRunning = false;
	}

#if DEMOMODE
	// wait for a full render cycle before actually starting
	// this gives some extra time for video and web decoders to set themselves up
	// because of size changes
	if (m_videoDriver->getFrameCounter () > (uint32_t)this->m_context.settings.render.maximumFPS) {
	    if (!initialized) {
		width = this->m_renderContext->getWallpapers ().begin ()->second->getWidth ();
		height = this->m_renderContext->getWallpapers ().begin ()->second->getHeight ();
		pixels.reserve (width * height * 3);
		init_encoder ("output.webm", width, height);
		initialized = true;
	    }

	    glBindFramebuffer (
		GL_FRAMEBUFFER, this->m_renderContext->getWallpapers ().begin ()->second->getWallpaperFramebuffer ()
	    );

	    glPixelStorei (GL_PACK_ALIGNMENT, 1);
	    glReadPixels (0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data ());
	    write_video_frame (pixels.data ());
	    frame++;

	    // stop after the given framecount
	    if (frame >= FRAME_COUNT) {
		this->m_context.state.general.keepRunning = false;
	    }
	}
#endif /* DEMOMODE */
	// check for fullscreen windows and wait until there's none fullscreen
	if (this->m_fullScreenDetector->anythingFullscreen () && this->m_context.state.general.keepRunning) {
	    this->m_isPaused = true;
	    this->m_pauseStart = std::chrono::steady_clock::now ();

	    m_renderContext->setPause (true);
	    return;
	}
    }

    this->updatePlaylists ();

    if (!this->m_context.settings.screenshot.take || this->m_screenShotTaken == true) {
	return;
    }

    if (this->m_videoDriver->getFrameCounter () < this->m_nextFrameScreenshot) {
	return;
    }

    this->takeScreenshot (this->m_context.settings.screenshot.path);
    this->m_screenShotTaken = true;
}

void WallpaperApplication::cleanup () {
    sLog.out ("Stopping");

#if DEMOMODE
    close_encoder ();
#endif /* DEMOMODE */

    SDL_Quit ();
}

void WallpaperApplication::show () {
	setup();

    // Emit the shm path over IPC once so the host knows where to mmap.
    // We wait until setup() completes because the output (which creates
    // the shm buffer) is initialized inside setup().
    if (this->m_ipcServer && this->m_context.settings.general.shmOutput) {
	auto* glOut = dynamic_cast<Render::Drivers::Output::GLFWWindowOutput*> (this->m_videoDriver->getOutputPtr ());
	if (glOut && glOut->shmActive ()) {
	    this->m_ipcServer->emitEvent (
		"shm",
		glOut->shmPath () + " " +
		std::to_string (glOut->getFullWidth ()) + " " +
		std::to_string (glOut->getFullHeight ())
	    );
	}
    }

    while (this->m_context.state.general.keepRunning) {
		if (this->m_ipcServer) this->m_ipcServer->poll ();
		this->pollEyedropper ();
		this->pollClickForFocus ();
		this->pollKeyboardForwarding ();
		render();
    }
    cleanup ();
}

void WallpaperApplication::update (Render::Drivers::Output::OutputViewport* viewport) {
    // render the scene
    m_renderContext->render (viewport);
}

void WallpaperApplication::signal (int signal) {
    sLog.out ("Stop requested by signal ", signal);
    this->m_context.state.general.keepRunning = false;
}

const std::map<std::string, ProjectUniquePtr>& WallpaperApplication::getBackgrounds () const {
    return this->m_backgrounds;
}

ApplicationContext& WallpaperApplication::getContext () const { return this->m_context; }

const WallpaperEngine::Render::Drivers::Output::Output& WallpaperApplication::getOutput () const {
    return this->m_renderContext->getOutput ();
}

void WallpaperApplication::setDestinationFramebuffer (GLuint framebuffer) {
    this->m_destinationFramebuffer = framebuffer;
    // Update all wallpapers with the new destination framebuffer
    for (const auto& [screen, wallpaper] : this->m_renderContext->getWallpapers ()) {
	wallpaper->setDestinationFramebuffer (framebuffer);
    };
}

GLuint WallpaperApplication::getDestinationFramebuffer () const { return this->m_destinationFramebuffer; }