#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Environment/Sky/SkyComponent.h"
#include "Engine/Environment/Sky/SkyGradient.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

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

    struct AtmosphereTestUtil
    {
        /// 대기 모드의 하늘 하나 — 해는 해 쪽 방향 @p toSun, 흰색, 땅 반사율 @p groundAlbedo 입니다.
        static SkyGradient makeAtmosphere( const float3& toSun, float32 groundAlbedo )
        {
            SkyGradient sky;
            sky._mode        = SkyMode::Atmosphere;
            sky._intensity   = 1.0f;
            sky._groundColor = float3{ groundAlbedo, groundAlbedo, groundAlbedo };
            sky.setSun( toSun * -1.0f, float3{ 1.0f, 1.0f, 1.0f } ); // setSun 은 빛이 나아가는 방향을 받는다
            return sky;
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
    SW_EXPECT_NEAR( 0.0f, zenith._x, 1.0e-5f );
    SW_EXPECT_NEAR( 2.0f, zenith._z, 1.0e-5f );

    const float3 ground = gradient.evaluateRadiance( float3{ 0.0f, -1.0f, 0.0f } );
    SW_EXPECT_NEAR( 2.0f, ground._y, 1.0e-5f );
    SW_EXPECT_NEAR( 0.0f, ground._z, 1.0e-5f );

    const float3 horizon = gradient.evaluateRadiance( float3{ 3.0f, 0.0f, 4.0f } );
    SW_EXPECT_NEAR( 2.0f, horizon._x, 1.0e-5f );
    SW_EXPECT_NEAR( 2.0f, horizon._y, 1.0e-5f );
    SW_EXPECT_NEAR( 2.0f, horizon._z, 1.0e-5f );

    // 0 벡터는 지평선으로 본다(나누기 0 이 아니다).
    const float3 degenerate = gradient.evaluateRadiance( float3{} );
    SW_EXPECT_NEAR( 2.0f, degenerate._x, 1.0e-5f );
}

/**
 * @brief [SkyTest] 30 도 위는 지평선 비율 (1 - 0.5)^2 = 0.25 로 천정과 지평선을 섞는다 — 셰이더와 같은 식
 */
SW_TEST_CASE( SkyTest, HorizonWeightFollowsExponent )
{
    const SkyGradient gradient = SkyTestUtil::makeGradient();
    const float3      up30     = gradient.evaluateRadiance( float3{ MathUtil::sqrt( 3.0f ), 1.0f, 0.0f } );
    // 천정(0,0,1)과 지평선(1,1,1)을 0.25 로 섞으면 (0.25, 0.25, 1) — 세기 2 를 곱한다.
    SW_EXPECT_NEAR( 0.5f, up30._x, 1.0e-4f );
    SW_EXPECT_NEAR( 2.0f, up30._z, 1.0e-4f );
}

/**
 * @brief [SkyTest] 씬에 하늘이 없으면 찾기가 false 이고 세기 0 이며, 비활성 하늘은 건너뛰고 활성 하늘의 값을 돌려준다
 */
SW_TEST_CASE( SkyTest, FindSkySkipsInactiveAndReportsAbsence )
{
    GameObjectManager manager;
    SkyGradient       gradient;
    SW_EXPECT_FALSE( SkyComponent::findSky( manager, gradient ) );
    SW_EXPECT_NEAR( 0.0f, gradient._intensity, 1.0e-6f );

    GameObject*   pInactiveObject = manager.createGameObject( hashed_string( "InactiveSky" ) );
    SkyComponent* pInactive       = pInactiveObject != nullptr ? pInactiveObject->addComponent<SkyComponent>() : nullptr;
    SW_REQUIRE_NOT_NULL( pInactive );
    pInactive->setIntensity( 5.0f );
    pInactive->setActive( false );

    GameObject*   pSkyObject = manager.createGameObject( hashed_string( "Sky" ) );
    SkyComponent* pSky       = pSkyObject != nullptr ? pSkyObject->addComponent<SkyComponent>() : nullptr;
    SW_REQUIRE_NOT_NULL( pSky );
    pSky->setIntensity( 3.0f );

    SW_EXPECT_TRUE( SkyComponent::findSky( manager, gradient ) );
    SW_EXPECT_NEAR( 3.0f, gradient._intensity, 1.0e-6f );
}

/**
 * @brief [SkyTest] 해가 머리 위인 맑은 낮 하늘은 파랗다(파랑 > 빨강). 천정이 지평선 근처보다 푸르고 지평선은 더 하얗다
 */
SW_TEST_CASE( SkyTest, NoonAtmosphereIsBlue )
{
    const SkyGradient sky     = AtmosphereTestUtil::makeAtmosphere( float3{ 0.0f, 1.0f, 0.0f }, 0.3f );
    const float3      zenith  = sky.evaluateRadiance( float3{ 0.0f, 1.0f, 0.0f } );
    const float3      horizon = sky.evaluateRadiance( float3{ 1.0f, 0.05f, 0.0f } );
    SW_EXPECT_TRUE( zenith._z > zenith._x );
    SW_EXPECT_TRUE( zenith._x > 0.0f );
    // 지평선 쪽은 대기를 길게 지나 파랑이 흩어져 줄고 하얘진다(파랑 비율이 천정보다 낮다).
    SW_EXPECT_TRUE( horizon._z / horizon._x < zenith._z / zenith._x );
}

/**
 * @brief [SkyTest] 해가 지평선에 닿으면 해 쪽 하늘이 붉다(빨강 > 파랑)
 */
SW_TEST_CASE( SkyTest, SunsetTowardTheSunIsRed )
{
    const float3      toSun{ 1.0f, 0.02f, 0.0f };
    const SkyGradient sky       = AtmosphereTestUtil::makeAtmosphere( toSun * ( 1.0f / toSun.getLength() ), 0.3f );
    const float3      towardSun = sky.evaluateRadiance( toSun );
    SW_EXPECT_TRUE( towardSun._x > towardSun._z );
    // 해 반대쪽 하늘은 해 쪽보다 어둡다.
    const float3 away = sky.evaluateRadiance( float3{ -1.0f, 0.3f, 0.0f } );
    SW_EXPECT_TRUE( away._x + away._y + away._z < towardSun._x + towardSun._y + towardSun._z );
}

/**
 * @brief [SkyTest] 해가 행성 뒤로 넘어가면(지평선 아래 깊이) 하늘이 거의 어둡다 — 행성 그림자. 해가 없거나(색 0) 산란 계수가 0 이면 정확히 0 이다
 */
SW_TEST_CASE( SkyTest, NightAndMissingSunAreDark )
{
    const SkyGradient noon  = AtmosphereTestUtil::makeAtmosphere( float3{ 0.0f, 1.0f, 0.0f }, 0.3f );
    const float3      day   = noon.evaluateRadiance( float3{ 0.0f, 1.0f, 0.0f } );
    const float3      night = AtmosphereTestUtil::makeAtmosphere( float3{ 0.0f, -0.4f, 0.917f }, 0.3f ).evaluateRadiance( float3{ 0.0f, 1.0f, 0.0f } );
    SW_EXPECT_TRUE( night._x + night._y + night._z < 0.05f * ( day._x + day._y + day._z ) );

    SkyGradient noSun = noon;
    noSun.setSun( float3{}, float3{ 1.0f, 1.0f, 1.0f } ); // 길이 0 의 방향 = 해 없음
    const float3 dark = noSun.evaluateRadiance( float3{ 0.0f, 1.0f, 0.0f } );
    SW_EXPECT_NEAR( 0.0f, dark._x + dark._y + dark._z, 1.0e-7f );

    SkyGradient clear                = noon;
    clear._atmosphere._rayleighScale = 0.0f;
    clear._atmosphere._mieScale      = 0.0f;
    const float3 empty               = clear.evaluateRadiance( float3{ 0.0f, 1.0f, 0.0f } );
    SW_EXPECT_NEAR( 0.0f, empty._x + empty._y + empty._z, 1.0e-7f );
}

/**
 * @brief [SkyTest] 아래를 보면 행성 표면이 보인다 — 땅 반사율이 클수록 밝고 0 이면 산란만 남는다. 방향의 길이는 결과를 바꾸지 않는다
 */
SW_TEST_CASE( SkyTest, GroundAlbedoLightsTheDownwardView )
{
    const SkyGradient bright = AtmosphereTestUtil::makeAtmosphere( float3{ 0.0f, 1.0f, 0.0f }, 0.8f );
    const SkyGradient dull   = AtmosphereTestUtil::makeAtmosphere( float3{ 0.0f, 1.0f, 0.0f }, 0.0f );
    const float3      down{ 0.3f, -1.0f, 0.2f };
    const float3      lit  = bright.evaluateRadiance( down );
    const float3      base = dull.evaluateRadiance( down );
    SW_EXPECT_TRUE( lit._x + lit._y + lit._z > base._x + base._y + base._z );
    const float3 scaled = bright.evaluateRadiance( down * 7.0f );
    SW_EXPECT_NEAR( lit._y, scaled._y, 1.0e-5f );
}

/**
 * @brief [SkyTest] 대기 모드의 해는 씬의 첫 활성 방향광이다 — 해 쪽은 빛이 나아가는 방향의 반대이고, 방향광이 없으면 해 색이 0 이다. 구름 값은 컴포넌트에서 그대로 온다
 */
SW_TEST_CASE( SkyTest, FindSkyTakesTheSunFromTheDirectionalLight )
{
    GameObjectManager manager;
    GameObject*       pSkyObject = manager.createGameObject( hashed_string( "Sky" ) );
    SkyComponent*     pSky       = pSkyObject != nullptr ? pSkyObject->addComponent<SkyComponent>() : nullptr;
    SW_REQUIRE_NOT_NULL( pSky );
    SkyGradient gradient;
    SW_REQUIRE_TRUE( SkyComponent::findSky( manager, gradient ) );
    SW_EXPECT_NEAR( 0.0f, gradient._sunColor._x + gradient._sunColor._y + gradient._sunColor._z, 1.0e-7f ); // 방향광 없음

    GameObject*                pLightObject = manager.createGameObject( hashed_string( "Sun" ) );
    DirectionalLightComponent* pLight       = pLightObject != nullptr ? pLightObject->addComponent<DirectionalLightComponent>() : nullptr;
    SW_REQUIRE_NOT_NULL( pLight );
    pLight->setColor( float3{ 1.0f, 0.5f, 0.25f } );
    SW_REQUIRE_TRUE( SkyComponent::findSky( manager, gradient ) );
    const float3 travel = pLight->getLightDirection();
    SW_EXPECT_NEAR( -travel._x, gradient._toSunDirection._x, 1.0e-5f );
    SW_EXPECT_NEAR( -travel._y, gradient._toSunDirection._y, 1.0e-5f );
    SW_EXPECT_NEAR( -travel._z, gradient._toSunDirection._z, 1.0e-5f );
    SW_EXPECT_NEAR( 0.5f, gradient._sunColor._y, 1.0e-6f );
    SW_EXPECT_TRUE( gradient._clouds._bEnabled == SW_FALSE ); // 구름 기본은 끔
}
