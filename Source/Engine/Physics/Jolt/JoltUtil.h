/**
 * @file JoltUtil.h
 * @brief 엔진 수학 타입 ↔ Jolt 수학 타입 변환과, Jolt 참조 객체를 Jolt 할당자로 만드는 도우미입니다(`Physics/Jolt` 안에서만).
 * @details 쿼터니언은 두 쪽 모두 (x, y, z, w) 해밀턴 곱이라 성분을 그대로 옮깁니다. 행렬은 넘기지 않습니다(엔진은 행 벡터, Jolt 는 열 벡터).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include <Jolt/Jolt.h>
#include <Jolt/Math/Quat.h>
#include <Jolt/Math/Vec3.h>

namespace sw
{
    /** @brief 변환 도우미입니다. */
    struct JoltUtil
    {
        static JPH::Vec3  toJolt( const float3& value ) { return JPH::Vec3{ value._x, value._y, value._z }; }
        static JPH::Quat  toJolt( const quaternion& value ) { return JPH::Quat{ value._x, value._y, value._z, value._w }.Normalized(); }
        static float3     toEngine( JPH::Vec3Arg value ) { return float3{ value.GetX(), value.GetY(), value.GetZ() }; }
        static quaternion toEngine( JPH::QuatArg value ) { return quaternion{ value.GetX(), value.GetY(), value.GetZ(), value.GetW() }; }

        /**
         * @brief Jolt 참조 객체(`RefTarget` — 마지막 참조가 `delete this` 한다)를 그 타입의 `operator new` 로 잡은 자리에 만듭니다.
         * @details Jolt 타입은 `JPH_OVERRIDE_NEW_DELETE` 로 할당을 `JPH::Allocate`(엔진 할당자로 돌려 둔 것)로 보냅니다. 같은 짝으로 잡아야
         *          `delete this` 가 맞는 해제를 부릅니다 — 정렬이 기본보다 크면 정렬 할당 짝입니다(컴파일러가 `delete` 에서 고르는 것과 같은 기준).
         */
        template <typename T, typename... Args>
        static T* createObject( Args&&... args )
        {
            void* pMemory = nullptr;
            if constexpr ( alignof( T ) > __STDCPP_DEFAULT_NEW_ALIGNMENT__ )
                pMemory = T::operator new( sizeof( T ), std::align_val_t{ alignof( T ) } );
            else
                pMemory = T::operator new( sizeof( T ) );
            return sw_placement_new( pMemory ) T( std::forward<Args>( args )... );
        }

        /** @brief `createObject` 로 만든, 참조를 세지 않는 객체를 없앱니다(같은 `operator delete` 짝). */
        template <typename T>
        static void destroyObject( T* pObject )
        {
            if ( pObject == nullptr )
                return;
            pObject->~T();
            if constexpr ( alignof( T ) > __STDCPP_DEFAULT_NEW_ALIGNMENT__ )
                T::operator delete( pObject, std::align_val_t{ alignof( T ) } );
            else
                T::operator delete( pObject );
        }
    };
} // namespace sw
