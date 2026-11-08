#include "pch.h"

#include "Engine/Graphics/View/PostProcessValues.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/PropertyInfo.h"
#include "Engine/Reflection/PropertyRoleUtil.h"
#include "Engine/Reflection/TypeInfo.h"

namespace sw
{
    PostProcessOverrides::PostProcessOverrides()
        : _bExposureCompensation{ SW_FALSE }
        , _bBloomScale{ SW_FALSE }
        , _bVignetteIntensity{ SW_FALSE }
        , _bSaturation{ SW_FALSE }
        , _bContrast{ SW_FALSE }
        , _bLetterboxAspectRatio{ SW_FALSE }
        , _bAutoExposureMinEv{ SW_FALSE }
        , _bAutoExposureMaxEv{ SW_FALSE }
        , _bAutoExposureSpeedUp{ SW_FALSE }
        , _bAutoExposureSpeedDown{ SW_FALSE }
        , _bWhiteTemperature{ SW_FALSE }
        , _bWhiteTint{ SW_FALSE }
        , _bColorSaturation{ SW_FALSE }
        , _bColorContrast{ SW_FALSE }
        , _bColorGamma{ SW_FALSE }
        , _bColorGain{ SW_FALSE }
        , _bColorOffset{ SW_FALSE }
        , _bShadowsSaturation{ SW_FALSE }
        , _bShadowsContrast{ SW_FALSE }
        , _bShadowsGamma{ SW_FALSE }
        , _bShadowsGain{ SW_FALSE }
        , _bShadowsOffset{ SW_FALSE }
        , _bMidtonesSaturation{ SW_FALSE }
        , _bMidtonesContrast{ SW_FALSE }
        , _bMidtonesGamma{ SW_FALSE }
        , _bMidtonesGain{ SW_FALSE }
        , _bMidtonesOffset{ SW_FALSE }
        , _bHighlightsSaturation{ SW_FALSE }
        , _bHighlightsContrast{ SW_FALSE }
        , _bHighlightsGamma{ SW_FALSE }
        , _bHighlightsGain{ SW_FALSE }
        , _bHighlightsOffset{ SW_FALSE }
        , _bShadowsMax{ SW_FALSE }
        , _bHighlightsMin{ SW_FALSE }
        , _bColorGradingLut{ SW_FALSE }
        , _bColorGradingLutIntensity{ SW_FALSE }
        , _bChromaticAberrationIntensity{ SW_FALSE }
        , _bChromaticAberrationStart{ SW_FALSE }
        , _bLensDistortion{ SW_FALSE }
        , _bSharpen{ SW_FALSE }
        , _bFilmGrainIntensity{ SW_FALSE }
        , _bFilmGrainSize{ SW_FALSE }
    {
    }

    PostProcessOverrides PostProcessOverrides::makeAll()
    {
        PostProcessOverrides overrides;
        for ( const PropertyInfo& prop : PostProcessOverrides::StaticType()->getPropertiesWithBase() )
        {
            prop.setValue<bool>( &overrides, true );
        }
        return overrides;
    }

    PostProcessValues PostProcessValues::blend( const PostProcessValues& from, const PostProcessValues& to, float32 weight )
    {
        PostProcessValues values = from;
        blendOverrides( values, to, PostProcessOverrides::makeAll(), weight );
        return values;
    }

    void PostProcessValues::blendOverrides( PostProcessValues& inoutValues, const PostProcessValues& target, const PostProcessOverrides& overrides, float32 weight )
    {
        // 값 칸과 표시 칸은 같은 차례다(단위 테스트 PostProcessVolumeTest.OverrideFlagsPairEveryValue 가 이름으로 대조한다). 칸을 더해도 이 함수는 그대로다.
        const vector<PropertyInfo>& listValue    = PostProcessValues::StaticType()->getPropertiesWithBase();
        const vector<PropertyInfo>& listOverride = PostProcessOverrides::StaticType()->getPropertiesWithBase();
        SW_ASSERT( listValue.size() == listOverride.size() );
        const float32 alpha = MathUtil::saturate( weight );
        const size_t  count = MathUtil::min( listValue.size(), listOverride.size() );
        for ( size_t index = 0; index < count; ++index )
        {
            if ( listOverride[index].getValue<bool>( &overrides ) == false )
                continue;
            const PropertyInfo& prop   = listValue[index];
            void*               pValue = prop.getRawPtr( static_cast<void*>( &inoutValues ) );
            // 레터박스 0 은 "없음" 이라 0 과 비율 사이를 섞으면 막대가 화면 밖에서 들어온다 — 양쪽에 있을 때만 섞고 아니면 투영처럼 절반에서 바꾼다.
            if ( pValue == &inoutValues._letterboxAspectRatio )
            {
                const bool bBoth                  = inoutValues._letterboxAspectRatio > 0.0f && target._letterboxAspectRatio > 0.0f;
                inoutValues._letterboxAspectRatio = bBoth ? MathUtil::lerp( inoutValues._letterboxAspectRatio, target._letterboxAspectRatio, alpha )
                                                          : ( alpha < 0.5f ? inoutValues._letterboxAspectRatio : target._letterboxAspectRatio );
                continue;
            }
            // 경로는 섞을 수 없다 — 절반에서 바꾼다(언리얼은 LUT 둘을 무게로 섞은 LUT 를 굽지만, 여기서는 한 장만 건다).
            // shortcut: 겹친 볼륨의 LUT 사이는 절반에서 넘어간다, 두 LUT 를 섞는 전환이 필요해지면 LUT 를 무게로 굽는 패스를 더한다.
            if ( pValue == &inoutValues._colorGradingLut )
            {
                if ( alpha >= 0.5f )
                    inoutValues._colorGradingLut = target._colorGradingLut;
                continue;
            }
            // 섞는 함수는 from 을 값으로 읽은 뒤 쓴다 — from 과 결과가 같은 주소여도 된다.
            const bool bBlended = PropertyRoleUtil::applyInterpolated( prop, MutableObjectAddress( &inoutValues ), ValueAddress( pValue ), ValueAddress( prop.getRawPtr( static_cast<const void*>( &target ) ) ), alpha );
            SW_ASSERT( bBlended ); // 섞을 수 없는 칸은 위에서 주소로 골라 따로 다룬다 — 새 칸 형식이면 여기에 줄을 더한다
            (void)bBlended;
        }
    }
} // namespace sw
