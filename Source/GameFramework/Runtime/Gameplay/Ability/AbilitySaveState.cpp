#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Ability/AbilitySaveState.h"

#include "Engine/Reflection/TypeFqn.h"

namespace sw
{
    void AbilitySaveState::serializeState( StateIO& io )
    {
        io.header( kStateFormat );
        io.reflected( *this ); // 실수의 유한은 아카이브가, 개수 하한은 칸의 `PROPERTY( Min )` 이 본다
        if ( io.isReading() == false || io.isOk() == false )
            return;
        // 이름 칸은 범위로 적을 수 없다 — 빈 이름을 손으로 거절한다
        const auto hasNamedValues = []( const vector<SavedAttributeValue>& listValue )
        {
            for ( const SavedAttributeValue& value : listValue )
            {
                if ( value._attribute.empty() )
                    return false;
            }
            return true;
        };
        bool bNamed = hasNamedValues( _listBaseValue );
        for ( const SavedAbilityEffect& effect : _listEffect )
        {
            bNamed = bNamed && effect._effectID.empty() == false && hasNamedValues( effect._listSetByCaller ) && hasNamedValues( effect._listSourceAttribute );
            for ( const EvaluatedModifier& modifier : effect._listModifier )
            {
                bNamed = bNamed && modifier._attribute.empty() == false;
            }
        }
        for ( const SavedAbilityTag& tag : _listLooseTag )
        {
            bNamed = bNamed && tag._tag.empty() == false;
        }
        io.check( bNamed );
    }
} // namespace sw
