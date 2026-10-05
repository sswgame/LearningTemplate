#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/2D/ParallaxLayerComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

#include "TestFramework/TestFramework.h"

// ParallaxLayerTest — 2D 시차 레이어: 카메라에 대한 배율 · 되풀이 길이로 감기 · 저작 자리 복원. 디바이스 없음(nogpu).

/**
 * @brief [ParallaxLayerTest] 배율 1 은 월드와 같이, 0 은 카메라에 붙고, 0.5 는 카메라가 간 거리의 절반만큼 레이어가 따라간다
 * @details Godot `ParallaxLayer.motion_scale` 과 같은 뜻이다. 레이어 자리 = 저작 자리 + (카메라 − 기준) × (1 − 배율). 축마다 따로다.
 */
SW_TEST_CASE( ParallaxLayerTest, ScrollFactorScalesTheCameraMotion )
{
    const sw::float2 origin{ 1.0f, -2.0f };
    const sw::float2 reference{ 4.0f, 0.0f };
    const sw::float2 camera{ 14.0f, 6.0f };
    const sw::float2 noRepeat{ 0.0f, 0.0f };

    const sw::float2 world = sw::ParallaxLayerComponent::computeLayerPosition( origin, camera, reference, sw::float2{ 1.0f, 1.0f }, noRepeat );
    SW_EXPECT_NEAR_EQUAL( 1.0f, world._x, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( -2.0f, world._y, 1e-5f );

    const sw::float2 sky = sw::ParallaxLayerComponent::computeLayerPosition( origin, camera, reference, sw::float2{ 0.0f, 0.0f }, noRepeat );
    SW_EXPECT_NEAR_EQUAL( 1.0f + 10.0f, sky._x, 1e-5f ); // 카메라가 간 만큼 그대로 — 화면에서 멈춰 있다
    SW_EXPECT_NEAR_EQUAL( -2.0f + 6.0f, sky._y, 1e-5f );

    const sw::float2 half = sw::ParallaxLayerComponent::computeLayerPosition( origin, camera, reference, sw::float2{ 0.5f, 0.25f }, noRepeat );
    SW_EXPECT_NEAR_EQUAL( 1.0f + 5.0f, half._x, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( -2.0f + 4.5f, half._y, 1e-5f );
    // 카메라가 기준점에 있으면 저작한 자리다.
    const sw::float2 atReference = sw::ParallaxLayerComponent::computeLayerPosition( origin, reference, reference, sw::float2{ 0.3f, 0.7f }, noRepeat );
    SW_EXPECT_NEAR_EQUAL( origin._x, atReference._x, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( origin._y, atReference._y, 1e-5f );
}

/**
 * @brief [ParallaxLayerTest] 되풀이 길이가 있으면 레이어는 카메라에서 반 칸 넘게 벗어나지 않고, 감지 않은 자리와는 늘 칸의 배수만큼만 다르다
 * @details 내용이 M 마다 같으므로 M 의 배수만큼 튀는 것은 보이지 않는다 — 그래서 카메라가 아무리 멀리 가도 끝없이 이어진다. 감지 않으면 배율 0.5 의
 *          레이어는 카메라가 1000 칸 가는 동안 500 칸 뒤처져 화면 밖으로 사라진다.
 */
SW_TEST_CASE( ParallaxLayerTest, RepeatSizeWrapsTheLayerAroundTheCamera )
{
    const sw::float2 origin{ 0.0f, 3.0f };
    const sw::float2 reference{ 0.0f, 0.0f };
    const sw::float2 scroll{ 0.5f, 0.5f };
    const sw::float2 repeat{ 8.0f, 0.0f }; // X 만 감는다
    for ( float32 cameraX = -1000.0f; cameraX <= 1000.0f; cameraX += 37.25f )
    {
        const sw::float2 camera{ cameraX, 50.0f };
        const sw::float2 wrapped   = sw::ParallaxLayerComponent::computeLayerPosition( origin, camera, reference, scroll, repeat );
        const sw::float2 unwrapped = sw::ParallaxLayerComponent::computeLayerPosition( origin, camera, reference, scroll, sw::float2{ 0.0f, 0.0f } );
        const float32    relative  = wrapped._x - cameraX - ( origin._x - reference._x );
        SW_EXPECT_TRUE( -4.0f - 1e-3f <= relative && relative <= 4.0f + 1e-3f );
        const float32 cells = ( unwrapped._x - wrapped._x ) / repeat._x;
        SW_EXPECT_NEAR_EQUAL( sw::MathUtil::round( cells ), cells, 1e-3f );
        SW_EXPECT_NEAR_EQUAL( unwrapped._y, wrapped._y, 1e-4f ); // 감지 않는 축은 그대로
    }
    // 한 칸 안에서는 감지 않은 것과 같다.
    const sw::float2 nearStart = sw::ParallaxLayerComponent::computeLayerPosition( origin, sw::float2{ 6.0f, 0.0f }, reference, scroll, repeat );
    SW_EXPECT_NEAR_EQUAL( 3.0f, nearStart._x, 1e-5f );
}

/**
 * @brief [ParallaxLayerTest] 컴포넌트는 게임 카메라를 따라 자기 오브젝트(자식 포함)의 로컬 X · Y 를 옮기고 Z 는 두며, 플레이가 끝나면 저작 자리로 돌아간다
 */
SW_TEST_CASE( ParallaxLayerTest, LayerFollowsTheGameCameraAndRestoresOnEndPlay )
{
    sw::Scene scene( "ParallaxScene" );
    SW_ASSERT_TRUE( scene.ensureDefaultCameras() );
    sw::CameraComponent* pCamera = scene.getActiveGameCamera();
    SW_ASSERT_NOT_NULL( pCamera );
    pCamera->setLocalPosition( sw::float3{ 20.0f, 0.0f, -10.0f } );

    sw::GameObject* pLayerObject = scene.getObjectManager()->createGameObject( sw::hashed_string( "Hills" ) );
    SW_ASSERT_NOT_NULL( pLayerObject );
    sw::SceneComponent* pRoot = pLayerObject->addComponent<sw::SceneComponent>();
    SW_ASSERT_NOT_NULL( pRoot );
    pRoot->setLocalPosition( sw::float3{ 1.0f, 2.0f, 5.0f } );
    sw::ParallaxLayerComponent* pLayer = pLayerObject->addComponent<sw::ParallaxLayerComponent>();
    SW_ASSERT_NOT_NULL( pLayer );
    pLayer->setScrollFactor( sw::float2{ 0.25f, 1.0f } );

    pLayer->onBeginPlay();
    pLayer->onTick( 0.016f );
    const sw::float3 moved = pRoot->getLocalPosition();
    SW_EXPECT_NEAR_EQUAL( 1.0f + 20.0f * 0.75f, moved._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, moved._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, moved._z, 1e-4f );

    // 다음 틱은 저작 자리에서 다시 잰다(누적되지 않는다).
    pLayer->onTick( 0.016f );
    SW_EXPECT_NEAR_EQUAL( moved._x, pRoot->getLocalPosition()._x, 1e-4f );

    pLayer->onEndPlay();
    SW_EXPECT_NEAR_EQUAL( 1.0f, pRoot->getLocalPosition()._x, 1e-4f );
}
