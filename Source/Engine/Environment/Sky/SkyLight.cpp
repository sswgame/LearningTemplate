#include "pch.h"

#include "Engine/Environment/Sky/SkyLight.h"

#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Config/EngineConfig.h"

namespace sw
{
    namespace
    {
        struct SkyLightInternal
        {
            /// @brief 구 전체의 입체각(sr) — SH 투영의 가중치 합이다.
            static constexpr float32 kSphereSolidAngle = 4.0f * MathUtil::kPi;
        };
    } // namespace

    expected<void> SkyLightData::make( const SkyGradient& sky, const OctahedralRadianceMap* pHdriRadiance, const SkyLightConfig& config, SkyLightData& outData )
    {
        outData._ambient = SH9Color{};
        if ( expected<void> allocated = outData._reflection.allocate( float3{}, config._resolution, config._levelCount ); allocated.has_value() == false )
            return allocated;
        const bool bHdri = sky._mode == SkyMode::Hdri;
        if ( bHdri && ( pHdriRadiance == nullptr || pHdriRadiance->getMipCount() == 0 ) )
        {
            outData._reflection.clear();
            return unexpected( Error{ ErrorKind::InvalidArgument, "HDRI sky has no radiance map (_hdriRadianceMap) - run Scripts/dev/make_sky_hdri_prefabs.py" } );
        }

        // 원본 밉 0 — 텍셀 중심 방향의 하늘빛. 같은 순회에서 SH9 를 투영한다: 옥타헤드럴 사각형의 같은 넓이는 팔면체 면의 같은 넓이이고, 그 조각이 구에서 차지하는
        // 입체각은 원점까지 거리의 세제곱에 반비례한다. 팔면체 위의 점은 단위 방향 / (방향의 L1 길이)라 가중치는 L1 길이³ 이고, 합이 4π 가 되게 나중에 맞춘다.
        const uint32   sourceResolution = bHdri ? pHdriRadiance->getResolution() : config._resolution;
        vector<float3> listSourceTexel( static_cast<size_t>( sourceResolution ) * sourceResolution );
        float32        weightSum = 0.0f;
        for ( uint32 y = 0; y < sourceResolution; ++y )
        {
            for ( uint32 x = 0; x < sourceResolution; ++x )
            {
                const float3  direction                                          = OctahedralRadianceMap::computeTexelDirection( sourceResolution, x, y );
                const float3  radiance                                           = bHdri ? pHdriRadiance->sample( sky.computeHdriLookupDirection( direction ), 0.0f ) * sky._intensity : sky.evaluateRadiance( direction );
                const float32 axisSum                                            = MathUtil::abs( direction._x ) + MathUtil::abs( direction._y ) + MathUtil::abs( direction._z );
                const float32 weight                                             = axisSum * axisSum * axisSum;
                listSourceTexel[static_cast<size_t>( y ) * sourceResolution + x] = radiance;
                outData._ambient.addSample( direction, radiance, weight );
                weightSum += weight;
            }
        }
        outData._ambient.scale( SkyLightInternal::kSphereSolidAngle / weightSum );

        OctahedralRadianceMap source;
        expected<void>        filtered = source.assignBase( sourceResolution, std::move( listSourceTexel ) );
        if ( filtered.has_value() )
            filtered = source.buildMips( config._filterMipCount );
        if ( filtered.has_value() )
            filtered = outData._reflection.filterRoughnessLevels( source, config._filterSampleCount );
        if ( filtered.has_value() == false )
        {
            outData._reflection.clear();
            outData._ambient = SH9Color{};
            return filtered;
        }
        return {};
    }

    const OctahedralRadianceMap* SkyLightCache::findHdriRadiance( const SkyGradient& sky )
    {
        if ( sky._mode != SkyMode::Hdri )
            return nullptr;
        if ( sky._hdriRadianceMap != _hdriRadiancePath )
        {
            _hdriRadiancePath = sky._hdriRadianceMap;
            _hdriRadiance.clear();
            if ( _hdriRadiancePath.empty() == false )
            {
                if ( const expected<void> loaded = _hdriRadiance.loadFromAsset( AssetID{ _hdriRadiancePath.view() } ); loaded.has_value() == false )
                    SW_LOG_ERROR_ID( "SkyLight.HdriRadianceMapLoad", "HDRI radiance map '%#' could not be loaded (%#) - the sky gives no sky light", _hdriRadiancePath.c_str(), loaded.error().message() );
            }
        }
        return _hdriRadiance.getMipCount() > 0 ? &_hdriRadiance : nullptr;
    }

    const SkyLightData* SkyLightCache::update( const SkyGradient* pSky, const SkyLightConfig& config )
    {
        if ( pSky == nullptr )
        {
            _bCaptured = SW_FALSE;
            return nullptr;
        }
        // 처음 보는 하늘 — 스카이 라이트가 없었거나, 다른 하늘 컴포넌트이거나(장면 전환 · 레이어로 갈아 끼움), 거르기 설정(품질 단계)이 바뀌었다. 지난 하늘의 빛을 한 프레임도 쓰지 않는다.
        const bool bNewSky = _bCaptured == SW_FALSE || _source._sourceID != pSky->_sourceID || _data._reflection._resolution != config._resolution ||
                             _data._reflection._levelCount != config._levelCount || _filterSampleCount != config._filterSampleCount || _filterMipCount != config._filterMipCount;
        if ( _bCaptured == SW_TRUE && _framesSinceCapture < config._captureIntervalFrames )
            ++_framesSinceCapture;
        // 같은 하늘의 값이 바뀜(시간대 · 날씨) — 간격이 찼을 때만 다시 거른다. 그 사이 프레임은 지난 빛을 쓴다.
        const bool bDue = bNewSky || ( _framesSinceCapture >= config._captureIntervalFrames && _source.hasSameRadiance( *pSky ) == false );
        if ( bDue )
        {
            const expected<void> made = SkyLightData::make( *pSky, findHdriRadiance( *pSky ), config, _data );
            if ( made.has_value() == false )
            {
                SW_LOG_ERROR_ONCE_ID( "SkyLight.FilterFailed", "Sky light could not be filtered (%#) - the scene gets no sky light. Check EngineConfig _render._skyLight", made.error().message() );
                _bCaptured = SW_FALSE;
                return nullptr;
            }
            _source             = *pSky;
            _filterSampleCount  = config._filterSampleCount;
            _filterMipCount     = config._filterMipCount;
            _framesSinceCapture = 0;
            _bCaptured          = SW_TRUE;
            ++_data._revision;
        }
        return &_data;
    }
} // namespace sw
