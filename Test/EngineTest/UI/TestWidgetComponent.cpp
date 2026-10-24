#include "pch.h"

#include "Core/Math/MatrixMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/Widgets/ButtonWidget.h"
#include "Engine/UI/Widgets/ImageWidget.h"
#include "Engine/UI/World/WidgetComponent.h"

#include "EngineTest/UI/UITestWidgets.h"

#include "TestFramework/Harness/TestTick.h"
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
            const sw::float4x4 view = sw::float4x4::makeLookAt( sw::float3{ 0.0f, 0.0f, 0.0f }, sw::float3{ 0.0f, 0.0f, 1.0f }, sw::float3{ 0.0f, 1.0f, 0.0f } );
            return view * sw::float4x4::makePerspectiveFieldOfView( 1.0f, kWidth / kHeight, 0.1f, 100.0f );
        }

        static sw::WorldWidgetRay makeRay( const sw::float3& origin, const sw::float3& direction )
        {
            sw::WorldWidgetRay ray{};
            ray._origin    = origin;
            ray._direction = direction.normalize();
            return ray;
        }

        static sw::WidgetMarkerPlacement place( const sw::WidgetComponent& component, const sw::float3& worldPosition )
        {
            return component.computeMarkerPlacement( makeViewProjection(), sw::float3{}, worldPosition, sw::float2{ kWidth, kHeight } );
        }

        /** @brief 광선 · 카메라 시선을 막는 정적 상자 바디를 만듭니다(물리 씬은 `manager.beginPlay()` 뒤에 몇 프레임 돌아야 바디를 가진다). */
        static sw::RigidBodyComponent* spawnWall( sw::GameObjectManager& manager, const sw::float3& position, const sw::float3& halfExtents )
        {
            sw::GameObject* pObject = manager.createGameObject( sw::hashed_string( "Wall" ) );
            if ( pObject == nullptr )
                return nullptr;
            sw::RigidBodyComponent* pBody = pObject->addComponent<sw::RigidBodyComponent>();
            if ( pBody == nullptr )
                return nullptr;
            sw::PhysicsShapeDesc3D box;
            box._halfExtents = halfExtents;
            pBody->setShape( box );
            pBody->setBodyType( sw::PhysicsBodyType::Static );
            pBody->setLocalPosition( position );
            return pBody;
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
 * @brief [WidgetComponentTest] 화면 마커는 UI 시스템의 HUD 마커 화면에 붙고 앵커 하나를 받는다 — 위젯은 피벗이 원점에 오게(− 피벗 × 크기) 놓이고, 화면 자리 · 배율은
 *        앵커(물리 픽셀 정수)가 갖는다. 움직이거나 배율이 바뀌어도 다시 배치 · 칠하지 않는다(그리기 목록 번호 그대로) · 숨김은 접기 · 등록을 풀면 마커 화면이 닫힌다
 * @details 배율 1.5 화면이라 앵커는 UI 자리 × 1.5 를 반올림한 값이다. 사각형은 앵커 번호를 갖고 앵커 기준 위치(−75, −30 물리 픽셀)다.
 *          변이: `applyPlacement` 가 자리를 슬롯 오프셋에 다시 쓰면 움직인 프레임에 그리기 목록 번호가 올라 진다.
 */
SW_TEST_CASE( WidgetComponentTest, MarkerFollowsPlacementInUISystem )
{
    constexpr float32 kScale = 1.5f;
    sw::UISystem      ui;
    sw::UIViewport    viewport{};
    viewport._size         = sw::float2{ WidgetComponentTestUtil::kWidth, WidgetComponentTestUtil::kHeight };
    viewport._physicalSize = sw::float2{ viewport._size._x * kScale, viewport._size._y * kScale };
    viewport._uiScale      = kScale;
    sw::WidgetComponent component;
    component.setDrawSize( sw::float2{ 100.0f, 20.0f } );
    component.setPivot( sw::float2{ 0.5f, 1.0f } );
    sw::unique_ptr<sw::ImageWidget> plate = sw::make_unique<sw::ImageWidget>();
    plate->setBrush( sw::UIBrush::makeSolid( sw::float4{ 1.0f, 0.0f, 0.0f, 1.0f } ) );
    component.setContent( std::move( plate ) );
    component.bindUISystem( &ui );
    SW_ASSERT_EQUAL( 1u, ui.getScreenCount() );
    SW_EXPECT_TRUE( ui.getActiveScreen() == nullptr ); // HUD — 포커스를 받지 않는다

    sw::WidgetMarkerPlacement placement{};
    placement._position = sw::float2{ 500.3f, 300.0f };
    component.applyPlacement( placement );
    ui.update( 1.0f / 60.0f, viewport );
    sw::Widget* pMarker = component.getContent();
    SW_ASSERT_NOT_NULL( pMarker );
    const uint32 anchor = pMarker->getPaintAnchor();
    SW_ASSERT_TRUE( anchor < ui.getCanvasAnchors().size() );
    SW_EXPECT_NEAR_EQUAL( 750.0f, ui.getCanvasAnchors()[anchor]._position._x, 1e-4f ); // 500.3 × 1.5 = 750.45 → 750
    SW_EXPECT_NEAR_EQUAL( 450.0f, ui.getCanvasAnchors()[anchor]._position._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( -50.0f, pMarker->getGeometry().computeScreenBounds().getLeft(), 0.01f );
    SW_EXPECT_NEAR_EQUAL( -20.0f, pMarker->getGeometry().computeScreenBounds().getTop(), 0.01f );
    SW_ASSERT_EQUAL( size_t{ 1 }, ui.getCanvas()._listQuad.size() );
    const sw::CanvasQuad& quad = ui.getCanvas()._listQuad[0];
    SW_EXPECT_EQUAL( anchor, quad._anchor );
    SW_EXPECT_NEAR_EQUAL( -75.0f, quad._rect._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( -30.0f, quad._rect._y, 1e-4f );
    const uint64 placedRevision = ui.getCanvasRevision();

    placement._position = sw::float2{ 800.0f, 120.0f };
    placement._scale    = 2.0f;
    component.applyPlacement( placement );
    ui.update( 1.0f / 60.0f, viewport );
    SW_EXPECT_NEAR_EQUAL( 1200.0f, ui.getCanvasAnchors()[anchor]._position._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 180.0f, ui.getCanvasAnchors()[anchor]._position._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, ui.getCanvasAnchors()[anchor]._scale, 1e-6f );
    SW_EXPECT_EQUAL( placedRevision, ui.getCanvasRevision() ); // 사각형은 그대로 — 앵커만 바뀌었다

    placement._bVisible = SW_FALSE;
    component.applyPlacement( placement );
    SW_EXPECT_TRUE( pMarker->getVisibility() == sw::WidgetVisibility::Collapsed );

    component.bindUISystem( nullptr );
    ui.update( 1.0f / 60.0f, viewport );
    SW_EXPECT_EQUAL( 0u, ui.getScreenCount() );
    SW_EXPECT_EQUAL( 0u, ui.getWidgetComponentCount() );
}

/**
 * @brief [WidgetComponentTest] 화면 마커의 기준점은 오브젝트 자리 + 월드 오프셋(머리 위 HP 바)이고, 코드가 숨기면(`setHidden`) 카메라 안이어도 숨는다
 * @details `updateScreenMarker` 는 빈 `WidgetMarkerView` 를 받으면 게임 카메라를 스스로 찾는다 — 기대값은 같은 카메라 행렬로 `computeMarkerPlacement` 에 오프셋 더한 점을 넣은 것이다.
 *          변이: `updateScreenMarker` 가 `_worldOffset` 을 더하지 않으면 자리가 오브젝트 발밑이라 진다.
 */
SW_TEST_CASE( WidgetComponentTest, ScreenMarkerAnchorsAtWorldOffsetAndHides )
{
    sw::UISystem   ui;
    sw::UIViewport viewport{};
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
    pMarker->bindUISystem( &ui );

    const sw::CameraComponent* pGameCamera = manager.getCameraRegistry().selectCamera( sw::CameraRole::Game );
    SW_ASSERT_NOT_NULL( pGameCamera );
    const sw::float4x4              viewProjection = pGameCamera->getViewProjectionMatrix( viewport._physicalSize._x / viewport._physicalSize._y );
    const sw::WidgetMarkerPlacement atOffset =
        pMarker->computeMarkerPlacement( viewProjection, pGameCamera->getCameraPosition(), sw::float3{ 0.0f, 2.0f, 0.0f }, viewport._size );
    const sw::WidgetMarkerPlacement atFeet = pMarker->computeMarkerPlacement( viewProjection, pGameCamera->getCameraPosition(), sw::float3{}, viewport._size );
    SW_ASSERT_TRUE( atOffset._bVisible == SW_TRUE );
    SW_EXPECT_TRUE( atOffset._position._y < atFeet._position._y - 10.0f ); // 머리 위는 화면에서 위다

    {
        sw::WidgetMarkerView view{};
        pMarker->updateScreenMarker( viewport, view );
    }
    SW_EXPECT_TRUE( pMarker->getLastPlacement()._bVisible == SW_TRUE );
    SW_EXPECT_NEAR_EQUAL( atOffset._position._x, pMarker->getLastPlacement()._position._x, 0.01f );
    SW_EXPECT_NEAR_EQUAL( atOffset._position._y, pMarker->getLastPlacement()._position._y, 0.01f );

    pMarker->setHidden( true );
    {
        sw::WidgetMarkerView view{};
        pMarker->updateScreenMarker( viewport, view );
    }
    SW_EXPECT_TRUE( pMarker->getLastPlacement()._bVisible == SW_FALSE );
    SW_EXPECT_TRUE( pMarker->getContent()->getVisibility() == sw::WidgetVisibility::Collapsed );
    pMarker->setHidden( false );
    {
        sw::WidgetMarkerView view{};
        pMarker->updateScreenMarker( viewport, view );
    }
    SW_EXPECT_TRUE( pMarker->getLastPlacement()._bVisible == SW_TRUE );
    pMarker->bindUISystem( nullptr );
    manager.endPlay();
}

/**
 * @brief [WidgetComponentTest] World 위젯은 마커 화면 대신 자기 트리를 렌더 텍스처 크기(배율 1)로 놓고 칠해, 경로 `rendertarget/widget_<id>` 의 대상 목록을 낸다 —
 *        내용이 같으면 번호가 그대로, 바뀌면 오른다
 * @details 변이: `WidgetComponent::updateWorldCanvas` 의 같은 내용 확인을 빼면 바뀐 것이 없는 프레임에도 번호가 올라 진다.
 */
SW_TEST_CASE( WidgetComponentTest, WorldSpaceEmitsRenderTextureList )
{
    sw::UISystem   ui;
    sw::UIViewport viewport{};
    viewport._size         = sw::float2{ WidgetComponentTestUtil::kWidth, WidgetComponentTestUtil::kHeight };
    viewport._physicalSize = viewport._size;
    sw::WidgetComponent component;
    component.setSpace( sw::WidgetSpace::World );
    component.setDrawSize( sw::float2{ 64.0f, 32.0f } );
    sw::unique_ptr<sw::ImageWidget> image  = sw::make_unique<sw::ImageWidget>();
    sw::ImageWidget*                pImage = image.get();
    pImage->setBrush( sw::UIBrush::makeSolid( sw::float4{ 1.0f, 0.0f, 0.0f, 1.0f } ) );
    component.setContent( std::move( image ) );
    component.bindUISystem( &ui );
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
    pImage->setBrush( sw::UIBrush::makeSolid( sw::float4{ 0.0f, 0.0f, 1.0f, 1.0f } ) );
    ui.update( 1.0f / 60.0f, viewport );
    SW_EXPECT_EQUAL( firstRevision + 1, component.getWorldCanvasRevision() );
    component.bindUISystem( nullptr );
    listTarget.clear();
    ui.collectWorldCanvases( listTarget );
    SW_EXPECT_TRUE( listTarget.empty() );
}

/**
 * @brief [WidgetComponentTest] 가장자리에 붙인 마커의 방향 각은 화면 가운데에서 목표 쪽이다 — 오른쪽 π/2 · 왼쪽 −π/2 · 위 0(시계 방향, 라디안), 오른쪽 뒤도 오른쪽
 * @details 변이: 각을 y 를 뒤집지 않고(UI y 아래가 +) 측정하면 위의 목표가 π 가 되어 진다.
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

/**
 * @brief [WidgetComponentTest] 입력을 켠 World 위젯은 광선이 맞힌 자리로 포인터 이벤트를 받는다 — 단추 가운데를 겨눈 누름 · 뗌이 클릭이 되고, 사각형 밖을 겨누면 처리하지 않는다
 * @details 입력을 끄면(기본) 같은 광선도 받지 않는다. 변이: `UIWorldPointerRouter::route` 가 맞힌 위젯이 바뀔 때 이전 위젯의 호버를 풀지 않으면 사각형 밖을 겨눈 뒤에도 단추가 호버로 남아 진다.
 */
SW_TEST_CASE( WidgetComponentTest, WorldInputClicksWidgetUnderRay )
{
    sw::UISystem   ui;
    sw::UIViewport viewport{};
    viewport._size         = sw::float2{ WidgetComponentTestUtil::kWidth, WidgetComponentTestUtil::kHeight };
    viewport._physicalSize = viewport._size;
    sw::GameObjectManager manager;
    sw::GameObject*       pPanel = manager.createGameObject( sw::hashed_string( "Panel" ) );
    SW_ASSERT_NOT_NULL( pPanel );
    sw::SceneComponent* pRoot = pPanel->addComponent<sw::SceneComponent>();
    SW_ASSERT_NOT_NULL( pRoot );
    pRoot->setLocalPosition( sw::float3{ 0.0f, 0.0f, 5.0f } );
    sw::WidgetComponent* pWidget = pPanel->addComponent<sw::WidgetComponent>();
    SW_ASSERT_NOT_NULL( pWidget );
    pWidget->setSpace( sw::WidgetSpace::World );
    pWidget->setDrawSize( sw::float2{ 100.0f, 50.0f } );
    pWidget->setWorldSize( sw::float2{ 2.0f, 1.0f } );
    sw::unique_ptr<sw::ButtonWidget> button     = sw::make_unique<sw::ButtonWidget>();
    sw::ButtonWidget*                pButton    = button.get();
    int32                            clickCount = 0;
    (void)pButton->getOnClicked().add( [&clickCount]( sw::WidgetID )
    { ++clickCount; } );
    pWidget->setContent( std::move( button ) );
    pWidget->bindUISystem( &ui );
    manager.flushSceneTransforms();
    ui.update( 1.0f / 60.0f, viewport );

    const sw::WorldWidgetRay center = WidgetComponentTestUtil::makeRay( sw::float3{}, sw::float3{ 0.0f, 0.0f, 1.0f } );
    sw::UIPointerEvent       event{};
    event._kind = sw::UIPointerEventKind::Move;
    SW_EXPECT_FALSE( ui.routeWorldPointer( center, event ) ); // 입력을 끈 위젯은 받지 않는다
    SW_EXPECT_TRUE( pButton->isHovered() == false );

    pWidget->setAcceptsInput( true );
    (void)ui.routeWorldPointer( center, event );
    SW_EXPECT_TRUE( pButton->isHovered() );
    event._kind = sw::UIPointerEventKind::Down;
    SW_EXPECT_TRUE( ui.routeWorldPointer( center, event ) );
    event._kind = sw::UIPointerEventKind::Up;
    SW_EXPECT_TRUE( ui.routeWorldPointer( center, event ) );
    SW_EXPECT_EQUAL( 1, clickCount );

    // 사각형 밖(월드 x 1.5 — 로컬 0.75)을 겨누면 호버가 풀리고 누름은 처리되지 않는다.
    const sw::WorldWidgetRay outside = WidgetComponentTestUtil::makeRay( sw::float3{ 1.5f, 0.0f, 0.0f }, sw::float3{ 0.0f, 0.0f, 1.0f } );
    event._kind                      = sw::UIPointerEventKind::Move;
    (void)ui.routeWorldPointer( outside, event );
    SW_EXPECT_TRUE( pButton->isHovered() == false );
    event._kind = sw::UIPointerEventKind::Down;
    SW_EXPECT_FALSE( ui.routeWorldPointer( outside, event ) );
    pWidget->bindUISystem( nullptr );
}

/**
 * @brief [WidgetComponentTest] 칠할 것이 없는 World 위젯은 프레임마다 칠하기 · 목록 견주기를 건너뛴다 — 처음 칠한 뒤 가만히 있으면 번호가 그대로이고 건너뛴 수만 오르며,
 *        내용이 바뀐 프레임에는 다시 칠해 번호가 오른다
 * @details 변이: `updateWorldCanvas` 의 `canReuseOutput` 건너뛰기를 빼면 건너뛴 수가 0 이라 진다. 건너뛰기가 무효화를 놓치면(항상 건너뜀) 바뀐 내용의 번호가 안 올라 진다.
 */
SW_TEST_CASE( WidgetComponentTest, CleanWorldWidgetSkipsRepaint )
{
    sw::UISystem   ui;
    sw::UIViewport viewport{};
    viewport._size         = sw::float2{ WidgetComponentTestUtil::kWidth, WidgetComponentTestUtil::kHeight };
    viewport._physicalSize = viewport._size;
    sw::WidgetComponent component;
    component.setSpace( sw::WidgetSpace::World );
    component.setDrawSize( sw::float2{ 64.0f, 32.0f } );
    sw::unique_ptr<sw::ImageWidget> image  = sw::make_unique<sw::ImageWidget>();
    sw::ImageWidget*                pImage = image.get();
    pImage->setBrush( sw::UIBrush::makeSolid( sw::float4{ 1.0f, 0.0f, 0.0f, 1.0f } ) );
    component.setContent( std::move( image ) );
    component.bindUISystem( &ui );

    ui.update( 1.0f / 60.0f, viewport ); // 처음 칠하기
    const uint64 firstRevision = component.getWorldCanvasRevision();
    SW_EXPECT_EQUAL( uint64{ 0 }, component.getWorldCleanSkipCount() );
    for ( uint32 frame = 0; frame < 5; ++frame )
    {
        ui.update( 1.0f / 60.0f, viewport );
    }
    SW_EXPECT_EQUAL( firstRevision, component.getWorldCanvasRevision() );
    SW_EXPECT_EQUAL( uint64{ 5 }, component.getWorldCleanSkipCount() );

    pImage->setBrush( sw::UIBrush::makeSolid( sw::float4{ 0.0f, 0.0f, 1.0f, 1.0f } ) );
    ui.update( 1.0f / 60.0f, viewport );
    SW_EXPECT_EQUAL( firstRevision + 1, component.getWorldCanvasRevision() );
    SW_EXPECT_EQUAL( uint64{ 5 }, component.getWorldCleanSkipCount() ); // 바뀐 프레임은 건너뛰지 않았다
    component.bindUISystem( nullptr );
}

/**
 * @brief [WidgetComponentTest] 가림을 켠 World 위젯은 물리 바디가 광선을 사각형보다 가까이에서 막으면 입력을 받지 않고, 벽을 비껴 겨누면 받는다
 * @details 가림을 끄면(기본) 같은 벽 뒤의 위젯도 받는다. 변이: `pickWorld` 가 `_bOccludedByPhysics` 를 보지 않으면 가림을 켠 뒤에도 호버가 남아 진다.
 *          벽은 폭 1 m(반 0.5), 두께 0.5 m 로 z 2.5 에 서고 위젯은 z 5 에 선다 — 가운데 광선은 벽 앞면(2.25)에서 막힌다.
 */
SW_TEST_CASE( WidgetComponentTest, PhysicsBodyBlocksWorldInputWhenOccluded )
{
    sw::UISystem   ui;
    sw::UIViewport viewport{};
    viewport._size         = sw::float2{ WidgetComponentTestUtil::kWidth, WidgetComponentTestUtil::kHeight };
    viewport._physicalSize = viewport._size;
    sw::GameObjectManager manager;
    SW_ASSERT_NOT_NULL( WidgetComponentTestUtil::spawnWall( manager, sw::float3{ 0.0f, 0.0f, 2.5f }, sw::float3{ 0.5f, 2.0f, 0.25f } ) );
    sw::GameObject* pPanel = manager.createGameObject( sw::hashed_string( "Panel" ) );
    SW_ASSERT_NOT_NULL( pPanel );
    sw::SceneComponent* pRoot = pPanel->addComponent<sw::SceneComponent>();
    SW_ASSERT_NOT_NULL( pRoot );
    pRoot->setLocalPosition( sw::float3{ 0.0f, 0.0f, 5.0f } );
    sw::WidgetComponent* pWidget = pPanel->addComponent<sw::WidgetComponent>();
    SW_ASSERT_NOT_NULL( pWidget );
    pWidget->setSpace( sw::WidgetSpace::World );
    pWidget->setDrawSize( sw::float2{ 100.0f, 50.0f } );
    pWidget->setWorldSize( sw::float2{ 2.0f, 1.0f } );
    pWidget->setAcceptsInput( true );
    sw::unique_ptr<sw::ButtonWidget> button  = sw::make_unique<sw::ButtonWidget>();
    sw::ButtonWidget*                pButton = button.get();
    pWidget->setContent( std::move( button ) );
    manager.beginPlay();
    test::updateFrames( manager, 2 ); // 물리 씬이 벽 바디를 만든다
    pWidget->bindUISystem( &ui );
    manager.flushSceneTransforms();
    ui.update( 1.0f / 60.0f, viewport );

    const sw::WorldWidgetRay center = WidgetComponentTestUtil::makeRay( sw::float3{}, sw::float3{ 0.0f, 0.0f, 1.0f } );
    // 벽 옆(x 3 → 0.5)으로 비껴 지나는 광선: 벽 자리(z 2.25 ~ 2.75)에서 x 가 1.6 이상이라 벽 폭 0.5 밖이다.
    const sw::WorldWidgetRay aside = WidgetComponentTestUtil::makeRay( sw::float3{ 3.0f, 0.0f, 0.0f }, sw::float3{ -2.5f, 0.0f, 5.0f } );
    sw::UIPointerEvent       event{};
    event._kind = sw::UIPointerEventKind::Move;

    SW_EXPECT_FALSE( pWidget->isOccludedByPhysics() );
    (void)ui.routeWorldPointer( center, event );
    SW_EXPECT_TRUE( pButton->isHovered() ); // 가림을 끄면 벽 뒤 위젯도 받는다

    pWidget->setOccludedByPhysics( true );
    (void)ui.routeWorldPointer( center, event );
    SW_EXPECT_TRUE( pButton->isHovered() == false ); // 벽이 광선을 막는다
    event._kind = sw::UIPointerEventKind::Down;
    SW_EXPECT_FALSE( ui.routeWorldPointer( center, event ) );

    event._kind = sw::UIPointerEventKind::Move;
    (void)ui.routeWorldPointer( aside, event );
    SW_EXPECT_TRUE( pButton->isHovered() ); // 벽을 비껴 겨누면 받는다
    pWidget->bindUISystem( nullptr );
    manager.endPlay();
}

/**
 * @brief [WidgetComponentTest] 가림을 켠 화면 마커는 카메라와 마커 점 사이에 물리 바디가 서면 숨고, 벽을 비낀 자리에서는 보인다
 * @details 가림을 끄면(기본) 벽 뒤에서도 보인다. 변이: `updateScreenMarker` 가 `isRayBlockedByPhysics` 를 빼면 가림을 켠 뒤에도 마커가 보여 진다.
 */
SW_TEST_CASE( WidgetComponentTest, PhysicsBodyHidesOccludedScreenMarker )
{
    sw::UISystem   ui;
    sw::UIViewport viewport{};
    viewport._size         = sw::float2{ WidgetComponentTestUtil::kWidth, WidgetComponentTestUtil::kHeight };
    viewport._physicalSize = viewport._size;
    sw::GameObjectManager manager;
    sw::GameObject*       pCameraObject = manager.createGameObject( sw::hashed_string( "Camera" ) );
    SW_ASSERT_NOT_NULL( pCameraObject );
    SW_ASSERT_NOT_NULL( pCameraObject->addComponent<sw::CameraComponent>() );
    SW_ASSERT_NOT_NULL( WidgetComponentTestUtil::spawnWall( manager, sw::float3{ 0.0f, 0.0f, 2.5f }, sw::float3{ 1.0f, 2.0f, 0.25f } ) );
    sw::GameObject* pTarget = manager.createGameObject( sw::hashed_string( "Target" ) );
    SW_ASSERT_NOT_NULL( pTarget );
    sw::SceneComponent* pRoot = pTarget->addComponent<sw::SceneComponent>();
    SW_ASSERT_NOT_NULL( pRoot );
    pRoot->setLocalPosition( sw::float3{ 0.0f, 0.0f, 5.0f } );
    sw::WidgetComponent* pMarker = pTarget->addComponent<sw::WidgetComponent>();
    SW_ASSERT_NOT_NULL( pMarker );
    manager.flushSceneTransforms();
    manager.beginPlay();
    test::updateFrames( manager, 2 ); // 물리 씬이 벽 바디를 만든다
    pMarker->setContent( sw::make_unique<sw::uitest::TestBoxWidget>( "tag" ) );
    pMarker->bindUISystem( &ui );

    const auto updateMarker = [&]()
    {
        sw::WidgetMarkerView view{};
        pMarker->updateScreenMarker( viewport, view );
    };
    updateMarker();
    SW_ASSERT_TRUE( pMarker->getLastPlacement()._bVisible == SW_TRUE ); // 가림을 끄면 벽 뒤에서도 보인다

    pMarker->setOccludedByPhysics( true );
    updateMarker();
    SW_EXPECT_TRUE( pMarker->getLastPlacement()._bVisible == SW_FALSE ); // 벽이 시선을 막는다
    SW_EXPECT_TRUE( pMarker->getContent()->getVisibility() == sw::WidgetVisibility::Collapsed );

    // 벽 폭(반 1) 밖으로 비낀 자리(x 2.5): 벽 자리(z 2.5)에서 시선이 x 1.25 라 막히지 않는다.
    pRoot->setLocalPosition( sw::float3{ 2.5f, 0.0f, 5.0f } );
    manager.flushSceneTransforms();
    updateMarker();
    SW_EXPECT_TRUE( pMarker->getLastPlacement()._bVisible == SW_TRUE );
    pMarker->bindUISystem( nullptr );
    manager.endPlay();
}
