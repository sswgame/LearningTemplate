/**
 * @file AABB.h
 * @brief 가벼운 AABB 와 충돌 레이어를 고려한 겹침 질의입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Physics/CollisionLayers.h"

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
                float3{MathUtil::MaxFloat, MathUtil::MaxFloat, MathUtil::MaxFloat},
                float3{MathUtil::MinFloat, MathUtil::MinFloat, MathUtil::MinFloat}
            };
        }

        static constexpr AABB infinite() noexcept
        {
            return AABB{
                float3{MathUtil::MinFloat, MathUtil::MinFloat, MathUtil::MinFloat},
                float3{MathUtil::MaxFloat, MathUtil::MaxFloat, MathUtil::MaxFloat}
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
