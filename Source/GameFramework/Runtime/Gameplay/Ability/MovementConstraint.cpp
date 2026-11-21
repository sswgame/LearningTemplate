#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Ability/MovementConstraint.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Runtime/Gameplay/Ability/AbilitySystemComponent.h"

namespace sw
{
    TagID MovementConstraint::getMoveBlockedTag()
    {
        return "State.MoveBlocked"_tag;
    }

    TagID MovementConstraint::getJumpBlockedTag()
    {
        return "State.JumpBlocked"_tag;
    }

    const hashed_string& MovementConstraint::getSpeedScaleAttribute()
    {
        static const hashed_string s_name( "MoveSpeedScale" );
        return s_name;
    }

    MovementConstraint MovementConstraint::read( const GameObject& owner )
    {
        MovementConstraint                  constraint;
        const AbilitySystemComponent* const pAbility = owner.getComponent<AbilitySystemComponent>();
        if ( pAbility == nullptr )
            return constraint;
        const TagCountTable& tags = pAbility->getOwnedTagCounts();
        constraint._bMoveBlocked  = tags.hasMatchingTag( getMoveBlockedTag() ) ? SW_TRUE : SW_FALSE;
        constraint._bJumpBlocked  = ( constraint._bMoveBlocked == SW_TRUE || tags.hasMatchingTag( getJumpBlockedTag() ) ) ? SW_TRUE : SW_FALSE;
        if ( const optional<float32> scale = pAbility->getAttributeValue( getSpeedScaleAttribute() ); scale.has_value() )
            constraint._speedScale = MathUtil::max( *scale, 0.0f );
        return constraint;
    }
} // namespace sw
