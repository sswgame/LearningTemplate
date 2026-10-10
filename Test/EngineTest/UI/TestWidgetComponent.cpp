#include "pch.h"

#include "Core/Math/MatrixMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Screen/UiScreen.h"
#include "Engine/UI/UiSystem.h"
#include "Engine/UI/Widgets/ImageWidget.h"
#include "Engine/UI/World/WidgetComponent.h"

#include "EngineTest/UI/UiTestWidgets.h"

#include "TestFramework/TestFramework.h"

// WidgetComponentTest — 오브젝트에 붙는 UI: 화면 마커 투영 · 카메라 뒤 · 최대 거리 · 거리 배율 · UI 시스템 마커 화면의 자리. 디바이스 없음(nogpu).

namespace
{
    struct WidgetComponentTestUtil
    {
        static constexpr float32 kWidth  = 1920.0f;
        static constexpr float32 kHeight = 1080.0f;

        /** @brief 원점에서 +z 를 보는 카메라(세로 시야 1 rad, 16:9)의 뷰 · 투영입니다. */
        static sw::float4x4 makeViewProjection()
        {
            const sw::float4x4 view = sw::float4x4::createLookAt( sw::float3{ 0.0f, 0.0f, 0.0f }, sw::float3{ 0.0f, 0.0f, 1.0f }, sw::float3{ 0.0f, 1.0f, 0.0f } );
            return view * sw::float4x4::createPerspectiveFieldOfView( 1.0f, kWidth / kHeight, 0.1f, 100.0f );
        }

        static sw::WidgetMarkerPlacement place( const sw::WidgetComponent& component, const sw::float3& worldPosition )
        {
            return component.computeMarkerPlacement( makeViewProjection(), sw::float3{}, worldPosition, sw::float2{ kWidth, kHeight } );
        }
    };
} // namespace

/** @brief [WidgetComponentTest] 카메라 정면 5 m 의 점은 화면 가운데(UI 단위), 오른쪽 1 m 는 가운데 오른쪽, 위 1 m 는 가운데 위다 */
SW_TEST_CASE( WidgetComponentTest, ScreenMarkerProjectsWorldPoint )
{
    using Util = WidgetComponentTestUtil;
    sw::WidgetComponent             component;
    const sw::WidgetMarkerPlacement center = Util::place( component, sw::float3{ 0.0f, 0.0f, 5.0f } );
    SW_EXPECT_TRUE( center._bVisible == SW_TRUE );
    SW_EXPECT_NEAR_EQUAL( 960.0f, center._position._x, 0.01f );
    SW_EXPECT_NEAR_EQUAL( 540.0f, center._position._y, 0.01f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, center._distance, 1e-4f );
    const sw::WidgetMarkerPlacement right = Util::place( component, sw::float3{ 1.0f, 0.0f, 5.0f } );
    SW_EXPECT_TRUE( right._position._x > 1000.0f );
    SW_EXPECT_NEAR_EQUAL( 540.0f, right._position._y, 0.01f );
    const sw::WidgetMarkerPlacement up = Util::place( component, sw::float3{ 0.0f, 1.0f, 5.0f } );
    SW_EXPECT_TRUE( up._position._y < 500.0f );
}

/**
 * @brief [WidgetComponentTest] 카메라 뒤의 점은 숨기고, 가장자리 붙이기면 그쪽 변(오른쪽 뒤 → 오른쪽 변, 여백 24)에 붙인다 — 화면 밖 앞쪽 점도 같다
 * @details 변이: 카메라 뒤 판정을 빼면 뒤의 점이 거꾸로 투영되어 보이는 것으로 나와 진다.
 */
SW_TEST_CASE( WidgetComponentTest, BehindCameraHidesOrClamps )
{
    using Util = WidgetComponentTestUtil;
    sw::WidgetComponent component;
    SW_EXPECT_TRUE( Util::place( component, sw::float3{ 1.0f, 0.0f, -5.0f } )._bVisible == SW_FALSE );
    component.setClampToScreenEdge( true );
    const sw::WidgetMarkerPlacement behind = Util::place( component, sw::float3{ 1.0f, 0.0f, -5.0f } );
    SW_EXPECT_TRUE( behind._bVisible == SW_TRUE );
    SW_EXPECT_TRUE( behind._bClamped == SW_TRUE );
    SW_EXPECT_NEAR_EQUAL( 1896.0f, behind._position._x, 0.01f );
    SW_EXPECT_NEAR_EQUAL( 540.0f, behind._position._y, 0.01f );
    const sw::WidgetMarkerPlacement farLeft = Util::place( component, sw::float3{ -50.0f, 0.0f, 5.0f } );
    SW_EXPECT_TRUE( farLeft._bClamped == SW_TRUE );
    SW_EXPECT_NEAR_EQUAL( 24.0f, farLeft._position._x, 0.01f );
}

/** @brief [WidgetComponentTest] 최대 거리 밖이면 숨긴다(0 은 한도 없음) */
SW_TEST_CASE( WidgetComponentTest, MaxDistanceHides )
{
    using Util = WidgetComponentTestUtil;
    sw::WidgetComponent component;
    SW_EXPECT_TRUE( Util::place( component, sw::float3{ 0.0f, 0.0f, 50.0f } )._bVisible == SW_TRUE );
    component.setMaxDistance( 10.0f );
    SW_EXPECT_TRUE( Util::place( component, sw::float3{ 0.0f, 0.0f, 50.0f } )._bVisible == SW_FALSE );
    SW_EXPECT_TRUE( Util::place( component, sw::float3{ 0.0f, 0.0f, 5.0f } )._bVisible == SW_TRUE );
}

/** @brief [WidgetComponentTest] 거리 배율 = 기준 거리 / 거리(0.25 ~ 2 로 묶는다), 끄면 1 */
SW_TEST_CASE( WidgetComponentTest, ScaleWithDistance )
{
    using Util = WidgetComponentTestUtil;
    sw::WidgetComponent component;
    SW_EXPECT_NEAR_EQUAL( 1.0f, Util::place( component, sw::float3{ 0.0f, 0.0f, 20.0f } )._scale, 1e-6f );
    component.setScaleWithDistance( true, 10.0f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, Util::place( component, sw::float3{ 0.0f, 0.0f, 20.0f } )._scale, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, Util::place( component, sw::float3{ 0.0f, 0.0f, 1.0f } )._scale, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, Util::place( component, sw::float3{ 0.0f, 0.0f, 90.0f } )._scale, 1e-5f );
}

/**
 * @brief [WidgetComponentTest] 화면 마커는 UI 시스템의 HUD 마커 화면에 붙고, 자리 = 기준점 − 피벗 × 크기, 배율은 피벗 둘레 렌더 변환으로 기하에 얹힌다 · 숨김은 접기 ·
 *        등록을 풀면 마커 화면이 닫힌다
 * @details 변이: `Widget::setRenderTransform` 의 kArrange 를 빼면 배율만 바뀐 프레임에 기하가 그대로라 진다.
 */
SW_TEST_CASE( WidgetComponentTest, MarkerFollowsPlacementInUiSystem )
{
    sw::UiSystem   ui;
    sw::UiViewport viewport{};
    viewport._size         = sw::float2{ WidgetComponentTestUtil::kWidth, WidgetComponentTestUtil::kHeight };
    viewport._physicalSize = viewport._size;
    sw::WidgetComponent component;
    component.setDrawSize( sw::float2{ 100.0f, 20.0f } );
    component.setPivot( sw::float2{ 0.5f, 1.0f } );
    component.setContent( sw::make_unique<sw::uitest::TestBoxWidget>( "hp" ) );
    component.bindUiSystem( &ui );
    SW_ASSERT_EQUAL( 1u, ui.getScreenCount() );
    SW_EXPECT_TRUE( ui.getActiveScreen() == nullptr ); // HUD — 포커스를 받지 않는다

    sw::WidgetMarkerPlacement placement{};
    placement._position = sw::float2{ 500.0f, 300.0f };
    component.applyPlacement( placement );
    ui.update( 1.0f / 60.0f, viewport );
    sw::Widget* pMarker = component.getContent();
    SW_ASSERT_NOT_NULL( pMarker );
    SW_EXPECT_NEAR_EQUAL( 450.0f, pMarker->getGeometry().computeScreenBounds().getLeft(), 0.01f );
    SW_EXPECT_NEAR_EQUAL( 280.0f, pMarker->getGeometry().computeScreenBounds().getTop(), 0.01f );

    placement._scale = 2.0f;
    component.applyPlacement( placement );
    ui.update( 1.0f / 60.0f, viewport );
    const sw::UiRect scaled = pMarker->getGeometry().computeScreenBounds();
    SW_EXPECT_NEAR_EQUAL( 200.0f, scaled.getRight() - scaled.getLeft(), 0.01f );
    SW_EXPECT_NEAR_EQUAL( 300.0f, scaled.getBottom(), 0.01f ); // 피벗(가운데 아래)은 그대로

    placement._bVisible = SW_FALSE;
    component.applyPlacement( placement );
    SW_EXPECT_TRUE( pMarker->getVisibility() == sw::WidgetVisibility::Collapsed );

    component.bindUiSystem( nullptr );
    ui.update( 1.0f / 60.0f, viewport );
    SW_EXPECT_EQUAL( 0u, ui.getScreenCount() );
    SW_EXPECT_EQUAL( 0u, ui.getWidgetComponentCount() );
}

/**
 * @brief [WidgetComponentTest] 화면 마커의 기준점은 오브젝트 자리 + 월드 오프셋(머리 위 HP 바)이고, 코드가 숨기면(`setHidden`) 카메라 안이어도 숨는다
 * @details `updateScreenMarker` 는 게임 카메라를 스스로 찾는다 — 기대값은 같은 카메라 행렬로 `computeMarkerPlacement` 에 오프셋 더한 점을 넣은 것이다.
 *          변이: `updateScreenMarker` 가 `_worldOffset` 을 더하지 않으면 자리가 오브젝트 발밑이라 진다.
 */
SW_TEST_CASE( WidgetComponentTest, ScreenMarkerAnchorsAtWorldOffsetAndHides )
{
    sw::UiSystem   ui;
    sw::UiViewport viewport{};
    viewport._size         = sw::float2{ WidgetComponentTestUtil::kWidth, WidgetComponentTestUtil::kHeight };
    viewport._physicalSize = viewport._size;
    sw::GameObjectManager manager;
    sw::GameObject*       pCameraObject = manager.createGameObject( sw::hashed_string( "Camera" ) );
    SW_ASSERT_NOT_NULL( pCameraObject );
    sw::CameraComponent* pCamera = pCameraObject->addComponent<sw::CameraComponent>();
    SW_ASSERT_NOT_NULL( pCamera );
    pCamera->setLocalPosition( sw::float3{ 0.0f, 1.0f, -10.0f } );
    sw::GameObject* pTarget = manager.createGameObject( sw::hashed_string( "Target" ) );
    SW_ASSERT_NOT_NULL( pTarget );
    sw::SceneComponent* pRoot = pTarget->addComponent<sw::SceneComponent>();
    SW_ASSERT_NOT_NULL( pRoot );
    sw::WidgetComponent* pMarker = pTarget->addComponent<sw::WidgetComponent>();
    SW_ASSERT_NOT_NULL( pMarker );
    manager.flushSceneTransforms();
    manager.beginPlay();
    pMarker->setWorldOffset( sw::float3{ 0.0f, 2.0f, 0.0f } );
    pMarker->setContent( sw::make_unique<sw::uitest::TestBoxWidget>( "hp" ) );
    pMarker->bindUiSystem( &ui );

    const sw::CameraComponent* pGameCamera = manager.getCameraRegistry().selectCamera( sw::CameraRole::Game );
    SW_ASSERT_NOT_NULL( pGameCamera );
    const sw::float4x4              viewProjection = pGameCamera->getViewProjectionMatrix( viewport._physicalSize._x / viewport._physicalSize._y );
    const sw::WidgetMarkerPlacement atOffset =
        pMarker->computeMarkerPlacement( viewProjection, pGameCamera->getCameraPosition(), sw::float3{ 0.0f, 2.0f, 0.0f }, viewport._size );
    const sw::WidgetMarkerPlacement atFeet = pMarker->computeMarkerPlacement( viewProjection, pGameCamera->getCameraPosition(), sw::float3{}, viewport._size );
    SW_ASSERT_TRUE( atOffset._bVisible == SW_TRUE );
    SW_EXPECT_TRUE( atOffset._position._y < atFeet._position._y - 10.0f ); // 머리 위는 화면에서 위다

    pMarker->updateScreenMarker( viewport );
    SW_EXPECT_TRUE( pMarker->getLastPlacement()._bVisible == SW_TRUE );
    SW_EXPECT_NEAR_EQUAL( atOffset._position._x, pMarker->getLastPlacement()._position._x, 0.01f );
    SW_EXPECT_NEAR_EQUAL( atOffset._position._y, pMarker->getLastPlacement()._position._y, 0.01f );

    pMarker->setHidden( true );
    pMarker->updateScreenMarker( viewport );
    SW_EXPECT_TRUE( pMarker->getLastPlacement()._bVisible == SW_FALSE );
    SW_EXPECT_TRUE( pMarker->getContent()->getVisibility() == sw::WidgetVisibility::Collapsed );
    pMarker->setHidden( false );
    pMarker->updateScreenMarker( viewport );
    SW_EXPECT_TRUE( pMarker->getLastPlacement()._bVisible == SW_TRUE );
    pMarker->bindUiSystem( nullptr );
    manager.endPlay();
}

/**
 * @brief [WidgetComponentTest] World 위젯은 마커 화면 대신 자기 트리를 렌더 텍스처 크기(배율 1)로 놓고 칠해, 경로 `rendertarget/widget_<id>` 의 대상 목록을 낸다 —
 *        내용이 같으면 번호가 그대로, 바뀌면 오른다
 * @details 변이: `WidgetComponent::updateWorldCanvas` 의 같은 내용 확인을 빼면 바뀐 것이 없는 프레임에도 번호가 올라 진다.
 */
SW_TEST_CASE( WidgetComponentTest, WorldSpaceEmitsRenderTextureList )
{
    sw::UiSystem   ui;
    sw::UiViewport viewport{};
    viewport._size         = sw::float2{ WidgetComponentTestUtil::kWidth, WidgetComponentTestUtil::kHeight };
    viewport._physicalSize = viewport._size;
    sw::WidgetComponent component;
    component.setSpace( sw::WidgetSpace::World );
    component.setDrawSize( sw::float2{ 64.0f, 32.0f } );
    sw::unique_ptr<sw::ImageWidget> image  = sw::make_unique<sw::ImageWidget>();
    sw::ImageWidget*                pImage = image.get();
    pImage->setBrush( sw::UiBrush::makeSolid( sw::float4{ 1.0f, 0.0f, 0.0f, 1.0f } ) );
    component.setContent( std::move( image ) );
    component.bindUiSystem( &ui );
    SW_EXPECT_EQUAL( 0u, ui.getScreenCount() ); // 마커 화면을 쓰지 않는다

    ui.update( 1.0f / 60.0f, viewport );
    sw::vector<sw::CanvasTargetDrawList> listTarget;
    ui.collectWorldCanvases( listTarget );
    SW_ASSERT_EQUAL( size_t{ 1 }, listTarget.size() );
    SW_EXPECT_STREQ( component.getRenderTargetPath().c_str(), listTarget[0]._targetPath.c_str() );
    SW_EXPECT_NEAR_EQUAL( 64.0f, listTarget[0]._list._targetSize._x, 1e-6f );
    SW_ASSERT_EQUAL( size_t{ 1 }, listTarget[0]._list._listQuad.size() );
    SW_EXPECT_NEAR_EQUAL( 64.0f, listTarget[0]._list._listQuad[0]._rect._z, 1e-4f ); // 위젯이 텍스처 전체(배율 1)
    const uint64 firstRevision = listTarget[0]._contentRevision;

    ui.update( 1.0f / 60.0f, viewport );
    SW_EXPECT_EQUAL( firstRevision, component.getWorldCanvasRevision() );
    pImage->setBrush( sw::UiBrush::makeSolid( sw::float4{ 0.0f, 0.0f, 1.0f, 1.0f } ) );
    ui.update( 1.0f / 60.0f, viewport );
    SW_EXPECT_EQUAL( firstRevision + 1, component.getWorldCanvasRevision() );
    component.bindUiSystem( nullptr );
    listTarget.clear();
    ui.collectWorldCanvases( listTarget );
    SW_EXPECT_TRUE( listTarget.empty() );
}

/**
 * @brief [WidgetComponentTest] 가장자리에 붙인 마커의 방향 각은 화면 가운데에서 목표 쪽이다 — 오른쪽 π/2 · 왼쪽 −π/2 · 위 0(시계 방향, 라디안), 오른쪽 뒤도 오른쪽
 * @details 변이: 각을 y 를 뒤집지 않고(UI y 아래가 +) 재면 위의 목표가 π 가 되어 진다.
 */
SW_TEST_CASE( WidgetComponentTest, EdgeClampArrowPointsToTarget )
{
    using Util = WidgetComponentTestUtil;
    sw::WidgetComponent component;
    component.setClampToScreenEdge( true );
    const sw::WidgetMarkerPlacement right = Util::place( component, sw::float3{ 50.0f, 0.0f, 5.0f } );
    SW_ASSERT_TRUE( right._bClamped == SW_TRUE );
    SW_EXPECT_NEAR_EQUAL( sw::MathUtil::kHalfPi, right._edgeAngle, 0.001f );
    const sw::WidgetMarkerPlacement left = Util::place( component, sw::float3{ -50.0f, 0.0f, 5.0f } );
    SW_EXPECT_NEAR_EQUAL( -sw::MathUtil::kHalfPi, left._edgeAngle, 0.001f );
    const sw::WidgetMarkerPlacement up = Util::place( component, sw::float3{ 0.0f, 50.0f, 5.0f } );
    SW_ASSERT_TRUE( up._bClamped == SW_TRUE );
    SW_EXPECT_NEAR_EQUAL( 0.0f, up._edgeAngle, 0.001f );
    const sw::WidgetMarkerPlacement behindRight = Util::place( component, sw::float3{ 1.0f, 0.0f, -5.0f } );
    SW_EXPECT_NEAR_EQUAL( sw::MathUtil::kHalfPi, behindRight._edgeAngle, 0.001f );
    const sw::WidgetMarkerPlacement inside = Util::place( component, sw::float3{ 0.0f, 0.0f, 5.0f } );
    SW_EXPECT_TRUE( inside._bClamped == SW_FALSE ); // 화면 안 — 화살표 없음
}
