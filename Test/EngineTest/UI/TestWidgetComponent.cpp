#include "pch.h"

#include "Core/Math/MatrixMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/UI/Core/Widget.h"
#include "Engine/UI/Screen/UiScreen.h"
#include "Engine/UI/UiSystem.h"
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
 * @brief [WidgetComponentTest] 화면 마커는 UI 시스템의 Hud 마커 화면에 붙고, 자리 = 기준점 − 피벗 × 크기, 배율은 피벗 둘레 렌더 변환으로 기하에 얹힌다 · 숨김은 접기 ·
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
    SW_EXPECT_TRUE( ui.getActiveScreen() == nullptr ); // Hud — 포커스를 받지 않는다

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
