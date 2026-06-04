#include "GLFWWindowOutput.h"
#include "GLFWOutputViewport.h"
#include "WallpaperEngine/Logging/Log.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

using namespace WallpaperEngine::Render::Drivers::Output;

GLFWWindowOutput::GLFWWindowOutput (ApplicationContext& context, VideoDriver& driver) : Output (context, driver) {
    if (this->m_context.settings.render.mode != Application::ApplicationContext::NORMAL_WINDOW
	&& this->m_context.settings.render.mode != Application::ApplicationContext::EXPLICIT_WINDOW) {
	sLog.exception ("Initializing window output when not in output mode, how did you get here?!");
    }

    // window should be visible
    driver.showWindow ();

    if (this->m_context.settings.render.mode == Application::ApplicationContext::EXPLICIT_WINDOW) {
	this->m_fullWidth = this->m_context.settings.render.window.geometry.z;
	this->m_fullHeight = this->m_context.settings.render.window.geometry.w;
	this->repositionWindow ();
    } else {
	// take the size from the driver (default window size)
	this->m_fullWidth = this->m_driver.getFramebufferSize ().x;
	this->m_fullHeight = this->m_driver.getFramebufferSize ().y;
    }

    // register the default viewport
    this->m_viewports["default"]
	= new GLFWOutputViewport { { 0, 0, this->m_fullWidth, this->m_fullHeight }, "default" };

    // Set up shared-memory pixel export if requested.
    if (this->m_context.settings.general.shmOutput) {
	this->setupShm ();
    }
}

GLFWWindowOutput::~GLFWWindowOutput () {
    if (this->m_shmBuffer) {
	munmap (this->m_shmBuffer, this->m_shmSize);
	this->m_shmBuffer = nullptr;
    }
    if (this->m_shmFd >= 0) {
	close (this->m_shmFd);
	shm_unlink (this->m_shmPath.c_str ());
    }
}

void GLFWWindowOutput::setupShm () {
    // Allocate enough for BGRA at the current framebuffer size. The buffer
    // has a 16-byte header: [width:u32 | height:u32 | frameCounter:u32 | reserved:u32]
    // followed by width*height*4 bytes of pixel data.
    const uint32_t headerSize = 16;
    this->m_shmSize = headerSize + this->m_fullWidth * this->m_fullHeight * 4;

    // Use the IPC socket path as the basis for the shm name so it's unique
    // per lwe instance and correlates with the owning session.
    this->m_shmPath = "/kuro-wpe-" + std::to_string (getpid ());

    this->m_shmFd = shm_open (this->m_shmPath.c_str (), O_CREAT | O_RDWR, 0600);
    if (this->m_shmFd < 0) {
	sLog.error ("shm_open failed: ", strerror (errno));
	return;
    }
    if (ftruncate (this->m_shmFd, this->m_shmSize) < 0) {
	sLog.error ("ftruncate shm failed: ", strerror (errno));
	close (this->m_shmFd);
	this->m_shmFd = -1;
	return;
    }
    this->m_shmBuffer = mmap (nullptr, this->m_shmSize, PROT_READ | PROT_WRITE, MAP_SHARED, this->m_shmFd, 0);
    if (this->m_shmBuffer == MAP_FAILED) {
	sLog.error ("mmap shm failed: ", strerror (errno));
	this->m_shmBuffer = nullptr;
	close (this->m_shmFd);
	this->m_shmFd = -1;
	return;
    }

    // Write the header with initial dimensions. Frame counter starts at 0.
    auto* header = static_cast<uint32_t*> (this->m_shmBuffer);
    header[0] = this->m_fullWidth;
    header[1] = this->m_fullHeight;
    header[2] = 0; // frame counter
    header[3] = 0; // reserved

    sLog.out ("SHM output enabled: ", this->m_shmPath, " (", this->m_fullWidth, "x", this->m_fullHeight, ", ", this->m_shmSize, " bytes)");
}

void GLFWWindowOutput::repositionWindow () const {
    // reposition the window
    this->m_driver.resizeWindow (this->m_context.settings.render.window.geometry);
}

void GLFWWindowOutput::reset () {
    if (this->m_context.settings.render.mode == Application::ApplicationContext::EXPLICIT_WINDOW) {
	this->repositionWindow ();
    }
}

bool GLFWWindowOutput::renderVFlip () const { return true; }

bool GLFWWindowOutput::renderMultiple () const { return false; }

bool GLFWWindowOutput::haveImageBuffer () const { return this->m_shmBuffer != nullptr; }

void* GLFWWindowOutput::getImageBuffer () const {
    if (!this->m_shmBuffer) return nullptr;
    // Skip the 16-byte header — pixel data starts at offset 16.
    return static_cast<char*> (this->m_shmBuffer) + 16;
}

uint32_t GLFWWindowOutput::getImageBufferSize () const {
    return this->m_shmSize > 16 ? this->m_shmSize - 16 : 0;
}

void GLFWWindowOutput::updateRender () const {
    this->m_fullWidth = this->m_driver.getFramebufferSize ().x;
    this->m_fullHeight = this->m_driver.getFramebufferSize ().y;

    this->m_viewports["default"]->viewport = { 0, 0, this->m_fullWidth, this->m_fullHeight };

    // Update the shm header's frame counter so the consumer can detect
    // fresh frames without a separate IPC event per tick.
    if (this->m_shmBuffer) {
	auto* header = static_cast<uint32_t*> (this->m_shmBuffer);
	header[0] = this->m_fullWidth;
	header[1] = this->m_fullHeight;
	header[2]++; // frame counter
    }
}