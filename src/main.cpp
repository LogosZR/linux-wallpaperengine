#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <fcntl.h>
#include <unistd.h>

#include "WallpaperEngine/Application/ApplicationContext.h"
#include "WallpaperEngine/Application/WallpaperApplication.h"
#include "WallpaperEngine/Logging/Log.h"

volatile std::sig_atomic_t signalWriteFd = -1;

void signalhandler (const int sig) {
    const int savedErrno = errno;
    const int descriptor = signalWriteFd;
    if (descriptor >= 0) {
	const auto value = static_cast<unsigned char> (sig);
	// write(2) is async-signal-safe. The nonblocking pipe may already contain
	// a stop request; dropping a duplicate in that case is intentional.
	(void) write (descriptor, &value, sizeof (value));
    }
    errno = savedErrno;
}

void initLogging () {
    sLog.addOutput (new std::ostream (std::cout.rdbuf ()));
    sLog.addError (new std::ostream (std::cerr.rdbuf ()));
}

int main (int argc, char* argv[]) {
    std::unique_ptr<WallpaperEngine::Application::WallpaperApplication> ownedApp;
    int signalPipe[2] = { -1, -1 };

    try {
	// CEF subprocesses re-exec this binary with a --type switch. They retain
	// default signal behavior so supervisor group escalation cannot be swallowed.
	bool cefSubprocess = false;
	bool enableLogging = true;
	const std::string typeZygote = "--type=zygote";
	const std::string typeUtility = "--type=utility";

	for (int i = 1; i < argc; i++) {
	    if (strncmp (argv[i], "--type=", 7) == 0) cefSubprocess = true;
	    if (strncmp (typeZygote.c_str (), argv[i], typeZygote.size ()) == 0
		|| strncmp (typeUtility.c_str (), argv[i], typeUtility.size ()) == 0) {
		enableLogging = false;
	    }
	}

	if (enableLogging) initLogging ();

	WallpaperEngine::Application::ApplicationContext appContext (argc, argv);
	appContext.loadSettingsFromArgv ();

	if (!cefSubprocess && !appContext.settings.general.onlyListProperties) {
	    if (pipe2 (signalPipe, O_CLOEXEC | O_NONBLOCK) < 0) {
		throw std::runtime_error ("Failed to create signal self-pipe");
	    }
	    appContext.settings.general.stopSignalFd = signalPipe[0];
	    signalWriteFd = signalPipe[1];

	    // Signal handlers only enqueue a byte. Application state and logging are
	    // touched later on the main render thread.
	    std::signal (SIGINT, signalhandler);
	    std::signal (SIGTERM, signalhandler);
	}

	ownedApp = std::make_unique<WallpaperEngine::Application::WallpaperApplication> (appContext);

	// CefExecuteProcess exits from inside construction for real subprocesses.
	// onlyListProperties performs its work during construction.
	if (cefSubprocess || appContext.settings.general.onlyListProperties) {
	    ownedApp.reset ();
	    return 0;
	}

	ownedApp->show ();

	// Disconnect new handlers while cleanup runs. Keep both pipe descriptors
	// open until process exit: a handler already in flight may have copied the
	// old descriptor, and closing it here could redirect that write after reuse.
	signalWriteFd = -1;
	ownedApp.reset ();

	std::signal (SIGINT, SIG_DFL);
	std::signal (SIGTERM, SIG_DFL);
	return 0;
    } catch (const std::exception& e) {
	signalWriteFd = -1;
	ownedApp.reset ();
	std::signal (SIGINT, SIG_DFL);
	std::signal (SIGTERM, SIG_DFL);
	std::cerr << e.what () << std::endl;
	return 1;
    }
}
