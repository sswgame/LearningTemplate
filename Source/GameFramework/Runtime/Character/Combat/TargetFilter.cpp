#include "pch.h"

#include "GameFramework/Runtime/Character/Combat/TargetFilter.h"

#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    bool TargetFilter::isSelectable( const GameObject& object )
    {
        return object.isActiveInHierarchy() && object.hasTag( getHiddenTag(), true ) == false && object.hasTag( getDeadTag(), true ) == false;
    }
} // namespace sw
