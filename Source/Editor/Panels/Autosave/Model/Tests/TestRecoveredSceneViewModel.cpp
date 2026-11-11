#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/Autosave/Model/RecoveredSceneViewModel.h"

// RecoveredSceneViewModelTest — 복구 창 줄의 "언제" 글. 시각을 모르면 회색 대시, 미래 시각(시계가 뒤로 간 기계)은 0 초다.

/**
 * @brief [RecoveredSceneViewModelTest] 지난 시간은 초 · 분 · 시 단위로 내리고, 모르는 시각은 대시다
 */
SW_TEST_CASE( RecoveredSceneViewModelTest, SavedAgeReadsInTheLargestWholeUnit )
{
    using ViewModel         = sw::editor::RecoveredSceneViewModel;
    constexpr int64 kSecond = sw::FileUtil::kFileTimeTicksPerSecond;
    constexpr int64 kNow    = 1000000 * kSecond;
    SW_EXPECT_TRUE( ViewModel::formatSavedAge( 0, kNow ) == "-" );
    SW_EXPECT_TRUE( ViewModel::formatSavedAge( kNow - 59 * kSecond, kNow ) == "59 s ago" );
    SW_EXPECT_TRUE( ViewModel::formatSavedAge( kNow - 61 * kSecond, kNow ) == "1 min ago" );
    SW_EXPECT_TRUE( ViewModel::formatSavedAge( kNow - 7200 * kSecond, kNow ) == "2 h ago" );
    SW_EXPECT_TRUE( ViewModel::formatSavedAge( kNow + 5 * kSecond, kNow ) == "0 s ago" );
}
