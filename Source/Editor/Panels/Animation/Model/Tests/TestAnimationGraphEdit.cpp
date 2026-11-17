// AnimationGraphEditTest — 애니메이션 그래프 편집 명령은 공용 그래프 입구로 알리고 되돌리기 이름을 정한다(감사 E-16).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/GUI/Panel/Model/EditorGraphEditSession.h"
#include "Editor/Common/Workspace/Undo/DocumentEditRecorder.h"
#include "Editor/Panels/Animation/Model/AnimationGraphEdit.h"

using namespace sw;
using namespace sw::editor;

/** @brief [AnimationGraphEditTest] 상태 이름 바꾸기 한 번은 알림 한 번("Rename Animation State")이고 이름이 바뀐다 */
SW_TEST_CASE( AnimationGraphEditTest, RenameStateNotifiesOnceWithItsName )
{
    uint32                       count{ 0 };
    string                       lastLabel;
    const DocumentEditRecorder   recorder{ DocumentEditRecorder::RecordDelegate::create( [&count, &lastLabel]( string_view label )
    {
        ++count;
        lastLabel = string{ label };
    } ) };
    const EditorGraphEditSession session{ recorder };
    string                       stateName{ "Idle" };
    AnimationGraphEdit::renameState( stateName, session, "Run" );
    SW_EXPECT_EQUAL( count, 1u );
    SW_EXPECT_TRUE( lastLabel == "Rename Animation State" );
    SW_EXPECT_TRUE( stateName == "Run" );
}
