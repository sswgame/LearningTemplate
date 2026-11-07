#include "pch.h"

#include "Engine/Environment/Surface/SurfaceStamp.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct SurfaceStampInternal
        {
            /** @brief 바닥이 평평한 안쪽의 비율입니다 — 이 반경 비율까지는 가장 깊고, 바깥 가장자리까지 부드럽게 올라온다(발바닥 · 바퀴 자국의 평평한 바닥). */
            static constexpr float32 kFlatBottomRatio = 0.55f;
            /** @brief 깔때기 홈이 바깥으로 갈수록 감기는 정도(라디안) — 홈이 곧은 바큇살이 아니라 나선이 된다. */
            static constexpr float32 kFunnelTwist = 6.0f;
            /** @brief 깔때기 홈의 깊이 비율 — 홈 사이 둔덕은 이만큼 덜 꺼진다. */
            static constexpr float32 kFunnelGrooveShare = 0.3f;

            /** @brief 정규 반경 @p radius01(0 = 중심, 1 = 가장자리)의 꺼짐 비율(0..1)입니다. */
            static float32 computeBowl( float32 radius01 ) { return 1.0f - MathUtil::smoothstep( kFlatBottomRatio, 1.0f, radius01 ); }

            /** @brief 가장자리 바깥 @p outside(m)의 테두리 높이(m)입니다 — 폭 안에서 0 → 높이 → 0 으로 솟는다. */
            static float32 computeRim( const SurfaceStampDef& def, float32 outside )
            {
                if ( def._rimHeight <= 0.0f || def._rimWidth <= 0.0f || outside < 0.0f || outside >= def._rimWidth )
                    return 0.0f;
                const float32 ratio = outside / def._rimWidth;
                return def._rimHeight * 4.0f * ratio * ( 1.0f - ratio );
            }
        };
    } // namespace

    bool SurfaceStampDef::validate( string_view path, string_view owner ) const
    {
        if ( isSet() == false )
            return true;
        bool bOk = true;
        if ( _size._x <= 0.0f || ( _shape != SurfaceStampShape::Funnel && _size._y <= 0.0f ) )
        {
            SW_LOG_ERROR_ID( "Surface.StampSizeMissing", "%#: stamp of '%#' sinks or raises the ground but has no _size", path, owner );
            bOk = false;
        }
        // 메워지지 않는 도장은 영영 남는다 — 빠뜨린 칸이다(끝없이 남길 자국은 높이장 편집이지 변형 층이 아니다).
        if ( _fillSpeed <= 0.0f )
        {
            SW_LOG_ERROR_ID( "Surface.StampFillMissing", "%#: stamp of '%#' has no _fillSpeed and would never fill back", path, owner );
            bOk = false;
        }
        if ( ( _rimHeight > 0.0f ) != ( _rimWidth > 0.0f ) )
        {
            SW_LOG_ERROR_ID( "Surface.StampRimHalf", "%#: stamp of '%#' sets only one of _rimHeight and _rimWidth", path, owner );
            bOk = false;
        }
        return bOk;
    }

    float32 SurfaceStampDef::computeHeight( float32 right, float32 forward, float32 length, float32 phase ) const
    {
        using Internal = SurfaceStampInternal;
        switch ( _shape )
        {
            case SurfaceStampShape::Print:
            {
                const float32 halfWidth  = _size._x * 0.5f;
                const float32 halfLength = _size._y * 0.5f;
                const float32 scaledX    = right / halfWidth;
                const float32 scaledZ    = forward / halfLength;
                const float32 radius01   = MathUtil::sqrt( scaledX * scaledX + scaledZ * scaledZ );
                if ( radius01 < 1.0f )
                    return -_depth * Internal::computeBowl( radius01 );
                // 타원 바깥 거리는 짧은 반지름으로 어림한다 — 테두리 폭이 반지름보다 훨씬 작아 모양 차이가 보이지 않는다.
                return Internal::computeRim( *this, ( radius01 - 1.0f ) * MathUtil::min( halfWidth, halfLength ) );
            }
            case SurfaceStampShape::Trail:
            {
                const float32 halfWidth   = _size._x * 0.5f;
                const float32 halfSegment = ( length > 0.0f ? length : _size._y ) * 0.5f;
                const float32 along       = MathUtil::clamp( forward, -halfSegment, halfSegment );
                const float32 aside       = forward - along;
                const float32 distance    = MathUtil::sqrt( right * right + aside * aside );
                if ( distance < halfWidth )
                    return -_depth * Internal::computeBowl( distance / halfWidth );
                return Internal::computeRim( *this, distance - halfWidth );
            }
            case SurfaceStampShape::Funnel:
            {
                const float32 radius   = _size._x * 0.5f;
                const float32 distance = MathUtil::sqrt( right * right + forward * forward );
                if ( distance >= radius )
                    return Internal::computeRim( *this, distance - radius );
                const float32 radius01 = distance / radius;
                float32       share    = 1.0f;
                if ( _armCount > 0 )
                {
                    const float32 angle  = MathUtil::atan2( right, forward );
                    const float32 groove = 0.5f + 0.5f * MathUtil::cos( static_cast<float32>( _armCount ) * ( angle - phase ) + radius01 * Internal::kFunnelTwist );
                    share                = 1.0f - Internal::kFunnelGrooveShare + Internal::kFunnelGrooveShare * groove;
                }
                return -_depth * ( 1.0f - radius01 ) * share;
            }
        }
        return 0.0f;
    }

    float32 SurfaceStampDef::computeReach( float32 length ) const
    {
        switch ( _shape )
        {
            case SurfaceStampShape::Print:
                return MathUtil::max( _size._x, _size._y ) * 0.5f + _rimWidth;
            case SurfaceStampShape::Trail:
                return ( length > 0.0f ? length : _size._y ) * 0.5f + _size._x * 0.5f + _rimWidth;
            case SurfaceStampShape::Funnel:
                return _size._x * 0.5f + _rimWidth;
        }
        return 0.0f;
    }
} // namespace sw
