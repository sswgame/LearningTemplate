/**
 * @file ScheduleNPCComponent.h
 * @brief 오브젝트가 하루 일정(`ScheduleSystem`)의 어느 NPC 인지 드는 정체 칸입니다.
 * @details 일정 NPC 와 씬 오브젝트를 이름으로 맞추지 않는다 — 이름은 사람이 읽는 값이라 바뀌고 겹친다. 디버그 HUD(`npc` 절) · 상호작용 끼어들기가 이 칸을 읽는다.
 *          언리얼 Mass 엔티티 핸들을 액터 컴포넌트가 드는 것, 유니티 ECS 의 엔티티 참조 컴포넌트와 같은 자리다. 스폰하는 게임 모드가 `setNPCID` 로 채운다.
 */
#pragma once
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct ScheduleNPCDef;
    class GameObject;
    class ScheduleSystem;

    /**
     * @class ScheduleNPCComponent
     * @brief 일정 NPC id 하나를 듭니다(파일 머리말 참고).
     */
    REFLECT( Category = "AI", DisplayName = "Schedule NPC", Tooltip = "Which daily schedule NPC this object is (debug HUD and interaction interruptions read it)" )
    class SW_GF_API ScheduleNPCComponent : public Component
    {
    public:
        REFLECT_BODY();

        ScheduleNPCComponent();
        ~ScheduleNPCComponent() override = default;

        const hashed_string& getNPCID() const { return _npcID.key(); }
        void                 setNPCID( const hashed_string& npcID ) { _npcID = CatalogKey<ScheduleNPCDef>( npcID ); }

        /** @brief @p object 가 @p schedule 의 몇 번째 NPC 인지 이 컴포넌트로 찾습니다. 컴포넌트가 없거나 그 일정에 없는 id 면 비어 있습니다(이름으로 맞추지 않는다). */
        [[nodiscard]] static optional<int32> findNPCIndex( const ScheduleSystem& schedule, const GameObject& object );

    private:
        PROPERTY( Required, Category = "AI", DisplayName = "NPC ID", Tooltip = "NPC id in the schedule catalog" )
        CatalogKey<ScheduleNPCDef> _npcID;
    };
} // namespace sw
