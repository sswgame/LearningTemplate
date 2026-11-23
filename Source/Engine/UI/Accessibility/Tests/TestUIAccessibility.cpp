#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/UI/Accessibility/UIAccessibilityService.h"
#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/Screen/UIScreen.h"

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
