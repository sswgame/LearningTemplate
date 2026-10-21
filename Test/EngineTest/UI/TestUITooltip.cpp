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
