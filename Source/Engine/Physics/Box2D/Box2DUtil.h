/**
 * @file Box2DUtil.h
 * @brief 엔진 수학 타입 ↔ Box2D 타입 변환과 id 묶기 도우미입니다(`Physics/Box2D` 안에서만).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Physics/PhysicsTypes.h"

#include <box2d/box2d.h>

namespace sw
{
    /** @brief 변환 도우미입니다. */
    struct Box2DUtil
    {
        static b2Vec2 toBox2D( const float2& value ) { return b2Vec2{ value._x, value._y }; }
        static float2 toEngine( b2Vec2 value ) { return float2{ value.x, value.y }; }

        /** @brief 바디 핸들을 Box2D 사용자 데이터(포인터 자리)에 싣습니다. 가리키는 것이 아니라 값입니다. */
        static void*             toUserData( PhysicsBodyHandle body ) { return reinterpret_cast<void*>( static_cast<uintptr_t>( body.packed() ) ); }
        static PhysicsBodyHandle fromUserData( void* pUserData ) { return PhysicsBodyHandle::fromPacked( static_cast<uint64>( reinterpret_cast<uintptr_t>( pUserData ) ) ); }

        /** @brief 셰이프 id 를 표의 키로 묶습니다(색인 | 세대 << 32). 같은 월드 안에서만 씁니다. */
        static uint64 makeShapeKey( b2ShapeId shapeId )
        {
            return static_cast<uint64>( static_cast<uint32>( shapeId.index1 ) ) | ( static_cast<uint64>( shapeId.generation ) << 32 );
        }
    };
} // namespace sw
