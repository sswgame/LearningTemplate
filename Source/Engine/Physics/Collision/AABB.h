/**
 * @file AABB.h
 * @brief 가벼운 AABB 와 충돌 레이어를 고려한 겹침 질의입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Physics/Collision/CollisionLayers.h"

namespace sw
{
    /** @brief 헤더 전용 POD 입니다(인라인 메서드의 dllimport 를 피하려고 SW_API 를 붙이지 않습니다). */
    struct AABB
    {
        float3 _min{ 0.0f, 0.0f, 0.0f };
        float3 _max{ 0.0f, 0.0f, 0.0f };

        static constexpr AABB empty() noexcept
        {
            return AABB{
                float3{MathUtil::kMaxFloat, MathUtil::kMaxFloat, MathUtil::kMaxFloat},
                float3{MathUtil::kMinFloat, MathUtil::kMinFloat, MathUtil::kMinFloat}
            };
        }

        static constexpr AABB infinite() noexcept
        {
            return AABB{
                float3{MathUtil::kMinFloat, MathUtil::kMinFloat, MathUtil::kMinFloat},
                float3{MathUtil::kMaxFloat, MathUtil::kMaxFloat, MathUtil::kMaxFloat}
            };
        }

        static constexpr AABB zero() noexcept
        {
            return AABB{
                float3{0.0f, 0.0f, 0.0f},
                float3{0.0f, 0.0f, 0.0f}
            };
        }

        /** @brief min≤max 이면 유효합니다. */
        bool isValid() const noexcept { return _min._x <= _max._x && _min._y <= _max._y && _min._z <= _max._z; }
        /**
         * @brief 이 상자(로컬)를 행렬(행 벡터, `로컬 × 월드`)로 옮긴 것을 덮는 축 정렬 상자입니다.
         * @details 축마다 회전 · 스케일 성분의 절댓값으로 반 크기를 모은다(언리얼 `FBox::TransformBy`). 모서리 여덟 개를 옮기는 것과 같은 결과다.
         */
        AABB transformedBy( const float4x4& m ) const noexcept
        {
            const float3 center = float3::transform( float3{ ( _min._x + _max._x ) * 0.5f, ( _min._y + _max._y ) * 0.5f, ( _min._z + _max._z ) * 0.5f }, m );
            const float3 half{ ( _max._x - _min._x ) * 0.5f, ( _max._y - _min._y ) * 0.5f, ( _max._z - _min._z ) * 0.5f };
            const float3 extent{ MathUtil::abs( m._11 ) * half._x + MathUtil::abs( m._21 ) * half._y + MathUtil::abs( m._31 ) * half._z,
                                 MathUtil::abs( m._12 ) * half._x + MathUtil::abs( m._22 ) * half._y + MathUtil::abs( m._32 ) * half._z,
                                 MathUtil::abs( m._13 ) * half._x + MathUtil::abs( m._23 ) * half._y + MathUtil::abs( m._33 ) * half._z };
            return AABB{
                float3{center._x - extent._x, center._y - extent._y, center._z - extent._z},
                float3{center._x + extent._x, center._y + extent._y, center._z + extent._z}
            };
        }
        /** @brief 두 상자를 덮는 상자입니다. */
        AABB unionWith( const AABB& other ) const noexcept
        {
            return AABB{
                float3{MathUtil::min( _min._x, other._min._x ), MathUtil::min( _min._y, other._min._y ), MathUtil::min( _min._z, other._min._z )},
                float3{MathUtil::max( _max._x, other._max._x ), MathUtil::max( _max._y, other._max._y ), MathUtil::max( _max._z, other._max._z )}
            };
        }

        /** @brief 점이 AABB 안에 있는지 반환합니다. */
        bool contains( const float3& point ) const noexcept
        {
            return _min._x <= point._x && point._x <= _max._x &&
                   _min._y <= point._y && point._y <= _max._y &&
                   _min._z <= point._z && point._z <= _max._z;
        }

        /** @brief 다른 AABB 와 겹치는지 반환합니다. */
        bool intersects( const AABB& other ) const noexcept
        {
            return _min._x <= other._max._x && other._min._x <= _max._x &&
                   _min._y <= other._max._y && other._min._y <= _max._y &&
                   _min._z <= other._max._z && other._min._z <= _max._z;
        }

        /** @brief 다른 AABB 를 완전히 포함하는지 반환합니다. */
        bool contains( const AABB& other ) const noexcept
        {
            return _min._x <= other._min._x && other._max._x <= _max._x &&
                   _min._y <= other._min._y && other._max._y <= _max._y &&
                   _min._z <= other._min._z && other._max._z <= _max._z;
        }

        float3 getCenter() const noexcept { return isValid() ? ( _min + _max ) * 0.5f : float3{ 0.0f, 0.0f, 0.0f }; }
        float3 getExtents() const noexcept { return isValid() ? ( _max - _min ) * 0.5f : float3{ 0.0f, 0.0f, 0.0f }; }
    };

    /** @brief AABB 가 겹치고 CollisionLayers 가 그 쌍을 허용하면 true 입니다. */
    inline bool queryOverlaps( const AABB& a, uint8 layerA, const AABB& b, uint8 layerB,
                               const CollisionLayers& layers )
    {
        if ( layers.shouldCollide( layerA, layerB ) == false )
            return false;
        return a.intersects( b );
    }
} // namespace sw
