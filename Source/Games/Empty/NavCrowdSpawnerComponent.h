/**
 * @file NavCrowdSpawnerComponent.h
 * @brief 내비 부하 시나리오(`load/navcrowd`)의 무대 — 플레이를 시작하면 떠돌이 내비메시 에이전트(`NavWanderComponent`) N 개를 격자로 세웁니다.
 * @details 목표 규모(에이전트 1 천)가 목적지를 바꿔 가며 걷는 씬을 데이터 한 줄(컴포넌트 칸)로 만든다 — 경로 질의 · 군중 진행이 게임 스레드를 떠났는지를
 *          `ctest -L load` 의 프레임 측정으로 본다. 세운 뒤에는 할 일이 없다(에이전트마다 자기 틱이 돈다). 새 게임을 시작할 때 `StressTestScene.*` 와 함께 지우는 파일이다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class NavCrowdSpawnerComponent
     * @brief 떠돌이 에이전트 N 개를 이 오브젝트 둘레의 집 상자에 세웁니다. 파일 머리말 참고.
     */
    REFLECT()
    class NavCrowdSpawnerComponent : public Component
    {
    public:
        REFLECT_BODY();

        NavCrowdSpawnerComponent();
        ~NavCrowdSpawnerComponent() override = default;

        void onBeginPlay() override;

    private:
        /** @brief 에이전트를 격자로 세웁니다(구조 변경 — 틱 밖에서 돈다). */
        void spawnAgents( GameObjectManager& manager );

    private:
        PROPERTY( Category = "Crowd", DisplayName = "Agent Type", Tooltip = "Navmesh agent kind of the spawned agents" )
        hashed_string _agentType;
        PROPERTY( Category = "Crowd", Min = 0, Tooltip = "How many wandering agents to spawn" )
        uint32 _agentCount;
        PROPERTY( Category = "Crowd", Min = 0.0, Tooltip = "Half size of the box the agents spawn and wander in, around this object", Units = m )
        float32 _halfExtent;
        PROPERTY( Category = "Crowd", Min = 0.0, Tooltip = "Farthest a new destination lies from an agent", Units = m )
        float32 _wanderRadius;
        PROPERTY( Category = "Crowd", Min = 0.0, Tooltip = "Top speed of each agent", Units = "m/s" )
        float32 _maxSpeed;
        PROPERTY( Category = "Crowd", Tooltip = "Seed of the destination sequences (agent index is mixed in)" )
        uint32 _seed;
        PROPERTY( Category = "Crowd", Tooltip = "Every Nth new destination of an agent also asks an async path query (0 = never)" )
        uint32 _probeInterval;
    };
} // namespace sw
