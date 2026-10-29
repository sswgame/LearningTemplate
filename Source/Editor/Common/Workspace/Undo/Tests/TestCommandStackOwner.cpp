/**
 * @file TestCommandStackOwner.cpp
 * @brief 코드 단계의 주인(`UndoStepOwner`)이 사라지면 그 단계를 받은 스택에서 명령이 빠지는지, 스택이 먼저 사라져도 안전한지 봅니다.
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/Common/Workspace/Undo/UndoStep.h"

namespace
{
    void doNothing()
    {
    }

    sw::CommandStack::Command makeOwnedCommand( const sw::editor::UndoStepOwner& owner )
    {
        sw::CommandStack::Command cmd;
        cmd._label = "owned";
        cmd._undo  = sw::editor::UndoStep::makeTransient( sw::Delegate<void()>::create<&doNothing>(), owner );
        cmd._redo  = sw::editor::UndoStep::makeTransient( sw::Delegate<void()>::create<&doNothing>(), owner );
        return cmd;
    }
} // namespace

/** @brief [CommandStackOwnerTest] 주인이 사라지면 그 주인의 코드 단계를 가진 명령이 스택에서 빠진다(전역 스택 목록 없이 구독으로) */
SW_TEST_CASE( CommandStackOwnerTest, OwnerReleaseDropsItsCommands )
{
    sw::CommandStack stack;
    {
        sw::editor::UndoStepOwner owner;
        stack.push( makeOwnedCommand( owner ) );
        stack.push( makeOwnedCommand( owner ) );
        SW_EXPECT_TRUE( stack.getCommandCount() == 2u );
    }
    SW_EXPECT_TRUE( stack.getCommandCount() == 0u );
}

/** @brief [CommandStackOwnerTest] 스택이 주인보다 먼저 사라져도 주인의 사라짐 알림이 사라진 스택을 부르지 않는다 */
SW_TEST_CASE( CommandStackOwnerTest, StackReleasedBeforeOwner )
{
    sw::editor::UndoStepOwner owner;
    {
        sw::CommandStack stack;
        stack.push( makeOwnedCommand( owner ) );
        SW_EXPECT_TRUE( stack.getCommandCount() == 1u );
    }
    SW_EXPECT_TRUE( owner.getID() != 0u ); // 주인이 사라질 때(스코프 끝) 풀린 구독은 부르지 않는다 — 터지지 않으면 통과다
}
