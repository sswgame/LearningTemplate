#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
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

/**
 * @brief [CommandStack] GameObject 바이너리 스냅샷 기반 다단계 Undo/Redo 트랜잭션 스트레스 검증
 */
SW_TEST_CASE( CommandStack, GameObjectBinarySnapshotUndoRedoTransactions )
{
	GameObjectManager manager;
	GameObject*		  pObject = manager.createGameObject( hashed_string( "TransactionHero" ) );
	SW_ASSERT_NOT_NULL( pObject );
	SceneComponent* pSceneComp = pObject->addComponent<SceneComponent>();
	SW_ASSERT_NOT_NULL( pSceneComp );
	pSceneComp->setLocalPosition( float3{ 0.0f, 0.0f, 0.0f } );

	CommandStack stack;

	constexpr int32 kTotalSteps = 30;
	for ( int32 stepIndex = 1; stepIndex <= kTotalSteps; ++stepIndex )
	{
		vector<uint8> beforeBytes;
		SW_ASSERT_TRUE( ObjectStateSerializer::saveToBinaryBuffer( pObject, beforeBytes ) );

		const float3 newPos{ static_cast<float32>( stepIndex * 2 ), static_cast<float32>( stepIndex * 3 ), static_cast<float32>( stepIndex * 4 ) };
		pSceneComp->setLocalPosition( newPos );

		vector<uint8> afterBytes;
		SW_ASSERT_TRUE( ObjectStateSerializer::saveToBinaryBuffer( pObject, afterBytes ) );

		CommandStack::Command cmd;
		cmd._label = "MoveStep_" + to_string( stepIndex );
		cmd._redo  = SW_DELEGATE_LAMBDA( Delegate<void()>, [pObject, afterBytes]()
		 {
			 string parentName;
			 ObjectStateSerializer::loadFromBinaryBuffer( pObject, afterBytes.data(), afterBytes.size(), parentName );
		 } );
		cmd._undo  = SW_DELEGATE_LAMBDA( Delegate<void()>, [pObject, beforeBytes]()
		 {
			 string parentName;
			 ObjectStateSerializer::loadFromBinaryBuffer( pObject, beforeBytes.data(), beforeBytes.size(), parentName );
		 } );

		stack.push( std::move( cmd ) );
	}

	SW_EXPECT_EQUAL( static_cast<size_t>( kTotalSteps ), stack.getCommandCount() );
	SW_EXPECT_TRUE( stack.canUndo() );

	// 1. 30단계 전체 Undo 롤백
	for ( int32 stepIndex = kTotalSteps; stepIndex >= 1; --stepIndex )
	{
		stack.undo();
	}
	SW_EXPECT_FALSE( stack.canUndo() );
	SW_EXPECT_TRUE( stack.canRedo() );

	// 최초 상태 (0,0,0) 복원 확인
	SceneComponent* pRestoredComp = pObject->getPrimarySceneComponent();
	SW_ASSERT_NOT_NULL( pRestoredComp );
	SW_EXPECT_NEAR_EQUAL( 0.0f, pRestoredComp->getLocalPosition()._x, 0.001f );
	SW_EXPECT_NEAR_EQUAL( 0.0f, pRestoredComp->getLocalPosition()._y, 0.001f );
	SW_EXPECT_NEAR_EQUAL( 0.0f, pRestoredComp->getLocalPosition()._z, 0.001f );

	// 2. 30단계 전체 Redo 재실행
	for ( int32 stepIndex = 1; stepIndex <= kTotalSteps; ++stepIndex )
	{
		stack.redo();
	}
	SW_EXPECT_TRUE( stack.canUndo() );
	SW_EXPECT_FALSE( stack.canRedo() );

	// 최종 상태 (60, 90, 120) 확인
	pRestoredComp = pObject->getPrimarySceneComponent();
	SW_ASSERT_NOT_NULL( pRestoredComp );
	SW_EXPECT_NEAR_EQUAL( 60.0f, pRestoredComp->getLocalPosition()._x, 0.001f );
	SW_EXPECT_NEAR_EQUAL( 90.0f, pRestoredComp->getLocalPosition()._y, 0.001f );
	SW_EXPECT_NEAR_EQUAL( 120.0f, pRestoredComp->getLocalPosition()._z, 0.001f );

	// 3. jumpTo(15) -> 15단계 시점 (30, 45, 60)으로 임의 점프
	stack.jumpTo( 15 );
	pRestoredComp = pObject->getPrimarySceneComponent();
	SW_ASSERT_NOT_NULL( pRestoredComp );
	SW_EXPECT_NEAR_EQUAL( 30.0f, pRestoredComp->getLocalPosition()._x, 0.001f );
	SW_EXPECT_NEAR_EQUAL( 45.0f, pRestoredComp->getLocalPosition()._y, 0.001f );
	SW_EXPECT_NEAR_EQUAL( 60.0f, pRestoredComp->getLocalPosition()._z, 0.001f );
}

/**
 * @brief [CommandStack] PIE 스냅샷 및 계층 구조 대규모 변이 복원 스트레스 검증
 */
SW_TEST_CASE( CommandStack, EditorPlaySessionBinaryHierarchySnapshotStress )
{
	GameObjectManager manager;

	// 1. 20개의 계층형 GameObject 생성 (Root -> Child)
	constexpr size_t kHierarchyCount = 10;
	struct ObjectSnapshotRecord
	{
		string		  _name;
		vector<uint8> _bytes;
	};
	vector<ObjectSnapshotRecord> listRecord;
	listRecord.reserve( kHierarchyCount * 2 );

	for ( size_t rootIndex = 0; rootIndex < kHierarchyCount; ++rootIndex )
	{
		const string rootName = "Root_" + to_string( rootIndex );
		GameObject*	 pRoot	  = manager.createGameObject( hashed_string( rootName.c_str() ) );
		SW_ASSERT_NOT_NULL( pRoot );
		SceneComponent* pRootSc = pRoot->addComponent<SceneComponent>();
		SW_ASSERT_NOT_NULL( pRootSc );
		pRootSc->setLocalPosition( float3{ static_cast<float32>( rootIndex * 10 ), 0.0f, 0.0f } );

		const string childName = "Child_" + to_string( rootIndex );
		GameObject*	 pChild	   = manager.createGameObject( hashed_string( childName.c_str() ) );
		SW_ASSERT_NOT_NULL( pChild );
		SceneComponent* pChildSc = pChild->addComponent<SceneComponent>();
		SW_ASSERT_NOT_NULL( pChildSc );
		pChildSc->setLocalPosition( float3{ 1.0f, 2.0f, 3.0f } );
		pChildSc->attachToComponent( pRootSc );

		vector<uint8> rootBytes;
		SW_ASSERT_TRUE( ObjectStateSerializer::saveToBinaryBuffer( pRoot, rootBytes ) );
		listRecord.push_back( { rootName, std::move( rootBytes ) } );

		vector<uint8> childBytes;
		SW_ASSERT_TRUE( ObjectStateSerializer::saveToBinaryBuffer( pChild, childBytes ) );
		listRecord.push_back( { childName, std::move( childBytes ) } );
	}

	// 2. PIE 세션 모의 변이 (삭제, 추가, 위치 변형)
	for ( size_t rootIndex = 0; rootIndex < kHierarchyCount / 2; ++rootIndex )
	{
		const string rootName = "Root_" + to_string( rootIndex );
		GameObject*	 pRoot	  = manager.findGameObjectByName( hashed_string( rootName.c_str() ) );
		if ( pRoot != nullptr )
			manager.destroyObject( pRoot );
	}
	for ( size_t newIndex = 0; newIndex < 5; ++newIndex )
	{
		const string pieSpawnName = "PieSpawn_" + to_string( newIndex );
		manager.createGameObject( hashed_string( pieSpawnName.c_str() ) );
	}

	// 3. PIE 세션 종료 롤백: 전체 초기화 후 바이너리 스냅샷 복원
	manager.clear();

	for ( const auto& record : listRecord )
	{
		GameObject* pRestored = manager.createGameObject( hashed_string( record._name.c_str() ) );
		SW_ASSERT_NOT_NULL( pRestored );
		string		 parentName;
		const size_t bytesRead = ObjectStateSerializer::loadFromBinaryBuffer( pRestored, record._bytes.data(), record._bytes.size(), parentName );
		SW_EXPECT_EQUAL( record._bytes.size(), bytesRead );
	}

	// 4. 모든 20개 오브젝트가 원본 상태로 정확히 복원되었는지 검증
	for ( size_t rootIndex = 0; rootIndex < kHierarchyCount; ++rootIndex )
	{
		const string rootName = "Root_" + to_string( rootIndex );
		GameObject*	 pRoot	  = manager.findGameObjectByName( hashed_string( rootName.c_str() ) );
		SW_ASSERT_NOT_NULL( pRoot );
		SceneComponent* pRootSc = pRoot->getPrimarySceneComponent();
		SW_ASSERT_NOT_NULL( pRootSc );
		SW_EXPECT_NEAR_EQUAL( static_cast<float32>( rootIndex * 10 ), pRootSc->getLocalPosition()._x, 0.001f );

		const string childName = "Child_" + to_string( rootIndex );
		GameObject*	 pChild	   = manager.findGameObjectByName( hashed_string( childName.c_str() ) );
		SW_ASSERT_NOT_NULL( pChild );
		SceneComponent* pChildSc = pChild->getPrimarySceneComponent();
		SW_ASSERT_NOT_NULL( pChildSc );
		SW_EXPECT_NEAR_EQUAL( 1.0f, pChildSc->getLocalPosition()._x, 0.001f );
		SW_EXPECT_NEAR_EQUAL( 2.0f, pChildSc->getLocalPosition()._y, 0.001f );
		SW_EXPECT_NEAR_EQUAL( 3.0f, pChildSc->getLocalPosition()._z, 0.001f );
	}
}

/**
 * @brief [CommandStack] 컴포넌트 클립보드 바이너리 복사/붙여넣기 정밀도 검증
 */
SW_TEST_CASE( CommandStack, ComponentBinaryClipboardValuePasting )
{
	GameObjectManager manager;
	GameObject*		  pSourceObj = manager.createGameObject( hashed_string( "ClipboardSource" ) );
	SW_ASSERT_NOT_NULL( pSourceObj );
	SceneComponent* pSourceSc = pSourceObj->addComponent<SceneComponent>();
	SW_ASSERT_NOT_NULL( pSourceSc );
	pSourceSc->setLocalPosition( float3{ 12.5f, -34.0f, 56.75f } );
	pSourceSc->setLocalRotation( float3{ 10.0f, 20.0f, 30.0f } );
	pSourceSc->setLocalScale( float3{ 2.0f, 2.0f, 2.0f } );

	// 클립보드에 바이너리 직렬화
	vector<uint8>	clipBytes;
	const TypeInfo* pTypeInfo = pSourceSc->getTypeInfo();
	SW_ASSERT_NOT_NULL( pTypeInfo );
	BinarySerializer::serializeVersioned( 0, pSourceSc, *pTypeInfo, clipBytes );
	SW_EXPECT_FALSE( clipBytes.empty() );

	// 대상 컴포넌트에 바이너리 붙여넣기
	GameObject* pTargetObj = manager.createGameObject( hashed_string( "ClipboardTarget" ) );
	SW_ASSERT_NOT_NULL( pTargetObj );
	SceneComponent* pTargetSc = pTargetObj->addComponent<SceneComponent>();
	SW_ASSERT_NOT_NULL( pTargetSc );

	uint32 schemaVer{ 0 };
	SW_ASSERT_TRUE( BinarySerializer::deserializeVersioned( schemaVer, pTargetSc, *pTypeInfo, clipBytes.data(), clipBytes.size(), 0 ) );

	SW_EXPECT_NEAR_EQUAL( 12.5f, pTargetSc->getLocalPosition()._x, 0.001f );
	SW_EXPECT_NEAR_EQUAL( -34.0f, pTargetSc->getLocalPosition()._y, 0.001f );
	SW_EXPECT_NEAR_EQUAL( 56.75f, pTargetSc->getLocalPosition()._z, 0.001f );
	SW_EXPECT_NEAR_EQUAL( 10.0f, pTargetSc->getLocalRotation()._x, 0.001f );
	SW_EXPECT_NEAR_EQUAL( 20.0f, pTargetSc->getLocalRotation()._y, 0.001f );
	SW_EXPECT_NEAR_EQUAL( 30.0f, pTargetSc->getLocalRotation()._z, 0.001f );
	SW_EXPECT_NEAR_EQUAL( 2.0f, pTargetSc->getLocalScale()._x, 0.001f );
	SW_EXPECT_NEAR_EQUAL( 2.0f, pTargetSc->getLocalScale()._y, 0.001f );
	SW_EXPECT_NEAR_EQUAL( 2.0f, pTargetSc->getLocalScale()._z, 0.001f );
}
