/**
 * @file NavWanderComponent.h
 * @brief 내비 부하 시나리오(`load/navcrowd`)의 에이전트 하나 — 같은 오브젝트의 내비메시 에이전트가 멈추면 둘레의 새 목적지를 겁니다. 몇 번에 한 번은 비동기 경로 질의를 함께 냅니다.
 * @details 자기 오브젝트만 만지므로(에이전트 요청은 그 오브젝트의 틱에서 불러도 된다) 1 천 개가 병렬 틱으로 돈다 — 한 컴포넌트가 다른 오브젝트 일을 대신하는 모양을 피한다.
 *          비동기 경로 질의는 결과를 받고(`EmptyNav.WanderProbeResults`), 하나 걸러 버리는 요청도 내어 버린 요청이 수확 뒤에도 결과를 내지 않는지 센다(`EmptyNav.WanderCancelLeaks`).
 *          무작위는 오브젝트마다 씨앗 하나라 실행마다 같은 목적지 열이다. 새 게임을 시작할 때 `StressTestScene.*` 와 함께 지우는 파일이다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/DeterministicRandom.h"
#include "Core/Math/Math.h"

#include "Engine/Navigation/PathRequestQueue.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class NavMeshAgentComponent;
    class SceneNavigation;

    /**
     * @class NavWanderComponent
     * @brief 집 상자 안에서 둘레를 떠도는 에이전트입니다. 파일 머리말 참고.
     */
    REFLECT()
    class NavWanderComponent : public Component
    {
    public:
        REFLECT_BODY();

        NavWanderComponent();
        ~NavWanderComponent() override = default;

        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;
        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( TickContext& context ) override;

        /** @brief 집 상자(가운데 · 반폭), 한 번에 떠도는 반경, 씨앗, 질의 간격(목적지 몇 번에 한 번 — 0 은 묻지 않는다)을 정합니다. 플레이 전에 부릅니다. */
        void setWander( const float3& homeCenter, float32 homeHalfExtent, float32 wanderRadius, uint32 seed, uint32 probeInterval );

        uint32 getArrivalCount() const { return _arrivalCount; }
        uint32 getProbeResultCount() const { return _probeResultCount; }
        uint32 getCancelLeakCount() const { return _cancelLeakCount; }

    private:
        /** @brief 지난 프레임에 낸 질의의 결과를 받고, 버리기 시험(내기 → 다음 프레임 버리기 → 그다음 프레임 확인)을 한 칸 진행합니다. */
        void collectProbeResults();
        /** @brief 지금 자리 둘레의 새 목적지를 겁니다(집 상자 안). 간격이 되면 그 경로를 비동기로 함께 묻습니다. */
        void retarget( NavMeshAgentComponent& agent );

    private:
        PROPERTY( Category = "Wander", Tooltip = "Centre of the box the agent wanders in", Units = m )
        float3 _homeCenter;
        PROPERTY( Category = "Wander", Min = 0.0, Tooltip = "Half size of the box the agent wanders in", Units = m )
        float32 _homeHalfExtent;
        PROPERTY( Category = "Wander", Min = 0.0, Tooltip = "Farthest a new destination lies from the agent", Units = m )
        float32 _wanderRadius;
        PROPERTY( Category = "Wander", Tooltip = "Seed of the destination sequence (same seed, same walk)" )
        uint32 _seed;
        PROPERTY( Category = "Wander", Tooltip = "Every Nth new destination also asks an async path query (0 = never)" )
        uint32 _probeInterval;

        DeterministicRandom32 _random;
        SceneNavigation*      _pSceneNavigation; ///< 등록한 씬의 내비게이션(등록 동안)
        PathRequestHandle     _probe;            ///< 결과를 기다리는 질의
        PathRequestHandle     _cancelPending;    ///< 다음 프레임에 버릴 질의(그 사이 워커에 넘어간다)
        PathRequestHandle     _cancelled;        ///< 버린 질의 — 수확 뒤에도 결과가 없어야 한다
        uint32                _retargetCount;
        uint32                _arrivalCount;
        uint32                _probeResultCount;
        uint32                _cancelLeakCount;
    };
} // namespace sw
