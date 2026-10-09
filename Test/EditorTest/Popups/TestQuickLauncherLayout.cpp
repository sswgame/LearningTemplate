#include "pch.h"

#include "Editor/Popups/QuickLauncherLayout.h"

#include "TestFramework/TestFramework.h"

// QuickLauncherLayoutTest — Quick Open 결과 줄의 종류 표시 · 이름 · 경로가 겹치지 않는다(패널 점검 D23). ImGui 없음.

/**
 * @brief [QuickLauncherLayoutTest] 종류 표시 열은 가장 넓은 표시보다 넓고, 이름은 경로 열 앞에서 잘리며, 줄 높이는 프레임 높이를 따른다
 * @details 고정 위치(이름 x 95, 경로 x 300)는 `[GameObject]` 가 95 px 보다 넓은 글자 크기 · 배율에서 이름과 겹쳤다.
 */
SW_TEST_CASE( QuickLauncherLayoutTest, ColumnsDoNotOverlap )
{
    using sw::editor::QuickLauncherLayoutUtil;
    using sw::editor::QuickLauncherRowLayout;
    constexpr float32            kAvailWidth  = 600.0f;
    constexpr float32            kBadgeWidth  = 120.0f; // 배율 1.5 의 "[GameObject]" 쯤
    constexpr float32            kFrameHeight = 27.0f;
    constexpr float32            kTextHeight  = 20.0f;
    constexpr float32            kSpacing     = 8.0f;
    const QuickLauncherRowLayout layout       = QuickLauncherLayoutUtil::makeRowLayout( kAvailWidth, kBadgeWidth, kFrameHeight, kTextHeight, kSpacing );

    SW_EXPECT_TRUE( layout._badgeX + kBadgeWidth < layout._titleX );       // 종류 표시와 이름 사이 여백
    SW_EXPECT_TRUE( layout._titleX < layout._titleClipRight );             // 이름이 그려질 자리가 있다
    SW_EXPECT_TRUE( layout._titleClipRight < layout._detailX );            // 이름은 경로 열 앞에서 잘린다
    SW_EXPECT_TRUE( layout._detailX < kAvailWidth );                       // 경로 열은 줄 안
    SW_EXPECT_NEAR_EQUAL( kFrameHeight * 1.5f, layout._rowHeight, 1e-4f ); // 줄 높이는 글자 크기 · 배율을 따른다
    SW_EXPECT_TRUE( layout._textOffsetY + kTextHeight <= layout._rowHeight );

    // 좁은 줄에서도 열 순서가 뒤집히지 않는다.
    const QuickLauncherRowLayout narrow = QuickLauncherLayoutUtil::makeRowLayout( 100.0f, kBadgeWidth, kFrameHeight, kTextHeight, kSpacing );
    SW_EXPECT_TRUE( narrow._titleX <= narrow._detailX );
}
