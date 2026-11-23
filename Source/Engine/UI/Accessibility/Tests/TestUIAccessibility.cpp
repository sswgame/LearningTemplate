#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/UI/Accessibility/UIAccessibilityService.h"
#include "Engine/UI/Accessibility/UIAccessibilitySnapshot.h"
#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/Widgets/SliderWidget.h"

#include <limits>

// UIAccessibilityTest — 스크린 리더가 읽고 시키는 화면의 규칙(플랫폼 API · 디바이스 없이)(nogpu).

using namespace sw;

namespace
{
    unique_ptr<UIScreen> makeAccessibilityScreen( UILayer layer, bool bModal )
    {
        UIScreenDef desc;
        desc._layer  = layer;
        desc._bModal = bModal;
        return make_unique<UIScreen>( desc, make_unique<CanvasPanel>() );
    }
} // namespace

/** @brief [UIAccessibilityTest] 숫자가 아닌 값은 슬라이더 값이 되지 않는다(결함: 스크린 리더가 보낸 NaN 이 범위 제한을 지나 설정 값이 됐다) */
SW_TEST_CASE( UIAccessibilityTest, SliderRefusesNotANumber )
{
    SliderWidget slider;
    slider.setRange( 0.0f, 1.0f, 0.1f );
    slider.setValueFromUser( 0.5f );
    slider.setValueFromUser( std::numeric_limits<float32>::quiet_NaN() );
    SW_EXPECT_EQUAL( 0.5f, slider.getValue() );
    slider.setValueFromUser( std::numeric_limits<float32>::infinity() ); // 무한대는 범위 끝이다
    SW_EXPECT_EQUAL( 1.0f, slider.getValue() );
}

/** @brief [UIAccessibilityTest] 알리는 자리는 UI 배율과 게임 뷰의 자리를 옮긴 창 픽셀이다(결함: 리눅스 브리지는 UI 단위를 그대로 알려 배율이 1 이 아니면 강조 상자가 어긋났다) */
SW_TEST_CASE( UIAccessibilityTest, ClientRectFollowsScaleAndOrigin )
{
    UIViewport viewport;
    viewport._uiScale       = 2.0f;
    viewport._pointerOrigin = int2{ 30, 40 };
    const float4 client     = viewport.toClientRect( float4{ 10.0f, 20.0f, 100.0f, 50.0f } );
    SW_EXPECT_EQUAL( 50.0f, client._x );
    SW_EXPECT_EQUAL( 80.0f, client._y );
    SW_EXPECT_EQUAL( 200.0f, client._z );
    SW_EXPECT_EQUAL( 100.0f, client._w );
}

/** @brief [UIAccessibilityTest] 점 아래의 요소는 맨 위 화면에서 고르고, 그 화면 안에서 가장 깊은 노드다(결함: 목록 끝부터 골라 겹친 화면에서 아래 화면의 요소를 답했다) */
SW_TEST_CASE( UIAccessibilityTest, NodeAtPointComesFromTheTopScreen )
{
    UIAccessibilitySnapshot snapshot;
    const auto              addNode = [&snapshot]( uint32 screen, const float4& bounds )
    {
        AccessibleNode node;
        node._screen = screen;
        node._bounds = bounds;
        snapshot._listNode.push_back( std::move( node ) );
    };
    addNode( 2, float4{ 0.0f, 0.0f, 100.0f, 100.0f } );   // 위 화면의 바탕
    addNode( 2, float4{ 10.0f, 10.0f, 20.0f, 20.0f } );   // 위 화면의 단추
    addNode( 1, float4{ 0.0f, 0.0f, 400.0f, 400.0f } );   // 아래 화면의 바탕
    addNode( 1, float4{ 10.0f, 10.0f, 300.0f, 300.0f } ); // 아래 화면의 단추

    SW_EXPECT_TRUE( snapshot.findNodeAt( float2{ 15.0f, 15.0f } ) == 1u );
    SW_EXPECT_TRUE( snapshot.findNodeAt( float2{ 60.0f, 60.0f } ) == 0u );
    SW_EXPECT_TRUE( snapshot.findNodeAt( float2{ 200.0f, 200.0f } ) == 3u ); // 위 화면 밖이면 아래 화면이다
    SW_EXPECT_FALSE( snapshot.findNodeAt( float2{ 500.0f, 500.0f } ).has_value() );
}

/** @brief [UIAccessibilityTest] 시킬 수 있는 화면은 위에서 모달까지다 — 모달 아래 · 오버레이에는 시키지 못한다(결함: 쥐고 있던 요소로 모달 아래 단추를 눌렀다) */
SW_TEST_CASE( UIAccessibilityTest, InteractiveScreensStopAtTheModal )
{
    vector<unique_ptr<UIScreen>> listScreen; // 그리기 순서(아래 → 위)
    listScreen.push_back( makeAccessibilityScreen( UILayer::HUD, false ) );
    listScreen.push_back( makeAccessibilityScreen( UILayer::Menu, false ) );
    listScreen.push_back( makeAccessibilityScreen( UILayer::Menu, false ) );
    listScreen.push_back( makeAccessibilityScreen( UILayer::Overlay, false ) );
    vector<UIScreen*> listInteractive;
    UIAccessibilityService::collectInteractiveScreens( listScreen, listInteractive );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listInteractive.size() );
    SW_EXPECT_TRUE( listInteractive[0] == listScreen[2].get() ); // 위에서 아래로
    SW_EXPECT_TRUE( listInteractive[2] == listScreen[0].get() );

    listScreen.insert( listScreen.begin() + 2, makeAccessibilityScreen( UILayer::Menu, true ) );
    UIAccessibilityService::collectInteractiveScreens( listScreen, listInteractive );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listInteractive.size() );
    SW_EXPECT_TRUE( listInteractive[0] == listScreen[3].get() );
    SW_EXPECT_TRUE( listInteractive[1] == listScreen[2].get() ); // 모달에서 멈춘다
}
