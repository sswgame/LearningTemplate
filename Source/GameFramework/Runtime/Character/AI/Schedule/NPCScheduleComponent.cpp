#include "pch.h"

#include "GameFramework/Runtime/Character/AI/Schedule/NPCScheduleComponent.h"

#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleSystem.h"

namespace sw
{
    NPCScheduleComponent::NPCScheduleComponent()
        : _npcID{}
    {
    }

    optional<int32> NPCScheduleComponent::findNPCIndex( const ScheduleSystem& schedule, const GameObject& object )
    {
        const NPCScheduleComponent* const pNPC = object.getComponent<NPCScheduleComponent>();
        if ( pNPC == nullptr || pNPC->_npcID.empty() )
            return nullopt;
        return schedule.findNPCIndex( pNPC->_npcID.key() );
    }
} // namespace sw
