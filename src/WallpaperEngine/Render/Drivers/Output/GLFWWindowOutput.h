#pragma once

#include "Output.h"
#include "WallpaperEngine/Render/Drivers/VideoDriver.h"
#include <string>

namespace WallpaperEngine::Render::Drivers::Output {
class GLFWWindowOutput final : public Output {
public:
    GLFWWindowOutput (ApplicationContext& context, VideoDriver& driver);
    ~GLFWWindowOutput ();

    void reset () override;
    bool renderVFlip () const override;
    bool renderMultiple () const override;
    bool haveImageBuffer () const override;
    void* getImageBuffer () const override;
    uint32_t getImageBufferSize () const override;
    void updateRender () const override;

    /** Non-empty when --shm-output is active. The host reads this path
     *  and mmaps the same buffer to consume rendered frames. */
    const std::string& shmPath () const { return m_shmPath; }
    bool shmActive () const { return m_shmBuffer != nullptr; }

private:
    void repositionWindow () const;
    void setupShm ();

    std::string m_shmPath;
    void* m_shmBuffer = nullptr;
    uint32_t m_shmSize = 0;
    int m_shmFd = -1;
};
} // namespace WallpaperEngine::Render::Drivers::Output