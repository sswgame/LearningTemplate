#include "pch.h"

#include "Engine/UI/Layout/UIScale.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

SW_GLOBAL_VARIABLE( float32, gv_uiSafeZone, 0.0f, "UI 안전 영역 — 각 변을 화면 크기의 이 비율(0..0.1)만큼 안쪽으로 민다(TV 오버스캔 · 노치 흉내)" );

namespace sw
{
    namespace
    {
        struct UIScaleInternal
        {
            /** @brief 안전 영역 비율의 상한입니다(각 변 10 %이며, 언리얼 r.DebugSafeZone 과 같은 범위입니다). */
            static constexpr float32 kMaxSafeZoneRatio = 0.1f;
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool UIScaleSettings::loadFromAsset( const AssetID& assetID )
    {
        const string_view assetPath = assetID.view();
        if ( XMLSerializer::loadFromAsset( assetID, this, *StaticType() ) == false )
        {
            SW_LOG_ERROR_ID( "UIScale.UIScaleSettingsCould", "[UI] UI scale settings could not be read or hold unknown keys: %#", assetPath );
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
            case UIScaleRule::MatchWidthOrHeight:
            {
                // 로그 공간 보간(유니티 CanvasScaler 와 같다): 비율이 달라도 너비 쪽과 높이 쪽 확대가 대칭이다.
                // 양 끝은 로그와 지수를 쓰지 않는다. 프레임마다 한 번 불리므로 중간 값도 로그 둘과 지수 하나뿐이다.
                const float32 match       = MathUtil::clamp( _matchWidthOrHeight, 0.0f, 1.0f );
                const float32 widthScale  = physicalSize._x / MathUtil::max( 1.0f, _referenceWidth );
                const float32 heightScale = physicalSize._y / MathUtil::max( 1.0f, _referenceHeight );
                const bool    bWidthOnly  = match <= 0.0f;
                const bool    bHeightOnly = match >= 1.0f;
                if ( bWidthOnly )
                {
                    scale = widthScale;
                }
                else if ( bHeightOnly )
                {
                    scale = heightScale;
                }
                else
                {
                    constexpr float32 kTiny = 1.0e-6f; // 0 크기 창에서 로그가 무한대가 되지 않게 한다
                    scale                   = MathUtil::exp( ( 1.0f - match ) * MathUtil::log( MathUtil::max( widthScale, kTiny ) ) + match * MathUtil::log( MathUtil::max( heightScale, kTiny ) ) );
                }
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

    float32 UIScaleSettings::computeScaledFontSize( float32 fontSize, float32 textScale )
    {
        const float32 scaled = fontSize * ( textScale > 0.0f ? textScale : 1.0f );
        return MathUtil::max( scaled, MathUtil::min( fontSize, kMinScaledFontSize ) );
    }

    optional<ResourceTagSet> UIScaleSettings::makeScreenTags( const UIViewport& viewport ) const
    {
        if ( viewport._size._x <= 0.0f || viewport._size._y <= 0.0f )
            return nullopt;
        const float32  widthDp = viewport._physicalSize._x / ( viewport._contentScale > 0.0f ? viewport._contentScale : 1.0f );
        ResourceTagSet tags;
        tags.add( widthDp < _compactMaxWidth ? "screen.compact" : "screen.regular" );
        tags.add( viewport._size._y > viewport._size._x ? "orientation.portrait" : "orientation.landscape" );
        return tags;
    }

    UIViewport UIScaleSettings::makeViewport( const UIScaleSettings& settings, const float2& physicalSize, float32 userScale, float32 contentScale, const float4& platformSafeInsets,
                                              float32 safeZoneRatio )
    {
        float32 scale = settings.computeResolutionScale( physicalSize ) * ( userScale > 0.0f ? userScale : 1.0f );
        if ( settings._bApplyContentScale && contentScale > 0.0f )
            scale *= contentScale;

        const float32 ratio = MathUtil::clamp( safeZoneRatio, 0.0f, UIScaleInternal::kMaxSafeZoneRatio );
        UIViewport    viewport{};
        viewport._physicalSize    = physicalSize;
        viewport._uiScale         = scale;
        viewport._size            = float2{ physicalSize._x / scale, physicalSize._y / scale };
        viewport._touchTargetSize = settings._minTouchTargetSize;
        viewport._contentScale    = contentScale > 0.0f ? contentScale : 1.0f;
        const float32 debugX      = physicalSize._x * ratio;
        const float32 debugY      = physicalSize._y * ratio;
        // 변마다 플랫폼 여백(노치 · 시스템 바)과 흉내 중 큰 것을 UI 단위로 — 노치는 한 변에만 있고 흉내는 네 변 대칭이다.
        viewport._safeInsets = float4{ MathUtil::max( platformSafeInsets._x, debugX ) / scale, MathUtil::max( platformSafeInsets._y, debugY ) / scale,
                                       MathUtil::max( platformSafeInsets._z, debugX ) / scale, MathUtil::max( platformSafeInsets._w, debugY ) / scale };
        return viewport;
    }

    /** @brief UI 배율 설정 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 파일을 읽는다). */
    SW_ASSET_TYPE( UIScale, AssetTypeDef::makeDataFile( "uiscale", AssetPathRule{ {}, {}, "uiscale.xml" }, AssetTypeLoader::of<UIScaleSettings>() ) );
} // namespace sw
