#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/Common/Workspace/Undo/EditorDocumentState.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 값 하나가 문서인 시험용 모델입니다 — 생성자에서 기록에 지금 값을 만들고 넣는 법을 준다. */
    template <typename T>
    struct StateProbe
    {
        T                         _current;
        EditorDocumentState<T, T> _state;

        explicit StateProbe( T initial )
            : _current{ initial }
            , _state{ initial }
        {
            _state.bind( SW_DELEGATE_LAMBDA( Delegate<T()>, [this]()
            { return _current; } ),
                         SW_DELEGATE_LAMBDA( Delegate<void( const T& )>, [this]( const T& snapshot )
            { _current = snapshot; } ) );
        }
        void edit( T value, string_view key = {} )
        {
            _state.record( _current, key );
            _current = value;
        }
    };
} // namespace

/** @brief [EditorDocumentStateTest] 새로 읽으면 저장 값과 기록이 함께 바뀌고, 저장은 기록을 남기며, 되돌린 값이 저장 값과 같으면 깨끗하다 */
SW_TEST_CASE( EditorDocumentStateTest, ResetClearsHistoryAndSaveKeepsIt )
{
    StateProbe<string> probe{ string{ "a" } };
    SW_EXPECT_FALSE( probe._state.isDirty( probe._current ) );

    probe.edit( "b" );
    SW_EXPECT_TRUE( probe._state.isDirty( probe._current ) );
    SW_EXPECT_TRUE( probe._state.canUndo() );

    probe._state.markSaved( probe._current );
    SW_EXPECT_FALSE( probe._state.isDirty( probe._current ) );
    SW_EXPECT_TRUE( probe._state.canUndo() ); // 저장 뒤에도 되돌릴 수 있다

    SW_REQUIRE_TRUE( probe._state.undo() );
    SW_EXPECT_EQUAL( string{ "a" }, probe._current );
    SW_EXPECT_TRUE( probe._state.isDirty( probe._current ) ); // 저장한 값은 "b" 다
    SW_REQUIRE_TRUE( probe._state.redo() );
    SW_EXPECT_EQUAL( string{ "b" }, probe._current );

    probe._state.reset( string{ "c" } );
    SW_EXPECT_FALSE( probe._state.canUndo() );
    SW_EXPECT_FALSE( probe._state.canRedo() );
    SW_EXPECT_EQUAL( string{ "c" }, probe._state.getSaved() );
}

/** @brief [EditorDocumentStateTest] 같은 키의 연속 편집은 한 명령으로 합쳐지고, 합치기를 끝내면 새 명령이다 */
SW_TEST_CASE( EditorDocumentStateTest, CoalesceKeyMergesOneDrag )
{
    StateProbe<int32> probe{ 0 };
    probe.edit( 1, "drag" );
    probe.edit( 2, "drag" );
    probe._state.endCoalescing();
    probe.edit( 3, "drag" );

    SW_EXPECT_EQUAL( size_t{ 2 }, probe._state.getStack().getCommandCount() );
    SW_REQUIRE_TRUE( probe._state.undo() );
    SW_EXPECT_EQUAL( 2, probe._current );
    SW_REQUIRE_TRUE( probe._state.undo() );
    SW_EXPECT_EQUAL( 0, probe._current );
    SW_EXPECT_FALSE( probe._state.undo() );
}

/** @brief [EditorDocumentStateTest] 기록은 명령 스택이다 — 묶은 스택의 Ctrl+Z(스택 undo)가 모델을 되돌리고, 열린 상호작용 동안은 거절하며, 거절한 편집은 다시 하기에도 남지 않는다 */
SW_TEST_CASE( EditorDocumentStateTest, TheHistoryIsTheBoundCommandStack )
{
    CommandStack stack{
        UndoBufferLimit{ 64, 1u << 20 }
    };
    StateProbe<int32> probe{ 0 };
    probe._state.bindStack( stack );
    probe.edit( 1 );
    probe.edit( 2 );
    SW_REQUIRE_EQUAL( size_t{ 2 }, stack.getCommandCount() );

    {
        const CommandStackInteractionScope interaction = stack.openInteraction();
        SW_EXPECT_FALSE( probe._state.undo() ); // 끄는 중에는 되돌리지 않는다
    }
    SW_REQUIRE_TRUE( stack.undo() );
    SW_EXPECT_EQUAL( 1, probe._current );
    SW_REQUIRE_TRUE( stack.redo() );
    SW_EXPECT_EQUAL( 2, probe._current );

    probe.edit( 3 );
    probe._state.cancelLast(); // 편집을 거절했다
    SW_EXPECT_EQUAL( 2, probe._current );
    SW_EXPECT_FALSE( stack.canRedo() );
    SW_EXPECT_EQUAL( size_t{ 2 }, stack.getCommandCount() );
}

/** @brief [EditorDocumentStateTest] 모델이 사라지거나 문서를 다시 읽으면 그 명령만 묶은 스택에서 빠진다 — 다른 모델의 명령은 남는다 */
SW_TEST_CASE( EditorDocumentStateTest, ReleasedHistoryLeavesOtherCommands )
{
    CommandStack stack{
        UndoBufferLimit{ 64, 1u << 20 }
    };
    StateProbe<int32> kept{ 0 };
    kept._state.bindStack( stack );
    kept.edit( 1 );
    {
        StateProbe<int32> gone{ 0 };
        gone._state.bindStack( stack );
        gone.edit( 5 );
        SW_REQUIRE_EQUAL( size_t{ 2 }, stack.getCommandCount() );
    }
    SW_EXPECT_EQUAL( size_t{ 1 }, stack.getCommandCount() );
    kept._state.reset( 1 );
    SW_EXPECT_EQUAL( size_t{ 0 }, stack.getCommandCount() );
}
