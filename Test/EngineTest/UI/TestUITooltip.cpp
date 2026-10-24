#include "pch.h"

#include "Engine/UI/Base/UIEventRouter.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/Screen/UITooltipService.h"

#include "EngineTest/UI/UITestWidgets.h"

#include "TestFramework/TestFramework.h"

// 런타임 UI 툴팁과 커서 모양(docs/plans/UiTooltipAndCursor.md) — 호버 경로에서 주인과 모양을 고르는 규칙.

/**
 * @brief [UITooltipTest] 커서 모양은 잎에서 올라가 처음 만난 Inherit 가 아닌 값이고, 루트까지 모두 Inherit 면 화살표다. 툴팁 주인은 설명 글을 가진 가장 가까운 위젯이다
 */
SW_TEST_CASE( UITooltipTest, HoverPathPicksTheNearestCursorAndTooltip )
{
    auto        root   = sw::make_unique<sw::BoxPanel>();
    sw::Widget* pRoot  = root.get();
    sw::Widget* pPlain = root->addChild( sw::make_unique<sw::uitest::TestBoxWidget>( "plain" ) );
    sw::Widget* pLink  = root->addChild( sw::make_unique<sw::uitest::TestBoxWidget>( "link" ) );
    pRoot->setTooltip( "panel help" );
    pLink->setCursorShape( sw::UICursorShape::Hand );
    pLink->setTooltip( "opens the store" );
    sw::UIScreen          screen( sw::UIScreenDesc{}, std::move( root ) );
    const sw::WidgetTree& tree = screen.getTree();

    sw::UIWidgetPath plainPath;
    plainPath._listWidget = { pRoot->getID(), pPlain->getID() };
    sw::UIWidgetPath linkPath;
    linkPath._listWidget = { pRoot->getID(), pLink->getID() };

    SW_EXPECT_TRUE( sw::UITooltipService::resolveCursorShape( tree, plainPath ) == sw::UICursorShape::Arrow ); // 모두 Inherit
    SW_EXPECT_TRUE( sw::UITooltipService::resolveCursorShape( tree, linkPath ) == sw::UICursorShape::Hand );
    pRoot->setCursorShape( sw::UICursorShape::NotAllowed );
    SW_EXPECT_TRUE( sw::UITooltipService::resolveCursorShape( tree, plainPath ) == sw::UICursorShape::NotAllowed ); // 부모를 따른다
    SW_EXPECT_TRUE( sw::UITooltipService::toCursorShape( sw::UICursorShape::Hand ) == sw::CursorShape::Hand );

    SW_EXPECT_TRUE( sw::UITooltipService::findTooltipOwner( tree, linkPath ) == pLink );
    SW_EXPECT_TRUE( sw::UITooltipService::findTooltipOwner( tree, plainPath ) == pRoot ); // 잎에 없으면 조상의 것
    pRoot->setTooltip( "" );
    SW_EXPECT_TRUE( sw::UITooltipService::findTooltipOwner( tree, plainPath ) == nullptr );
}

/**
 * @brief [UITooltipTest] 툴팁 자리는 포인터 오른쪽 아래가 기본이고, 화면 오른쪽 · 아래를 넘으면 포인터 왼쪽 · 위로 뒤집히며, 화면보다 크면 왼쪽 위 변에 붙는다 — 배치 전(크기 0)에는 기본 자리
 * @details 변이: 뒤집기를 빼면 오른쪽 변 근처 툴팁이 화면 밖으로 잘려 진다. 클램프를 빼면 화면보다 큰 툴팁이 음수 자리에 놓여 진다.
 */
SW_TEST_CASE( UITooltipTest, PlacementFlipsAtScreenEdgesAndClamps )
{
    const sw::float2 screen{ 800.0f, 600.0f };
    const sw::float2 size{ 200.0f, 40.0f };
    const sw::float2 normal = sw::UITooltipService::computePlacement( sw::float2{ 100.0f, 100.0f }, size, screen );
    SW_EXPECT_NEAR_EQUAL( 100.0f + sw::UITooltipService::kPointerOffsetX, normal._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 100.0f + sw::UITooltipService::kPointerOffsetY, normal._y, 1e-4f );

    const sw::float2 nearRight = sw::UITooltipService::computePlacement( sw::float2{ 700.0f, 100.0f }, size, screen );
    SW_EXPECT_NEAR_EQUAL( 700.0f - sw::UITooltipService::kPointerOffsetX - 200.0f, nearRight._x, 1e-4f ); // 포인터 왼쪽
    SW_EXPECT_NEAR_EQUAL( 100.0f + sw::UITooltipService::kPointerOffsetY, nearRight._y, 1e-4f );

    const sw::float2 nearBottom = sw::UITooltipService::computePlacement( sw::float2{ 100.0f, 580.0f }, size, screen );
    SW_EXPECT_NEAR_EQUAL( 100.0f + sw::UITooltipService::kPointerOffsetX, nearBottom._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 580.0f - sw::UITooltipService::kPointerOffsetY - 40.0f, nearBottom._y, 1e-4f ); // 포인터 위

    const sw::float2 corner = sw::UITooltipService::computePlacement( sw::float2{ 790.0f, 595.0f }, size, screen );
    SW_EXPECT_TRUE( corner._x + size._x <= screen._x && corner._y + size._y <= screen._y );
    SW_EXPECT_TRUE( corner._x >= 0.0f && corner._y >= 0.0f );

    const sw::float2 huge = sw::UITooltipService::computePlacement( sw::float2{ 50.0f, 50.0f }, sw::float2{ 900.0f, 700.0f }, screen );
    SW_EXPECT_NEAR_EQUAL( 0.0f, huge._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, huge._y, 1e-4f );

    const sw::float2 beforeLayout = sw::UITooltipService::computePlacement( sw::float2{ 100.0f, 100.0f }, sw::float2{}, screen );
    SW_EXPECT_NEAR_EQUAL( normal._x, beforeLayout._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( normal._y, beforeLayout._y, 1e-4f );
}
