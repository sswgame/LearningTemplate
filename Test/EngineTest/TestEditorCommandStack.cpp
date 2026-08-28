#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Utility/CommandStack.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

SW_TEST_CASE( CommandStack, PushUndoRedoAndBranch )
{
	CommandStack stack;
	int32		 value{ 0 };

	CommandStack::Command inc;
	inc._label = "inc";
	inc._redo  = SW_DELEGATE_LAMBDA( Delegate<void()>, [&value]()
	 {
		 ++value;
	 } );
	inc._undo  = SW_DELEGATE_LAMBDA( Delegate<void()>, [&value]()
	 {
		 --value;
	 } );
	inc._redo();
	stack.push( std::move( inc ) );

	SW_EXPECT_TRUE( stack.canUndo() );
	SW_EXPECT_FALSE( stack.canRedo() );
	SW_EXPECT_EQUAL( 1, value );

	stack.undo();
	SW_EXPECT_EQUAL( 0, value );
	SW_EXPECT_TRUE( stack.canRedo() );
	SW_EXPECT_EQUAL( sw::string( "inc" ), sw::string( stack.peekRedoLabel().c_str() ) );

	stack.redo();
	SW_EXPECT_EQUAL( 1, value );

	CommandStack::Command dec;
	dec._label = "dec";
	dec._redo  = SW_DELEGATE_LAMBDA( Delegate<void()>, [&value]()
	 {
		 --value;
	 } );
	dec._undo  = SW_DELEGATE_LAMBDA( Delegate<void()>, [&value]()
	 {
		 ++value;
	 } );
	stack.undo();
	dec._redo();
	stack.push( std::move( dec ) );
	SW_EXPECT_FALSE( stack.canRedo() );
	SW_EXPECT_EQUAL( -1, value );
	SW_EXPECT_EQUAL( sw::string( "dec" ), sw::string( stack.peekUndoLabel().c_str() ) );

	stack.clear();
	SW_EXPECT_FALSE( stack.canUndo() );
	SW_EXPECT_FALSE( stack.canRedo() );
}

/**
 * @brief [CommandStack] 전역 싱글톤 CommandStack 및 다단계 Undo/Redo 체인 검증
 */
SW_TEST_CASE( CommandStack, GlobalSingletonAndMultiStepChain )
{
	sw::CommandStack& globalStack = sw::engine::getCommandStack();
	globalStack.clear();

	SW_EXPECT_FALSE( globalStack.canUndo() );
	SW_EXPECT_FALSE( globalStack.canRedo() );

	int32 count{ 0 };

	for ( int32 cmdIndex = 0; cmdIndex < 3; ++cmdIndex )
	{
		sw::CommandStack::Command cmd;
		cmd._label = "step_" + sw::to_string( cmdIndex );
		cmd._redo  = SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [&count]()
		 { ++count; } );
		cmd._undo  = SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [&count]()
		 { --count; } );
		cmd._redo();
		globalStack.push( std::move( cmd ) );
	}

	SW_EXPECT_EQUAL( 3, count );
	SW_EXPECT_TRUE( globalStack.canUndo() );

	// 3단계 연속 Undo
	globalStack.undo();
	globalStack.undo();
	globalStack.undo();
	SW_EXPECT_EQUAL( 0, count );
	SW_EXPECT_FALSE( globalStack.canUndo() );
	SW_EXPECT_TRUE( globalStack.canRedo() );

	// 3단계 연속 Redo
	globalStack.redo();
	globalStack.redo();
	globalStack.redo();
	SW_EXPECT_EQUAL( 3, count );
	SW_EXPECT_FALSE( globalStack.canRedo() );

	globalStack.clear();
}

/**
 * @brief [CommandStack] 복합 트랜잭션 (Compound Transaction) begin/end/cancel 검증
 */
SW_TEST_CASE( CommandStack, CompoundTransaction )
{
	CommandStack stack;
	int32		 valA{ 0 };
	int32		 valB{ 10 };

	stack.beginTransaction( "MultiEdit" );
	SW_EXPECT_TRUE( stack.isInsideTransaction() );

	CommandStack::Command cmd1;
	cmd1._label = "op1";
	cmd1._redo	= SW_DELEGATE_LAMBDA( Delegate<void()>, [&valA]()
	 {
		 valA += 5;
	 } );
	cmd1._undo	= SW_DELEGATE_LAMBDA( Delegate<void()>, [&valA]()
	 {
		 valA -= 5;
	 } );
	cmd1._redo();
	stack.push( std::move( cmd1 ) );

	CommandStack::Command cmd2;
	cmd2._label = "op2";
	cmd2._redo	= SW_DELEGATE_LAMBDA( Delegate<void()>, [&valB]()
	 {
		 valB *= 2;
	 } );
	cmd2._undo	= SW_DELEGATE_LAMBDA( Delegate<void()>, [&valB]()
	 {
		 valB /= 2;
	 } );
	cmd2._redo();
	stack.push( std::move( cmd2 ) );

	// 아직 커밋 전이므로 stack 자체는 canUndo false
	SW_EXPECT_FALSE( stack.canUndo() );
	SW_EXPECT_EQUAL( 5, valA );
	SW_EXPECT_EQUAL( 20, valB );

	stack.endTransaction();
	SW_EXPECT_FALSE( stack.isInsideTransaction() );
	SW_EXPECT_TRUE( stack.canUndo() );
	SW_EXPECT_EQUAL( sw::string( "MultiEdit" ), sw::string( stack.peekUndoLabel().c_str() ) );

	// 1번의 Undo로 두 연산 모두 롤백
	stack.undo();
	SW_EXPECT_EQUAL( 0, valA );
	SW_EXPECT_EQUAL( 10, valB );
	SW_EXPECT_TRUE( stack.canRedo() );

	// 1번의 Redo로 두 연산 모두 복원
	stack.redo();
	SW_EXPECT_EQUAL( 5, valA );
	SW_EXPECT_EQUAL( 20, valB );

	// 트랜잭션 취소 검증
	stack.beginTransaction( "CancelledOp" );
	CommandStack::Command cmd3;
	cmd3._label = "op3";
	cmd3._redo	= SW_DELEGATE_LAMBDA( Delegate<void()>, [&valA]()
	 {
		 valA += 100;
	 } );
	cmd3._undo	= SW_DELEGATE_LAMBDA( Delegate<void()>, [&valA]()
	 {
		 valA -= 100;
	 } );
	cmd3._redo();
	stack.push( std::move( cmd3 ) );
	stack.cancelTransaction();
	SW_EXPECT_FALSE( stack.isInsideTransaction() );

	// stack 상단은 여전히 MultiEdit
	SW_EXPECT_EQUAL( sw::string( "MultiEdit" ), sw::string( stack.peekUndoLabel().c_str() ) );
}

/**
 * @brief [CommandStack] 슬라이더/드래그 연속 액션 병합 (pushCoalesce) 검증
 */
SW_TEST_CASE( CommandStack, PushCoalesce )
{
	CommandStack stack;
	float32		 sliderValue{ 0.0f };

	// 슬라이더를 0.0 -> 1.0 -> 2.5 -> 5.0 으로 드래그했을 때
	const float32 initialVal			= 0.0f;
	const float32 arrIntermediateVals[] = { 1.0f, 2.5f, 5.0f };

	for ( const float32 targetVal : arrIntermediateVals )
	{
		CommandStack::Command cmd;
		cmd._label = "SliderDrag";
		cmd._redo  = SW_DELEGATE_LAMBDA( Delegate<void()>, [&sliderValue, targetVal]()
		 {
			 sliderValue = targetVal;
		 } );
		cmd._undo  = SW_DELEGATE_LAMBDA( Delegate<void()>, [&sliderValue, initialVal]()
		 {
			 sliderValue = initialVal;
		 } );
		cmd._redo();
		stack.pushCoalesce( "Transform_PosX", std::move( cmd ) );
	}

	// 3번 pushCoalesce 되었지만 스택에는 1개의 명령만 존재해야 함
	SW_EXPECT_EQUAL( 5.0f, sliderValue );
	SW_EXPECT_TRUE( stack.canUndo() );

	// 1번의 Undo로 초기값 0.0f 로 복원
	stack.undo();
	SW_EXPECT_EQUAL( 0.0f, sliderValue );
	SW_EXPECT_FALSE( stack.canUndo() );
	SW_EXPECT_TRUE( stack.canRedo() );

	// 1번의 Redo로 최종값 5.0f 로 복원
	stack.redo();
	SW_EXPECT_EQUAL( 5.0f, sliderValue );
}

/**
 * @brief [CommandStack] 히스토리 검사 및 특정 시점 다단계 점프 (jumpTo) 검증
 */
SW_TEST_CASE( CommandStack, JumpToAndHistoryInspection )
{
	CommandStack stack;
	int32		 value{ 0 };

	for ( int32 cmdIndex = 1; cmdIndex <= 5; ++cmdIndex )
	{
		CommandStack::Command cmd;
		cmd._label = "step_" + sw::to_string( cmdIndex );
		cmd._redo  = SW_DELEGATE_LAMBDA( Delegate<void()>, [&value, cmdIndex]()
		 { value = cmdIndex; } );
		cmd._undo  = SW_DELEGATE_LAMBDA( Delegate<void()>, [&value, cmdIndex]()
		 { value = cmdIndex - 1; } );
		cmd._redo();
		stack.push( std::move( cmd ) );
	}

	SW_EXPECT_EQUAL( 5, value );
	SW_EXPECT_EQUAL( static_cast<size_t>( 5 ), stack.getCommandCount() );
	SW_EXPECT_EQUAL( static_cast<size_t>( 5 ), stack.getCurrentIndex() );
	SW_EXPECT_EQUAL( sw::string( "step_1" ), sw::string( stack.getCommand( 0 )._label.c_str() ) );
	SW_EXPECT_EQUAL( sw::string( "step_5" ), sw::string( stack.getCommand( 4 )._label.c_str() ) );

	// jumpTo(2) -> 2번 상태 (step_2 실행 완료 시점)로 롤백
	stack.jumpTo( 2 );
	SW_EXPECT_EQUAL( 2, value );
	SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), stack.getCurrentIndex() );
	SW_EXPECT_TRUE( stack.canUndo() );
	SW_EXPECT_TRUE( stack.canRedo() );

	// jumpTo(0) -> 최초 상태 (0번)로 롤백
	stack.jumpTo( 0 );
	SW_EXPECT_EQUAL( 0, value );
	SW_EXPECT_EQUAL( static_cast<size_t>( 0 ), stack.getCurrentIndex() );
	SW_EXPECT_FALSE( stack.canUndo() );
	SW_EXPECT_TRUE( stack.canRedo() );

	// jumpTo(5) -> 최종 상태 (5번)로 고속 복원
	stack.jumpTo( 5 );
	SW_EXPECT_EQUAL( 5, value );
	SW_EXPECT_EQUAL( static_cast<size_t>( 5 ), stack.getCurrentIndex() );
	SW_EXPECT_TRUE( stack.canUndo() );
	SW_EXPECT_FALSE( stack.canRedo() );
}
