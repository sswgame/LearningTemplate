#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/Undo/EditorDocumentState.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorDocumentStateTest] 새로 읽으면 저장 값과 기록이 함께 바뀌고, 저장은 기록을 남기며, 되돌린 값이 저장 값과 같으면 깨끗한지 검증
 */
SW_TEST_CASE( EditorDocumentStateTest, ResetClearsHistoryAndSaveKeepsIt )
{
    EditorDocumentState<string> state{ string{ "a" } };
    string                      current{ "a" };
    SW_EXPECT_FALSE( state.isDirty( current ) );

    state.record( current );
    current = "b";
    SW_EXPECT_TRUE( state.isDirty( current ) );
    SW_EXPECT_TRUE( state.canUndo() );

    state.markSaved( current );
    SW_EXPECT_FALSE( state.isDirty( current ) );
    SW_EXPECT_TRUE( state.canUndo() ); // 저장 뒤에도 되돌릴 수 있다

    SW_REQUIRE_TRUE( state.undo( current ) );
    SW_EXPECT_EQUAL( string{ "a" }, current );
    SW_EXPECT_TRUE( state.isDirty( current ) ); // 저장한 값은 "b" 다
    SW_EXPECT_TRUE( state.canRedo() );

    state.reset( string{ "c" } );
    SW_EXPECT_FALSE( state.canUndo() );
    SW_EXPECT_FALSE( state.canRedo() );
    SW_EXPECT_EQUAL( string{ "c" }, state.getSaved() );
}

/**
 * @brief [EditorDocumentStateTest] 같은 키의 연속 편집은 한 단계로 합쳐지고 합치기를 끝내면 새 단계인지 검증
 */
SW_TEST_CASE( EditorDocumentStateTest, CoalesceKeyMergesOneDrag )
{
    EditorDocumentState<int32, int32> state{ 0 };
    int32                             current = 0;
    state.record( current, "drag" );
    current = 1;
    state.record( current, "drag" );
    current = 2;
    state.endCoalescing();
    state.record( current, "drag" );
    current = 3;

    SW_REQUIRE_TRUE( state.undo( current ) );
    SW_EXPECT_EQUAL( 2, current );
    SW_REQUIRE_TRUE( state.undo( current ) );
    SW_EXPECT_EQUAL( 0, current );
    SW_EXPECT_FALSE( state.undo( current ) );
}
