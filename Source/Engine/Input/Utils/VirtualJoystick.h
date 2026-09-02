/**
 * @file VirtualJoystick.h
 * @brief 마우스 드래그 / 터치 스크린 기반 가상 조이스틱 2D 축 계산 유틸리티
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /**
     * @struct VirtualJoystick
     * @brief 앵커 중심점, 현재 터치/마우스 위치, 반경 및 데드존을 바탕으로 정규화된 2D 벡터 산출
     */
    struct VirtualJoystick final
    {
        float2  _anchorPos{ 0.0f, 0.0f };
        float32 _radius{ 64.0f };
        float32 _deadzone{ 0.1f };
        float32 _outerDeadzone{ 1.0f };

        constexpr VirtualJoystick() noexcept = default;
        constexpr VirtualJoystick( const float2 anchor, const float32 radius, const float32 deadzone = 0.1f, const float32 outerDeadzone = 1.0f ) noexcept
            : _anchorPos{ anchor }
            , _radius{ radius > 1e-4f ? radius : 64.0f }
            , _deadzone{ deadzone }
            , _outerDeadzone{ outerDeadzone }
        {
        }

        /** @brief 현재 터치/마우스 위치(currentPos)를 전달받아 [-1.0, 1.0] 범위의 정규화된 2D 축 벡터를 산출합니다. */
        float2 calculateVector( const float2 currentPos ) const noexcept
        {
            return calculateVector( _anchorPos, currentPos, _radius, _deadzone, _outerDeadzone );
        }

        /** @brief 정적 헬퍼 함수로 2D 가상 스틱 축 벡터를 계산합니다. */
        static float2 calculateVector( const float2 anchor, const float2 currentPos, const float32 radius, const float32 deadzone = 0.1f, const float32 outerDeadzone = 1.0f ) noexcept
        {
            const float32 dx     = currentPos._x - anchor._x;
            const float32 dy     = currentPos._y - anchor._y;
            const float32 distSq = dx * dx + dy * dy;
            const float32 dist   = MathUtil::sqrt( distSq );

            if ( dist < 1e-4f )
                return float2{ 0.0f, 0.0f };

            const float32 r              = radius > 1e-4f ? radius : 64.0f;
            const float32 normalizedDist = dist / r;
            if ( normalizedDist <= deadzone )
                return float2{ 0.0f, 0.0f };

            const float32 maxRadius   = ( outerDeadzone > deadzone ) ? outerDeadzone : 1.0f;
            const float32 safeSpan    = ( maxRadius - deadzone ) > 1e-4f ? ( maxRadius - deadzone ) : 1.0f;
            const float32 clampedDist = MathUtil::clamp( ( normalizedDist - deadzone ) / safeSpan, 0.0f, 1.0f );

            const float32 dirX = dx / dist;
            const float32 dirY = dy / dist;
            return float2{ dirX * clampedDist, dirY * clampedDist };
        }
    };
} // namespace sw
