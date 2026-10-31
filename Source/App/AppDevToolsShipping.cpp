#include "pch.h"

#include "App/AppDevTools.h"

// 배포 빌드의 구현이다 — 모듈은 정적으로 링크해 핫 리로드 단축키가 없고, 백엔드 시험 훅(개발 명령)도 없다.
// 개발 빌드는 `AppDevTools.cpp` 를 CMake 가 고른다(`Source/App/CMakeLists.txt`).
namespace sw
{
    void AppDevTools::bindRenderThreadHooks( [[maybe_unused]] RenderThread& renderThread, [[maybe_unused]] RHIBackendSwitcher& backendSwitcher )
    {
    }

    void AppDevTools::pollShellActions( [[maybe_unused]] float32 deltaTime, [[maybe_unused]] ModuleReloadDriver& moduleReload, [[maybe_unused]] bool bEnableEditor )
    {
    }
} // namespace sw
