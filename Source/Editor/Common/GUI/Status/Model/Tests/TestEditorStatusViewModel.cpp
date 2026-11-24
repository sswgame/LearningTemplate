// EditorStatusViewModelTest — 하는 일이 없으면 상태줄 항목은 값을 채우지 않고 false 이고, 취소는 할 일이 없다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/GUI/Status/Model/EditorStatusViewModel.h"

using namespace sw;
using namespace sw::editor;

/** @brief [EditorStatusViewModelTest] 굽기가 돌지 않으면 진행 값은 부른 쪽의 것 그대로다 */
SW_TEST_CASE( EditorStatusViewModelTest, IdleBakesReportNothing )
{
    EditorProbeBakeStatus status;
    status._done  = 3;
    status._total = 7;
    SW_EXPECT_FALSE( EditorStatusViewModel::gatherLightProbeBake( status ) );
    SW_EXPECT_FALSE( EditorStatusViewModel::gatherReflectionProbeBake( status ) );
    SW_EXPECT_EQUAL( 3u, status._done );
    SW_EXPECT_EQUAL( 7u, status._total );
    EditorStatusViewModel::cancelLightProbeBake(); // 돌고 있지 않으면 아무것도 하지 않는다
    EditorStatusViewModel::cancelReflectionProbeBake();
    SW_EXPECT_FALSE( EditorStatusViewModel::gatherLightProbeBake( status ) );
}
