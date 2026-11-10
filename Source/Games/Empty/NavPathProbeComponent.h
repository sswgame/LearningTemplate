/**
 * @file NavPathProbeComponent.h
 * @brief 비동기 경로 요청의 정확성 시나리오(`navasync`)가 쓰는 탐침 — 플레이를 시작한 자리에서 이름 붙은 대상까지의 경로를 계속 비동기로 묻고, 첫 완전 경로가 오면 같은 오브젝트의 에이전트를 대상으로 보냅니다.
 * @details 결과마다 경로 길이를 적어(`EmptyNav.ProbePathLength`) 장애물 · 재베이크가 비동기 결과에 드는지 보고, 질의 하나 걸러 버리는 요청을 내어 버린 요청이 수확 뒤에도
 *          결과를 내지 않는지 센다(`EmptyNav.ProbeCancelLeaks`). 언리얼 `ANavigationTestingActor`(비동기 경로 시험)의 자리다.
 *          새 게임을 시작할 때 `StressTestScene.*` 와 함께 지우는 파일이며, 이 컴포넌트를 쓰는 시나리오도 함께 지운다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Navigation/NavigationTypes.h"
#include "Engine/Navigation/PathRequestQueue.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class SceneNavigation;

    /**
     * @class NavPathProbeComponent
     * @brief 시작 자리 → 대상 오브젝트의 비동기 경로를 계속 묻는 탐침입니다. 파일 머리말 참고.
     */
    REFLECT()
    class NavPathProbeComponent : public Component
    {
    public:
        REFLECT_BODY();

        NavPathProbeComponent();
        ~NavPathProbeComponent() override = default;

        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;
        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( TickContext& context ) override;

        /** @brief 마지막으로 받은 경로의 길이(m)입니다. 아직 받지 못했으면 음수입니다. */
        float32 getLastPathLength() const { return _lastPathLength; }
        uint32  getResultCount() const { return _resultCount; }
        uint32  getCancelLeakCount() const { return _cancelLeakCount; }

    private:
        /** @brief 결과를 받고(첫 완전 경로면 에이전트를 보낸다), 버리기 시험을 한 칸 진행하고, 기다리는 질의가 없으면 새로 냅니다. */
        void updateProbe();

    private:
        PROPERTY( Category = "Probe", DisplayName = "Target", Tooltip = "Name of the object the probe asks a path to" )
        hashed_string _target;

        SceneNavigation*  _pSceneNavigation; ///< 등록한 씬의 내비게이션(등록 동안)
        float3            _origin;           ///< 플레이를 시작한 자리 — 에이전트가 움직여도 질의는 여기서 낸다
        PathRequestHandle _probe;
        PathRequestHandle _cancelPending; ///< 다음 프레임에 버릴 질의(그 사이 워커에 넘어간다)
        PathRequestHandle _cancelled;     ///< 버린 질의 — 수확 뒤에도 결과가 없어야 한다
        float32           _lastPathLength;
        uint32            _requestCount;
        uint32            _resultCount;
        uint32            _cancelLeakCount;
        uint8             _bAgentSent : 1; ///< 첫 완전 경로로 에이전트를 대상에 보냈다
        uint8             _reserved   : 7;
    };
} // namespace sw
