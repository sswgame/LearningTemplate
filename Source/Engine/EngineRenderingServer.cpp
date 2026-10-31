#include "pch.h"

#include "Engine/Engine.h"

// 전용 서버(SW_TARGET_TYPE=Server)의 엔진 루프 렌더러 몫이다 — 창 · RHI · 렌더 스레드 · 렌더러가 없다. 렌더러를 짓는 타깃은 `EngineRendering.cpp` 다.
namespace sw
{
    void Engine::onMainWindowResize( [[maybe_unused]] const uint32 width, [[maybe_unused]] const uint32 height )
    {
        // 서버에는 주 창이 없어 이 콜백을 걸지 않는다.
    }

    void Engine::drainRenderThreadPackets()
    {
        // 렌더 스레드가 없다 — 비울 패킷이 없다.
    }

    void Engine::submitRenderPacket( float32 deltaTime, const HostViewTargets& views, const ViewCameraProviderDelegate& sceneViewCameraProvider )
    {
        // Server 타깃 — 렌더러가 없어 올릴 머티리얼도 낼 패킷도 없다(뷰 · 카메라 조회는 쓰지 않는다).
        (void)deltaTime;
        (void)views;
        (void)sceneViewCameraProvider;
    }

    bool Engine::applyPendingBackendChange()
    {
        return false; // Server 타깃 — RHI 단계가 없어 바꿀 백엔드가 없다
    }
} // namespace sw
