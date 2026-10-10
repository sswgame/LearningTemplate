#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Environment/Water/WaterBodyComponent.h"
#include "Engine/Environment/Water/WaterWaveMath.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "TestFramework/TestFramework.h"

// 물 — 거스트너 변위 · 노멀 · 수면 높이 질의(부력의 기반) · 호수 · 강 · 물속 판정. GPU 쪽 같은 식의 대조는 RenderPassGPUTest.WaterWaveShaderMatchesCpu.

using namespace sw;

namespace
{
    struct WaterTestUtil
    {
        static constexpr float32 kGravity = 9.81f; ///< 시험의 파도 분산 중력(m/s²) — 엔진 설정 표의 기본값과 같다

        static void makeWaves( float4 ( &outArrWave )[shaderslot::kGerstnerWaveCount] )
        {
            outArrWave[0] = GerstnerWave{ 0.3f, 12.0f, 0.35f, 0.8f }.toVector();
            outArrWave[1] = GerstnerWave{ 2.1f, 5.0f, 0.12f, 0.6f }.toVector();
            outArrWave[2] = GerstnerWave{ -1.0f, 2.5f, 0.05f, 0.5f }.toVector();
            outArrWave[3] = float4{ 0.0f, 1.0f, 0.0f, 0.0f }; // 빈 칸
        }
    };
} // namespace

/**
 * @brief [WaterTest] 파도가 없으면 변위 0 · 노멀 위쪽이고, 사인 파도 하나(가파름 0)는 높이가 A sin(k x − ω t) 다
 */
SW_TEST_CASE( WaterTest, SingleSineWaveMatchesClosedForm )
{
    float4 arrWave[shaderslot::kGerstnerWaveCount] = {};
    for ( float4& wave : arrWave )
    {
        wave = float4{ 0.0f, 1.0f, 0.0f, 0.0f };
    }
    const float3 still = WaterWaveMath::computeDisplacement( float2{ 3.0f, 4.0f }, 2.0f, WaterTestUtil::kGravity, arrWave );
    SW_EXPECT_NEAR_EQUAL( 0.0f, still.getLength(), 1.0e-7f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, WaterWaveMath::computeNormal( float2{ 3.0f, 4.0f }, 2.0f, WaterTestUtil::kGravity, arrWave )._y, 1.0e-7f );

    arrWave[0]                 = GerstnerWave{ 0.0f, 10.0f, 0.5f, 0.0f }.toVector();
    const float32 waveNumber   = 6.28318530718f / 10.0f;
    const float32 angularSpeed = MathUtil::sqrt( WaterTestUtil::kGravity * waveNumber );
    for ( uint32 probe = 0; probe < 16; ++probe )
    {
        const float32 x            = static_cast<float32>( probe ) * 1.3f;
        const float32 time         = 0.7f + static_cast<float32>( probe ) * 0.1f;
        const float3  displacement = WaterWaveMath::computeDisplacement( float2{ x, 5.0f }, time, WaterTestUtil::kGravity, arrWave );
        SW_EXPECT_NEAR_EQUAL( 0.5f * MathUtil::sin( waveNumber * x - angularSpeed * time ), displacement._y, 1.0e-5f );
        SW_EXPECT_NEAR_EQUAL( 0.0f, displacement._x, 1.0e-6f ); // 가파름 0 은 옆으로 옮기지 않는다
    }
}

/**
 * @brief [WaterTest] 수면 높이 질의는 그 자리로 옮겨 오는 원점을 찾는다 — 원점 + 수평 변위 = 질의 자리, 높이 = 그 원점의 변위 y
 */
SW_TEST_CASE( WaterTest, SurfaceHeightQueryInvertsHorizontalDisplacement )
{
    float4 arrWave[shaderslot::kGerstnerWaveCount];
    WaterTestUtil::makeWaves( arrWave );
    float32 worstMiss{ 0.0f };
    for ( uint32 probe = 0; probe < 64; ++probe )
    {
        const float2  position{ -20.0f + static_cast<float32>( probe % 8 ) * 5.3f, -15.0f + static_cast<float32>( probe / 8 ) * 4.1f };
        const float32 time = 1.0f + static_cast<float32>( probe ) * 0.05f;
        float2        origin{};
        const float32 height       = WaterWaveMath::computeSurfaceHeight( position, time, WaterTestUtil::kGravity, arrWave, 8u, &origin );
        const float3  displacement = WaterWaveMath::computeDisplacement( origin, time, WaterTestUtil::kGravity, arrWave );
        worstMiss                  = MathUtil::max( worstMiss, ( origin + float2{ displacement._x, displacement._z } - position ).getLength() );
        SW_EXPECT_NEAR_EQUAL( displacement._y, height, 1.0e-6f );
    }
    SW_EXPECT_TRUE( worstMiss < 1.0e-3f );
}

/**
 * @brief [WaterTest] 노멀은 옮겨 간 면의 접선 둘(유한 차분)에 수직이다
 */
SW_TEST_CASE( WaterTest, NormalIsPerpendicularToTheDisplacedSurface )
{
    float4 arrWave[shaderslot::kGerstnerWaveCount];
    WaterTestUtil::makeWaves( arrWave );
    constexpr float32 kStep = 1.0e-2f;
    float32           worstDot{ 0.0f };
    for ( uint32 probe = 0; probe < 32; ++probe )
    {
        const float2  origin{ static_cast<float32>( probe ) * 0.77f, static_cast<float32>( probe ) * -0.41f };
        const float32 time    = 3.0f;
        auto          surface = [&]( const float2& point )
        {
            const float3 displacement = WaterWaveMath::computeDisplacement( point, time, WaterTestUtil::kGravity, arrWave );
            return float3{ point._x + displacement._x, displacement._y, point._y + displacement._z };
        };
        const float3 tangentX = surface( origin + float2{ kStep, 0.0f } ) - surface( origin - float2{ kStep, 0.0f } );
        const float3 tangentZ = surface( origin + float2{ 0.0f, kStep } ) - surface( origin - float2{ 0.0f, kStep } );
        const float3 normal   = WaterWaveMath::computeNormal( origin, time, WaterTestUtil::kGravity, arrWave );
        worstDot              = MathUtil::max( worstDot, MathUtil::max( MathUtil::abs( normal.dot( tangentX.normalize() ) ), MathUtil::abs( normal.dot( tangentZ.normalize() ) ) ) );
    }
    SW_EXPECT_TRUE( worstDot < 2.0e-3f );
}

/**
 * @brief [WaterTest] 호수 컴포넌트 — 덮는 자리 · 수면 높이(오너 높이 + 파도) · 물속 판정과 깊이 · 물속 안개 찾기
 */
SW_TEST_CASE( WaterTest, LakeQueriesAndUnderwater )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Lake" ) );
    SW_ASSERT_NOT_NULL( pObject );
    WaterBodyComponent* pWater = pObject->addComponent<WaterBodyComponent>();
    SW_ASSERT_NOT_NULL( pWater );
    pWater->setLocalPosition( float3{ 10.0f, 3.0f, -5.0f } );
    pWater->setSize( float2{ 20.0f, 10.0f } );
    pWater->setWaves( {
        GerstnerWave{ 0.4f, 8.0f, 0.2f, 0.5f }
    } );
    pWater->setMaterialPath( "" );
    pWater->rebuildSurface();
    pWater->setWaveTime( 1.5f );
    SW_EXPECT_TRUE( pWater->getSurfaceVertexCount() > 0 );

    SW_EXPECT_TRUE( pWater->coversPosition( 19.0f, -1.0f ) );
    SW_EXPECT_FALSE( pWater->coversPosition( 21.0f, -1.0f ) );
    float32 height{ 0.0f };
    SW_ASSERT_TRUE( pWater->computeSurfaceHeight( 12.0f, -4.0f, height ) );
    float4 arrWave[shaderslot::kGerstnerWaveCount];
    pWater->getWaveVectors( arrWave );
    SW_EXPECT_NEAR_EQUAL( 3.0f + WaterWaveMath::computeSurfaceHeight( float2{ 12.0f, -4.0f }, 1.5f, WaterTestUtil::kGravity, arrWave ), height, 1.0e-5f );

    float32 depth{ 0.0f };
    SW_EXPECT_TRUE( pWater->isUnderwater( float3{ 12.0f, 1.0f, -4.0f }, depth ) );
    SW_EXPECT_NEAR_EQUAL( height - 1.0f, depth, 1.0e-5f );
    SW_EXPECT_FALSE( pWater->isUnderwater( float3{ 12.0f, 4.0f, -4.0f }, depth ) );
    WaterUnderwaterFog fog;
    SW_EXPECT_TRUE( WaterBodyComponent::findUnderwaterFog( manager, float3{ 12.0f, 1.0f, -4.0f }, fog ) );
    SW_EXPECT_FALSE( WaterBodyComponent::findUnderwaterFog( manager, float3{ 50.0f, 1.0f, -4.0f }, fog ) );
    SW_EXPECT_TRUE( WaterBodyComponent::findWaterAt( manager, 12.0f, -4.0f ) == pWater );
}

/**
 * @brief [WaterTest] 강 — 경로 띠 안만 덮고, 기준 높이는 경로 점 높이를 따라 보간된다(파도 없이)
 */
SW_TEST_CASE( WaterTest, RiverFollowsItsPath )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "River" ) );
    SW_ASSERT_NOT_NULL( pObject );
    WaterBodyComponent* pWater = pObject->addComponent<WaterBodyComponent>();
    SW_ASSERT_NOT_NULL( pWater );
    pWater->setShape( WaterBodyShape::River );
    pWater->setRiverPath( {
                              float3{ 0.0f, 4.0f,  0.0f},
                              float3{20.0f, 2.0f,  0.0f},
                              float3{20.0f, 0.0f, 20.0f}
    },
                          4.0f );
    pWater->setMaterialPath( "" );
    pWater->rebuildSurface();
    SW_EXPECT_TRUE( pWater->getSurfaceVertexCount() > 0 );

    float32 height{ 0.0f };
    SW_ASSERT_TRUE( pWater->computeSurfaceHeight( 10.0f, 1.5f, height ) );
    SW_EXPECT_NEAR_EQUAL( 3.0f, height, 1.0e-5f );
    SW_ASSERT_TRUE( pWater->computeSurfaceHeight( 21.0f, 10.0f, height ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, height, 1.0e-5f );
    SW_EXPECT_FALSE( pWater->computeSurfaceHeight( 10.0f, 3.0f, height ) ); // 반폭 2 밖
    SW_EXPECT_FALSE( pWater->coversPosition( 10.0f, 10.0f ) );
}
