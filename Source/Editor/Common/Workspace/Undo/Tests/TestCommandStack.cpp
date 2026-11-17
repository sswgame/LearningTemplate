/**
 * @file TestCommandStack.cpp
 * @brief 되돌리기 스택의 입구 판정을 봅니다 — 열린 편집(거래 · 상호작용) 동안 되돌리기 · 다시 하기 · 이동을 거절한다(감사 E-2).
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/Common/Workspace/Undo/UndoStep.h"

namespace
{
    struct CommandStackTestInternal
    {
        static void doNothing() {}

        static sw::CommandStack::Command makeCommand( const sw::editor::UndoStepOwner& owner, const char* pLabel )
        {
            sw::CommandStack::Command cmd;
            cmd._label = pLabel;
            cmd._undo  = sw::editor::UndoStep::makeTransient( sw::Delegate<void()>::create<&CommandStackTestInternal::doNothing>(), owner );
            cmd._redo  = sw::editor::UndoStep::makeTransient( sw::Delegate<void()>::create<&CommandStackTestInternal::doNothing>(), owner );
            return cmd;
        }
    };
} // namespace

/** @brief [CommandStackTest] 상호작용(기즈모 · 문서 위젯 끌기)이 열린 동안 undo · redo · jumpTo 는 위치를 움직이지 않고, 닫으면 다시 된다 */
SW_TEST_CASE( CommandStackTest, RefusesUndoWhileInteractionOpen )
{
    using Internal = CommandStackTestInternal;

    sw::editor::UndoStepOwner owner;
    sw::CommandStack          stack;
    stack.push( Internal::makeCommand( owner, "A" ) );
    stack.push( Internal::makeCommand( owner, "B" ) );
    SW_REQUIRE_TRUE( stack.undo() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, stack.getCurrentIndex() );

    stack.beginInteraction();
    SW_EXPECT_TRUE( stack.isEditOpen() );
    SW_EXPECT_FALSE( stack.canUndo() );
    SW_EXPECT_FALSE( stack.canRedo() );
    SW_EXPECT_FALSE( stack.undo() );
    SW_EXPECT_FALSE( stack.redo() );
    stack.jumpTo( 0 );
    SW_EXPECT_EQUAL( size_t{ 1 }, stack.getCurrentIndex() ); // 끄는 동안 되돌리면 끝의 단계가 낡은 "전" 으로 다시 하기를 잘랐다
    SW_EXPECT_TRUE( stack.peekRedoLabel() == "B" );          // 이름은 열린 편집과 무관하다

    stack.endInteraction();
    SW_EXPECT_TRUE( stack.redo() );
    SW_EXPECT_EQUAL( size_t{ 2 }, stack.getCurrentIndex() );
}

/** @brief [CommandStackTest] 거래가 열린 동안 되돌리기를 거절한다 — 언리얼 `UTransBuffer::CanUndo` 가 열린 트랜잭션에서 거절하는 것과 같다 */
SW_TEST_CASE( CommandStackTest, RefusesUndoWhileTransactionOpen )
{
    using Internal = CommandStackTestInternal;

    sw::editor::UndoStepOwner owner;
    sw::CommandStack          stack;
    stack.push( Internal::makeCommand( owner, "A" ) );

    stack.beginTransaction( "Open" );
    SW_EXPECT_FALSE( stack.undo() );
    SW_EXPECT_EQUAL( size_t{ 1 }, stack.getCurrentIndex() );
    stack.endTransaction();
    SW_EXPECT_TRUE( stack.undo() );
    SW_EXPECT_EQUAL( size_t{ 0 }, stack.getCurrentIndex() );
}
