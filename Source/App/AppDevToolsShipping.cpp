#include "pch.h"

#include "App/AppDevTools.h"

// 배포 빌드의 구현이다 — 개발 도구(DevTools)를 링크하지 않고, 핫 리로드 단축키 · 개발 콘솔이 없다.
// 개발 빌드는 `AppDevTools.cpp` 를 CMake 가 고른다(`Source/App/CMakeLists.txt`).
namespace sw
{
    void AppDevTools::addToHostScope( [[maybe_unused]] SubsystemScope& host, [[maybe_unused]] Engine& engine )
    {
    }

    void AppDevTools::attachFrameHooks( [[maybe_unused]] Engine& engine )
    {
    }

    void AppDevTools::startConsole( [[maybe_unused]] bool bEnableEditor )
    {
    }

    void AppDevTools::stopConsole()
    {
    }

    void AppDevTools::bindRenderThreadHooks( [[maybe_unused]] RenderThread& renderThread, [[maybe_unused]] RHIBackendSwitcher& backendSwitcher )
    {
    }

    void AppDevTools::pollShellActions( [[maybe_unused]] float32 deltaTime, [[maybe_unused]] ModuleReloadDriver& moduleReload, [[maybe_unused]] bool bEnableEditor )
    {
    }

    void AppDevTools::detach()
    {
    }
} // namespace sw
