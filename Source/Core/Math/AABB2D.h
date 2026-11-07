/**
 * @file AABB2D.h
 * @brief 2 차원 축 정렬 경계 상자입니다(3 차원 `AABB` 의 짝).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /**
     * @struct AABB2D
     * @brief 2차원 축 정렬 경계 상자(Axis-Aligned Bounding Box)입니다.
     */
    struct AABB2D
    {
        /** @note 3차원 `AABB` 와 같은 모양입니다. 그쪽도 `float3 _min/_max` 를 듭니다. */
        float2 _min{ 0.0f, 0.0f };
        float2 _max{ 0.0f, 0.0f };

        static constexpr AABB2D empty() noexcept
        {
            return AABB2D{
                float2{MathUtil::kMaxFloat, MathUtil::kMaxFloat},
                float2{MathUtil::kMinFloat, MathUtil::kMinFloat}
            };
        }

        static constexpr AABB2D infinite() noexcept
        {
            return AABB2D{
                float2{MathUtil::kMinFloat, MathUtil::kMinFloat},
                float2{MathUtil::kMaxFloat, MathUtil::kMaxFloat}
            };
        }

        static constexpr AABB2D zero() noexcept
        {
            return AABB2D{
                float2{0.0f, 0.0f},
                float2{0.0f, 0.0f}
            };
        }

        bool isValid() const noexcept
        {
            return _min._x <= _max._x && _min._y <= _max._y;
        }

        bool contains( float32 pointX, float32 pointY ) const noexcept
        {
            return _min._x <= pointX && pointX <= _max._x && _min._y <= pointY && pointY <= _max._y;
        }

        bool intersects( const AABB2D& other ) const noexcept
        {
            return _min._x <= other._max._x && other._min._x <= _max._x &&
                   _min._y <= other._max._y && other._min._y <= _max._y;
        }

        bool contains( const AABB2D& other ) const noexcept
        {
            return _min._x <= other._min._x && other._max._x <= _max._x && _min._y <= other._min._y && other._max._y <= _max._y;
        }

        float32 getWidth() const noexcept { return _max._x - _min._x; }
        float32 getHeight() const noexcept { return _max._y - _min._y; }
        float32 getCenterX() const noexcept { return ( _min._x + _max._x ) * 0.5f; }
        float32 getCenterY() const noexcept { return ( _min._y + _max._y ) * 0.5f; }
    };
} // namespace sw
