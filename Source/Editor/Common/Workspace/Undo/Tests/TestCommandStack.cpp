/**
 * @file TestCommandStack.cpp
 * @brief 되돌리기 스택의 입구 판정(열린 편집 동안 거절 — 감사 E-2)과 한도(명령 수 · 쥔 바이트 — 감사 E-13)를 봅니다.
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

        /** @brief 넉넉한 한도입니다 — 한도를 보지 않는 테스트가 쓴다. */
        static constexpr sw::UndoBufferLimit kRoomyLimit{ 128u, 1ull << 20 };

        /** @brief 이름 @p pLabel 의 명령입니다. 두 단계가 각각 @p heldBytes 를 쥔다고 적는다. */
        static sw::CommandStack::Command makeCommand( const sw::editor::UndoStepOwner& owner, const char* pLabel, sw::uint64 heldBytes = 0u )
        {
            sw::CommandStack::Command cmd;
            cmd._label = pLabel;
            cmd._undo  = sw::editor::UndoStep::makeTransient( sw::Delegate<void()>::create<&CommandStackTestInternal::doNothing>(), owner, heldBytes );
            cmd._redo  = sw::editor::UndoStep::makeTransient( sw::Delegate<void()>::create<&CommandStackTestInternal::doNothing>(), owner, heldBytes );
            return cmd;
        }
    };
} // namespace

/** @brief [CommandStackTest] 상호작용(기즈모 · 문서 위젯 끌기)이 열린 동안 undo · redo · jumpTo 는 위치를 움직이지 않고, 닫으면 다시 된다 */
SW_TEST_CASE( CommandStackTest, RefusesUndoWhileInteractionOpen )
{
    using Internal = CommandStackTestInternal;

    sw::editor::UndoStepOwner owner;
    sw::CommandStack          stack{ Internal::kRoomyLimit };
    stack.push( Internal::makeCommand( owner, "A" ) );
    stack.push( Internal::makeCommand( owner, "B" ) );
    SW_REQUIRE_TRUE( stack.undo() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, stack.getCurrentIndex() );

    sw::CommandStackInteractionScope interaction = stack.openInteraction();
    SW_EXPECT_TRUE( stack.isEditOpen() );
    SW_EXPECT_FALSE( stack.canUndo() );
    SW_EXPECT_FALSE( stack.canRedo() );
    SW_EXPECT_FALSE( stack.undo() );
    SW_EXPECT_FALSE( stack.redo() );
    stack.jumpTo( 0 );
    SW_EXPECT_EQUAL( size_t{ 1 }, stack.getCurrentIndex() ); // 끄는 동안 되돌리면 끝의 단계가 낡은 "전" 으로 다시 하기를 잘랐다
    SW_EXPECT_TRUE( stack.peekRedoLabel() == "B" );          // 이름은 열린 편집과 무관하다

    interaction.reset();
    SW_EXPECT_TRUE( stack.redo() );
    SW_EXPECT_EQUAL( size_t{ 2 }, stack.getCurrentIndex() );
}

/** @brief [CommandStackTest] 거래가 열린 동안 되돌리기를 거절한다 — 언리얼 `UTransBuffer::CanUndo` 가 열린 트랜잭션에서 거절하는 것과 같다 */
SW_TEST_CASE( CommandStackTest, RefusesUndoWhileTransactionOpen )
{
    using Internal = CommandStackTestInternal;

    sw::editor::UndoStepOwner owner;
    sw::CommandStack          stack{ Internal::kRoomyLimit };
    stack.push( Internal::makeCommand( owner, "A" ) );

    stack.beginTransaction( "Open" );
    SW_EXPECT_FALSE( stack.undo() );
    SW_EXPECT_EQUAL( size_t{ 1 }, stack.getCurrentIndex() );
    stack.endTransaction();
    SW_EXPECT_TRUE( stack.undo() );
    SW_EXPECT_EQUAL( size_t{ 0 }, stack.getCurrentIndex() );
}

/** @brief [CommandStackTest] 쥔 바이트가 한도(설정의 Undo Buffer Size)를 넘으면 가장 오래된 명령부터 버리고 위치를 맞춘다 — 개수 상수만 있던 때는 문서 글을 쥔 단계가 끝없이 쌓였다(감사 E-13) */
SW_TEST_CASE( CommandStackTest, EvictsByByteBudget )
{
    using Internal = CommandStackTestInternal;

    sw::editor::UndoStepOwner owner;
    sw::CommandStack          stack{
        sw::UndoBufferLimit{ 100u, 1000u }
    };
    stack.push( Internal::makeCommand( owner, "A", 200u ) ); // 명령 하나가 이름 1 + 단계 둘 400 = 401 바이트
    stack.push( Internal::makeCommand( owner, "B", 200u ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, stack.getCommandCount() );

    stack.push( Internal::makeCommand( owner, "C", 200u ) ); // 1203 > 1000 — A 를 버린다
    SW_EXPECT_EQUAL( size_t{ 2 }, stack.getCommandCount() );
    SW_EXPECT_EQUAL( size_t{ 2 }, stack.getCurrentIndex() );
    SW_EXPECT_TRUE( stack.getCommand( 0 )._label == "B" );

    stack.push( Internal::makeCommand( owner, "Huge", 5000u ) ); // 혼자 한도보다 크다 — 앞을 모두 버리고 방금 한 편집은 남긴다
    SW_EXPECT_EQUAL( size_t{ 1 }, stack.getCommandCount() );
    SW_EXPECT_TRUE( stack.peekUndoLabel() == "Huge" );
}

/** @brief [CommandStackTest] 명령 수가 한도를 넘으면 가장 오래된 것부터 버린다 */
SW_TEST_CASE( CommandStackTest, EvictsByCommandCount )
{
    using Internal = CommandStackTestInternal;

    sw::editor::UndoStepOwner owner;
    sw::CommandStack          stack{
        sw::UndoBufferLimit{ 2u, 1ull << 20 }
    };
    stack.push( Internal::makeCommand( owner, "A" ) );
    stack.push( Internal::makeCommand( owner, "B" ) );
    stack.push( Internal::makeCommand( owner, "C" ) );
    SW_EXPECT_EQUAL( size_t{ 2 }, stack.getCommandCount() );
    SW_EXPECT_TRUE( stack.getCommand( 0 )._label == "B" );
    SW_EXPECT_EQUAL( size_t{ 2 }, stack.getCurrentIndex() );
}

/** @brief [CommandStackTest] 상호작용 핸들은 사라질 때 닫는다(기즈모가 끌기 도중 사라져도 스택이 되돌리기를 영영 거절하지 않는다), 스택이 먼저 사라져도 핸들은 안전하다 */
SW_TEST_CASE( CommandStackTest, InteractionHandleClosesOnDestruction )
{
    using Internal = CommandStackTestInternal;

    sw::editor::UndoStepOwner owner;
    sw::CommandStack          stack{ Internal::kRoomyLimit };
    stack.push( Internal::makeCommand( owner, "A" ) );
    {
        const sw::CommandStackInteractionScope interaction = stack.openInteraction();
        SW_EXPECT_TRUE( interaction.isOpen() );
        SW_EXPECT_FALSE( stack.canUndo() );
    }
    SW_EXPECT_FALSE( stack.isEditOpen() );
    SW_EXPECT_TRUE( stack.undo() );

    sw::CommandStackInteractionScope orphan;
    {
        sw::CommandStack shortLived{ Internal::kRoomyLimit };
        orphan = shortLived.openInteraction();
    }
    SW_EXPECT_FALSE( orphan.isOpen() );
    orphan.reset(); // 사라진 스택을 부르지 않는다 — 터지지 않으면 통과다
}
