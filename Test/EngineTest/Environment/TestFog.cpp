#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Environment/Fog/FogComponent.h"
#include "Engine/Environment/Fog/FogSettings.h"
#include "Engine/Environment/Water/WaterBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"

#include "TestFramework/TestFramework.h"

// 안개 — 높이 · 거리 지수 안개의 광학 두께 식(셰이더 fog.hlsl · fogcommon.hlsli 가 같은 식)과 컴포넌트 찾기 · 물속 덮어쓰기.

using namespace sw;

namespace
{
    struct FogTestUtil
    {
        /// 시선 위 광학 두께를 잘게 나눈 합으로 구합니다(식의 닫힌 형태를 확인하는 기준값).
        static float32 integrateNumerically( const FogSettings& fog, const float3& eye, const float3& point )
        {
            constexpr uint32 kStepCount = 20000;
            const float3     delta      = point - eye;
            const float32    length     = delta.getLength();
            const float32    stepLength = length / static_cast<float32>( kStepCount );
            float32          sum{ 0.0f };
            for ( uint32 step = 0; step < kStepCount; ++step )
            {
                const float32 distance = ( static_cast<float32>( step ) + 0.5f ) * stepLength;
                const float3  position = eye + delta * ( distance / length );
                if ( distance < fog._startDistance )
                    continue;
                sum += fog._density * MathUtil::exp( -fog._heightFalloff * ( position._y - fog._baseHeight ) ) * stepLength;
            }
            return sum;
        }
    };
} // namespace

/**
 * @brief [FogTest] 균일 안개(높이 감쇠 0)의 광학 두께는 밀도 × 길이 하나이고, 불투명도는 1 - exp(-두께)에서 최대 불투명도로 잘린다
 */
SW_TEST_CASE( FogTest, UniformFogOpticalDepthIsDensityTimesLength )
{
    FogSettings fog;
    fog._density       = 0.05f;
    fog._heightFalloff = 0.0f;
    fog._maxOpacity    = 0.8f;
    const float3 eye{ 0.0f, 2.0f, 0.0f };
    const float3 point{ 30.0f, 2.0f, 40.0f }; // 길이 50
    SW_EXPECT_NEAR_EQUAL( 0.05f * 50.0f, fog.computeOpticalDepth( eye, point ), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f - MathUtil::exp( -2.5f ), fog.computeOpacity( eye, point ), 1.0e-4f );
    // 아주 먼 점은 최대 불투명도에서 멈춘다.
    SW_EXPECT_NEAR_EQUAL( 0.8f, fog.computeOpacity( eye, float3{ 0.0f, 2.0f, 5000.0f } ), 1.0e-6f );
    // 눈과 같은 점은 두께가 없다.
    SW_EXPECT_NEAR_EQUAL( 0.0f, fog.computeOpticalDepth( eye, eye ), 1.0e-7f );
}

/**
 * @brief [FogTest] 높이 안개의 닫힌 형태는 잘게 나눈 합과 같다 — 오르막 · 내리막 · 수평(지수가 0 에 가까운 갈래)과 시작 거리
 */
SW_TEST_CASE( FogTest, HeightFogClosedFormMatchesNumericIntegration )
{
    FogSettings fog;
    fog._density       = 0.08f;
    fog._heightFalloff = 0.15f;
    fog._baseHeight    = 1.0f;
    fog._startDistance = 3.0f;
    const float3 eye{ 0.0f, 4.0f, 0.0f };
    const float3 arrPoint[] = {
        float3{20.0f,  25.0f,  5.0f}, // 오르막
        float3{15.0f,  -6.0f, 10.0f}, // 내리막
        float3{40.0f,   4.0f, 30.0f}, // 수평 — 지수 인자가 0
        float3{30.0f, 4.001f,  0.0f}, // 거의 수평 — 테일러 갈래
    };
    for ( const float3& point : arrPoint )
    {
        const float32 expected = FogTestUtil::integrateNumerically( fog, eye, point );
        SW_EXPECT_NEAR_EQUAL( expected, fog.computeOpticalDepth( eye, point ), expected * 2.0e-3f + 1.0e-5f );
    }
    // 시작 거리 안쪽의 점은 안개가 없다.
    SW_EXPECT_NEAR_EQUAL( 0.0f, fog.computeOpticalDepth( eye, float3{ 1.0f, 4.0f, 1.0f } ), 1.0e-7f );
}

/**
 * @brief [FogTest] 안개 아래로 아주 가파르게 내려가는 시선도 유한한 값을 낸다(지수가 넘치지 않는다) — 불투명도는 0..1 안이다
 */
SW_TEST_CASE( FogTest, SteepDescentStaysFinite )
{
    FogSettings fog;
    fog._density       = 0.02f;
    fog._heightFalloff = 0.5f;
    fog._baseHeight    = 0.0f;
    const float3  eye{ 0.0f, 1000.0f, 0.0f };
    const float32 depth   = fog.computeOpticalDepth( eye, float3{ 0.0f, -5000.0f, 0.0f } );
    const float32 opacity = fog.computeOpacity( eye, float3{ 0.0f, -5000.0f, 0.0f } );
    SW_EXPECT_TRUE( depth >= 0.0f );
    SW_EXPECT_TRUE( 0.0f <= opacity && opacity <= 1.0f );
    SW_EXPECT_TRUE( depth == depth ); // NaN 이 아니다
}

/**
 * @brief [FogTest] 방향광 산란 가중치는 해를 바라볼 때 1 이고, 지수가 클수록 해 둘레만 남으며, 반대편은 0 이다
 */
SW_TEST_CASE( FogTest, InscatterWeightPeaksTowardTheSun )
{
    FogSettings fog;
    fog._inscatterExponent = 8.0f;
    const float3 toSun{ 0.0f, 0.6f, 0.8f };
    SW_EXPECT_NEAR_EQUAL( 1.0f, fog.computeInscatterWeight( toSun, toSun ), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, fog.computeInscatterWeight( float3{ 0.0f, -0.6f, -0.8f }, toSun ), 1.0e-6f );
    const float32 wideWeight = fog.computeInscatterWeight( float3{ 0.0f, 0.5f, 0.866025f }, toSun );
    fog._inscatterExponent   = 32.0f;
    SW_EXPECT_TRUE( fog.computeInscatterWeight( float3{ 0.0f, 0.5f, 0.866025f }, toSun ) < wideWeight );
}

/**
 * @brief [FogTest] 컴포넌트가 없으면 안개가 없고, 있으면 속성이 값이 되며, 카메라가 물속이면 물의 안개(균일, 볼륨 없음)가 컴포넌트의 값을 덮는다
 */
SW_TEST_CASE( FogTest, ComponentAndUnderwaterOverride )
{
    GameObjectManager manager;
    FogSettings       settings;
    SW_EXPECT_FALSE( FogComponent::findFog( manager, float3{ 0.0f, 5.0f, 0.0f }, settings ) );

    GameObject* pFogObject = manager.createGameObject( hashed_string( "Fog" ) );
    SW_ASSERT_NOT_NULL( pFogObject );
    FogComponent* pFog = pFogObject->addComponent<FogComponent>();
    SW_ASSERT_NOT_NULL( pFog );
    pFog->setDensity( 0.07f );
    pFog->setHeightFalloff( 0.3f );
    pFog->setVolumetric( true );
    SW_ASSERT_TRUE( FogComponent::findFog( manager, float3{ 0.0f, 5.0f, 0.0f }, settings ) );
    SW_EXPECT_NEAR_EQUAL( 0.07f, settings._density, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.3f, settings._heightFalloff, 1.0e-6f );
    SW_EXPECT_TRUE( settings._bVolumetric == SW_TRUE );
    SW_EXPECT_TRUE( settings._bUnderwater == SW_FALSE );

    // 호수 하나 — 수면 높이 3. 눈이 수면 아래면 물의 안개다.
    GameObject* pLakeObject = manager.createGameObject( hashed_string( "Lake" ) );
    SW_ASSERT_NOT_NULL( pLakeObject );
    WaterBodyComponent* pWater = pLakeObject->addComponent<WaterBodyComponent>();
    SW_ASSERT_NOT_NULL( pWater );
    pWater->setLocalPosition( float3{ 0.0f, 3.0f, 0.0f } );
    pWater->setSize( float2{ 40.0f, 40.0f } );
    pWater->setMaterialPath( "" );
    pWater->rebuildSurface();
    SW_ASSERT_TRUE( FogComponent::findFog( manager, float3{ 1.0f, 0.0f, 1.0f }, settings ) );
    SW_EXPECT_TRUE( settings._bUnderwater == SW_TRUE );
    SW_EXPECT_NEAR_EQUAL( 0.0f, settings._heightFalloff, 1.0e-7f ); // 균일
    SW_EXPECT_TRUE( settings._bVolumetric == SW_FALSE );            // 물속은 볼륨이 없다
    // 수면 위는 컴포넌트의 값이다.
    SW_ASSERT_TRUE( FogComponent::findFog( manager, float3{ 1.0f, 10.0f, 1.0f }, settings ) );
    SW_EXPECT_TRUE( settings._bUnderwater == SW_FALSE );
    SW_EXPECT_NEAR_EQUAL( 0.07f, settings._density, 1.0e-6f );
}

/**
 * @brief [FogTest] 안개 패스의 표 줄 — 장면 깊이만 읽는 계약이고, 블렌드(프리멀티플라이)로 색 위에 그리며, 파이프라인 XML 에 쓸 수 있다
 */
SW_TEST_CASE( FogTest, PassTypeRowDeclaresDepthOnlyBlendedDraw )
{
    const RenderPassTypeInfo& info = getRenderPassTypeInfo( RenderPassType::Fog );
    SW_EXPECT_TRUE( isPipelinePassType( RenderPassType::Fog ) );
    SW_EXPECT_TRUE( info.hasFlag( RenderPassTraitFlag::kBlend ) && info.hasFlag( RenderPassTraitFlag::kPremultipliedAlpha ) );
    SW_EXPECT_FALSE( info.hasFlag( RenderPassTraitFlag::kGenericFullscreen ) ); // 전용 실행 — 일반 길의 복사 · 지우기를 타면 장면이 지워진다
    SW_EXPECT_TRUE( info._inputContract.reads( RenderPassInputRole::SceneDepth ) );
    SW_EXPECT_FALSE( info._inputContract.reads( RenderPassInputRole::SourceColor ) ); // 색을 읽지 않는다 — 읽으면서 쓰는 첨부가 없다
    // 컴퓨트 둘은 파이프라인 XML 에 나올 수 없는 엔진 내부 PSO 슬롯이다.
    SW_EXPECT_FALSE( isPipelinePassType( RenderPassType::VolumetricFog ) );
    SW_EXPECT_TRUE( getRenderPassTypeInfo( RenderPassType::VolumetricFog ).hasFlag( RenderPassTraitFlag::kCompute ) );
}
