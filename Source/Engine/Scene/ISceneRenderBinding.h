/**
 * @file Engine/Scene/ISceneRenderBinding.h
 * @brief 씬 매니저가 렌더 쪽에 맡기는 일입니다 — 월드 코어(층 5)는 GPU 디바이스와 메시 컴포넌트를 모릅니다. 구현은 렌더러의 `SceneRHISubsystem` 이고, 걸려 있지 않으면(전용 서버 · 테스트) 그리는 준비를 건너뜁니다.
 */
#pragma once

namespace sw
{
    class Scene;

    /**
     * @class ISceneRenderBinding
     * @brief 받아들이는 씬의 렌더 기본값 걸기와, 씬 교체 앞의 렌더 스레드 비우기입니다.
     */
    class ISceneRenderBinding
    {
    public:
        virtual ~ISceneRenderBinding() = default;

        /** @brief 렌더 스레드가 받은 일을 끝낼 때까지 기다립니다(GPU 는 기다리지 않는다) — 새 씬이 기본 자원을 표에 올리는 동안 기록하지 않게 합니다. */
        virtual void flushRenderThread() = 0;
        /** @brief @p scene 의 메시에 렌더 에셋과 씬 기본 머티리얼을 걸고, 잡은 머티리얼을 올립니다. */
        virtual void bindSceneDefaults( Scene& scene ) = 0;
    };
} // namespace sw
