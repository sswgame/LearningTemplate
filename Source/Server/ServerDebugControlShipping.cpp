#include "pch.h"

#include "Server/ServerDebugControl.h"

// 배포 빌드의 구현이다 — 개발 콘솔(DevTools) · 디버그 채널 · 일시정지 명령이 없어 명령을 받지 않고 틱을 붙잡지 않는다.
// 개발 빌드는 `ServerDebugControl.cpp` 를 CMake 가 고른다(`Source/Server/CMakeLists.txt`).
namespace sw
{
    struct ServerDebugControl::DevParts
    {
    };

    ServerDebugControl::ServerDebugControl( const ServerTickCountDelegate& tickCount )
        : _tickCount{ tickCount }
        , _pDev{ nullptr }
    {
    }

    ServerDebugControl::~ServerDebugControl() = default;

    const utf8* ServerDebugControl::getHelpSuffix()
    {
        return "";
    }

    void ServerDebugControl::startConsole()
    {
    }

    void ServerDebugControl::stopConsole()
    {
    }

    bool ServerDebugControl::startChannel()
    {
        return true;
    }

    void ServerDebugControl::stopChannel()
    {
    }

    void ServerDebugControl::pump()
    {
    }

    bool ServerDebugControl::executeCommand( [[maybe_unused]] string_view command )
    {
        return false;
    }

    void ServerDebugControl::onTickFinished()
    {
    }
} // namespace sw
