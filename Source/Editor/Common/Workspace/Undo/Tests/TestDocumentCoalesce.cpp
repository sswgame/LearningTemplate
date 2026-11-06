// DocumentCoalesceTest — 문서 편집의 합치기 키: 같은 패널 · 같은 문서 · 같은 로드 회차의 이어진 편집만 한 단계로 합친다.
// 예전 키는 패널 종류마다 고정이라, 문서를 바꾼 뒤의 끌기가 앞 문서의 되돌리기 항목에 합쳐져 그 편집을 되돌릴 수 없었다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/Play/EditorSessionPolicy.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/Common/Workspace/Undo/UndoStep.h"

namespace
{
    struct DocumentCoalesceTestUtil
    {
        static void doNothing() {}

        static sw::CommandStack::Command makeCommand()
        {
            sw::CommandStack::Command command;
            command._label = "edit";
            command._undo  = sw::editor::UndoStep::makeTransient( sw::Delegate<void()>::create<&doNothing>(), s_owner );
            command._redo  = sw::editor::UndoStep::makeTransient( sw::Delegate<void()>::create<&doNothing>(), s_owner );
            return command;
        }

        static inline sw::editor::UndoStepOwner s_owner{};
    };
} // namespace

/** @brief [DocumentCoalesceTest] 패널 · 문서 · 로드 회차 중 하나라도 다르면 키가 다르다 */
SW_TEST_CASE( DocumentCoalesceTest, KeyIsScopedToPanelDocumentAndLoad )
{
    using Policy         = sw::editor::EditorSessionPolicy;
    const sw::string key = Policy::makeDocumentCoalesceKey( "Graph.Move", 1, "a.xml", 1 );
    SW_EXPECT_EQUAL( key, Policy::makeDocumentCoalesceKey( "Graph.Move", 1, "a.xml", 1 ) );
    SW_EXPECT_FALSE( key == Policy::makeDocumentCoalesceKey( "Graph.Move", 1, "b.xml", 1 ) );
    SW_EXPECT_FALSE( key == Policy::makeDocumentCoalesceKey( "Graph.Move", 2, "a.xml", 1 ) );
    SW_EXPECT_FALSE( key == Policy::makeDocumentCoalesceKey( "Graph.Move", 1, "a.xml", 2 ) );
    SW_EXPECT_FALSE( key == Policy::makeDocumentCoalesceKey( "Graph.Value", 1, "a.xml", 1 ) );
    SW_EXPECT_TRUE( Policy::makeDocumentCoalesceKey( "", 1, "a.xml", 1 ).empty() ); // 합치지 않는 편집이다
}

/** @brief [DocumentCoalesceTest] 같은 문서의 이어진 끌기는 한 단계이고, 문서를 바꾼 뒤의 끌기는 새 단계다 */
SW_TEST_CASE( DocumentCoalesceTest, SwitchingDocumentStartsANewStep )
{
    using Policy = sw::editor::EditorSessionPolicy;
    sw::CommandStack stack;
    stack.pushCoalesce( Policy::makeDocumentCoalesceKey( "Graph.Move", 1, "a.xml", 1 ), DocumentCoalesceTestUtil::makeCommand() );
    stack.pushCoalesce( Policy::makeDocumentCoalesceKey( "Graph.Move", 1, "a.xml", 1 ), DocumentCoalesceTestUtil::makeCommand() );
    SW_EXPECT_EQUAL( size_t( 1 ), stack.getCommandCount() );

    stack.pushCoalesce( Policy::makeDocumentCoalesceKey( "Graph.Move", 1, "b.xml", 2 ), DocumentCoalesceTestUtil::makeCommand() );
    SW_EXPECT_EQUAL( size_t( 2 ), stack.getCommandCount() );
}
