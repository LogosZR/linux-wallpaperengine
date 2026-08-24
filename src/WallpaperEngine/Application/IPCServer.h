#pragma once

#include <cstdint>
#include <string>

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
 * Request/response commands: ping, sample_pixel, sample_region, load_scene.
 * Readiness probe: `#<id> ping` -> `#<id> ok pong`.
 *
 * Events: cursor, click (emitted while eyedropper mode is active).
 *
 * By default the server preserves its legacy ownership behavior: it removes a
 * stale pathname before bind and unlinks it during destruction. When
 * `ownerCleans` is true, it never unlinks; the spawning owner is responsible.
 */
class IPCServer {
public:
	IPCServer (const std::string& path, bool ownerCleans, WallpaperApplication& app);
	~IPCServer ();

	/// Poll the socket once. Call every frame from the main loop. Non-blocking.
	void poll ();

	/**
	 * Push a server-initiated event to the connected client. No-op when no
	 * client is attached. Line is queued as "!<type> <data>\n" for the next
	 * poll. Queued output is bounded; overflow or a terminal send error closes
	 * the client so it can reconnect and resubscribe.
	 */
	void emitEvent (const std::string& type, const std::string& data);

private:
	std::string m_path;
	bool m_ownerCleans;
	WallpaperApplication& m_app;
	int m_listenFd = -1;
	int m_clientFd = -1;
	std::string m_clientBuffer;
	std::string m_outputBuffer;

	void acceptClient ();
	void readClient ();
	void flushOutput ();
	void handleLine (const std::string& line);
	void closeClient ();

	/// Parse and dispatch a request carrying an id ("#<id> <cmd> [args]").
	void handleRequest (uint64_t id, const std::string& cmd, const std::string& rest);
	/// Write a response line for request `id`. `ok` determines ok/err prefix.
	void writeResponse (uint64_t id, bool ok, const std::string& data);
	/// Queue one complete line for bounded, non-blocking delivery from poll().
	void writeLine (const std::string& line);
};

} // namespace WallpaperEngine::Application
