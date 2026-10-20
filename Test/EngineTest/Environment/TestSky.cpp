#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Environment/Sky/SkyComponent.h"
#include "Engine/Environment/Sky/SkyGradient.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "TestFramework/TestFramework.h"

// 하늘 — 그러데이션 식(천정 · 지평선 · 땅, 지수, 세기)과 씬의 하늘 찾기. 같은 식의 셰이더(sky.hlsl evaluateSky)는 GPU 캡처로 본다.

using namespace sw;

namespace
{
    struct SkyTestUtil
    {
        static SkyGradient makeGradient()
        {
            SkyGradient gradient;
            gradient._zenithColor     = float3{ 0.0f, 0.0f, 1.0f };
            gradient._horizonColor    = float3{ 1.0f, 1.0f, 1.0f };
            gradient._groundColor     = float3{ 0.0f, 1.0f, 0.0f };
            gradient._intensity       = 2.0f;
            gradient._horizonExponent = 2.0f;
            return gradient;
        }
    };
} // namespace

/**
 * @brief [SkyTest] 바로 위는 천정 색, 바로 아래는 땅 색, 수평은 지평선 색이고 모두 세기를 곱한다. 방향의 길이는 결과를 바꾸지 않는다
 */
SW_TEST_CASE( SkyTest, GradientPolesAndHorizonMatchColorsTimesIntensity )
{
    const SkyGradient gradient = SkyTestUtil::makeGradient();

    const float3 zenith = gradient.evaluateRadiance( float3{ 0.0f, 5.0f, 0.0f } );
    SW_EXPECT_NEAR_EQUAL( 0.0f, zenith._x, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, zenith._z, 1.0e-5f );

    const float3 ground = gradient.evaluateRadiance( float3{ 0.0f, -1.0f, 0.0f } );
    SW_EXPECT_NEAR_EQUAL( 2.0f, ground._y, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, ground._z, 1.0e-5f );

    const float3 horizon = gradient.evaluateRadiance( float3{ 3.0f, 0.0f, 4.0f } );
    SW_EXPECT_NEAR_EQUAL( 2.0f, horizon._x, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, horizon._y, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, horizon._z, 1.0e-5f );

    // 0 벡터는 지평선으로 본다(나누기 0 이 아니다).
    const float3 degenerate = gradient.evaluateRadiance( float3{} );
    SW_EXPECT_NEAR_EQUAL( 2.0f, degenerate._x, 1.0e-5f );
}

/**
 * @brief [SkyTest] 30 도 위는 지평선 비율 (1 - 0.5)^2 = 0.25 로 천정과 지평선을 섞는다 — 셰이더와 같은 식
 */
SW_TEST_CASE( SkyTest, HorizonWeightFollowsExponent )
{
    const SkyGradient gradient = SkyTestUtil::makeGradient();
    const float3      up30     = gradient.evaluateRadiance( float3{ MathUtil::sqrt( 3.0f ), 1.0f, 0.0f } );
    // 천정(0,0,1)과 지평선(1,1,1)을 0.25 로 섞으면 (0.25, 0.25, 1) — 세기 2 를 곱한다.
    SW_EXPECT_NEAR_EQUAL( 0.5f, up30._x, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, up30._z, 1.0e-4f );
}

/**
 * @brief [SkyTest] 씬에 하늘이 없으면 찾기가 false 이고 세기 0 이며, 비활성 하늘은 건너뛰고 활성 하늘의 값을 돌려준다
 */
SW_TEST_CASE( SkyTest, FindSkySkipsInactiveAndReportsAbsence )
{
    GameObjectManager manager;
    SkyGradient       gradient;
    SW_EXPECT_FALSE( SkyComponent::findSky( manager, gradient ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, gradient._intensity, 1.0e-6f );

    GameObject*   pInactiveObject = manager.createGameObject( hashed_string( "InactiveSky" ) );
    SkyComponent* pInactive       = pInactiveObject != nullptr ? pInactiveObject->addComponent<SkyComponent>() : nullptr;
    SW_ASSERT_NOT_NULL( pInactive );
    pInactive->setIntensity( 5.0f );
    pInactive->setActive( false );

    GameObject*   pSkyObject = manager.createGameObject( hashed_string( "Sky" ) );
    SkyComponent* pSky       = pSkyObject != nullptr ? pSkyObject->addComponent<SkyComponent>() : nullptr;
    SW_ASSERT_NOT_NULL( pSky );
    pSky->setIntensity( 3.0f );

    SW_EXPECT_TRUE( SkyComponent::findSky( manager, gradient ) );
    SW_EXPECT_NEAR_EQUAL( 3.0f, gradient._intensity, 1.0e-6f );
}
