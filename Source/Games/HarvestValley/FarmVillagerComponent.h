/**
 * @file FarmVillagerComponent.h
 * @brief 마을 사람 한 명의 모습 — 마을 디렉터가 적은 일정상 자리를 따라 자기 메시를 옮깁니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class FarmVillagerComponent
     * @brief `TickGroup::PostUpdate` 에서 디렉터를 읽기만 하고 자기 오브젝트의 메시에만 씁니다. 디렉터는 핸들로 들고 매 프레임 풉니다.
     */
    REFLECT( Category = "Farming", DisplayName = "Farm Villager View", Tooltip = "Follows one villager's scheduled position from the village director" )
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
    };
} // namespace sw
