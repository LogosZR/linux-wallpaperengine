#pragma once

#include <cstdint>
#include <string>
#include <glm/glm.hpp>

namespace WallpaperEngine::Application {

class WallpaperApplication;

/**
 * Unix-socket IPC server for runtime control of a running lwe instance.
 * Non-blocking; polled from the main render loop.
 *
 * Protocol is newline-delimited text. Three line shapes, distinguished by
 * the first character:
 *   <cmd> [args]          — fire-and-forget request, no response
 *   #<id> <cmd> [args]    — request with id; server responds with #<id> ok|err ...
 *   !<type> [data]        — server-initiated event (pushed without a request)
 *
 * Fire-and-forget commands: reposition, set_property, set_background_mode, set_pointer,
 * start_eyedropper, stop_eyedropper.
 *
 * Request/response commands: sample_pixel, sample_region, load_scene.
 *
 * Events: cursor, click (emitted while eyedropper mode is active).
 */
class IPCServer {
public:
	IPCServer (const std::string& path, WallpaperApplication& app);
	~IPCServer ();

	/// Poll the socket once. Call every frame from the main loop. Non-blocking.
	void poll ();

	/**
	 * Push a server-initiated event to the connected client. No-op when no
	 * client is attached. Line is written as "!<type> <data>\n". Safe to
	 * call each frame; silently drops on write error (client will
	 * reconnect and resubscribe).
	 */
	void emitEvent (const std::string& type, const std::string& data);

private:
	std::string m_path;
	WallpaperApplication& m_app;
	int m_listenFd = -1;
	int m_clientFd = -1;
	std::string m_clientBuffer;

	void acceptClient ();
	void readClient ();
	void handleLine (const std::string& line);
	void closeClient ();

	/// Parse and dispatch a request carrying an id ("#<id> <cmd> [args]").
	void handleRequest (uint64_t id, const std::string& cmd, const std::string& rest);
	/// Write a response line for request `id`. `ok` determines ok/err prefix.
	void writeResponse (uint64_t id, bool ok, const std::string& data);
	/// Raw write; swallows EPIPE/EAGAIN (drops line on bad client).
	void writeLine (const std::string& line);
};

} // namespace WallpaperEngine::Application
