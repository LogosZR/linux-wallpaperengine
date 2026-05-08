#include "IPCServer.h"
#include "WallpaperApplication.h"
#include "WallpaperEngine/Logging/Log.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <sstream>

using namespace WallpaperEngine::Application;

IPCServer::IPCServer (const std::string& path, WallpaperApplication& app) :
	m_path (path), m_app (app) {
	// Remove any stale socket file (prior crashed instance).
	unlink (path.c_str ());

	m_listenFd = socket (AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (m_listenFd < 0) {
		sLog.error ("IPCServer: socket() failed: ", strerror (errno));
		return;
	}

	sockaddr_un addr {};
	addr.sun_family = AF_UNIX;
	std::strncpy (addr.sun_path, path.c_str (), sizeof (addr.sun_path) - 1);

	if (bind (m_listenFd, reinterpret_cast<sockaddr*> (&addr), sizeof (addr)) < 0) {
		sLog.error ("IPCServer: bind(", path, ") failed: ", strerror (errno));
		close (m_listenFd);
		m_listenFd = -1;
		return;
	}

	if (listen (m_listenFd, 1) < 0) {
		sLog.error ("IPCServer: listen() failed: ", strerror (errno));
		close (m_listenFd);
		m_listenFd = -1;
		return;
	}

	sLog.out ("IPCServer: listening on ", path);
}

IPCServer::~IPCServer () {
	closeClient ();
	if (m_listenFd >= 0) close (m_listenFd);
	unlink (m_path.c_str ());
}

void IPCServer::poll () {
	if (m_listenFd < 0) return;

	if (m_clientFd < 0) {
		acceptClient ();
	} else {
		readClient ();
	}
}

void IPCServer::acceptClient () {
	int fd = accept4 (m_listenFd, nullptr, nullptr, SOCK_NONBLOCK);
	if (fd < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK) {
			sLog.error ("IPCServer: accept() failed: ", strerror (errno));
		}
		return;
	}
	m_clientFd = fd;
	m_clientBuffer.clear ();
	sLog.out ("IPCServer: client connected");
}

void IPCServer::readClient () {
	char buf[1024];
	ssize_t n = read (m_clientFd, buf, sizeof (buf));
	if (n < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) return;
		sLog.error ("IPCServer: read() failed: ", strerror (errno));
		closeClient ();
		return;
	}
	if (n == 0) {
		// Client disconnected.
		closeClient ();
		return;
	}

	m_clientBuffer.append (buf, n);

	// Drain complete lines from the buffer.
	size_t pos;
	while ((pos = m_clientBuffer.find ('\n')) != std::string::npos) {
		std::string line = m_clientBuffer.substr (0, pos);
		m_clientBuffer.erase (0, pos + 1);
		// Strip trailing \r if present (Windows-style line endings).
		if (!line.empty () && line.back () == '\r') line.pop_back ();
		if (!line.empty ()) handleLine (line);
	}
}

void IPCServer::handleLine (const std::string& line) {
	// Request with id: "#<id> <cmd> [args]"
	if (!line.empty () && line.front () == '#') {
		std::istringstream iss (line.substr (1));
		uint64_t id = 0;
		std::string cmd;
		if (!(iss >> id >> cmd)) {
			sLog.error ("IPCServer: malformed request (expected #<id> <cmd>): ", line);
			return;
		}
		std::string rest;
		std::getline (iss, rest);
		if (!rest.empty () && rest.front () == ' ') rest.erase (0, 1);
		handleRequest (id, cmd, rest);
		return;
	}

	// Fire-and-forget command, no response.
	std::istringstream iss (line);
	std::string cmd;
	iss >> cmd;

	if (cmd == "reposition") {
		int x, y, w, h;
		if (iss >> x >> y >> w >> h) {
			m_app.ipcReposition ({x, y, w, h});
		} else {
			sLog.error ("IPCServer: malformed reposition: ", line);
		}
	} else if (cmd == "set_property") {
		std::string key;
		iss >> key;
		std::string value;
		std::getline (iss, value);
		if (!value.empty () && value.front () == ' ') value.erase (0, 1);
		if (key.empty ()) {
			sLog.error ("IPCServer: set_property missing key: ", line);
		} else {
			m_app.ipcSetProperty (key, value);
		}
	} else if (cmd == "set_background_mode") {
		std::string value;
		std::getline (iss, value);
		if (!value.empty () && value.front () == ' ') value.erase (0, 1);
		if (value.empty ()) {
			sLog.error ("IPCServer: set_background_mode missing mode: ", line);
		} else {
			m_app.ipcSetBackgroundMode (value);
		}
	} else if (cmd == "start_eyedropper") {
		m_app.ipcSetEyedropperActive (true);
	} else if (cmd == "stop_eyedropper") {
		m_app.ipcSetEyedropperActive (false);
	} else {
		sLog.error ("IPCServer: unknown command: ", cmd);
	}
}

void IPCServer::handleRequest (uint64_t id, const std::string& cmd, const std::string& rest) {
	if (cmd == "sample_pixel") {
		std::istringstream iss (rest);
		int x, y;
		if (!(iss >> x >> y)) {
			writeResponse (id, false, "expected: sample_pixel <x> <y>");
			return;
		}
		std::string hex;
		if (m_app.ipcSamplePixel (x, y, hex)) {
			writeResponse (id, true, hex);
		} else {
			writeResponse (id, false, "sample failed");
		}
	} else {
		writeResponse (id, false, "unknown command: " + cmd);
	}
}

void IPCServer::writeResponse (uint64_t id, bool ok, const std::string& data) {
	std::ostringstream line;
	line << '#' << id << ' ' << (ok ? "ok" : "err");
	if (!data.empty ()) line << ' ' << data;
	line << '\n';
	writeLine (line.str ());
}

void IPCServer::emitEvent (const std::string& type, const std::string& data) {
	if (m_clientFd < 0) return;
	std::ostringstream line;
	line << '!' << type;
	if (!data.empty ()) line << ' ' << data;
	line << '\n';
	writeLine (line.str ());
}

void IPCServer::writeLine (const std::string& line) {
	if (m_clientFd < 0) return;
	const ssize_t n = ::write (m_clientFd, line.data (), line.size ());
	if (n < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EPIPE) {
			sLog.error ("IPCServer: write failed: ", strerror (errno));
		}
		// EPIPE / any error: client is gone; drop it so accept reopens.
		if (errno == EPIPE) closeClient ();
	}
}

void IPCServer::closeClient () {
	if (m_clientFd >= 0) {
		close (m_clientFd);
		m_clientFd = -1;
		m_clientBuffer.clear ();
		sLog.out ("IPCServer: client disconnected");
	}
}
