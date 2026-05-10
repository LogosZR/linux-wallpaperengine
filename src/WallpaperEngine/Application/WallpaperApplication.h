#pragma once

#include <chrono>
#include <random>

#include "WallpaperEngine/Application/ApplicationContext.h"
#include "WallpaperEngine/Assets/AssetLocator.h"

#include "WallpaperEngine/Render/CWallpaper.h"
#include "WallpaperEngine/Render/Drivers/Detectors/FullScreenDetector.h"
#include "WallpaperEngine/Render/Drivers/GLFWOpenGLDriver.h"
#include "WallpaperEngine/Render/Drivers/Output/GLFWWindowOutput.h"
#include "WallpaperEngine/Render/RenderContext.h"

#include "WallpaperEngine/Audio/Drivers/SDLAudioDriver.h"

#include "WallpaperEngine/Input/InputContext.h"
#include "WallpaperEngine/WebBrowser/WebBrowserContext.h"

#include "WallpaperEngine/Data/Model/Types.h"

#include <set>

namespace WallpaperEngine::Application {

class IPCServer;

using namespace WallpaperEngine::Assets;
using namespace WallpaperEngine::Data::Model;
/**
 * Small wrapper class over the actual wallpaper's main application skeleton
 */
class WallpaperApplication {
public:
    explicit WallpaperApplication (ApplicationContext& context);
    ~WallpaperApplication ();

    /**
     * Prepares the application for rendering.
     */
    void setup();
    /**
     * Renders a frame of the application.
     */
    void render();
    /**
     * Cleans up all the resources used by the application.
     */
    static void cleanup();
    /**
     * Shows the application until it's closed
     */
    void show ();
    /**
     * Handles a OS signal sent to this PID
     *
     * @param signal
     */
    void signal (int signal);
    /**
     * @return Maps screens to loaded backgrounds
     */
    [[nodiscard]] const std::map<std::string, ProjectUniquePtr>& getBackgrounds () const;
    /**
     * @return The current application context
     */
    [[nodiscard]] ApplicationContext& getContext () const;
    /**
     * Renders a frame
     */
    void update (Render::Drivers::Output::OutputViewport* viewport);
    /**
     * Gets the output
     */
    [[nodiscard]] const WallpaperEngine::Render::Drivers::Output::Output& getOutput () const;
    /**
     * Sets the destination framebuffer for rendering. If not called, the default framebuffer will be used.
     */
    void setDestinationFramebuffer (GLuint framebuffer);

    /**
     * Gets the currently set destination framebuffer for rendering. If not set, returns 0 (the default framebuffer).
     */
    [[nodiscard]] GLuint getDestinationFramebuffer () const;

    /**
     * Handle IPC reposition command. Routes to the video driver's resizeWindow.
     * No-op if not in EXPLICIT_WINDOW mode.
     */
    void ipcReposition (glm::ivec4 geometry);

    /**
     * Handle IPC set_property command. Looks up the property by key on each
     * running project and calls update(value). Logs and ignores unknown keys.
     */
    void ipcSetProperty (const std::string& key, const std::string& value);

    /**
     * Handle IPC set_background_mode command. Parses the same syntax as
     * --background-mode and swaps settings.general.backgroundMode. Kicks
     * off or tears down the backdrop-blur pipeline if transitioning to or
     * from Blur mode. Returns true on success, false on invalid input.
     */
    bool ipcSetBackgroundMode (const std::string& value);

    /**
     * Handle IPC sample_pixel request. Reads one pixel from the default
     * framebuffer at (x, y) in window coords (top-left origin) and writes
     * "#rrggbb" into `outHex`. Returns false on failure (out of bounds,
     * GL error, no active video driver).
     */
    bool ipcSamplePixel (int x, int y, std::string& outHex);

    /**
     * Handle IPC sample_region request. Reads a (w x h) block starting
     * at (x, y) in window coords (top-left origin) and writes space-
     * separated "#rrggbb" values in row-major order (top row first,
     * left to right) into `outHex`. Pixels outside the framebuffer are
     * reported as #000000 so clients always get w*h values. Caps w and
     * h at 64. Returns false on GL error or missing video driver.
     */
    bool ipcSampleRegion (int x, int y, int w, int h, std::string& outHex);

    /**
     * Handle IPC start/stop_eyedropper commands. When active, the main
     * loop emits !cursor / !click events each frame whenever the mouse
     * moves inside the window or left-click is pressed.
     */
    void ipcSetEyedropperActive (bool active);

    /**
     * Read the active scene's live clear color (a.k.a. scheme color). Used
     * by --background-mode=color=scheme to paint the pillarbox with
     * whatever the scene is currently rendering as its clear. Returns
     * black when no scene is loaded or the wallpaper isn't a Scene.
     */
    glm::vec3 getSceneClearColor () const;

    /**
     * Invoked each frame from the main loop while eyedropper mode is
     * active. Queries the current mouse position + left-click state and
     * pushes !cursor x y #rrggbb / !click x y #rrggbb events over the
     * IPC socket when they change or fire.
     */
    void pollEyedropper ();

private:
    /**
     * Sets up an asset locator for the given background
     *
     * @param bg
     */
    AssetLocatorUniquePtr setupAssetLocator (const std::string& bg) const;
    /**
     * Loads projects based off the settings
     */
    void loadBackgrounds ();
    /**
     * Loads the given project
     *
     * @param bg
     * @return
     */
    [[nodiscard]] ProjectUniquePtr loadBackground (const std::string& bg);
    /**
     * Prepares all background's values and updates their properties if required
     */
    void setupProperties ();
    /**
     * Updates the properties for the given background based on the current context
     *
     * @param project
     */
    void setupPropertiesForProject (const Project& project);
    /**
     * Prepares CEF browser to be used
     */
    void setupBrowser ();
    /**
     * Prepares desktop environment-related things (like render, window, fullscreen detector, etc)
     */
    void setupOutput ();
    /**
     * Prepares all audio-related things (like detector, output, etc)
     */
    void setupAudio ();
    /**
     * Prepares the render-context of all the backgrounds so they can be displayed on the screen
     */
    void prepareOutputs ();
    /**
     * Prepares output debugging for all opengl errors
     */
    void setupOpenGLDebugging ();
    /**
     * Takes an screenshot of the background and saves it to the specified path
     *
     * @param filename
     */
    void takeScreenshot (const std::filesystem::path& filename) const;

    struct ActivePlaylist {
	ApplicationContext::PlaylistDefinition definition;
	std::vector<std::size_t> order;
	std::size_t orderIndex = 0;
	std::chrono::steady_clock::time_point nextSwitch;
	std::chrono::steady_clock::time_point lastUpdate;
	std::set<std::size_t> failedIndices;
    };

    void initializePlaylists ();
    void updatePlaylists ();
    void advancePlaylist (
	const std::string& screen, ActivePlaylist& playlist, const std::chrono::steady_clock::time_point& now
    );
    bool selectNextCandidate (ActivePlaylist& playlist, std::size_t& outOrderIndex);
    bool preflightWallpaper (const std::string& path);
    std::vector<std::size_t> buildPlaylistOrder (const ApplicationContext::PlaylistDefinition& definition);
    void ensureBrowserForProject (const Project& project);
    bool makeAnyViewportCurrent () const;

    /** The application context that contains the current app settings */
    ApplicationContext& m_context;
    /** Maps screens to backgrounds */
    std::map<std::string, ProjectUniquePtr> m_backgrounds {};
    std::map<std::string, ActivePlaylist> m_activePlaylists {};

    std::unique_ptr<WallpaperEngine::Audio::Drivers::Detectors::AudioPlayingDetector> m_audioDetector = nullptr;
    std::unique_ptr<WallpaperEngine::Audio::AudioContext> m_audioContext = nullptr;
    std::unique_ptr<WallpaperEngine::Audio::Drivers::SDLAudioDriver> m_audioDriver = nullptr;
    std::unique_ptr<WallpaperEngine::Audio::Drivers::Recorders::PlaybackRecorder> m_audioRecorder = nullptr;
    std::unique_ptr<WallpaperEngine::Render::RenderContext> m_renderContext = nullptr;
    std::unique_ptr<WallpaperEngine::Render::Drivers::VideoDriver> m_videoDriver = nullptr;
    std::unique_ptr<WallpaperEngine::Render::Drivers::Detectors::FullScreenDetector> m_fullScreenDetector = nullptr;
    std::unique_ptr<WallpaperEngine::WebBrowser::WebBrowserContext> m_browserContext = nullptr;
    std::unique_ptr<IPCServer> m_ipcServer;
    // Eyedropper mode — while active, pollEyedropper() runs each frame
    // and streams !cursor / !click events. State cached here so we don't
    // spam identical cursor events on still frames.
    bool m_eyedropperActive = false;
    glm::ivec2 m_lastEyedropperPos = { -1, -1 };
    int m_lastEyedropperClick = 0;
    std::mt19937 m_playlistRng { std::random_device {}() };
    bool m_isPaused = false;
    bool m_screenShotTaken = false;
    uint32_t m_nextFrameScreenshot = 0;
    std::chrono::steady_clock::time_point m_pauseStart {};
    GLuint m_destinationFramebuffer = 0;
};
} // namespace WallpaperEngine::Application
