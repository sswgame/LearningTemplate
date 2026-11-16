#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Ability/AbilitySaveState.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct AbilitySaveStateInternal
        {
            static constexpr uint32 kNameMinBytes     = sizeof( uint32 );                        ///< 길이가 붙은 이름 하나의 최소 바이트
            static constexpr uint32 kValueMinBytes    = kNameMinBytes + sizeof( float32 );       ///< 이름 + 수
            static constexpr uint32 kModifierMinBytes = kValueMinBytes + sizeof( uint8 );        ///< 이름 + 연산 + 수
            static constexpr uint32 kTagMinBytes      = kNameMinBytes + sizeof( int32 );         ///< 글 + 개수
            static constexpr uint32 kEffectMinBytes   = kNameMinBytes * 2 + sizeof( int32 ) * 2; ///< 이펙트 하나의 앞쪽 칸만 센다(개수 상한 검사용)

            /** @brief NaN · 무한을 거절합니다. 시간 · 크기는 모두 유한해야 다음 틱에서 만료 · 집계가 끝난다. */
            static void serializeFinite( StateIO& io, float32& inoutValue )
            {
                io.value( inoutValue );
                (void)io.check( MathUtil::isFinite( inoutValue ) );
            }
        };
    } // namespace

    void SavedAttributeValue::serializeState( StateIO& io )
    {
        io.value( _attribute );
        (void)io.check( _attribute.empty() == false );
        AbilitySaveStateInternal::serializeFinite( io, _value );
    }

    void SavedAbilityModifier::serializeState( StateIO& io )
    {
        io.value( _attribute );
        (void)io.check( _attribute.empty() == false );
        io.enumValue( _op, AttributeModOp::Override );
        AbilitySaveStateInternal::serializeFinite( io, _magnitude );
    }

    void SavedAbilityEffect::serializeState( StateIO& io )
    {
        io.value( _effectID );
        (void)io.check( _effectID.empty() == false );
        io.value( _sourceName );
        io.value( _level );
        io.value( _stackCount );
        (void)io.check( _stackCount >= 1 );
        AbilitySaveStateInternal::serializeFinite( io, _duration );
        AbilitySaveStateInternal::serializeFinite( io, _remainingTime );
        AbilitySaveStateInternal::serializeFinite( io, _periodTimer );
        AbilitySaveStateInternal::serializeFinite( io, _startTime );
        io.list( _listSetByCaller, AbilitySaveStateInternal::kValueMinBytes, [&io]( SavedAttributeValue& value )
        { io.value( value ); } );
        io.list( _listSourceAttribute, AbilitySaveStateInternal::kValueMinBytes, [&io]( SavedAttributeValue& value )
        { io.value( value ); } );
        io.list( _listDynamicGrantedTag );
        io.list( _listModifier, AbilitySaveStateInternal::kModifierMinBytes, [&io]( SavedAbilityModifier& modifier )
        { io.value( modifier ); } );
    }

    void SavedAbilityTag::serializeState( StateIO& io )
    {
        io.value( _tag );
        (void)io.check( _tag.empty() == false );
        io.value( _count );
        (void)io.check( _count > 0 );
    }

    void AbilitySaveState::serializeState( StateIO& io )
    {
        io.header( kStateTag, kStateVersion );
        AbilitySaveStateInternal::serializeFinite( io, _time );
        io.list( _listBaseValue, AbilitySaveStateInternal::kValueMinBytes, [&io]( SavedAttributeValue& value )
        { io.value( value ); } );
        io.list( _listEffect, AbilitySaveStateInternal::kEffectMinBytes, [&io]( SavedAbilityEffect& effect )
        { io.value( effect ); } );
        io.list( _listLooseTag, AbilitySaveStateInternal::kTagMinBytes, [&io]( SavedAbilityTag& tag )
        { io.value( tag ); } );
    }
} // namespace sw
