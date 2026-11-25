#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Config/EngineConfig.h"
#include "Engine/Environment/Sky/SkyLight.h"

// 스카이 라이트 — 하늘을 거칠기 단계마다 거른 데이터(반사 · SH9)와 다시 거르는 때. 셰이더 쪽 읽기(lighting.hlsli SkyLight)는 시나리오 그림으로 본다.

using namespace sw;

namespace
{
    struct SkyLightTestUtil
    {
        static constexpr uint32 kIntervalFrames = 4;

        static SkyLightConfig makeConfig()
        {
            SkyLightConfig config;
            config._resolution            = 16;
            config._levelCount            = 4;
            config._filterSampleCount     = 32;
            config._filterMipCount        = 5;
            config._captureIntervalFrames = kIntervalFrames;
            return config;
        }

        /** @brief 위가 @p zenith 밝기, 지평선이 0.5, 아래가 0 인 그러데이션입니다. */
        static SkyGradient makeSky( float32 zenith, uint64 sourceID )
        {
            SkyGradient sky;
            sky._zenithColor     = float3{ zenith, zenith, zenith };
            sky._horizonColor    = float3{ 0.5f, 0.5f, 0.5f };
            sky._groundColor     = float3{};
            sky._intensity       = 1.0f;
            sky._horizonExponent = 1.0f;
            sky._sourceID        = sourceID;
            return sky;
        }

        static float32 evaluateDiffuse( const SkyLightData& data, const float3& normal ) { return data._ambient.evaluateIrradiance( normal )._x / MathUtil::kPi; }
    };
    using Util = SkyLightTestUtil;
} // namespace

/** @brief [SkyLightTest] 한 색 하늘은 어느 단계 어느 방향이나 그 색 × 세기이고, 확산(조도 / π)도 같다 — 거르기와 SH 투영이 빛을 더하거나 잃지 않는다 */
SW_TEST_CASE( SkyLightTest, UniformSkyGivesTheSameLightEverywhere )
{
    SkyGradient sky;
    sky._zenithColor  = float3{ 0.25f, 0.5f, 1.0f };
    sky._horizonColor = sky._zenithColor;
    sky._groundColor  = sky._zenithColor;
    sky._intensity    = 2.0f;

    SkyLightData data;
    SW_REQUIRE_TRUE( SkyLightData::make( sky, nullptr, Util::makeConfig(), data ) );
    SW_EXPECT_TRUE( data._reflection.validate() );
    SW_EXPECT_EQUAL( 16u, data._reflection._resolution );
    SW_EXPECT_EQUAL( 4u, data._reflection._levelCount );
    uint32 wrongTexelCount = 0;
    for ( const float3& texel : data._reflection._listTexel )
    {
        const bool bSame = MathUtil::abs( texel._x - 0.5f ) < 1.0e-3f && MathUtil::abs( texel._y - 1.0f ) < 1.0e-3f && MathUtil::abs( texel._z - 2.0f ) < 1.0e-3f;
        wrongTexelCount += bSame ? 0u : 1u;
    }
    SW_EXPECT_EQUAL( 0u, wrongTexelCount );

    const float3 arrNormal[] = {
        float3{0.0f,  1.0f,  0.0f},
        float3{0.0f, -1.0f,  0.0f},
        float3{1.0f,  0.0f,  0.0f},
        float3{0.0f,  0.0f, -1.0f}
    };
    for ( const float3& normal : arrNormal )
    {
        // 한 변 16 의 중점 구적 오차는 0.4 % 아래다.
        const float3 diffuse = data._ambient.evaluateIrradiance( normal ) * ( 1.0f / MathUtil::kPi );
        SW_EXPECT_NEAR( 0.5f, diffuse._x, 0.01f );
        SW_EXPECT_NEAR( 2.0f, diffuse._z, 0.04f );
    }
}

/** @brief [SkyLightTest] 위가 밝은 하늘 — 단계 0 은 하늘 식 그대로이고, 거친 단계는 위아래를 섞으며, 확산은 위를 보는 면이 옆 · 아래보다 밝다 */
SW_TEST_CASE( SkyLightTest, GradientSkyIsFilteredByRoughnessAndProjectedToAmbient )
{
    const SkyGradient sky = Util::makeSky( 1.0f, 1 );
    SkyLightData      data;
    SW_REQUIRE_TRUE( SkyLightData::make( sky, nullptr, Util::makeConfig(), data ) );

    const float3 up{ 0.0f, 1.0f, 0.0f };
    const float3 down{ 0.0f, -1.0f, 0.0f };
    const float3 side{ 1.0f, 0.0f, 0.0f };
    // 단계 0 의 텍셀은 그 텍셀 방향의 하늘빛이다.
    const float3 texelDirection = data._reflection.computeTexelDirection( 5, 9 );
    SW_EXPECT_NEAR( sky.evaluateRadiance( texelDirection )._x, data._reflection._listTexel[data._reflection.computeTexelIndex( 0, 5, 9 )]._x, 1.0e-5f );
    // 매끈한 면은 위에서 천정 색에 가깝고, 가장 거친 단계는 반구를 섞어 위가 어두워지고 아래가 밝아진다.
    const uint32  roughLevel = data._reflection._levelCount - 1u;
    const float32 sharpUp    = data._reflection.sampleLevel( 0, up )._x;
    const float32 roughUp    = data._reflection.sampleLevel( roughLevel, up )._x;
    const float32 roughDown  = data._reflection.sampleLevel( roughLevel, down )._x;
    SW_EXPECT_TRUE( sharpUp > 0.9f );
    SW_EXPECT_TRUE( roughUp < sharpUp );
    SW_EXPECT_TRUE( roughDown > data._reflection.sampleLevel( 0, down )._x );
    SW_EXPECT_TRUE( roughUp > roughDown );

    // 확산 — 해석값(균일 구 적분)은 위 0.83 · 옆 0.50 · 아래 0.17 이다.
    SW_EXPECT_NEAR( 0.83f, Util::evaluateDiffuse( data, up ), 0.02f );
    SW_EXPECT_NEAR( 0.50f, Util::evaluateDiffuse( data, side ), 0.02f );
    SW_EXPECT_NEAR( 0.17f, Util::evaluateDiffuse( data, down ), 0.02f );
}

/** @brief [SkyLightTest] 범위 밖 설정은 까닭과 함께 거절하고 데이터를 비운다 — 캐시는 그 프레임에 스카이 라이트가 없다 */
SW_TEST_CASE( SkyLightTest, OutOfRangeConfigIsRejected )
{
    const SkyGradient sky = Util::makeSky( 1.0f, 1 );
    SkyLightData      data;
    SkyLightConfig    config = Util::makeConfig();
    config._resolution       = ReflectionProbeAsset::kMaxResolution + 1u;
    SW_EXPECT_FALSE( SkyLightData::make( sky, nullptr, config, data ) );
    SW_EXPECT_TRUE( data._reflection._listTexel.empty() );

    config                 = Util::makeConfig();
    config._filterMipCount = 0;
    SW_EXPECT_FALSE( SkyLightData::make( sky, nullptr, config, data ) );
    SW_EXPECT_TRUE( data._reflection._listTexel.empty() );

    config                    = Util::makeConfig();
    config._filterSampleCount = 0;
    SW_EXPECT_FALSE( SkyLightData::make( sky, nullptr, config, data ) );
    SW_EXPECT_TRUE( data._reflection._listTexel.empty() );

    SkyLightCache cache;
    SW_EXPECT_LOG( "SkyLight.FilterFailed" );
    SW_EXPECT_NULL( cache.update( &sky, config ) );
    SW_EXPECT_EQUAL( 0u, cache.getRevision() );
}

/** @brief [SkyLightTest] 하늘이 없으면 스카이 라이트도 없고, 하늘이 생긴 프레임에 바로 거르며, 하늘이 그대로면 다시 거르지 않는다(구름만 바뀌어도 그대로다) */
SW_TEST_CASE( SkyLightTest, CapturesOnceWhileTheSkyStaysTheSame )
{
    const SkyLightConfig config = Util::makeConfig();
    SkyLightCache        cache;
    SW_EXPECT_NULL( cache.update( nullptr, config ) );
    SW_EXPECT_EQUAL( 0u, cache.getRevision() );

    SkyGradient sky = Util::makeSky( 1.0f, 1 );
    SW_EXPECT_NOT_NULL( cache.update( &sky, config ) );
    SW_EXPECT_EQUAL( 1u, cache.getRevision() );
    sky._clouds._coverage = 0.9f; // 구름은 스카이 라이트에 들지 않는다
    for ( uint32 frame = 0; frame < Util::kIntervalFrames * 3u; ++frame )
    {
        SW_EXPECT_NOT_NULL( cache.update( &sky, config ) );
    }
    SW_EXPECT_EQUAL( 1u, cache.getRevision() );

    // 하늘이 사라지면 놓고, 다시 생긴 프레임에 바로 거른다.
    SW_EXPECT_NULL( cache.update( nullptr, config ) );
    SW_EXPECT_NOT_NULL( cache.update( &sky, config ) );
    SW_EXPECT_EQUAL( 2u, cache.getRevision() );
}

/** @brief [SkyLightTest] 같은 하늘의 값이 프레임마다 바뀌어도(시간대) 간격에 한 번만 다시 거르고, 간격이 지난 뒤의 첫 변화는 그 프레임에 거른다 */
SW_TEST_CASE( SkyLightTest, ChangingSkyIsRecapturedOncePerInterval )
{
    const SkyLightConfig config = Util::makeConfig();
    SkyLightCache        cache;
    SkyGradient          sky = Util::makeSky( 1.0f, 1 );
    SW_REQUIRE_NOT_NULL( cache.update( &sky, config ) );
    // 프레임마다 조금씩 밝아진다 — 간격의 세 배 동안 세 번만 거른다.
    for ( uint32 frame = 0; frame < Util::kIntervalFrames * 3u; ++frame )
    {
        sky._zenithColor._x += 0.01f;
        const SkyLightData* const pData = cache.update( &sky, config );
        SW_REQUIRE_NOT_NULL( pData );
    }
    SW_EXPECT_EQUAL( 4u, cache.getRevision() );

    // 오래 그대로이던 하늘이 바뀌면 기다리지 않는다.
    for ( uint32 frame = 0; frame < Util::kIntervalFrames * 2u; ++frame )
    {
        SW_EXPECT_NOT_NULL( cache.update( &sky, config ) );
    }
    SW_EXPECT_EQUAL( 4u, cache.getRevision() );
    sky._zenithColor._x              = 3.0f;
    const SkyLightData* const pAfter = cache.update( &sky, config );
    SW_REQUIRE_NOT_NULL( pAfter );
    SW_EXPECT_EQUAL( 5u, cache.getRevision() );
    SW_EXPECT_TRUE( pAfter->_reflection.sampleLevel( 0, float3{ 0.0f, 1.0f, 0.0f } )._x > 2.5f );
}

/** @brief [SkyLightTest] 다른 하늘(다른 컴포넌트: 장면 전환 · 레이어 교체)과 거르기 설정의 변화는 간격을 기다리지 않는다. 지난 하늘의 빛을 한 프레임도 쓰지 않는다 */
SW_TEST_CASE( SkyLightTest, ADifferentSkyOrConfigIsCapturedAtOnce )
{
    SkyLightConfig    config = Util::makeConfig();
    SkyLightCache     cache;
    const SkyGradient day   = Util::makeSky( 1.0f, 1 );
    const SkyGradient night = Util::makeSky( 0.1f, 2 );
    SW_REQUIRE_NOT_NULL( cache.update( &day, config ) );
    const SkyLightData* const pNight = cache.update( &night, config );
    SW_REQUIRE_NOT_NULL( pNight );
    SW_EXPECT_EQUAL( 2u, cache.getRevision() );
    SW_EXPECT_TRUE( pNight->_reflection.sampleLevel( 0, float3{ 0.0f, 1.0f, 0.0f } )._x < 0.2f );

    config._resolution               = 8;
    const SkyLightData* const pSmall = cache.update( &night, config );
    SW_REQUIRE_NOT_NULL( pSmall );
    SW_EXPECT_EQUAL( 3u, cache.getRevision() );
    SW_EXPECT_EQUAL( 8u, pSmall->_reflection._resolution );

    config._filterMipCount = 1;
    SW_REQUIRE_NOT_NULL( cache.update( &night, config ) );
    SW_EXPECT_EQUAL( 4u, cache.getRevision() );
}

/** @brief [SkyLightTest] HDRI 하늘의 반사는 SH9 가 아니라 원본(`.radiancemap`)을 거른 것이다 — 회전 · 세기를 따르고, 원본이 없으면 까닭과 함께 거절한다 */
SW_TEST_CASE( SkyLightTest, HdriSkyFiltersTheRadianceMap )
{
    // 원본 한 변 32(거른 데이터 16 보다 크다) — +Z 쪽만 밝고 나머지는 어둡다. SH9 는 일부러 한 색으로 둬서 반사가 SH9 를 읽지 않음을 가린다.
    const uint32   sourceResolution = 32;
    vector<float3> listTexel;
    for ( uint32 y = 0; y < sourceResolution; ++y )
    {
        for ( uint32 x = 0; x < sourceResolution; ++x )
        {
            const float3 direction = OctahedralRadianceMap::computeTexelDirection( sourceResolution, x, y );
            listTexel.push_back( direction._z > 0.5f ? float3{ 4.0f, 4.0f, 4.0f } : float3{ 0.1f, 0.1f, 0.1f } );
        }
    }
    OctahedralRadianceMap radiance;
    SW_REQUIRE_TRUE( radiance.assignBase( sourceResolution, std::move( listTexel ) ) );

    SkyGradient sky;
    sky._mode        = SkyMode::Hdri;
    sky._intensity   = 2.0f;
    sky._hdriAmbient = SH9Color::makeConstant( float3{ 1.0f, 1.0f, 1.0f } );
    SkyLightData data;
    SW_EXPECT_FALSE( SkyLightData::make( sky, nullptr, Util::makeConfig(), data ) );
    SW_EXPECT_TRUE( data._reflection._listTexel.empty() );
    SW_REQUIRE_TRUE( SkyLightData::make( sky, &radiance, Util::makeConfig(), data ) );

    const float3 front{ 0.0f, 0.0f, 1.0f };
    const float3 back{ 0.0f, 0.0f, -1.0f };
    // 매끈한 단계는 원본 × 세기(앞 8 · 뒤 0.2), 거친 단계는 앞이 어두워지고 옆이 밝아진다.
    SW_EXPECT_NEAR( 8.0f, data._reflection.sampleLevel( 0, front )._x, 0.05f );
    SW_EXPECT_NEAR( 0.2f, data._reflection.sampleLevel( 0, back )._x, 0.05f );
    const uint32 roughLevel = data._reflection._levelCount - 1u;
    SW_EXPECT_TRUE( data._reflection.sampleLevel( roughLevel, front )._x < 8.0f );
    // 반사 로브는 노멀의 반구만 모은다 — 정면으로 등진 쪽은 거칠어도 그대로고, 밝은 쪽을 반쯤 보는 옆은 밝아진다.
    const float3 side = float3{ 0.95f, 0.0f, 0.31f }.normalizeOr( front, 1e-6f );
    SW_EXPECT_NEAR( 0.2f, data._reflection.sampleLevel( 0, side )._x, 0.05f );
    SW_EXPECT_TRUE( data._reflection.sampleLevel( roughLevel, side )._x > 0.4f );
    // 확산도 원본에서 투영한다 — 앞을 보는 면이 뒤보다 밝다(SH9 계수 한 색이면 같았을 것이다).
    SW_EXPECT_TRUE( Util::evaluateDiffuse( data, front ) > Util::evaluateDiffuse( data, back ) * 2.0f );

    // 반 바퀴 돌리면 밝은 쪽이 뒤로 간다.
    sky._hdriRotation = MathUtil::kPi;
    SW_REQUIRE_TRUE( SkyLightData::make( sky, &radiance, Util::makeConfig(), data ) );
    SW_EXPECT_NEAR( 8.0f, data._reflection.sampleLevel( 0, back )._x, 0.05f );
    SW_EXPECT_NEAR( 0.2f, data._reflection.sampleLevel( 0, front )._x, 0.05f );

    // 원본 경로가 없는 HDRI 하늘 — 캐시는 SH9 로 메우지 않고 오류를 남기며 스카이 라이트가 없다.
    SkyLightCache cache;
    SW_EXPECT_LOG( "SkyLight.FilterFailed" );
    SW_EXPECT_NULL( cache.update( &sky, Util::makeConfig() ) );
}
