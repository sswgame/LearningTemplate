#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/2D/PixelPerfectCameraComponent.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/CameraRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/MeshInstanceBatch.h"
#include "Engine/Renderer/Scene/GpuSceneBuilder.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Scene/Scene.h"

#include "TestFramework/TestFramework.h"

// PixelPerfectCameraTest — 픽셀 아트 카메라: 정수 배율 · 직교 높이 · 격자 스냅 · 레터박스 띠 · 스프라이트 스냅 단위 전파. 디바이스 없음(nogpu).

/**
 * @brief [PixelPerfectCameraTest] 배율은 두 축이 모두 들어가는 가장 큰 정수이고, 직교 높이는 자산 픽셀 하나가 화면 픽셀 배율 칸이 되게 고른다
 * @details 기준 320 × 180 · PPU 16. 1280 × 720 은 배율 4(띠 없음), 1366 × 768 은 배율 4 에 좌우 43 · 위아래 24 픽셀 띠, 울트라와이드 2560 × 1080 은
 *          세로가 정하는 배율 6 에 좌우 320 픽셀 띠, 기준보다 작은 뷰포트는 배율 1(잘리지 않는다). 직교 높이 = 높이 / (배율 × PPU).
 */
SW_TEST_CASE( PixelPerfectCameraTest, ZoomIsTheLargestIntegerThatFitsBothAxes )
{
    const sw::float2             reference{ 320.0f, 180.0f };
    const sw::PixelPerfectLayout hd = sw::PixelPerfectCameraComponent::computeLayout( reference, 16.0f, 1280, 720, true, true );
    SW_EXPECT_EQUAL( 4, hd._zoom );
    SW_EXPECT_NEAR_EQUAL( 720.0f / 64.0f, hd._orthoHeight, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 1.0f / 64.0f, hd._screenPixelUnit, 1e-7f );
    SW_EXPECT_NEAR_EQUAL( 1.0f / 16.0f, hd._assetPixelUnit, 1e-7f );
    SW_EXPECT_EQUAL( 0, hd._barWidthPixels );
    SW_EXPECT_EQUAL( 0, hd._barHeightPixels );

    const sw::PixelPerfectLayout laptop = sw::PixelPerfectCameraComponent::computeLayout( reference, 16.0f, 1366, 768, true, true );
    SW_EXPECT_EQUAL( 4, laptop._zoom );
    SW_EXPECT_EQUAL( 43, laptop._barWidthPixels );
    SW_EXPECT_EQUAL( 24, laptop._barHeightPixels );
    SW_EXPECT_NEAR_EQUAL( 768.0f / 64.0f, laptop._orthoHeight, 1e-5f );
    // 자르기를 끄면 띠가 없다(배율 · 높이는 같다 — 더 넓게 보일 뿐).
    const sw::PixelPerfectLayout uncropped = sw::PixelPerfectCameraComponent::computeLayout( reference, 16.0f, 1366, 768, false, false );
    SW_EXPECT_EQUAL( 4, uncropped._zoom );
    SW_EXPECT_EQUAL( 0, uncropped._barWidthPixels );

    const sw::PixelPerfectLayout ultrawide = sw::PixelPerfectCameraComponent::computeLayout( reference, 16.0f, 2560, 1080, true, false );
    SW_EXPECT_EQUAL( 6, ultrawide._zoom );
    SW_EXPECT_EQUAL( 320, ultrawide._barWidthPixels );
    SW_EXPECT_EQUAL( 0, ultrawide._barHeightPixels );

    const sw::PixelPerfectLayout tiny = sw::PixelPerfectCameraComponent::computeLayout( reference, 16.0f, 300, 170, true, true );
    SW_EXPECT_EQUAL( 1, tiny._zoom );
    SW_EXPECT_EQUAL( 0, tiny._barWidthPixels );
    SW_EXPECT_EQUAL( 0, tiny._barHeightPixels );
}

/**
 * @brief [PixelPerfectCameraTest] 격자 스냅은 가장 가까운 격자점이고 반은 위로 간다 — 음수도 같은 규칙이다(0 을 사이에 두고 갈라지지 않는다)
 */
SW_TEST_CASE( PixelPerfectCameraTest, SnapRoundsToTheNearestGridPoint )
{
    const float32 unit = 1.0f / 64.0f;
    SW_EXPECT_NEAR_EQUAL( 66.0f / 64.0f, sw::PixelPerfectCameraComponent::snapToGrid( 1.03f, unit ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( -66.0f / 64.0f, sw::PixelPerfectCameraComponent::snapToGrid( -1.03f, unit ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, sw::PixelPerfectCameraComponent::snapToGrid( -0.5f * unit, unit ), 1e-6f ); // 반은 위로
    SW_EXPECT_NEAR_EQUAL( unit, sw::PixelPerfectCameraComponent::snapToGrid( 0.5f * unit, unit ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.2345f, sw::PixelPerfectCameraComponent::snapToGrid( 1.2345f, 0.0f ), 1e-6f ); // 단위 0 은 끔
}

/**
 * @brief [PixelPerfectCameraTest] 카메라에 붙이면 직교 · 높이를 맞추고 그리는 눈만 격자에 붙이며(트랜스폼은 그대로), 스냅 단위가 씬의 스프라이트와
 *        나중에 생긴 스프라이트에도 실리고, 띠가 기준 해상도 밖을 덮는다
 * @details 트랜스폼을 반올림하면 감쇠로 따라가는 카메라가 반 픽셀 아래의 움직임을 잃고 멈춘다 — 그래서 `CameraComponent::setViewOffset` 만 바꾼다.
 *          스냅 단위는 sprite2d.hlsl 이 읽는 인스턴스 칸(`GpuSpriteInstanceData::_pixelSnap`)으로 간다.
 */
SW_TEST_CASE( PixelPerfectCameraTest, CameraSnapsTheEyeAndSpritesReceiveTheSnapUnit )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::Scene scene( "PixelPerfectScene" );
    SW_ASSERT_TRUE( scene.ensureDefaultCameras() );
    sw::GameObjectManager* pManager = scene.getObjectManager();
    sw::CameraComponent*   pCamera  = scene.getActiveGameCamera();
    SW_ASSERT_NOT_NULL( pCamera );
    pCamera->setLocalPosition( sw::float3{ 1.003f, -2.0f, -10.0f } );

    sw::GameObject* pEarly = pManager->createGameObject( sw::hashed_string( "EarlySprite" ) );
    SW_ASSERT_NOT_NULL( pEarly );
    sw::SpriteComponent* pEarlySprite = pEarly->addComponent<sw::SpriteComponent>();
    SW_ASSERT_NOT_NULL( pEarlySprite );

    sw::PixelPerfectCameraComponent* pPixel = pCamera->getOwner()->addComponent<sw::PixelPerfectCameraComponent>();
    SW_ASSERT_NOT_NULL( pPixel );
    pPixel->setCrop( true, true );
    pPixel->onBeginPlay();
    pManager->getCameraRegistry().setViewportSize( 1366, 768 );
    pPixel->onTick( 0.016f );

    const float32 unit = 1.0f / 16.0f; // 스프라이트는 자산 픽셀 격자(1 / PPU), 눈은 화면 픽셀 격자(1 / (배율 × PPU))
    SW_EXPECT_TRUE( pCamera->isOrthographic() );
    SW_EXPECT_NEAR_EQUAL( 12.0f, pCamera->getOrthoHeight(), 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 1.003f, pCamera->getWorldPosition()._x, 1e-6f );         // 트랜스폼은 그대로
    SW_EXPECT_NEAR_EQUAL( 64.0f / 64.0f, pCamera->getCameraPosition()._x, 1e-6f ); // 그리는 눈은 격자 위
    SW_EXPECT_NEAR_EQUAL( unit, pManager->getCameraRegistry().getPixelSnapUnit(), 1e-7f );
    SW_EXPECT_NEAR_EQUAL( unit, pEarlySprite->getSpriteInstanceData()._pixelSnap, 1e-7f );

    // 나중에 생긴 스프라이트도 등록될 때 단위를 받고, 프레임 · 색을 바꿔도 단위는 남는다.
    sw::GameObject*      pLate       = pManager->createGameObject( sw::hashed_string( "LateSprite" ) );
    sw::SpriteComponent* pLateSprite = pLate->addComponent<sw::SpriteComponent>();
    SW_ASSERT_NOT_NULL( pLateSprite );
    pLateSprite->setTint( sw::float4{ 1.0f, 0.0f, 0.0f, 1.0f } );
    SW_EXPECT_NEAR_EQUAL( unit, pLateSprite->getSpriteInstanceData()._pixelSnap, 1e-7f );

    // 빌더가 그 칸을 GPU 인스턴스로 옮긴다.
    sw::GpuSceneBuilder builder;
    builder.buildFromScene( &scene, pCamera->getCameraPosition() );
    bool bAllSnapped = builder.getInstances().empty() == false;
    for ( const sw::GpuInstance& instance : builder.getInstances() )
    {
        if ( instance._sprite._pixelSnap != unit && instance._sprite.getTint()._x > 0.5f )
            bAllSnapped = false; // 띠(검정)는 스냅하지 않는다
    }
    SW_EXPECT_TRUE( bAllSnapped );

    // 1366 × 768 에서 기준 1280 × 720 밖을 덮는 띠 넷이 보인다.
    SW_EXPECT_EQUAL( 43, pPixel->getLayout()._barWidthPixels );
    SW_EXPECT_EQUAL( 24, pPixel->getLayout()._barHeightPixels );

    // 끝나면 단위와 눈 오프셋을 거둔다.
    pPixel->onEndPlay();
    SW_EXPECT_NEAR_EQUAL( 0.0f, pManager->getCameraRegistry().getPixelSnapUnit(), 1e-7f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pEarlySprite->getSpriteInstanceData()._pixelSnap, 1e-7f );
    SW_EXPECT_NEAR_EQUAL( 1.003f, pCamera->getCameraPosition()._x, 1e-6f );
}
