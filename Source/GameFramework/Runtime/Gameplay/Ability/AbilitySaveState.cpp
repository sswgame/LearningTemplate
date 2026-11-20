#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Ability/AbilitySaveState.h"

#include "Engine/Reflection/TypeFqn.h"
#include "Engine/Serialization/Format/FormatUpgrade.h"

namespace sw
{
    namespace
    {
        /** @brief 판 1(손 본문) 바이트를 옛 칸 순서로 읽습니다 — 판 1 → 2 단계만 쓴다. 검사는 올린 바이트를 읽는 `reflected` 가 한다. */
        struct AbilitySaveStateV1
        {
            static constexpr uint32 kNameMinBytes     = sizeof( uint32 );                        ///< 길이가 붙은 이름 하나의 최소 바이트
            static constexpr uint32 kValueMinBytes    = kNameMinBytes + sizeof( float32 );       ///< 이름 + 수
            static constexpr uint32 kModifierMinBytes = kValueMinBytes + sizeof( uint8 );        ///< 이름 + 연산 + 수
            static constexpr uint32 kTagMinBytes      = kNameMinBytes + sizeof( int32 );         ///< 글 + 개수
            static constexpr uint32 kEffectMinBytes   = kNameMinBytes * 2 + sizeof( int32 ) * 2; ///< 이펙트 하나의 앞쪽 칸만 센다(개수 상한 검사용)

            static void readValue( StateIO& io, SavedAttributeValue& outValue )
            {
                io.value( outValue._attribute );
                io.value( outValue._value );
            }

            static void readEffect( StateIO& io, SavedAbilityEffect& outEffect )
            {
                io.value( outEffect._effectID );
                io.value( outEffect._sourceName );
                io.value( outEffect._level );
                io.value( outEffect._stackCount );
                io.value( outEffect._duration );
                io.value( outEffect._remainingTime );
                io.value( outEffect._periodTimer );
                io.value( outEffect._startTime );
                io.list( outEffect._listSetByCaller, kValueMinBytes, [&io]( SavedAttributeValue& value )
                { readValue( io, value ); } );
                io.list( outEffect._listSourceAttribute, kValueMinBytes, [&io]( SavedAttributeValue& value )
                { readValue( io, value ); } );
                io.list( outEffect._listDynamicGrantedTag );
                io.list( outEffect._listModifier, kModifierMinBytes, [&io]( EvaluatedModifier& modifier )
                {
                    io.value( modifier._attribute );
                    io.enumValue( modifier._op, AttributeModOp::Override );
                    io.value( modifier._magnitude );
                } );
            }

            /** @brief 판 1 → 2: 옛 칸으로 읽어 리플렉션 본문으로 다시 씁니다(연산 열거가 uint8 에서 int64 로, 앞에 리플렉션 머리). */
            [[nodiscard]] static bool upgradeTo2( Archive& inOldBody, Archive& outNewBody )
            {
                AbilitySaveState state;
                StateIO          in( inOldBody );
                in.value( state._time );
                in.list( state._listBaseValue, kValueMinBytes, [&in]( SavedAttributeValue& value )
                { readValue( in, value ); } );
                in.list( state._listEffect, kEffectMinBytes, [&in]( SavedAbilityEffect& effect )
                { readEffect( in, effect ); } );
                in.list( state._listLooseTag, kTagMinBytes, [&in]( SavedAbilityTag& tag )
                {
                    in.value( tag._tag );
                    in.value( tag._count );
                } );
                if ( in.isOk() == false )
                    return false;
                StateIO out( outNewBody );
                out.reflected( state );
                return true;
            }
        };
    } // namespace

    SW_STATE_FORMAT_UPGRADE( abilitySaveState1To2, "ABIL", 1, 2, &AbilitySaveStateV1::upgradeTo2 );

    void AbilitySaveState::serializeState( StateIO& io )
    {
        io.header( kStateTag, kStateVersion );
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
