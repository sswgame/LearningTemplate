// EditorPlayToolbarViewModelTest — Step N 칸의 프레임 수는 1 과 세션 상한 사이로 자른다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/GUI/Model/EditorPlayToolbarViewModel.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

using namespace sw;
using namespace sw::editor;

/** @brief [EditorPlayToolbarViewModelTest] 0 · 음수는 1 로, 상한을 넘으면 상한으로 자르고 범위 안의 값은 그대로다 */
SW_TEST_CASE( EditorPlayToolbarViewModelTest, StepFrameCountIsClamped )
{
    const int32                maxFrameCount = static_cast<int32>( EditorPlaySession::kMaxStepFrameCount );
    EditorPlayToolbarViewModel model;
    model.editUIState()._stepFrameCount = -3;
    model.clampStepFrameCount();
    SW_EXPECT_EQUAL( 1, model.getUIState()._stepFrameCount );
    model.editUIState()._stepFrameCount = maxFrameCount + 1;
    model.clampStepFrameCount();
    SW_EXPECT_EQUAL( maxFrameCount, model.getUIState()._stepFrameCount );
    model.editUIState()._stepFrameCount = 25;
    model.clampStepFrameCount();
    SW_EXPECT_EQUAL( 25, model.getUIState()._stepFrameCount );
}
