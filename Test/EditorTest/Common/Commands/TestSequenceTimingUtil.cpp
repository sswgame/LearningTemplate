#include "pch.h"

#include "Editor/Common/Commands/SequenceTimingUtil.h"

#include "TestFramework/TestFramework.h"

// SequenceTimingUtilTest — 시퀀서 타임라인 끌기가 dirty · 되돌리기에 남는지 가르는 배치 비교(패널 점검 D17). ImGui 없음.

/**
 * @brief [SequenceTimingUtilTest] 클립을 옮기거나(시작 · 끝) 종류를 바꾸거나 더하거나 지우면 바뀐 것이고, 그대로면 바뀌지 않은 것이다
 * @details 패널은 ImSequencer 를 부른 뒤 `ImGui::IsItemEdited()` 로 편집을 판정했다 — 여러 항목을 그리는 위젯의 "마지막 항목" 은 클립 끌기가 아니라
 *          끌어 옮긴 클립이 dirty 표시 · History 에 남지 않았다. 부르기 전후의 배치를 비교한다.
 */
SW_TEST_CASE( SequenceTimingUtilTest, DetectsMovedAddedAndRemovedClips )
{
    using sw::editor::SequenceClipTiming;
    using sw::editor::SequenceTimingUtil;
    sw::vector<sw::SequenceTrackItem> listItem( 2 );
    listItem[0]._start = 0;
    listItem[0]._end   = 10;
    listItem[1]._start = 20;
    listItem[1]._end   = 30;

    sw::vector<SequenceClipTiming> listBefore;
    SequenceTimingUtil::captureTiming( listItem, listBefore );
    SW_EXPECT_FALSE( SequenceTimingUtil::hasTimingChanged( listBefore, listItem ) );

    // 이름 · 변환처럼 타임라인이 바꾸지 않는 칸은 배치가 아니다(그 칸은 자기 편집 칸이 알린다).
    listItem[0]._name = "Renamed";
    SW_EXPECT_FALSE( SequenceTimingUtil::hasTimingChanged( listBefore, listItem ) );

    // 끌어 옮긴 클립 — 시작 · 끝이 함께 움직인다.
    listItem[1]._start = 22;
    listItem[1]._end   = 32;
    SW_EXPECT_TRUE( SequenceTimingUtil::hasTimingChanged( listBefore, listItem ) );

    // 끝만 늘인 클립.
    SequenceTimingUtil::captureTiming( listItem, listBefore );
    listItem[0]._end = 12;
    SW_EXPECT_TRUE( SequenceTimingUtil::hasTimingChanged( listBefore, listItem ) );

    // 종류가 바뀐 클립.
    SequenceTimingUtil::captureTiming( listItem, listBefore );
    listItem[0]._kind = sw::SequenceItemKind::Event;
    SW_EXPECT_TRUE( SequenceTimingUtil::hasTimingChanged( listBefore, listItem ) );

    // 더한 클립과 지운 클립.
    SequenceTimingUtil::captureTiming( listItem, listBefore );
    listItem.push_back( sw::SequenceTrackItem{} );
    SW_EXPECT_TRUE( SequenceTimingUtil::hasTimingChanged( listBefore, listItem ) );
    SequenceTimingUtil::captureTiming( listItem, listBefore );
    listItem.pop_back();
    SW_EXPECT_TRUE( SequenceTimingUtil::hasTimingChanged( listBefore, listItem ) );
}
