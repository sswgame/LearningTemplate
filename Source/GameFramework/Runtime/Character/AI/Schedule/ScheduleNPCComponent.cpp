#include "pch.h"

#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleNPCComponent.h"

#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleSystem.h"

namespace sw
{
    ScheduleNPCComponent::ScheduleNPCComponent()
        : _npcID{}
    {
    }

    optional<int32> ScheduleNPCComponent::findNPCIndex( const ScheduleSystem& schedule, const GameObject& object )
    {
        const ScheduleNPCComponent* const pNPC = object.getComponent<ScheduleNPCComponent>();
        if ( pNPC == nullptr || pNPC->_npcID.empty() )
            return nullopt;
        return schedule.findNPCIndex( pNPC->_npcID );
    }
} // namespace sw
