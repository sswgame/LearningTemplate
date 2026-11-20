#include "pch.h"

#include "Engine/Graphics/View/PostProcessValues.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Material/Material.h"
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
        , _bColorGradingLUT{ SW_FALSE }
        , _bColorGradingLUTIntensity{ SW_FALSE }
        , _bChromaticAberrationIntensity{ SW_FALSE }
        , _bChromaticAberrationStart{ SW_FALSE }
        , _bLensDistortion{ SW_FALSE }
        , _bSharpen{ SW_FALSE }
        , _bFilmGrainIntensity{ SW_FALSE }
        , _bFilmGrainSize{ SW_FALSE }
        , _bBloomDirtMask{ SW_FALSE }
        , _bBloomDirtMaskIntensity{ SW_FALSE }
        , _bLensFlareIntensity{ SW_FALSE }
        , _bLensFlareThreshold{ SW_FALSE }
        , _bLensFlareTint{ SW_FALSE }
        , _bLensFlareGhostSpacing{ SW_FALSE }
        , _bLensFlareHaloWidth{ SW_FALSE }
        , _bScreenSpaceReflectionIntensity{ SW_FALSE }
        , _bScreenSpaceReflectionMaxDistance{ SW_FALSE }
        , _bScreenSpaceReflectionThickness{ SW_FALSE }
        , _bScreenSpaceReflectionMaxRoughness{ SW_FALSE }
        , _bListBlendable{ SW_FALSE }
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

    PostProcessBlendable::PostProcessBlendable() = default;

    bool PostProcessBlendable::isSameSlot( const PostProcessBlendable& other ) const
    {
        // 빈 참조끼리는 같지 않다 — 빈 항목은 섞지 않는다.
        return _location == other._location && _materialPath.empty() == false && _materialPath == other._materialPath;
    }

    bool PostProcessBlendable::hasActive( const vector<PostProcessBlendable>& listBlendable, PostProcessBlendableLocation location )
    {
        return VectorUtil::containsIf( listBlendable, [location]( const PostProcessBlendable& blendable )
        { return blendable._location == location && blendable.isActive(); } );
    }

    void PostProcessBlendable::blendList( vector<PostProcessBlendable>& inoutList, const vector<PostProcessBlendable>& target, float32 alpha, bool bFadeMissing )
    {
        const float32 clamped = MathUtil::saturate( alpha );
        // 아래 목록에만 있는 항목 — 볼륨은 그대로 두고(더하기만), 포즈 블렌드는 넘어가는 만큼 줄인다.
        if ( bFadeMissing )
        {
            for ( PostProcessBlendable& entry : inoutList )
            {
                const bool bInTarget = VectorUtil::containsIf( target, [&entry]( const PostProcessBlendable& other )
                { return entry.isSameSlot( other ); } );
                if ( bInTarget == false )
                    entry._weight *= 1.0f - clamped;
            }
        }
        // 위 목록의 항목 — 같은 (경로, 자리) 는 무게를 섞고, 없으면 무게 0 에서 섞여 들어온다.
        for ( const PostProcessBlendable& other : target )
        {
            if ( other._materialPath.empty() )
                continue;
            PostProcessBlendable* pExisting = nullptr;
            for ( PostProcessBlendable& entry : inoutList )
            {
                if ( entry.isSameSlot( other ) )
                {
                    pExisting = &entry;
                    break;
                }
            }
            if ( pExisting != nullptr )
            {
                pExisting->_weight = MathUtil::lerp( pExisting->_weight, other._weight, clamped );
                continue;
            }
            PostProcessBlendable added = other;
            added._weight              = other._weight * clamped;
            inoutList.push_back( std::move( added ) );
        }
        // 무게가 없는 항목은 지운다 — 그리지 않는 항목이 프레임마다 쌓이지 않게.
        VectorUtil::removeIf( inoutList, []( const PostProcessBlendable& entry )
        { return entry._weight <= 0.0f; } );
    }

    PostProcessValues PostProcessValues::blend( const PostProcessValues& from, const PostProcessValues& to, float32 weight )
    {
        PostProcessValues values = from;
        blendOverrides( values, to, PostProcessOverrides::makeAll(), weight );
        // 포즈 블렌드는 다른 카메라로 넘어간다 — 앞 카메라에만 있던 머티리얼도 넘어가는 만큼 사라진다(볼륨 섞기는 더하기만 한다).
        values._listBlendable = from._listBlendable;
        PostProcessBlendable::blendList( values._listBlendable, to._listBlendable, weight, true );
        return values;
    }

    void PostProcessValues::blendColorGradingLUT( PostProcessValues& inoutValues, const DataPath& target, float32 alpha )
    {
        // 읽지 않은 칸(빈 경로)은 항등 LUT 다 — 빈 칸끼리도 같은 LUT 다.
        const auto isSameLUT = []( const DataPath& lhs, const DataPath& rhs )
        { return lhs.isSet() == rhs.isSet() && ( lhs.isSet() == false || lhs == string_view( rhs.get() ) ); };
        constexpr float32 kMinLUTWeight = 1.0e-4f; // 이보다 가벼운 LUT 는 버린다(읽기 둘을 아낀다)
        const float32     a             = MathUtil::saturate( alpha );
        const float32     blend         = MathUtil::saturate( inoutValues._colorGradingLUTBlendWeight );
        const bool        bHasBlend     = blend > 0.0f;
        // 섞은 뒤의 세 무게 — 첫 LUT · 둘째 LUT · 목표(합 1).
        float32 firstWeight  = ( 1.0f - blend ) * ( 1.0f - a );
        float32 secondWeight = blend * ( 1.0f - a );
        if ( isSameLUT( target, inoutValues._colorGradingLUT ) )
            firstWeight += a;
        else if ( bHasBlend && isSameLUT( target, inoutValues._colorGradingLUTBlend ) )
            secondWeight += a;
        else if ( bHasBlend == false )
        {
            inoutValues._colorGradingLUTBlend = target;
            secondWeight                      = a;
        }
        else if ( a > MathUtil::min( firstWeight, secondWeight ) )
        {
            // 셋째 LUT — 가장 가벼운 것을 버린다(목표가 가장 가벼우면 짝 그대로 다시 정규화).
            if ( firstWeight < secondWeight )
            {
                inoutValues._colorGradingLUT = inoutValues._colorGradingLUTBlend;
                firstWeight                  = secondWeight;
            }
            inoutValues._colorGradingLUTBlend = target;
            secondWeight                      = a;
        }
        const float32 total  = firstWeight + secondWeight;
        float32       weight = total > 0.0f ? secondWeight / total : 0.0f;
        if ( weight >= 1.0f - kMinLUTWeight )
        {
            inoutValues._colorGradingLUT = inoutValues._colorGradingLUTBlend;
            weight                       = 0.0f;
        }
        if ( weight <= kMinLUTWeight )
        {
            inoutValues._colorGradingLUTBlend = DataPath{};
            weight                            = 0.0f;
        }
        inoutValues._colorGradingLUTBlendWeight = weight;
    }

    void PostProcessValues::blendOverrides( PostProcessValues& inoutValues, const PostProcessValues& target, const PostProcessOverrides& overrides, float32 weight )
    {
        // 값 칸과 표시 칸은 같은 차례다(단위 테스트 PostProcessVolumeTest.OverrideFlagsPairEveryValue 가 이름으로 대조한다). 칸을 더해도 이 함수는 그대로다.
        const PropertyListView listValue    = PostProcessValues::StaticType()->getPropertiesWithBase();
        const PropertyListView listOverride = PostProcessOverrides::StaticType()->getPropertiesWithBase();
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
            // 후처리 머티리얼 목록은 항목마다 무게를 섞는다(볼륨은 더하기만 — 아래 목록의 항목은 그대로 남는다).
            if ( pValue == &inoutValues._listBlendable )
            {
                PostProcessBlendable::blendList( inoutValues._listBlendable, target._listBlendable, alpha, false );
                continue;
            }
            // 컬러 그레이딩 LUT 는 두 장을 무게로 섞는다(언리얼 CombineLUTs 와 같은 셈 — blendColorGradingLUT).
            if ( pValue == &inoutValues._colorGradingLUT )
            {
                blendColorGradingLUT( inoutValues, target._colorGradingLUT, alpha );
                continue;
            }
            // 먼지 마스크 경로는 섞을 수 없다 — 절반에서 바꾼다(언리얼도 먼지 텍스처는 섞지 않고 바꾼다).
            if ( pValue == &inoutValues._bloomDirtMask )
            {
                if ( alpha >= 0.5f )
                    inoutValues._bloomDirtMask = target._bloomDirtMask;
                continue;
            }
            // 섞는 함수는 from 을 값으로 읽은 뒤 쓴다 — from 과 결과가 같은 주소여도 된다.
            const bool bBlended = PropertyRoleUtil::applyInterpolated( prop, MutableObjectAddress( &inoutValues ), ValueAddress( pValue ), ValueAddress( prop.getRawPtr( static_cast<const void*>( &target ) ) ), alpha );
            SW_ASSERT( bBlended ); // 섞을 수 없는 칸은 위에서 주소로 골라 따로 다룬다 — 새 칸 형식이면 여기에 줄을 더한다
            (void)bBlended;
        }
    }
} // namespace sw
