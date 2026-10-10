#include "pch.h"

#include "Engine/UI/Layout/UIScale.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/XMLSerializer.h"

SW_TEST_GLOBAL_VARIABLE( float32, gv_uiDebugSafeZone, 0.0f, "UI 안전 영역 흉내 — 각 변을 화면 크기의 이 비율(0..0.1)만큼 안쪽으로 민다" );

namespace sw
{
    SW_LOG_CALLER( "UIScale" );

    namespace
    {
        struct UIScaleInternal
        {
            /** @brief 안전 영역 비율의 위 한계입니다(각 변 10 % — 언리얼 r.DebugSafeZone 과 같은 범위). */
            static constexpr float32 kMaxSafeZoneRatio = 0.1f;
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool UIScaleSettings::loadFromResource( string_view resourcePath )
    {
        if ( XMLSerializer::loadFile( resourcePath, this, *StaticType() ) == false )
        {
            SW_LOG_ERROR( "[UI] UI scale settings could not be read or hold unknown keys: %#", resourcePath );
            return false;
        }
        return true;
    }

    float32 UIScaleSettings::computeResolutionScale( const float2& physicalSize ) const
    {
        float32 scale = 1.0f;
        switch ( _rule )
        {
            case UIScaleRule::ShortestSide:
            {
                scale = MathUtil::min( physicalSize._x, physicalSize._y ) / MathUtil::max( 1.0f, MathUtil::min( _referenceWidth, _referenceHeight ) );
                break;
            }
            case UIScaleRule::Width:
            {
                scale = physicalSize._x / MathUtil::max( 1.0f, _referenceWidth );
                break;
            }
            case UIScaleRule::Height:
            {
                scale = physicalSize._y / MathUtil::max( 1.0f, _referenceHeight );
                break;
            }
            case UIScaleRule::Fixed:
            {
                scale = 1.0f;
                break;
            }
        }
        return MathUtil::clamp( scale, _minScale, MathUtil::max( _minScale, _maxScale ) );
    }

    float32 UIScaleUtil::computeScaledFontSize( float32 fontSize, float32 textScale )
    {
        const float32 scaled = fontSize * ( textScale > 0.0f ? textScale : 1.0f );
        return MathUtil::max( scaled, MathUtil::min( fontSize, kMinScaledFontSize ) );
    }

    UIViewport UIScaleUtil::makeViewport( const UIScaleSettings& settings, const float2& physicalSize, float32 userScale, float32 contentScale,
                                          float32 safeZoneRatio )
    {
        float32 scale = settings.computeResolutionScale( physicalSize ) * ( userScale > 0.0f ? userScale : 1.0f );
        if ( settings._bApplyContentScale && contentScale > 0.0f )
            scale *= contentScale;

        const float32 ratio = MathUtil::clamp( safeZoneRatio, 0.0f, UIScaleInternal::kMaxSafeZoneRatio );
        UIViewport    viewport{};
        viewport._physicalSize = physicalSize;
        viewport._uiScale      = scale;
        viewport._size         = float2{ physicalSize._x / scale, physicalSize._y / scale };
        const float32 insetX   = physicalSize._x * ratio / scale;
        const float32 insetY   = physicalSize._y * ratio / scale;
        viewport._safeInsets   = float4{ insetX, insetY, insetX, insetY };
        return viewport;
    }
} // namespace sw
