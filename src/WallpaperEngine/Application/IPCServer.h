#pragma once

#include <string>
#include <glm/glm.hpp>

namespace WallpaperEngine::Application {

class WallpaperApplication;

/**
 * Unix-socket IPC server for runtime control of a running lwe instance.
 * Non-blocking; polled from the main render loop. Fire-and-forget protocol,
 * newline-delimited text commands.
 *
 * Supported commands:
 *   reposition <x> <y> <w> <h>
 *   set_property <key> <value>
 */
class IPCServer {
public:
	IPCServer (const std::string& path, WallpaperApplication& app);
	~IPCServer ();

	/// Poll the socket once. Call every frame from the main loop. Non-blocking.
	void poll ();

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
};

} // namespace WallpaperEngine::Application
