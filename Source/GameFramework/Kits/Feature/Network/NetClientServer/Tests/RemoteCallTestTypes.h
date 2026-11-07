/**
 * @file RemoteCallTestTypes.h
 * @brief 반사 함수 호출(`RemoteCall`) 테스트의 대상 형식입니다. 리플렉션 파서가 헤더만 읽으므로 테스트 `.cpp` 밖에 둡니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 서버 쪽 대상 — 신뢰 · 검증 함수, 비신뢰 함수, 원격으로 부를 수 없는 로컬 함수, 서버가 부르는 클라이언트 함수를 하나씩 둡니다. */
    REFLECT()
    struct RemoteCallTestAvatar
    {
        REFLECT_BODY();

        PROPERTY()
        int32 _lastTargetID{ 0 };
        PROPERTY()
        int32 _totalPower{ 0 };
        PROPERTY()
        float32 _movedDistance{ 0.0f };
        PROPERTY()
        int32 _localApplyCount{ 0 };
        PROPERTY()
        int32 _shownHealth{ 0 };

        /** @brief 검증을 지난 공격 — 대상과 힘을 적습니다. */
        FUNCTION( Server, Reliable, Validate )
        void attackTarget( int32 targetID, int32 power )
        {
            _lastTargetID = targetID;
            _totalPower += power;
        }

        /** @brief `attackTarget` 의 검증 짝 — 힘은 0 ~ 50 이어야 합니다(밖이면 조작이다). */
        FUNCTION()
        bool validateAttackTarget( int32 targetID, int32 power ) const { return targetID > 0 && power >= 0 && power <= 50; }

        /** @brief 비신뢰 이동 — 잃어도 되는 호출입니다. */
        FUNCTION( Server )
        void moveStep( float32 distance ) { _movedDistance += distance; }

        /** @brief 원격 역할이 없는 함수 — 서버가 원격으로 받지 않습니다. */
        FUNCTION()
        void applyLocal( int32 amount ) { _localApplyCount += amount; }

        /** @brief 서버가 주인 클라이언트에게 부르는 함수입니다. */
        FUNCTION( Client, Reliable )
        void receiveHealth( int32 health ) { _shownHealth = health; }
    };

    /** @brief `Validate` 인데 짝이 없는 대상 — 등록이 거절해야 합니다. */
    REFLECT()
    struct RemoteCallTestMissingValidator
    {
        REFLECT_BODY();

        PROPERTY()
        int32 _value{ 0 };

        /** @brief 짝 `validateAct` 가 없습니다. */
        FUNCTION( Server, Reliable, Validate )
        void act( int32 value ) { _value = value; }
    };

    /** @brief 글로 싣지 못하는 인자(벡터)를 받는 원격 함수 — 등록이 거절해야 합니다. */
    REFLECT()
    struct RemoteCallTestVectorArgument
    {
        REFLECT_BODY();

        PROPERTY()
        float3 _position{};

        /** @brief 벡터 인자는 원격 인자가 될 수 없습니다. */
        FUNCTION( Server, Reliable )
        void place( float3 position ) { _position = position; }
    };
} // namespace sw
