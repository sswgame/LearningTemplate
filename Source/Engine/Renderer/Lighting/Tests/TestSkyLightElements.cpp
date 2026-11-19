#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Config/EngineConfig.h"
#include "Engine/Environment/Sky/SkyLight.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Renderer/Lighting/GPULightBuffer.h"
#include "Engine/Scene/Scene.h"

// SkyLightElementsTest — 스카이 라이트가 프로브 버퍼(t13)에 놓이는 자리와 다시 올리는 때(CPU 쪽). 셰이더 lighting.hlsli SkyLight 가 같은 배치를 읽는다:
// 첫 원소부터 텍셀(단계 → 행 → 열), 그 뒤 SH9 원소 일곱. 디바이스 없음(nogpu).

using namespace sw;

namespace
{
    struct SkyLightElementsTestUtil
    {
        static constexpr uint32 kResolution = 4;
        static constexpr uint32 kLevelCount = 2;
        static constexpr uint32 kTexelCount = kResolution * kResolution * kLevelCount;

        /** @brief 위가 @p zenith 밝기인 하늘을 거른 스카이 라이트입니다(번호 @p revision). */
        static SkyLightData makeSkyLight( float32 zenith, uint32 revision )
        {
            SkyGradient sky;
            sky._zenithColor  = float3{ zenith, zenith, zenith };
            sky._horizonColor = float3{ 0.5f, 0.5f, 0.5f };
            sky._intensity    = 1.0f;
            SkyLightConfig config;
            config._resolution            = kResolution;
            config._levelCount            = kLevelCount;
            config._filterSampleCount     = 4;
            config._filterMipCount        = 3;
            config._captureIntervalFrames = 1;
            SkyLightData data;
            SW_EXPECT_TRUE( SkyLightData::make( sky, nullptr, config, data ) );
            data._revision = revision;
            return data;
        }
    };
    using Util = SkyLightElementsTestUtil;
} // namespace

/** @brief [SkyLightElementsTest] 스카이 라이트는 앞선 원소 뒤에 텍셀 · SH9 차례로 붙고, 자리(한 변 · 단계 수 · 첫 원소)는 패스 상수 모양으로 나온다 */
SW_TEST_CASE( SkyLightElementsTest, TexelsThenAmbientFollowTheEarlierElements )
{
    const SkyLightData       skyLight = Util::makeSkyLight( 1.0f, 1 );
    LightProbeElementBuilder builder;
    builder.begin();
    const RealtimeProbeSlot volume = builder.appendRealtimeVolume( 2 ); // 앞선 원소 — 프로브 둘(SH9 float4 열넷 + 모먼트)
    builder.appendSkyLight( skyLight );
    builder.finish();

    const uint32 firstElement = LightProbeElementBuilder::computeRealtimeElementCount( 2 );
    SW_EXPECT_EQUAL( 0u, volume._probeBase );
    SW_EXPECT_EQUAL( 2u * shaderslot::kLightProbeFloat4Count, volume._visibilityBase ); // 모먼트는 SH9 원소 바로 뒤
    SW_EXPECT_NEAR( static_cast<float32>( Util::kResolution ), builder.getSkyLight()._x, 0.0f );
    SW_EXPECT_NEAR( static_cast<float32>( Util::kLevelCount ), builder.getSkyLight()._y, 0.0f );
    SW_EXPECT_NEAR( static_cast<float32>( firstElement ), builder.getSkyLight()._z, 0.0f );

    const shared_ptr<const vector<float4>> pElements = builder.getElements();
    SW_REQUIRE_NOT_NULL( pElements );
    SW_REQUIRE_EQUAL( static_cast<size_t>( firstElement + Util::kTexelCount + SH9Color::kGPUFloat4Count ), pElements->size() );
    // 텍셀은 데이터의 차례 그대로다(단계 → 행 → 열).
    const uint32  lastTexel = Util::kTexelCount - 1u;
    const float3& first     = skyLight._reflection._listTexel[0];
    const float3& last      = skyLight._reflection._listTexel[lastTexel];
    SW_EXPECT_NEAR( first._x, ( *pElements )[firstElement]._x, 0.0f );
    SW_EXPECT_NEAR( last._z, ( *pElements )[firstElement + lastTexel]._z, 0.0f );
    // SH9 는 텍셀 바로 뒤다 — 셰이더가 읽는 조도 / π 원소(`SH9Color::makeGPUElement`).
    float4 arrAmbient[SH9Color::kGPUFloat4Count];
    skyLight._ambient.makeGPUElement( arrAmbient );
    for ( uint32 element = 0; element < SH9Color::kGPUFloat4Count; ++element )
    {
        const float4& uploaded = ( *pElements )[firstElement + Util::kTexelCount + element];
        SW_EXPECT_NEAR( arrAmbient[element]._x, uploaded._x, 0.0f );
        SW_EXPECT_NEAR( arrAmbient[element]._w, uploaded._w, 0.0f );
    }
}

/**
 * @brief [SkyLightElementsTest] 실시간 볼륨은 SH9 원소 뒤에 가시성 모먼트 묶음을 잡고, 그 묶음을 프로브 원소 수의 배수로 올려 다음 볼륨의 프로브 번호가 정수로 남는다
 * @details 묶음은 0 으로 채운다 — 거리² 0 은 "모름" 이라 갱신 컴퓨트가 쓰기 전에는 소비 셰이더가 가리지 않는다.
 */
SW_TEST_CASE( SkyLightElementsTest, RealtimeVolumeReservesVisibilityMomentsAfterSH9 )
{
    LightProbeElementBuilder builder;
    builder.begin();
    const RealtimeProbeSlot first  = builder.appendRealtimeVolume( 3 );
    const RealtimeProbeSlot second = builder.appendRealtimeVolume( 1 );
    builder.finish();

    const uint32 firstCount = LightProbeElementBuilder::computeRealtimeElementCount( 3 );
    SW_EXPECT_EQUAL( 0u, firstCount % shaderslot::kLightProbeFloat4Count );
    SW_EXPECT_TRUE( firstCount >= 3u * ( shaderslot::kLightProbeFloat4Count + shaderslot::kProbeVisibilityFloat4Count ) );
    SW_EXPECT_EQUAL( 3u * shaderslot::kLightProbeFloat4Count, first._visibilityBase );
    SW_EXPECT_EQUAL( firstCount / shaderslot::kLightProbeFloat4Count, second._probeBase );
    SW_EXPECT_EQUAL( firstCount + shaderslot::kLightProbeFloat4Count, second._visibilityBase );

    const shared_ptr<const vector<float4>> pElements = builder.getElements();
    SW_REQUIRE_NOT_NULL( pElements );
    SW_REQUIRE_EQUAL( static_cast<size_t>( firstCount + LightProbeElementBuilder::computeRealtimeElementCount( 1 ) ), pElements->size() );
    SW_EXPECT_NEAR( 0.0f, ( *pElements )[second._visibilityBase]._y, 0.0f );
}

/** @brief [SkyLightElementsTest] 번호가 같으면 지난 묶음을 그대로 주고(다시 올리지 않는다), 번호가 오르면 새 묶음이며, 스카이 라이트가 없는 수집은 자리가 0 이다 */
SW_TEST_CASE( SkyLightElementsTest, ElementsAreRebuiltOnlyWhenTheRevisionChanges )
{
    SkyLightData             skyLight = Util::makeSkyLight( 1.0f, 1 );
    LightProbeElementBuilder builder;
    builder.begin();
    builder.appendSkyLight( skyLight );
    builder.finish();
    const shared_ptr<const vector<float4>> pFirst = builder.getElements();
    SW_REQUIRE_NOT_NULL( pFirst );

    builder.begin();
    builder.appendSkyLight( skyLight );
    builder.finish();
    SW_EXPECT_TRUE( builder.getElements() == pFirst );

    skyLight = Util::makeSkyLight( 3.0f, 2 );
    builder.begin();
    builder.appendSkyLight( skyLight );
    builder.finish();
    SW_REQUIRE_NOT_NULL( builder.getElements() );
    SW_EXPECT_TRUE( builder.getElements() != pFirst );

    builder.begin();
    builder.finish();
    SW_EXPECT_NULL( builder.getElements() );
    SW_EXPECT_NEAR( 0.0f, builder.getSkyLight()._y, 0.0f );
}

/** @brief [SkyLightElementsTest] 씬 수집은 받은 스카이 라이트를 묶음에 붙이되 라이트 원소는 넣지 않는다 — 빛이 하나도 없는 씬도 스카이 라이트를 올린다 */
SW_TEST_CASE( SkyLightElementsTest, CollectAppendsTheSkyLightWithoutALightElement )
{
    const SkyLightData       skyLight = Util::makeSkyLight( 1.0f, 1 );
    Scene                    scene( "SkyLightOnly" );
    vector<GPULight>         listLight;
    LightProbeElementBuilder builder;
    collectSceneLights( &scene, listLight, nullptr, &builder, nullptr, nullptr, &skyLight );
    SW_EXPECT_TRUE( listLight.empty() );
    SW_REQUIRE_NOT_NULL( builder.getElements() );
    SW_EXPECT_EQUAL( static_cast<size_t>( Util::kTexelCount + SH9Color::kGPUFloat4Count ), builder.getElements()->size() );
    SW_EXPECT_NEAR( static_cast<float32>( Util::kLevelCount ), builder.getSkyLight()._y, 0.0f );

    collectSceneLights( &scene, listLight, nullptr, &builder );
    SW_EXPECT_NULL( builder.getElements() );
    SW_EXPECT_NEAR( 0.0f, builder.getSkyLight()._y, 0.0f );
}
