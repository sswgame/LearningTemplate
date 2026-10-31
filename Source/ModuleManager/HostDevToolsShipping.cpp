#include "pch.h"

#include "ModuleManager/HostDevTools.h"

// 배포 빌드의 구현이다 — 개발 도구(DevTools)를 링크하지 않아 개발 도구 호스트 · 프레임 훅 · 개발 콘솔이 없다.
// 개발 빌드는 `HostDevTools.cpp` 를 CMake 가 고른다(`Source/ModuleManager/CMakeLists.txt`).
namespace sw
{
    void HostDevTools::addToHostScope( [[maybe_unused]] SubsystemScope& host, [[maybe_unused]] Engine& engine )
    {
    }

    void HostDevTools::attachFrameHooks( [[maybe_unused]] Engine& engine )
    {
    }

    void HostDevTools::startConsole( [[maybe_unused]] bool bEnableEditor )
    {
    }

    void HostDevTools::stopConsole()
    {
    }

    void HostDevTools::detach()
    {
    }
} // namespace sw
