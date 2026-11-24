#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Environment/Rain/RainOcclusionComponent.h"
#include "Engine/Environment/Rain/RainOcclusionSettings.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

// 비 가림 맵 — 카메라를 따르는 정사영의 텍셀 스냅, 위에 있는 면이 더 가까운 깊이, 씬에서 찾기. 셰이더 읽기(computeRainExposure)는 시나리오(rainocclusion)가 본다.

using namespace sw;

namespace
{
    struct RainOcclusionTestUtil
    {
        static RainOcclusionSettings makeSettings()
        {
            RainOcclusionSettings settings;
            settings._extent      = 64.0f;
            settings._heightAbove = 60.0f;
            settings._depthBelow  = 40.0f;
            settings._resolution  = 512;
            return settings;
        }

        static float3 project( const float4x4& viewProj, const float3& point )
        {
            const float4 clip = float4::transform( float4{ point._x, point._y, point._z, 1.0f }, viewProj );
            return float3{ clip._x / clip._w, clip._y / clip._w, clip._z / clip._w };
        }
    };
} // namespace

/** @brief [RainOcclusionTest] 해상도 0(품질이 껐다 · 컴포넌트 없음)이면 맵이 없다 */
SW_TEST_CASE( RainOcclusionTest, ResolutionZeroMeansNoMap )
{
    RainOcclusionSettings settings = RainOcclusionTestUtil::makeSettings();
    SW_EXPECT_TRUE( settings.isEnabled() );
    settings._resolution = 0;
    SW_EXPECT_FALSE( settings.isEnabled() );
}

/** @brief [RainOcclusionTest] 텍셀 안에서 움직인 카메라는 같은 눈을 낸다 — 맵이 프레임마다 떨리지 않는다 */
SW_TEST_CASE( RainOcclusionTest, EyeSnapsToTheTexelGrid )
{
    const RainOcclusionSettings settings = RainOcclusionTestUtil::makeSettings(); // 텍셀 0.125 m
    const float3                first    = settings.computeEye( float3{ 10.01f, 1.6f, -3.01f } );
    const float3                second   = settings.computeEye( float3{ 10.10f, 1.9f, -3.05f } );
    SW_EXPECT_NEAR( first._x, second._x, 1.0e-5f );
    SW_EXPECT_NEAR( first._y, second._y, 1.0e-5f );
    SW_EXPECT_NEAR( first._z, second._z, 1.0e-5f );
    SW_EXPECT_NEAR( 61.0f, first._y, 1.0e-5f );
}

/** @brief [RainOcclusionTest] 투영은 덮는 정사각형을 NDC 에 채우고, 위에 있는 면일수록 깊이가 작다 — 지붕 아래의 점은 지붕보다 깊다 */
SW_TEST_CASE( RainOcclusionTest, HigherSurfacesAreCloser )
{
    using Util                           = RainOcclusionTestUtil;
    const RainOcclusionSettings settings = Util::makeSettings();
    const float3                eye      = settings.computeEye( float3{ 0.0f, 0.0f, 0.0f } );
    const float4x4              viewProj = settings.computeViewProjection( eye );
    const float3                roof     = Util::project( viewProj, float3{ 0.0f, 3.0f, 0.0f } );
    const float3                floor    = Util::project( viewProj, float3{ 0.0f, 0.0f, 0.0f } );
    SW_EXPECT_TRUE( roof._z < floor._z );
    SW_EXPECT_NEAR( 0.03f, floor._z - roof._z, 1.0e-4f ); // 3 m / 100 m
    SW_EXPECT_NEAR( 0.0f, floor._x, 1.0e-4f );
    const float3 edge = Util::project( viewProj, float3{ 32.0f, 0.0f, 0.0f } );
    SW_EXPECT_NEAR( 1.0f, MathUtil::abs( edge._x ) + MathUtil::abs( edge._y ), 1.0e-3f );
    SW_EXPECT_NEAR( 0.0015f, settings.makeShaderParams()._x, 1.0e-6f ); // 0.15 m / 100 m
}

/** @brief [RainOcclusionTest] 씬에 켜진 비 가림이 없으면 false 이고, 있으면 그 값이다(해상도는 품질이 채우므로 0) */
SW_TEST_CASE( RainOcclusionTest, FindSkipsInactive )
{
    GameObjectManager     manager;
    RainOcclusionSettings settings;
    SW_EXPECT_FALSE( RainOcclusionComponent::findRainOcclusion( manager, settings ) );
    GameObject*             pObject = manager.createGameObject( hashed_string( "Rain" ) );
    RainOcclusionComponent* pRain   = pObject != nullptr ? pObject->addComponent<RainOcclusionComponent>() : nullptr;
    SW_REQUIRE_NOT_NULL( pRain );
    SW_EXPECT_TRUE( RainOcclusionComponent::findRainOcclusion( manager, settings ) );
    SW_EXPECT_EQUAL( 0u, settings._resolution );
    pRain->setActive( false );
    SW_EXPECT_FALSE( RainOcclusionComponent::findRainOcclusion( manager, settings ) );
}
