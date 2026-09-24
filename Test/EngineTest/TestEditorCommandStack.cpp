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

SW_TEST_CASE( EditorCommandStackTest, PushUndoRedoAndBranch )
{
    CommandStack stack;
    int32        value{ 0 };

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
 * @brief [EditorCommandStackTest] 전역 싱글톤 CommandStack 및 다단계 Undo/Redo 체인 검증
 */
SW_TEST_CASE( EditorCommandStackTest, GlobalSingletonAndMultiStepChain )
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
 * @brief [EditorCommandStackTest] 복합 트랜잭션 (Compound Transaction) begin/end/cancel 검증
 */
/**
 * @brief [EditorCommandStackTest] 중첩 트랜잭션은 최외곽에서 하나로 커밋된다.
 * @details 예전에는 1비트 플래그라 안쪽 begin 이 바깥이 쌓아둔 목록을 clear 하고,
 *          안쪽 end 가 플래그를 풀어 바깥 Undo 기록이 통째로 유실됐다.
 */
SW_TEST_CASE( EditorCommandStackTest, NestedTransactionCommitsOnceAtOutermost )
{
    CommandStack stack;
    int32        value{ 0 };

    const auto pushAdd = [&stack, &value]( int32 amount )
    {
        CommandStack::Command cmd;
        cmd._label = "add";
        cmd._redo  = SW_DELEGATE_LAMBDA( Delegate<void()>, [&value, amount]()
         {
            value += amount;
        } );
        cmd._undo  = SW_DELEGATE_LAMBDA( Delegate<void()>, [&value, amount]()
         {
            value -= amount;
        } );
        cmd._redo();
        stack.push( std::move( cmd ) );
    };

    stack.beginTransaction( "Outer" );
    pushAdd( 1 );

    stack.beginTransaction( "Inner" ); // 중첩 진입 — 여기서 커밋되면 안 된다
    SW_EXPECT_TRUE( stack.isInsideTransaction() );
    pushAdd( 2 );
    stack.endTransaction(); // 안쪽 종료 — 아직 트랜잭션 안이어야 한다
    SW_EXPECT_TRUE( stack.isInsideTransaction() );
    SW_EXPECT_FALSE( stack.canUndo() ); // 최외곽이 끝나기 전엔 히스토리에 없어야 한다

    pushAdd( 4 );
    stack.endTransaction();

    SW_EXPECT_FALSE( stack.isInsideTransaction() );
    SW_EXPECT_EQUAL( 7, value );

    // 세 커맨드가 하나의 복합 커맨드로 합쳐져야 한다: 한 번의 undo 로 전부 되돌아간다.
    SW_EXPECT_TRUE( stack.canUndo() );
    stack.undo();
    SW_EXPECT_EQUAL( 0, value );
    SW_EXPECT_FALSE( stack.canUndo() );

    stack.redo();
    SW_EXPECT_EQUAL( 7, value );
};

SW_TEST_CASE( EditorCommandStackTest, CompoundTransaction )
{
    CommandStack stack;
    int32        valA{ 0 };
    int32        valB{ 10 };

    stack.beginTransaction( "MultiEdit" );
    SW_EXPECT_TRUE( stack.isInsideTransaction() );

    CommandStack::Command cmd1;
    cmd1._label = "op1";
    cmd1._redo  = SW_DELEGATE_LAMBDA( Delegate<void()>, [&valA]()
     {
        valA += 5;
    } );
    cmd1._undo  = SW_DELEGATE_LAMBDA( Delegate<void()>, [&valA]()
     {
        valA -= 5;
    } );
    cmd1._redo();
    stack.push( std::move( cmd1 ) );

    CommandStack::Command cmd2;
    cmd2._label = "op2";
    cmd2._redo  = SW_DELEGATE_LAMBDA( Delegate<void()>, [&valB]()
     {
        valB *= 2;
    } );
    cmd2._undo  = SW_DELEGATE_LAMBDA( Delegate<void()>, [&valB]()
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
    cmd3._redo  = SW_DELEGATE_LAMBDA( Delegate<void()>, [&valA]()
     {
        valA += 100;
    } );
    cmd3._undo  = SW_DELEGATE_LAMBDA( Delegate<void()>, [&valA]()
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
 * @brief [EditorCommandStackTest] 슬라이더/드래그 연속 액션 병합 (pushCoalesce) 검증
 */
SW_TEST_CASE( EditorCommandStackTest, PushCoalesce )
{
    CommandStack stack;
    float32      sliderValue{ 0.0f };

    // 슬라이더를 0.0 -> 1.0 -> 2.5 -> 5.0 으로 드래그했을 때
    const float32 initialVal            = 0.0f;
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
    SW_EXPECT_NEAR_EQUAL( 5.0f, sliderValue, 0.0001f );
    SW_EXPECT_TRUE( stack.canUndo() );

    // 1번의 Undo로 초기값 0.0f 로 복원
    stack.undo();
    SW_EXPECT_NEAR_EQUAL( 0.0f, sliderValue, 0.0001f );
    SW_EXPECT_FALSE( stack.canUndo() );
    SW_EXPECT_TRUE( stack.canRedo() );

    // 1번의 Redo로 최종값 5.0f 로 복원
    stack.redo();
    SW_EXPECT_NEAR_EQUAL( 5.0f, sliderValue, 0.0001f );
}

/**
 * @brief [EditorCommandStackTest] 히스토리 검사 및 특정 시점 다단계 점프 (jumpTo) 검증
 */
SW_TEST_CASE( EditorCommandStackTest, JumpToAndHistoryInspection )
{
    CommandStack stack;
    int32        value{ 0 };

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
 * @brief [EditorCommandStackTest] GameObject 바이너리 스냅샷 기반 다단계 Undo/Redo 트랜잭션 스트레스 검증
 */
SW_TEST_CASE( EditorCommandStackTest, GameObjectBinarySnapshotUndoRedoTransactions )
{
    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "TransactionHero" ) );
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
 * @brief [EditorCommandStackTest] PIE 스냅샷 및 계층 구조 대규모 변이 복원 스트레스 검증
 */
SW_TEST_CASE( EditorCommandStackTest, EditorPlaySessionBinaryHierarchySnapshotStress )
{
    GameObjectManager manager;

    // 1. 20개의 계층형 GameObject 생성 (Root -> Child)
    constexpr size_t kHierarchyCount = 10;
    struct ObjectSnapshotRecord
    {
        string        _name;
        vector<uint8> _bytes;
    };
    vector<ObjectSnapshotRecord> listRecord;
    listRecord.reserve( kHierarchyCount * 2 );

    for ( size_t rootIndex = 0; rootIndex < kHierarchyCount; ++rootIndex )
    {
        const string rootName = "Root_" + to_string( rootIndex );
        GameObject*  pRoot    = manager.createGameObject( hashed_string( rootName.c_str() ) );
        SW_ASSERT_NOT_NULL( pRoot );
        SceneComponent* pRootSc = pRoot->addComponent<SceneComponent>();
        SW_ASSERT_NOT_NULL( pRootSc );
        pRootSc->setLocalPosition( float3{ static_cast<float32>( rootIndex * 10 ), 0.0f, 0.0f } );

        const string childName = "Child_" + to_string( rootIndex );
        GameObject*  pChild    = manager.createGameObject( hashed_string( childName.c_str() ) );
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
        GameObject*  pRoot    = manager.findGameObjectByName( hashed_string( rootName.c_str() ) );
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
        string       parentName;
        const size_t bytesRead = ObjectStateSerializer::loadFromBinaryBuffer( pRestored, record._bytes.data(), record._bytes.size(), parentName );
        SW_EXPECT_EQUAL( record._bytes.size(), bytesRead );
    }

    // 4. 모든 20개 오브젝트가 원본 상태로 정확히 복원되었는지 검증
    for ( size_t rootIndex = 0; rootIndex < kHierarchyCount; ++rootIndex )
    {
        const string rootName = "Root_" + to_string( rootIndex );
        GameObject*  pRoot    = manager.findGameObjectByName( hashed_string( rootName.c_str() ) );
        SW_ASSERT_NOT_NULL( pRoot );
        SceneComponent* pRootSc = pRoot->getPrimarySceneComponent();
        SW_ASSERT_NOT_NULL( pRootSc );
        SW_EXPECT_NEAR_EQUAL( static_cast<float32>( rootIndex * 10 ), pRootSc->getLocalPosition()._x, 0.001f );

        const string childName = "Child_" + to_string( rootIndex );
        GameObject*  pChild    = manager.findGameObjectByName( hashed_string( childName.c_str() ) );
        SW_ASSERT_NOT_NULL( pChild );
        SceneComponent* pChildSc = pChild->getPrimarySceneComponent();
        SW_ASSERT_NOT_NULL( pChildSc );
        SW_EXPECT_NEAR_EQUAL( 1.0f, pChildSc->getLocalPosition()._x, 0.001f );
        SW_EXPECT_NEAR_EQUAL( 2.0f, pChildSc->getLocalPosition()._y, 0.001f );
        SW_EXPECT_NEAR_EQUAL( 3.0f, pChildSc->getLocalPosition()._z, 0.001f );
    }
}

/**
 * @brief [EditorCommandStackTest] 컴포넌트 클립보드 바이너리 복사/붙여넣기 정밀도 검증
 */
SW_TEST_CASE( EditorCommandStackTest, ComponentBinaryClipboardValuePasting )
{
    GameObjectManager manager;
    GameObject*       pSourceObj = manager.createGameObject( hashed_string( "ClipboardSource" ) );
    SW_ASSERT_NOT_NULL( pSourceObj );
    SceneComponent* pSourceSc = pSourceObj->addComponent<SceneComponent>();
    SW_ASSERT_NOT_NULL( pSourceSc );
    pSourceSc->setLocalPosition( float3{ 12.5f, -34.0f, 56.75f } );
    pSourceSc->setLocalRotation( float3{ 10.0f, 20.0f, 30.0f } );
    pSourceSc->setLocalScale( float3{ 2.0f, 2.0f, 2.0f } );

    // 클립보드에 바이너리 직렬화
    vector<uint8>   clipBytes;
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

/**
 * @brief [EditorCommandStackTest] Undo/Redo 도중 push 재진입 가드 엣지 케이스 검증
 */
SW_TEST_CASE( EditorCommandStackTest, ReentrancyPushGuardDuringUndoRedo )
{
    CommandStack stack;
    int32        val = 0;

    CommandStack::Command cmd1{};
    cmd1._label = "Cmd1";
    cmd1._undo  = [&]()
    {
        val = 0;
        // 재진입 push 시도
        CommandStack::Command reentrantCmd{};
        reentrantCmd._label = "Reentrant";
        reentrantCmd._undo  = []() {};
        reentrantCmd._redo  = []() {};
        stack.push( std::move( reentrantCmd ) );
    };
    cmd1._redo = [&]()
    { val = 1; };

    stack.push( std::move( cmd1 ) );
    SW_EXPECT_EQUAL( 1u, stack.getCommandCount() );
    SW_EXPECT_TRUE( stack.canUndo() );

    // Undo 실행 -> 콜백 내부에서 push 시도해도 스택 인덱스와 크기가 오염되지 않아야 함
    stack.undo();
    SW_EXPECT_EQUAL( 0, val );
    SW_EXPECT_EQUAL( 1u, stack.getCommandCount() );
    SW_EXPECT_TRUE( stack.canRedo() );

    stack.redo();
    SW_EXPECT_EQUAL( 1, val );
}

/**
 * @brief [EditorCommandStackTest] undo 실행 중의 pushCoalesce 가 지난 명령을 덮어쓰지 않는다
 * @details `_bIsExecuting` 은 undo/redo 콜백이 자기 자신을 새 명령으로 기록하지 못하게 막는
 *          재진입 방지다. `push` 는 그것을 보는데 `pushCoalesce` 는 보지 않았다. 그래서
 *          undo 콜백 안에서 병합 push 를 하면 `push` 는 거절당하는데 **coalesce 키는 그대로
 *          기록되어**, 그 다음의 정상적인 병합 push 가 같은 키를 보고 `_index - 1` 의 명령 —
 *          즉 **아무 상관 없는 지난 명령** — 의 redo 를 갈아치웠다. 되돌린 뒤 다시 실행하면
 *          다른 일이 일어난다.
 */
SW_TEST_CASE( EditorCommandStackTest, CoalesceDuringUndoDoesNotRewriteHistory )
{
    CommandStack stack;
    int32        firstValue{ 0 };
    int32        secondValue{ 0 };

    // 1) 평범한 명령 하나 — 이것이 나중에 덮어써지는 피해자다.
    {
        CommandStack::Command cmd;
        cmd._label = "SetFirst";
        cmd._redo  = SW_DELEGATE_LAMBDA( Delegate<void()>, [&firstValue]()
         {
            firstValue = 100;
        } );
        cmd._undo  = SW_DELEGATE_LAMBDA( Delegate<void()>, [&firstValue]()
         {
            firstValue = 0;
        } );
        cmd._redo();
        stack.push( std::move( cmd ) );
    }
    SW_ASSERT_EQUAL( 100, firstValue );

    // 2) undo 콜백 안에서 병합 push 를 시도하는 명령. 스택은 그 push 를 받아들이면 안 된다.
    {
        CommandStack::Command cmd;
        cmd._label = "Reentrant";
        cmd._redo  = SW_DELEGATE_LAMBDA( Delegate<void()>, [&secondValue]()
         {
            secondValue = 1;
        } );
        cmd._undo  = SW_DELEGATE_LAMBDA( Delegate<void()>, [&stack, &secondValue]()
         {
            secondValue = 0;

            CommandStack::Command inner;
            inner._label = "InnerDuringUndo";
            inner._redo  = SW_DELEGATE_LAMBDA( Delegate<void()>, []() {} );
            inner._undo  = SW_DELEGATE_LAMBDA( Delegate<void()>, []() {} );
            stack.pushCoalesce( "SharedKey", std::move( inner ) );
        } );
        cmd._redo();
        stack.push( std::move( cmd ) );
    }
    SW_ASSERT_EQUAL( size_t( 2 ), stack.getCommandCount() );

    // 3) 되돌린다 — 콜백 안의 병합 push 는 거절되어야 하고, 기록도 남기면 안 된다.
    stack.undo();
    SW_EXPECT_EQUAL( 0, secondValue );
    SW_EXPECT_EQUAL( size_t( 2 ), stack.getCommandCount() );

    // 4) 이제 같은 키로 정상적인 병합 push 를 한다. 이것은 **새 명령**이어야 한다 —
    //    지난 "SetFirst" 를 덮어쓰면 안 된다.
    int32 thirdValue{ 0 };
    {
        CommandStack::Command cmd;
        cmd._label = "SetThird";
        cmd._redo  = SW_DELEGATE_LAMBDA( Delegate<void()>, [&thirdValue]()
         {
            thirdValue = 7;
        } );
        cmd._undo  = SW_DELEGATE_LAMBDA( Delegate<void()>, [&thirdValue]()
         {
            thirdValue = 0;
        } );
        cmd._redo();
        stack.pushCoalesce( "SharedKey", std::move( cmd ) );
    }
    SW_ASSERT_EQUAL( 7, thirdValue );

    // "SetFirst" 는 그대로 살아 있어야 한다. 덮어써졌다면 두 번 되돌렸을 때 100 이 남는다.
    stack.undo(); // SetThird 취소
    SW_EXPECT_EQUAL( 0, thirdValue );
    SW_EXPECT_EQUAL( 100, firstValue );

    stack.undo(); // SetFirst 취소
    SW_EXPECT_EQUAL( 0, firstValue );
}

/**
 * @brief [EditorCommandStackTest] undo 콜백 안에서 jumpTo 를 불러도 멈추지 않는다
 * @details `push` · `pushCoalesce` · `undo` · `redo` 는 모두 재진입 깃발(`_bIsExecuting`)을 보는데
 *          `jumpTo` 만 보지 않았다. 콜백 안에서 부르면 안쪽 `undo()` 가 그 깃발 때문에 아무것도
 *          하지 않고 돌아오고, `_index` 가 줄지 않으므로 `while` 이 영원히 돈다 — 틀린 값이 아니라
 *          **멈춘 에디터**다. 이 케이스가 회귀하면 CTest 타임아웃까지 붙잡힌다.
 */
SW_TEST_CASE( EditorCommandStackTest, JumpToInsideUndoCallbackDoesNotSpin )
{
    sw::CommandStack stack;

    int32 value = 0;
    for ( int32 step = 1; step <= 3; ++step )
    {
        sw::CommandStack::Command cmd;
        cmd._label = "Step";
        cmd._redo  = SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [&value, step]()
         {
            value = step;
        } );
        cmd._undo  = SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [&value, step]()
         {
            value = step - 1;
        } );
        stack.push( std::move( cmd ) );
    }
    SW_ASSERT_EQUAL( size_t( 3 ), stack.getCurrentIndex() );

    // 마지막 명령의 undo 가 다시 jumpTo 를 부른다 — 재진입이다.
    sw::CommandStack::Command reentrant;
    reentrant._label = "Reentrant";
    reentrant._redo  = SW_DELEGATE_LAMBDA( sw::Delegate<void()>, []() {} );
    reentrant._undo  = SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [&stack]()
     {
        stack.jumpTo( 0 );
    } );
    stack.push( std::move( reentrant ) );
    SW_ASSERT_EQUAL( size_t( 4 ), stack.getCurrentIndex() );

    stack.undo(); // 안에서 jumpTo(0) 이 불린다 — 돌아와야 한다.
    SW_EXPECT_EQUAL( size_t( 3 ), stack.getCurrentIndex() );
}

/**
 * @brief [EditorCommandStackTest] 명령이 하나뿐인 트랜잭션도 트랜잭션 레이블을 쓴다
 * @details 여러 개일 때는 트랜잭션 레이블을 쓰면서 하나일 때만 안쪽 명령의 레이블을 그대로 썼다 —
 *          "Move 3 objects" 로 묶었는데 실제 명령이 하나면 실행 취소 메뉴에 "Set position" 이 떴다.
 */
SW_TEST_CASE( EditorCommandStackTest, SingleCommandTransactionKeepsTheTransactionLabel )
{
    sw::CommandStack stack;

    stack.beginTransaction( "Move 3 objects" );
    {
        sw::CommandStack::Command cmd;
        cmd._label = "Set position";
        cmd._redo  = SW_DELEGATE_LAMBDA( sw::Delegate<void()>, []() {} );
        cmd._undo  = SW_DELEGATE_LAMBDA( sw::Delegate<void()>, []() {} );
        stack.push( std::move( cmd ) );
    }
    stack.endTransaction();

    SW_ASSERT_EQUAL( size_t( 1 ), stack.getCommandCount() );
    SW_EXPECT_STREQ( "Move 3 objects", stack.peekUndoLabel().c_str() );
}

/**
 * @brief [EditorCommandStackTest] releaseCodeWithin 은 트랜잭션에 싸인 명령의 코드도 보고, 들어 있으면 스택을 통째로 비운다
 * @details 트랜잭션은 안쪽 명령을 엔진 쪽 람다 하나로 싸서, 쌓인 명령의 undo · redo 만 보면 안쪽이 보이지 않는다. 에디터 Undo 는 거의
 *          전부 트랜잭션이라, 들어올 때 코드 주소를 적어 두지 않으면 핫 리로드 뒤 Ctrl+Z 가 내려간 이미지로 뛴다. 코드가 없는 범위는
 *          스택을 건드리지 않는다.
 */
SW_TEST_CASE( EditorCommandStackTest, ReleaseCodeWithinSeesCommandsInsideATransaction )
{
    CommandStack stack;
    int32        value{ 0 };

    CommandStack::Command first;
    first._label = "first";
    first._redo  = SW_DELEGATE_LAMBDA( Delegate<void()>, [&value]()
     { value += 1; } );
    first._undo  = SW_DELEGATE_LAMBDA( Delegate<void()>, [&value]()
     { value -= 1; } );
    CommandStack::Command second;
    second._label            = "second";
    second._redo             = SW_DELEGATE_LAMBDA( Delegate<void()>, [&value]()
                { value += 10; } );
    second._undo             = SW_DELEGATE_LAMBDA( Delegate<void()>, [&value]()
                { value -= 10; } );
    const uint8* pSecondUndo = static_cast<const uint8*>( second._undo.getCodeAddress() );

    stack.beginTransaction( "pair" );
    stack.push( std::move( first ) );
    stack.push( std::move( second ) );
    stack.endTransaction();
    SW_ASSERT_EQUAL( size_t{ 1 }, stack.getCommandCount() );
    // 쌓인 것은 묶은 명령 하나이고, 그 undo 는 엔진 람다다 — 안쪽 스텁이 거기 보이지 않는다는 것이 이 테스트의 전제다.
    SW_EXPECT_FALSE( stack.getCommand( 0 )._undo.isCodeWithin( pSecondUndo, pSecondUndo + 1 ) );

    SW_EXPECT_EQUAL( 0u, stack.releaseCodeWithin( &value, &value + 1 ) );
    SW_EXPECT_TRUE( stack.canUndo() );

    SW_EXPECT_EQUAL( 1u, stack.releaseCodeWithin( pSecondUndo, pSecondUndo + 1 ) );
    SW_EXPECT_FALSE( stack.canUndo() );
    SW_EXPECT_EQUAL( size_t{ 0 }, stack.getCommandCount() );
    // 비운 뒤에는 적어 둔 주소도 없다.
    SW_EXPECT_EQUAL( 0u, stack.releaseCodeWithin( pSecondUndo, pSecondUndo + 1 ) );
}
