/**
 * @file FarmVillagerComponent.h
 * @brief 마을 사람 한 명의 모습 — 마을 디렉터가 적은 일정상 위치를 `NavAgent` 로 쫓아 자기 메시를 옮깁니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Actor/Navigation/NavAgent.h"

namespace sw
{
    /**
     * @class FarmVillagerComponent
     * @brief `TickGroup::PostUpdate` 에서 디렉터를 읽기만 하고 자기 오브젝트의 메시에만 씁니다. 디렉터는 핸들로 들고 매 프레임 풉니다.
     * @details 몸은 디렉터의 내비 격자 위를 걷는 `NavAgent` 다(가속 한계 · 도착 감속 · 다른 마을 사람의 일정상 위치에서 떨어지기 · 막힌 칸 미끄러지기).
     *          목표는 매 틱 일정상 위치이고, 일정상 위치가 디렉터의 `getBodySnapDistance` 넘게 떨어지면(처음 · 시계 감기) 그 자리에 놓는다.
     *          걷는 상태는 뷰의 것이라 저장하지 않는다 — 복원하면 일정상 위치에 놓인다.
     */
    REFLECT( Category = "Farming", DisplayName = "Farm Villager View", Tooltip = "Walks one villager's body after its scheduled position on the village nav grid" )
    class FarmVillagerComponent : public Component
    {
    public:
        REFLECT_BODY();

        FarmVillagerComponent();
        virtual ~FarmVillagerComponent() override = default;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 따라갈 디렉터와 마을 사람 번호를 정합니다(디렉터가 스폰한 뒤 부른다). */
        void assignVillager( GameObjectHandle director, int32 npcIndex );

    private:
        PROPERTY( Category = "Villager", DisplayName = "Director", Tooltip = "Object with the FarmVillageDirectorComponent" )
        GameObjectHandle _director;
        PROPERTY( Category = "Villager", DisplayName = "NPC Index", Tooltip = "Index of the schedule NPC this body follows" )
        int32 _npcIndex;
        PROPERTY( Category = "Villager", DisplayName = "Height Offset", Tooltip = "Lift above the ground point", Units = m )
        float32 _heightOffset;

        NavAgent       _agent;
        vector<float3> _listGoal;     ///< 목표 한 점(`NavAgent::followPath` 가 받는 모양)
        vector<float3> _listNeighbor; ///< 이번 틱 떨어질 이웃(다른 마을 사람의 일정상 위치)
        bool           _bPlaced;      ///< 몸을 한 번이라도 일정상 위치에 놓았다
    };
} // namespace sw
