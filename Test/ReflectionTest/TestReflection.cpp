#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Reflection/ReflectAny.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/JSONSerializer.h"

#include "ReflectionTest/TestReflectionFixtures.h"
#include "ReflectionTest/TestSampleActor.h"

#include "TestFramework/TestFramework.h"

// 리플렉션 나머지 — 컴포넌트 생명주기 · FUNCTION 인보크 · 제네릭 질의 · 바인딩 · 캐스트 · ReflectAny.
// 덩치가 큰 주제는 따로 있다: 직렬화 · 파서 · TypeInfo · 열거형 · 배치.

struct TestBindingActor
{
    uint32 _score{ 0 };
};

/**
 * @brief [ReflectionBindingTest] 양방향 프로퍼티 바인딩
 */
SW_TEST_CASE( ReflectionBindingTest, BiDirectionalPropertyBinding )
{
    sw::PropertyInfo prop( sw::hashed_string( "score" ), sw::hashed_string( "uint32" ), SW_OFFSET_OF( TestBindingActor, _score ) );

    bool bCalled{ false };
    prop.bindOnChanged( SW_DELEGATE_LAMBDA( sw::PropertyInfo::PropertyBindingDelegate, [&bCalled]( const sw::PropertyInfo& p, const sw::ObjectAddress inst )
    {
        (void)p;
        (void)inst;
        bCalled = true;
    } ) );

    TestBindingActor actor{};
    prop.setValue( &actor, 500u );
    SW_EXPECT_EQUAL( 500u, actor._score );
    SW_EXPECT_TRUE( bCalled );
}

/**
 * @brief [ReflectionFunctionMacroTest] 어노테이션 메서드 호출
 */
SW_TEST_CASE( ReflectionFunctionMacroTest, AnnotatedMethodInvoke )
{
    // 수동 연결 invoker 경로.
    REFLECT()
    struct FunctionAnnotatedActor
    {
        PROPERTY()
        int32 _health = 100;

        FUNCTION()
        void takeDamage( int32 damage )
        {
            _health -= damage;
        }
    } actor;

    sw::FunctionInfo funcInfo;
    funcInfo._name           = "takeDamage";
    funcInfo._hashName       = sw::hashed_string( "takeDamage" );
    funcInfo._returnTypeName = "void";
    funcInfo._listParameter.push_back( sw::FunctionParameterInfo( "damage", "sw::int32", "", nullptr ) );
    funcInfo._invoker = SW_DELEGATE_LAMBDA( sw::Delegate<sw::TaskValue( void*, const sw::TaskArgs& )>, []( void* pObjPtr, const sw::TaskArgs& args ) -> sw::TaskValue
    {
        static_cast<FunctionAnnotatedActor*>( pObjPtr )->takeDamage( args.get<int32>( 0 ) );
        return sw::TaskValue{};
    } );

    sw::TypeInfo info;
    info._name               = sw::hashed_string( "FunctionAnnotatedActor" );
    info._fullyQualifiedName = sw::hashed_string( "sw::FunctionAnnotatedActor" );
    info._size               = sizeof( FunctionAnnotatedActor );
    info._listMethod.push_back( funcInfo );

    sw::engine::getSubsystem<sw::TypeRegistry>().registerClass( info );

    sw::TaskArgs args;
    args.add( int32{ 35 } );
    sw::engine::getSubsystem<sw::TypeRegistry>().invokeMethod( &actor, sw::hashed_string( "sw::FunctionAnnotatedActor" ), sw::hashed_string( "takeDamage" ), args );

    SW_EXPECT_EQUAL( 65, actor._health );
}

/**
 * @brief [ReflectionFunctionMacroTest] 코드젠 메서드 호출
 */
SW_TEST_CASE( ReflectionFunctionMacroTest, CodegenMethodInvoke )
{
    // SampleTestActor::takeDamage / getHp 는 ReflectionParser 코드젠이 출력한다.
    sw::SampleTestActor actor;
    SW_ASSERT_NOT_NULL( sw::engine::getSubsystem<sw::TypeRegistry>().findType( sw::hashed_string( "sw::SampleTestActor" ) ) );

    const sw::TypeInfo* typeInfo = sw::engine::getSubsystem<sw::TypeRegistry>().findType( sw::hashed_string( "sw::SampleTestActor" ) );
    SW_ASSERT_NOT_NULL( typeInfo );
    const sw::PropertyInfo* nameProp = typeInfo->findProperty( sw::hashed_string( "_name" ) );
    SW_ASSERT_NOT_NULL( nameProp );
    SW_EXPECT_EQUAL( sw::string( "string" ), sw::string( nameProp->_typeName.c_str() ) );
    const sw::FunctionInfo* takeDamageFn = typeInfo->findMethod( sw::hashed_string( "takeDamage" ) );
    const sw::FunctionInfo* getHpFn      = typeInfo->findMethod( sw::hashed_string( "getHp" ) );
    SW_ASSERT_NOT_NULL( takeDamageFn );
    SW_ASSERT_NOT_NULL( getHpFn );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( takeDamageFn->_metadata->_bConst ) );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( getHpFn->_metadata->_bConst ) );

    sw::TaskArgs damageArgs;
    damageArgs.add( int32{ 40 } );
    sw::engine::getSubsystem<sw::TypeRegistry>().invokeMethod( &actor, sw::hashed_string( "sw::SampleTestActor" ),
                                                               sw::hashed_string( "takeDamage" ), damageArgs );
    SW_EXPECT_EQUAL( 60, actor._hp );

    sw::TaskValue hp = sw::engine::getSubsystem<sw::TypeRegistry>().invokeMethod( &actor, sw::hashed_string( "sw::SampleTestActor" ),
                                                                                  sw::hashed_string( "getHp" ) );
    SW_EXPECT_TRUE( hp.hasValue() );
    SW_EXPECT_EQUAL( 60, hp.getValue<int32>() );
}

/**
 * @brief [ReflectionComponentTest] Component beginPlay / tick / endPlay 동작 검증
 */
SW_TEST_CASE( ReflectionComponentTest, ComponentLifecycle )
{
    sw::GameObjectManager manager;

    sw::GameObject* obj1 = manager.createGameObject( sw::hashed_string( "TestObj1" ) );
    sw::GameObject* obj2 = manager.createGameObject( sw::hashed_string( "TestObj2" ) );

    sw::TestScriptComponent* pComp1 = obj1->addComponent<sw::TestScriptComponent>();
    sw::TestScriptComponent* pComp2 = obj2->addComponent<sw::TestScriptComponent>();
    SW_ASSERT_NOT_NULL( pComp1 );
    SW_ASSERT_NOT_NULL( pComp2 );

    manager.beginPlay();

    SW_EXPECT_EQUAL( obj1, pComp1->getOwner() );
    SW_EXPECT_EQUAL( obj2, pComp2->getOwner() );
    SW_EXPECT_TRUE( pComp1->_beganPlay );
    SW_EXPECT_TRUE( pComp2->_beganPlay );

    manager.update( 1.0f );
    manager.update( 1.0f );

    SW_EXPECT_EQUAL( 2, pComp1->_tickCount );
    SW_EXPECT_EQUAL( 2, pComp2->_tickCount );

    manager.endPlay();

    SW_EXPECT_TRUE( pComp1->_endedPlay );
    SW_EXPECT_TRUE( pComp2->_endedPlay );
}

/**
 * @brief [ReflectionComponentTest] Component 다중 상속 라이프사이클 및 틱 정상 동작 검증
 */
SW_TEST_CASE( ReflectionComponentTest, ComponentInheritanceMultiLevel )
{
    sw::GameObjectManager manager;

    sw::GameObject* baseObj       = manager.createGameObject( sw::hashed_string( "BaseObj" ) );
    sw::GameObject* derivedObj    = manager.createGameObject( sw::hashed_string( "DerivedObj" ) );
    sw::GameObject* grandChildObj = manager.createGameObject( sw::hashed_string( "GrandChildObj" ) );

    baseObj->addComponent<sw::TestScriptComponent>();
    derivedObj->addComponent<sw::TestDerivedScriptComponent>();
    grandChildObj->addComponent<sw::TestGrandChildScriptComponent>();

    manager.beginPlay();

    sw::TestScriptComponent*           baseComp       = baseObj->getComponent<sw::TestScriptComponent>();
    sw::TestDerivedScriptComponent*    derivedComp    = derivedObj->getComponent<sw::TestDerivedScriptComponent>();
    sw::TestGrandChildScriptComponent* grandChildComp = grandChildObj->getComponent<sw::TestGrandChildScriptComponent>();

    SW_ASSERT_NOT_NULL( baseComp );
    SW_ASSERT_NOT_NULL( derivedComp );
    SW_ASSERT_NOT_NULL( grandChildComp );

    SW_EXPECT_EQUAL( baseObj, baseComp->getOwner() );
    SW_EXPECT_EQUAL( derivedObj, derivedComp->getOwner() );
    SW_EXPECT_EQUAL( grandChildObj, grandChildComp->getOwner() );

    SW_EXPECT_TRUE( baseComp->_beganPlay );
    SW_EXPECT_TRUE( derivedComp->_beganPlay );
    SW_EXPECT_TRUE( grandChildComp->_beganPlay );

    manager.update( 0.016f );
    manager.update( 0.016f );

    baseComp       = baseObj->getComponent<sw::TestScriptComponent>();
    derivedComp    = derivedObj->getComponent<sw::TestDerivedScriptComponent>();
    grandChildComp = grandChildObj->getComponent<sw::TestGrandChildScriptComponent>();

    SW_EXPECT_EQUAL( 2, baseComp->_tickCount );

    SW_EXPECT_EQUAL( 2, derivedComp->_tickCount );
    SW_EXPECT_EQUAL( 4, derivedComp->_derivedTickCount );

    SW_EXPECT_EQUAL( 2, grandChildComp->_tickCount );
    SW_EXPECT_EQUAL( 4, grandChildComp->_derivedTickCount );
    SW_EXPECT_EQUAL( 6, grandChildComp->_grandChildTickCount );

    manager.endPlay();

    baseComp       = baseObj->getComponent<sw::TestScriptComponent>();
    derivedComp    = derivedObj->getComponent<sw::TestDerivedScriptComponent>();
    grandChildComp = grandChildObj->getComponent<sw::TestGrandChildScriptComponent>();

    SW_EXPECT_TRUE( baseComp->_endedPlay );
    SW_EXPECT_TRUE( derivedComp->_endedPlay );
    SW_EXPECT_TRUE( grandChildComp->_endedPlay );
}

/**
 * @brief [ReflectionComponentTest] Component에 선언된 PROPERTY가 TypeInfo에 반영되고 직렬화/역직렬화되는지 검증
 */
SW_TEST_CASE( ReflectionComponentTest, ComponentPropertySerialization )
{
    const sw::TypeInfo* pType = sw::engine::getSubsystem<sw::TypeRegistry>().findType( sw::hashed_string( "sw::TestScriptComponent" ) );
    SW_ASSERT_NOT_NULL( pType );

    const sw::PropertyInfo* pSpeedProp = pType->findProperty( sw::hashed_string( "_scriptSpeed" ) );
    SW_ASSERT_NOT_NULL( pSpeedProp );
    SW_EXPECT_TRUE( pSpeedProp->_typeName == sw::hashed_string( "float32" ) );

    sw::TestScriptComponent comp;
    SW_EXPECT_TRUE( sw::MathUtil::nearEqual( comp._scriptSpeed, 1.5f, 0.0001f ) );

    comp._scriptSpeed = 3.14f;

    pSpeedProp->setValue<float32>( &comp, 2.718f );
    SW_EXPECT_TRUE( sw::MathUtil::nearEqual( comp._scriptSpeed, 2.718f, 0.0001f ) );
}

/**
 * @brief [ReflectionComponentTest] 파일 상태 묶음에서 묶음에 없는 오브젝트를 가리키던 핸들은 없음이 되고, 옮기지 못하는 모양의 핸들은 로드 오류다 — 같은 실행 상태는 그대로 둔다
 * @details 묶음은 `GameObjectHandle` 을 어디에 들었든(단일 · 시퀀스 · set · 맵 키 · 값 · 중첩) 저장된 id 에서 이 실행의 오브젝트로 옮긴다. 원소가 컨테이너인 set 은
 *          제자리에서 고칠 수 없어 옮기지 못한다. 예전에는 비우고 경고했지만(값이 조용히 사라지는 폴백) 이제 `finish` 가 false 다.
 *          같은 실행의 상태(`ObjectIDSpace::Live`)는 런타임 id 그대로가 맞다. `ComponentHandle` 은 PROPERTY 에 둘 수 없다(파서 오류, docs/09_Decisions.md 5-6 타입 핸들 직렬화).
 */
SW_TEST_CASE( ReflectionComponentTest, LoadBatchFailsOnHandlesItCannotRemap )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pTarget = manager.createGameObject( sw::hashed_string( "HandleTarget" ) );
    sw::GameObject*       pHolder = manager.createGameObject( sw::hashed_string( "HandleHolder" ) );
    SW_ASSERT_TRUE( pTarget != nullptr && pHolder != nullptr );
    sw::TestHandleHolderComponent* pHolderComp = pHolder->addComponent<sw::TestHandleHolderComponent>();
    SW_ASSERT_NOT_NULL( pHolderComp );
    pHolderComp->_target          = pTarget->getHandle();
    pHolderComp->_listTarget      = { pTarget->getHandle() };
    pHolderComp->_mapSlotToTarget = {
        { 1, pTarget->getHandle() }
    };
    const sw::string state = sw::ObjectStateSerializer::saveToXMLString( pHolder, sw::ObjectSaveOptions{ sw::SerializePurpose::Authoring } );
    SW_ASSERT_TRUE( state.empty() == false );

    // 같은 실행 상태 — 묶음 밖의 오브젝트도 런타임 id 그대로다.
    {
        sw::GameObject* pLive = manager.createGameObject( sw::hashed_string( "LiveCopy" ) );
        SW_ASSERT_NOT_NULL( pLive );
        sw::ObjectStateBatch  batch( sw::ObjectIDSpace::Live );
        sw::ObjectLoadContext context{ sw::SerializePurpose::Snapshot };
        context._pBatch = &batch;
        SW_ASSERT_TRUE( sw::ObjectStateSerializer::loadFromXMLString( pLive, state, context ) );
        SW_EXPECT_TRUE( batch.finish() );
        const sw::TestHandleHolderComponent* pLoaded = pLive->getComponent<sw::TestHandleHolderComponent>();
        SW_ASSERT_NOT_NULL( pLoaded );
        SW_EXPECT_TRUE( pLoaded->_target == pTarget->getHandle() );
        SW_EXPECT_EQUAL( size_t( 1 ), pLoaded->_mapSlotToTarget.size() );
    }

    // 파일 상태 — 대상이 묶음에 없다. 옮기는 자리는 없음이 된다(파일 id 가 남아 다른 오브젝트를 가리키지 않는다).
    {
        sw::GameObject* pFresh = manager.createGameObject( sw::hashed_string( "FromFile" ) );
        SW_ASSERT_NOT_NULL( pFresh );
        sw::ObjectStateBatch  batch( sw::ObjectIDSpace::Saved );
        sw::ObjectLoadContext context{ sw::SerializePurpose::Snapshot };
        context._pBatch  = &batch;
        context._savedID = 900000001; // 런타임 id 와 겹치지 않는 파일 id
        SW_ASSERT_TRUE( sw::ObjectStateSerializer::loadFromXMLString( pFresh, state, context ) );
        SW_EXPECT_TRUE( batch.finish() );
        const sw::TestHandleHolderComponent* pFromFile = pFresh->getComponent<sw::TestHandleHolderComponent>();
        SW_ASSERT_NOT_NULL( pFromFile );
        SW_EXPECT_FALSE( pFromFile->_target.isValid() );
        SW_EXPECT_TRUE( pFromFile->_listTarget.size() == 1 && pFromFile->_listTarget[0].isValid() == false );
        SW_EXPECT_TRUE( pFromFile->_mapSlotToTarget.size() == 1 && pFromFile->_mapSlotToTarget.begin()->second.isValid() == false );
    }

    // 옮기지 못하는 모양 — 파일 상태면 로드 오류이고 필드 이름을 알린다. 값을 비우지 않는다.
    sw::GameObject* pRowHolder = manager.createGameObject( sw::hashed_string( "RowHolder" ) );
    SW_ASSERT_NOT_NULL( pRowHolder );
    sw::TestUnremappableHandleHolderComponent* pRowComp = pRowHolder->addComponent<sw::TestUnremappableHandleHolderComponent>();
    SW_ASSERT_NOT_NULL( pRowComp );
    pRowComp->_uniqueRow       = { { pTarget->getHandle() } };
    const sw::string rowState  = sw::ObjectStateSerializer::saveToXMLString( pRowHolder, sw::ObjectSaveOptions{ sw::SerializePurpose::Authoring } );
    sw::GameObject*  pRowFresh = manager.createGameObject( sw::hashed_string( "RowFromFile" ) );
    SW_ASSERT_NOT_NULL( pRowFresh );
    test::ScopedLogCollector logs;
    {
        SW_TEST_DEFENSIVE_SCOPE( "unremappable handle property" );
        sw::ObjectStateBatch  batch( sw::ObjectIDSpace::Saved );
        sw::ObjectLoadContext context{ sw::SerializePurpose::Snapshot };
        context._pBatch  = &batch;
        context._savedID = 900000002;
        SW_ASSERT_TRUE( sw::ObjectStateSerializer::loadFromXMLString( pRowFresh, rowState, context ) );
        SW_EXPECT_FALSE( batch.finish() );
        SW_EXPECT_FALSE( batch.finish() ); // 두 번째 호출도 같은 결과
    }
    const sw::TestUnremappableHandleHolderComponent* pRowLoaded = pRowFresh->getComponent<sw::TestUnremappableHandleHolderComponent>();
    SW_ASSERT_NOT_NULL( pRowLoaded );
    SW_EXPECT_EQUAL( size_t( 1 ), pRowLoaded->_uniqueRow.size() );
    SW_EXPECT_TRUE_MSG( logs.countWithID( "Object.PropertyCannotRemap" ) == 1, logs.joined().c_str() );
}

/**
 * @brief [ReflectionComponentTest] 파일 상태 묶음은 컨테이너 안의 핸들을 모두 이 실행의 오브젝트로 옮긴다 — 시퀀스 · set · 맵 값 · 맵 키 · 중첩, 세 형식 모두
 * @details 대상 오브젝트를 저장된 id 로 묶음에 넣고(`ObjectStateBatch::add`) 핸들을 든 오브젝트를 읽으면, 어느 자리의 핸들이든 새 대상을 가리켜야 한다.
 *          set 원소와 맵 키는 정렬 · 해시 자리라 제자리에서 고칠 수 없어 빼고 다시 넣는다. 구조체 필드 · 구조체 벡터 · 핸들 키 맵의 구조체 값 안의 핸들도 옮긴다(`ReflectedValueWalk`).
 */
SW_TEST_CASE( ReflectionComponentTest, LoadBatchRemapsHandlesInsideContainers )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pTarget = manager.createGameObject( sw::hashed_string( "RemapTarget" ) );
    sw::GameObject*       pHolder = manager.createGameObject( sw::hashed_string( "RemapHolder" ) );
    SW_ASSERT_TRUE( pTarget != nullptr && pHolder != nullptr );
    sw::TestHandleHolderComponent* pHolderComp = pHolder->addComponent<sw::TestHandleHolderComponent>();
    SW_ASSERT_NOT_NULL( pHolderComp );
    const sw::GameObjectHandle target = pTarget->getHandle();
    pHolderComp->_target              = target;
    pHolderComp->_listTarget          = { target };
    pHolderComp->_mapSlotToTarget     = {
        { 1, target }
    };
    pHolderComp->_uniqueTarget     = { target };
    pHolderComp->_mapTargetToScore = {
        { target, 77 }
    };
    pHolderComp->_listTargetRow = {
        { target, target }
    };
    pHolderComp->_mapGroupToTarget = {
        { "squad", { target } }
    };
    pHolderComp->_waypoint._order       = 1;
    pHolderComp->_waypoint._target      = target;
    pHolderComp->_waypoint._listWatcher = { target };
    sw::TestHandleWaypoint second;
    second._order                     = 2;
    second._target                    = target;
    pHolderComp->_listWaypoint        = { second };
    pHolderComp->_mapTargetToWaypoint = {
        { target, second }
    };

    sw::vector<uint8> binaryState;
    SW_ASSERT_TRUE( sw::ObjectStateSerializer::saveToBinaryBuffer( pHolder, binaryState, sw::ObjectSaveOptions{ sw::SerializePurpose::Snapshot } ) );
    const sw::string xmlState  = sw::ObjectStateSerializer::saveToXMLString( pHolder, sw::ObjectSaveOptions{ sw::SerializePurpose::Authoring } );
    const sw::string jsonState = sw::ObjectStateSerializer::saveToJSONString( pHolder, sw::ObjectSaveOptions{ sw::SerializePurpose::Authoring } );

    for ( uint32 formatIndex = 0; formatIndex < 3; ++formatIndex )
    {
        const utf8*     arrFormatName[] = { "xml", "json", "binary" };
        sw::GameObject* pNewTarget      = manager.createGameObject( sw::hashed_string( "RemapTargetCopy" ) );
        sw::GameObject* pNewHolder      = manager.createGameObject( sw::hashed_string( "RemapHolderCopy" ) );
        SW_ASSERT_TRUE( pNewTarget != nullptr && pNewHolder != nullptr );

        sw::ObjectStateBatch batch( sw::ObjectIDSpace::Saved );
        batch.add( pNewTarget, pTarget->getObjectID(), sw::hashed_string( "RemapTarget" ), false );
        sw::ObjectLoadContext context{ sw::SerializePurpose::Snapshot };
        context._pBatch  = &batch;
        context._savedID = 900000101 + formatIndex;
        bool bLoaded     = false;
        if ( formatIndex == 0 )
            bLoaded = sw::ObjectStateSerializer::loadFromXMLString( pNewHolder, xmlState, context );
        else if ( formatIndex == 1 )
            bLoaded = sw::ObjectStateSerializer::loadFromJSONString( pNewHolder, jsonState, context );
        else
            bLoaded = sw::ObjectStateSerializer::loadFromBinaryBuffer( pNewHolder, binaryState.data(), binaryState.size(), context ) != 0;
        SW_ASSERT_TRUE_MSG( bLoaded, arrFormatName[formatIndex] );
        SW_EXPECT_TRUE( batch.finish() );

        const sw::GameObjectHandle           moved   = pNewTarget->getHandle();
        const sw::TestHandleHolderComponent* pLoaded = pNewHolder->getComponent<sw::TestHandleHolderComponent>();
        SW_ASSERT_NOT_NULL( pLoaded );
        SW_EXPECT_TRUE_MSG( pLoaded->_target == moved, arrFormatName[formatIndex] );
        SW_EXPECT_TRUE_MSG( pLoaded->_listTarget.size() == 1 && pLoaded->_listTarget[0] == moved, arrFormatName[formatIndex] );
        SW_EXPECT_TRUE_MSG( pLoaded->_mapSlotToTarget.size() == 1 && pLoaded->_mapSlotToTarget.at( 1 ) == moved, arrFormatName[formatIndex] );
        SW_EXPECT_TRUE_MSG( pLoaded->_uniqueTarget.size() == 1 && *pLoaded->_uniqueTarget.begin() == moved, arrFormatName[formatIndex] );
        SW_EXPECT_TRUE_MSG( pLoaded->_mapTargetToScore.size() == 1 && pLoaded->_mapTargetToScore.begin()->first == moved &&
                                pLoaded->_mapTargetToScore.begin()->second == 77,
                            arrFormatName[formatIndex] );
        SW_EXPECT_TRUE_MSG( pLoaded->_listTargetRow.size() == 1 && pLoaded->_listTargetRow[0].size() == 2 && pLoaded->_listTargetRow[0][1] == moved,
                            arrFormatName[formatIndex] );
        SW_EXPECT_TRUE_MSG( pLoaded->_mapGroupToTarget.size() == 1 && pLoaded->_mapGroupToTarget.at( "squad" ).size() == 1 &&
                                pLoaded->_mapGroupToTarget.at( "squad" )[0] == moved,
                            arrFormatName[formatIndex] );
        // 구조체 필드 안 · 구조체 벡터 안 · 핸들 키 맵의 구조체 값 안 — 쓰기 처리기가 모든 깊이에서 저장 id 로 적으므로 읽기도 같은 깊이를 옮긴다.
        SW_EXPECT_TRUE_MSG( pLoaded->_waypoint._target == moved && pLoaded->_waypoint._order == 1, arrFormatName[formatIndex] );
        SW_EXPECT_TRUE_MSG( pLoaded->_waypoint._listWatcher.size() == 1 && pLoaded->_waypoint._listWatcher[0] == moved, arrFormatName[formatIndex] );
        SW_EXPECT_TRUE_MSG( pLoaded->_listWaypoint.size() == 1 && pLoaded->_listWaypoint[0]._target == moved, arrFormatName[formatIndex] );
        SW_EXPECT_TRUE_MSG( pLoaded->_mapTargetToWaypoint.size() == 1 && pLoaded->_mapTargetToWaypoint.begin()->first == moved &&
                                pLoaded->_mapTargetToWaypoint.begin()->second._target == moved && pLoaded->_mapTargetToWaypoint.begin()->second._order == 2,
                            arrFormatName[formatIndex] );
    }
}

namespace
{
    /** @brief 형제 컴포넌트를 가리키는 오브젝트입니다 — 프로브 둘(안정 키 `#0` · `#1`), 다른 타입 하나, 링크 하나. 링크는 두 번째 프로브와 [두 번째, 첫 번째] 목록을 가리킵니다. */
    struct ProbeObject
    {
        sw::GameObject*             _pObject{ nullptr };
        sw::TestProbeComponent*     _pFirstProbe{ nullptr };
        sw::TestProbeComponent*     _pSecondProbe{ nullptr };
        sw::TestProbeLinkComponent* _pLink{ nullptr };
    };

    ProbeObject makeProbeObject( sw::GameObjectManager& manager, const utf8* pName )
    {
        ProbeObject probe;
        probe._pObject      = manager.createGameObject( sw::hashed_string( pName ) );
        probe._pFirstProbe  = probe._pObject->addComponent<sw::TestProbeComponent>();
        probe._pSecondProbe = probe._pObject->addComponent<sw::TestProbeComponent>();
        (void)probe._pObject->addComponent<sw::TestOtherComponent>();
        probe._pLink            = probe._pObject->addComponent<sw::TestProbeLinkComponent>();
        const bool bProbesReady = probe._pFirstProbe != nullptr && probe._pSecondProbe != nullptr;
        if ( bProbesReady && probe._pLink != nullptr )
        {
            probe._pFirstProbe->_value  = 1;
            probe._pSecondProbe->_value = 2;
            probe._pLink->_probe        = sw::TComponentHandle<sw::TestProbeComponent>{ probe._pSecondProbe };
            probe._pLink->_listProbe    = { sw::TComponentHandle<sw::TestProbeComponent>{ probe._pSecondProbe }, sw::TComponentHandle<sw::TestProbeComponent>{ probe._pFirstProbe } };
        }
        return probe;
    }

    /** @brief @p text 의 @p from 첫 자리를 @p to 로 바꾼 글입니다. 없으면 빈 글입니다(테스트가 단언한다). */
    sw::string replaceFirstText( const sw::string& text, const sw::string& from, const sw::string& to )
    {
        const size_t pos = text.find( from );
        if ( pos == sw::string::npos )
            return {};
        sw::string edited = text;
        edited.replace( pos, from.size(), to );
        return edited;
    }

    /** @brief 파일 형태로 저장합니다 — 오브젝트의 저장 id 표(씬 파일과 같다)를 줍니다. */
    sw::ObjectSaveOptions makeFileOptions( const sw::ObjectSavedIDMap& savedIDs )
    {
        sw::ObjectSaveOptions options{ sw::SerializePurpose::Authoring };
        options._pSavedIDMap = &savedIDs;
        return options;
    }
} // namespace

/**
 * @brief [TypedComponentHandleTest] 같은 오브젝트의 형제 컴포넌트를 가리키는 타입 핸들이 파일 왕복(XML · JSON · 바이너리)에서 새 오브젝트의 형제를 가리킨다
 * @details 파일 상태에는 컴포넌트 자신의 id 가 없다. 쓸 때 `{저장 id, 안정 키}` 로 적고(같은 오브젝트면 저장 id 자리가 0), 읽은 뒤 묶음이 키로 새 컴포넌트를 찾는다.
 *          런타임 컴포넌트 id 를 그대로 적으면 읽는 실행에서 같은 값을 받은 다른 컴포넌트를 가리킨다 — 이 테스트의 "원래 오브젝트를 가리키지 않는다" 단언이 그것이다.
 */
SW_TEST_CASE( TypedComponentHandleTest, SiblingHandlesSurviveAFileRoundTrip )
{
    sw::GameObjectManager manager;
    const ProbeObject     source = makeProbeObject( manager, "ProbeSource" );
    SW_ASSERT_TRUE( source._pObject != nullptr && source._pFirstProbe != nullptr && source._pSecondProbe != nullptr && source._pLink != nullptr );

    const sw::ObjectSavedIDMap savedIDs{
        { source._pObject->getObjectID(), 900002001 }
    };
    const sw::ObjectSaveOptions options = makeFileOptions( savedIDs );
    sw::vector<uint8>           binaryState;
    SW_ASSERT_TRUE( sw::ObjectStateSerializer::saveToBinaryBuffer( source._pObject, binaryState, options ) );
    const sw::string xmlState  = sw::ObjectStateSerializer::saveToXMLString( source._pObject, options );
    const sw::string jsonState = sw::ObjectStateSerializer::saveToJSONString( source._pObject, options );
    SW_EXPECT_TRUE_MSG( xmlState.find( "TestProbeComponent#1" ) != sw::string::npos, xmlState.c_str() ); // 사람이 읽는 키로 적힌다

    for ( uint32 formatIndex = 0; formatIndex < 3; ++formatIndex )
    {
        const utf8*     arrFormatName[] = { "xml", "json", "binary" };
        sw::GameObject* pFresh          = manager.createGameObject( sw::hashed_string( "ProbeFresh" ) );
        SW_ASSERT_NOT_NULL( pFresh );

        sw::ObjectStateBatch  batch( sw::ObjectIDSpace::Saved );
        sw::ObjectLoadContext context{ sw::SerializePurpose::Authoring };
        context._pBatch  = &batch;
        context._savedID = 900002001;
        bool bLoaded     = false;
        if ( formatIndex == 0 )
            bLoaded = sw::ObjectStateSerializer::loadFromXMLString( pFresh, xmlState, context );
        else if ( formatIndex == 1 )
            bLoaded = sw::ObjectStateSerializer::loadFromJSONString( pFresh, jsonState, context );
        else
            bLoaded = sw::ObjectStateSerializer::loadFromBinaryBuffer( pFresh, binaryState.data(), binaryState.size(), context ) != 0;
        SW_ASSERT_TRUE_MSG( bLoaded, arrFormatName[formatIndex] );
        SW_EXPECT_TRUE_MSG( batch.finish(), arrFormatName[formatIndex] );

        const sw::TestProbeLinkComponent* pLoadedLink = pFresh->getComponent<sw::TestProbeLinkComponent>();
        SW_ASSERT_NOT_NULL( pLoadedLink );
        const sw::TestProbeComponent* pProbe = manager.resolveComponent( pLoadedLink->_probe );
        SW_ASSERT_TRUE_MSG( pProbe != nullptr, arrFormatName[formatIndex] );
        SW_EXPECT_TRUE_MSG( pProbe->getOwner() == pFresh && pProbe->_value == 2, arrFormatName[formatIndex] ); // 원래 오브젝트의 컴포넌트가 아니다
        SW_ASSERT_EQUAL( size_t( 2 ), pLoadedLink->_listProbe.size() );
        const sw::TestProbeComponent* pListFirst  = manager.resolveComponent( pLoadedLink->_listProbe[0] );
        const sw::TestProbeComponent* pListSecond = manager.resolveComponent( pLoadedLink->_listProbe[1] );
        SW_ASSERT_TRUE_MSG( pListFirst != nullptr && pListSecond != nullptr, arrFormatName[formatIndex] );
        SW_EXPECT_TRUE_MSG( pListFirst == pProbe && pListFirst->getOwner() == pFresh, arrFormatName[formatIndex] );
        SW_EXPECT_TRUE_MSG( pListSecond->getOwner() == pFresh && pListSecond->_value == 1, arrFormatName[formatIndex] );
    }
}

/**
 * @brief [TypedComponentHandleTest] 파일이 대상 타입이 아닌 컴포넌트를 가리키면 로드 오류이고, 사라진 컴포넌트를 가리키면 핸들만 비운다
 * @details 같은 키(`타입#n`)라도 타입이 다르면 다른 컴포넌트다. 대상 타입은 필드가 알고(`PropertyInfo::_handleTargetTypeName`) 묶음이 끝날 때 확인한다.
 *          대상이 사라진 핸들은 약한 참조의 정상 값이라 비우되 경고를 남긴다.
 */
SW_TEST_CASE( TypedComponentHandleTest, WrongTypeFailsTheLoadAndMissingTargetClearsTheHandle )
{
    sw::GameObjectManager manager;
    const ProbeObject     source = makeProbeObject( manager, "ProbeSource" );
    SW_ASSERT_TRUE( source._pObject != nullptr && source._pLink != nullptr );
    const sw::ObjectSavedIDMap savedIDs{
        { source._pObject->getObjectID(), 900002002 }
    };
    const sw::string xmlState = sw::ObjectStateSerializer::saveToXMLString( source._pObject, makeFileOptions( savedIDs ) );

    // 1) 첫 프로브 자리를 다른 타입(TestOtherComponent#0)으로 바꾼다 — 대상이 있는데 타입이 아니다.
    const sw::string wrongType = replaceFirstText( xmlState, "TestProbeComponent#1", "TestOtherComponent#0" );
    SW_ASSERT_TRUE( wrongType.empty() == false );
    {
        sw::GameObject* pFresh = manager.createGameObject( sw::hashed_string( "WrongTypeFresh" ) );
        SW_ASSERT_NOT_NULL( pFresh );
        test::ScopedLogCollector logs;
        SW_TEST_DEFENSIVE_SCOPE( "typed handle points at another type" );
        sw::ObjectStateBatch  batch( sw::ObjectIDSpace::Saved );
        sw::ObjectLoadContext context{ sw::SerializePurpose::Authoring };
        context._pBatch  = &batch;
        context._savedID = 900002002;
        SW_ASSERT_TRUE( sw::ObjectStateSerializer::loadFromXMLString( pFresh, wrongType, context ) );
        SW_EXPECT_FALSE( batch.finish() );
        SW_EXPECT_TRUE_MSG( logs.countWithID( "Object.HandleWrongTargetType" ) >= 1, logs.joined().c_str() );
    }

    // 2) 없는 키(#7)를 가리킨다 — 로드는 성공하고 그 핸들만 빈다. 같은 필드의 목록 원소는 따로 본다.
    const sw::string missingTarget = replaceFirstText( xmlState, "TestProbeComponent#1", "TestProbeComponent#7" );
    SW_ASSERT_TRUE( missingTarget.empty() == false );
    {
        sw::GameObject* pFresh = manager.createGameObject( sw::hashed_string( "MissingTargetFresh" ) );
        SW_ASSERT_NOT_NULL( pFresh );
        test::ScopedLogCollector logs;
        SW_TEST_DEFENSIVE_SCOPE( "typed handle points at a component that is gone and is cleared with a warning" ); // 비운 이유를 알리는 로그가 이 확인이 보려는 것이다
        sw::ObjectStateBatch  batch( sw::ObjectIDSpace::Saved );
        sw::ObjectLoadContext context{ sw::SerializePurpose::Authoring };
        context._pBatch  = &batch;
        context._savedID = 900002002;
        SW_ASSERT_TRUE( sw::ObjectStateSerializer::loadFromXMLString( pFresh, missingTarget, context ) );
        SW_EXPECT_TRUE( batch.finish() );
        const sw::TestProbeLinkComponent* pLoadedLink = pFresh->getComponent<sw::TestProbeLinkComponent>();
        SW_ASSERT_NOT_NULL( pLoadedLink );
        SW_EXPECT_FALSE( pLoadedLink->_probe.isValid() );
        SW_EXPECT_TRUE_MSG( logs.countWithID( "Object.HandleTargetGone" ) >= 1, logs.joined().c_str() );
    }
}

/**
 * @brief [TypedComponentHandleTest] 런타임 id 로 쓴 핸들은 같은 실행의 상태(Live)에서만 읽히고, 세이브는 어느 쪽 공간에서 읽어도 새 형제를 가리킨다
 * @details 스냅샷(되돌리기 · 복제)은 런타임 id 를 그대로 적고 Live 배치가 그대로 둔다. 파일 id 공간(Saved)으로 읽으면 같은 id 가 다른 컴포넌트일 수 있어 로드 오류다.
 *          세이브는 다른 실행에서 읽는 일이 있어 파일 형태로 쓰고, 같은 실행에서 복원(Live)해도 키 해시의 표지 비트로 형태를 알아본다.
 */
SW_TEST_CASE( TypedComponentHandleTest, RuntimeFormIsLiveOnlyAndSaveGameReadsInBothSpaces )
{
    sw::GameObjectManager manager;
    const ProbeObject     source = makeProbeObject( manager, "ProbeSource" );
    SW_ASSERT_TRUE( source._pObject != nullptr && source._pLink != nullptr );

    // 스냅샷 — 런타임 id 그대로. Live 로 읽으면 원래 오브젝트의 컴포넌트를 가리킨다.
    sw::vector<uint8> snapshotState;
    SW_ASSERT_TRUE( sw::ObjectStateSerializer::saveToBinaryBuffer( source._pObject, snapshotState, sw::ObjectSaveOptions{ sw::SerializePurpose::Snapshot } ) );
    {
        sw::GameObject* pLive = manager.createGameObject( sw::hashed_string( "SnapshotLive" ) );
        SW_ASSERT_NOT_NULL( pLive );
        sw::ObjectStateBatch  batch( sw::ObjectIDSpace::Live );
        sw::ObjectLoadContext context{ sw::SerializePurpose::Snapshot };
        context._pBatch = &batch;
        SW_ASSERT_TRUE( sw::ObjectStateSerializer::loadFromBinaryBuffer( pLive, snapshotState.data(), snapshotState.size(), context ) != 0 );
        SW_EXPECT_TRUE( batch.finish() );
        const sw::TestProbeLinkComponent* pLoadedLink = pLive->getComponent<sw::TestProbeLinkComponent>();
        SW_ASSERT_NOT_NULL( pLoadedLink );
        SW_EXPECT_TRUE( pLoadedLink->_probe == sw::TComponentHandle<sw::TestProbeComponent>{ source._pSecondProbe } );
    }
    // 같은 데이터를 파일 id 공간으로 읽으면 로드 오류다 — 비우고 조용히 넘기지 않는다.
    {
        sw::GameObject* pFresh = manager.createGameObject( sw::hashed_string( "SnapshotAsFile" ) );
        SW_ASSERT_NOT_NULL( pFresh );
        test::ScopedLogCollector logs;
        SW_TEST_DEFENSIVE_SCOPE( "runtime component id read as a saved file" );
        sw::ObjectStateBatch  batch( sw::ObjectIDSpace::Saved );
        sw::ObjectLoadContext context{ sw::SerializePurpose::Snapshot };
        context._pBatch  = &batch;
        context._savedID = 900002003;
        SW_ASSERT_TRUE( sw::ObjectStateSerializer::loadFromBinaryBuffer( pFresh, snapshotState.data(), snapshotState.size(), context ) != 0 );
        SW_EXPECT_FALSE( batch.finish() );
        SW_EXPECT_TRUE_MSG( logs.countWithID( "Object.HandleHoldsRuntimeID" ) >= 1, logs.joined().c_str() );
    }

    // 세이브 — 파일 형태. Live · Saved 어느 공간에서 읽어도 새 오브젝트의 형제다.
    sw::vector<uint8> saveGameState;
    SW_ASSERT_TRUE( sw::ObjectStateSerializer::saveToBinaryBuffer( source._pObject, saveGameState, sw::ObjectSaveOptions{ sw::SerializePurpose::SaveGame } ) );
    for ( uint32 spaceIndex = 0; spaceIndex < 2; ++spaceIndex )
    {
        sw::GameObject* pFresh = manager.createGameObject( sw::hashed_string( "SaveGameFresh" ) );
        SW_ASSERT_NOT_NULL( pFresh );
        sw::ObjectStateBatch  batch( spaceIndex == 0 ? sw::ObjectIDSpace::Live : sw::ObjectIDSpace::Saved );
        sw::ObjectLoadContext context{ sw::SerializePurpose::SaveGame };
        context._pBatch  = &batch;
        context._savedID = source._pObject->getObjectID(); // 세이브가 적은 그때의 오브젝트 id
        SW_ASSERT_TRUE( sw::ObjectStateSerializer::loadFromBinaryBuffer( pFresh, saveGameState.data(), saveGameState.size(), context ) != 0 );
        SW_EXPECT_TRUE( batch.finish() );
        const sw::TestProbeLinkComponent* pLoadedLink = pFresh->getComponent<sw::TestProbeLinkComponent>();
        SW_ASSERT_NOT_NULL( pLoadedLink );
        const sw::TestProbeComponent* pProbe = manager.resolveComponent( pLoadedLink->_probe );
        SW_ASSERT_TRUE_MSG( pProbe != nullptr, spaceIndex == 0 ? "live" : "saved" );
        SW_EXPECT_TRUE_MSG( pProbe->getOwner() == pFresh && pProbe->_value == 2, spaceIndex == 0 ? "live" : "saved" );
    }
}

/**
 * @brief [ReflectionGenericQueryTest] TypeRegistry 템플릿 조회 및 isA 헬퍼 검증
 */
SW_TEST_CASE( ReflectionGenericQueryTest, FindTypeAndIsA )
{
    const sw::TypeInfo* pType = sw::engine::getSubsystem<sw::TypeRegistry>().findType<sw::TestDerivedScriptComponent>();
    SW_ASSERT_NOT_NULL( pType );
    SW_EXPECT_TRUE( pType->_name == sw::hashed_string( "TestDerivedScriptComponent" ) );

    sw::TestDerivedScriptComponent comp;
    SW_EXPECT_TRUE( sw::isA<sw::TestDerivedScriptComponent>( &comp ) );
    SW_EXPECT_TRUE( sw::isA<sw::TestScriptComponent>( &comp ) );
    SW_EXPECT_TRUE( sw::isA<sw::Component>( &comp ) );
    SW_EXPECT_TRUE( sw::isA<sw::DummyActor>( &comp ) == false );
}

/**
 * @brief [ReflectionGenericQueryTest] TypeRegistry forEachType 및 getDerivedTypes 검증
 */
SW_TEST_CASE( ReflectionGenericQueryTest, ForEachTypeAndDerivedTypes )
{
    uint32 typeCount{ 0 };
    sw::engine::getSubsystem<sw::TypeRegistry>().forEachType(
        [&typeCount]( const sw::TypeInfo& )
    {
        ++typeCount;
    } );
    SW_EXPECT_TRUE( typeCount > 0 );

    const auto derivedComponents = sw::engine::getSubsystem<sw::TypeRegistry>().getDerivedTypes<sw::TestScriptComponent>();
    SW_EXPECT_TRUE( derivedComponents.size() >= 2 ); // TestDerivedScriptComponent, TestGrandChildScriptComponent
}

/**
 * @brief [ReflectionGenericQueryTest] PropertyInfo getRawPtr 및 findPropertyInHierarchy 검증
 */
SW_TEST_CASE( ReflectionGenericQueryTest, HierarchyPropertyLookupAndRawPtr )
{
    const sw::TypeInfo* pGrandChildType = sw::engine::getSubsystem<sw::TypeRegistry>().findType<sw::TestGrandChildScriptComponent>();
    SW_ASSERT_NOT_NULL( pGrandChildType );

    // 직계 프로퍼티
    const sw::PropertyInfo* pDirectProp = pGrandChildType->findProperty( sw::hashed_string( "_grandChildSpeed" ) );
    SW_ASSERT_NOT_NULL( pDirectProp );

    // 부모 프로퍼티 (findPropertyInHierarchy)
    const sw::PropertyInfo* pInheritedProp = pGrandChildType->findPropertyInHierarchy( sw::hashed_string( "_scriptSpeed" ) );
    SW_ASSERT_NOT_NULL( pInheritedProp );

    sw::TestGrandChildScriptComponent comp;
    comp._scriptSpeed = 42.0f;

    const void* pRaw = pInheritedProp->getRawPtr( &comp );
    SW_ASSERT_NOT_NULL( pRaw );
    const float32 val = *reinterpret_cast<const float32*>( pRaw );
    SW_EXPECT_TRUE( sw::MathUtil::nearEqual( val, 42.0f, 0.0001f ) );
}

/**
 * @brief [ReflectionCastTest] ReflectionCast 헬퍼 (HasStaticType, castTo, isA) 다형 상속 계층 검증
 */
SW_TEST_CASE( ReflectionCastTest, TypeTraitsAndPolymorphicCast )
{
    // 1) 타입 트레이트 정적 검증
    SW_EXPECT_TRUE( sw::HasGetTypeInfo_v<sw::TestScriptComponent> );
    SW_EXPECT_TRUE( sw::HasOwnReflectBody_v<sw::TestScriptComponent> );
    SW_EXPECT_TRUE( sw::HasStaticType_v<sw::TestScriptComponent> );
    SW_EXPECT_TRUE( sw::HasStaticType_v<sw::TestDerivedScriptComponent> );
    SW_EXPECT_TRUE( sw::HasStaticType_v<sw::TestGrandChildScriptComponent> );

    // 2) castTo & isA (상속 계층 업캐스트, 동일 타입 캐스트, 무관한 타입 실패 검증)
    sw::TestGrandChildScriptComponent grandChild;
    grandChild._scriptSpeed     = 5.0f;
    grandChild._grandChildSpeed = 10.0f;

    // 동일 타입 캐스트
    sw::TestGrandChildScriptComponent* pSelf = sw::castTo<sw::TestGrandChildScriptComponent>( &grandChild );
    SW_ASSERT_NOT_NULL( pSelf );
    SW_EXPECT_EQUAL( 10.0f, pSelf->_grandChildSpeed );

    // 파생 -> 부모 상속 계층 업캐스트
    sw::TestDerivedScriptComponent* pDerived = sw::castTo<sw::TestDerivedScriptComponent>( &grandChild );
    SW_ASSERT_NOT_NULL( pDerived );
    SW_EXPECT_EQUAL( 5.0f, pDerived->_scriptSpeed );

    sw::TestScriptComponent* pBase = sw::castTo<sw::TestScriptComponent>( &grandChild );
    SW_ASSERT_NOT_NULL( pBase );
    SW_EXPECT_EQUAL( 5.0f, pBase->_scriptSpeed );

    // 무관한 타입 간 캐스트 실패 검증
    sw::SampleTestActor* pUnrelated = sw::castTo<sw::SampleTestActor>( &grandChild );
    SW_EXPECT_NULL( pUnrelated );

    // isA 검증
    SW_EXPECT_TRUE( sw::isA<sw::TestScriptComponent>( &grandChild ) );
    SW_EXPECT_TRUE( sw::isA<sw::TestDerivedScriptComponent>( &grandChild ) );
    SW_EXPECT_TRUE( sw::isA<sw::TestGrandChildScriptComponent>( &grandChild ) );
    SW_EXPECT_FALSE( sw::isA<sw::SampleTestActor>( &grandChild ) );

    // nullptr 안전성
    sw::TestGrandChildScriptComponent* pNull = nullptr;
    SW_EXPECT_NULL( sw::castTo<sw::TestDerivedScriptComponent>( pNull ) );
    SW_EXPECT_FALSE( sw::isA<sw::TestDerivedScriptComponent>( pNull ) );
}

/**
 * @brief [ReflectAnyTest] ReflectAny 직접 생성, 비어있음 검사, 값 추출 및 타입 불일치 실패 검증
 */
SW_TEST_CASE( ReflectAnyTest, ReflectAnyDirectMakeAndExtract )
{
    // 1) 빈 ReflectAny
    sw::ReflectAny emptyAny;
    SW_EXPECT_TRUE( emptyAny.empty() );

    // 2) PolyPayloadA TypeInfo 기반 ReflectAny 생성
    const sw::TypeInfo* pTypeInfo = sw::engine::getSubsystem<sw::TypeRegistry>().findType( sw::hashed_string( "sw::PolyPayloadA" ) );
    SW_ASSERT_NOT_NULL( pTypeInfo );

    sw::PolyPayloadA source;
    source._a = 12345;

    sw::ReflectAny anyValue = sw::ReflectAny::makeFrom( *pTypeInfo, &source );
    SW_EXPECT_FALSE( anyValue.empty() );
    SW_EXPECT_TRUE( anyValue._typeFqn == sw::hashed_string( "sw::PolyPayloadA" ) );

    // 3) 올바른 타입으로 추출
    sw::PolyPayloadA extracted;
    SW_EXPECT_TRUE( anyValue.tryGetFrom( *pTypeInfo, &extracted ) );
    SW_EXPECT_EQUAL( 12345, extracted._a );

    // 4) 다른 타입으로 추출 시 실패 검증
    const sw::TypeInfo* pWrongType = sw::engine::getSubsystem<sw::TypeRegistry>().findType( sw::hashed_string( "sw::SampleTestActor" ) );
    SW_ASSERT_NOT_NULL( pWrongType );
    sw::SampleTestActor wrongTarget;
    SW_EXPECT_FALSE( anyValue.tryGetFrom( *pWrongType, &wrongTarget ) );
}

/**
 * @brief [ReflectionTest] 옛 이름(`REFLECT( Alias = … )`)으로도 컴포넌트를 붙일 수 있다 — 이름을 바꾼 컴포넌트가 옛 씬에서 빠지지 않는다
 * @details 팩토리는 지금 이름으로만 등록된다. `addComponentByName` 이 별칭을 보지 않으면 씬 · 프리팹을 읽을 때 옛 이름의 컴포넌트가 로그도
 *          없이 빠지고, 다시 저장하면 그 데이터가 지워진다.
 */
SW_TEST_CASE( ReflectionTest, ComponentLoadsByItsOldName )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pObj = manager.createGameObject( sw::hashed_string( "AliasHost" ) );
    SW_ASSERT_NOT_NULL( pObj );

    sw::Component* pByOldName = manager.addComponentByName( pObj, sw::hashed_string( "LegacyGrandChildScriptComponent" ) );
    SW_ASSERT_NOT_NULL( pByOldName );
    SW_EXPECT_TRUE( sw::castTo<sw::TestGrandChildScriptComponent>( pByOldName ) != nullptr );
    SW_EXPECT_TRUE( pByOldName->getTypeName() == sw::hashed_string( "TestGrandChildScriptComponent" ) );
}

/**
 * @brief [ReflectionTest] 별칭(`using`)으로 적은 컨테이너 프로퍼티도 컨테이너다 — 직렬화가 그 값을 쓰고 읽는다, 스칼라 별칭은 그대로
 * @details 파서가 적힌 이름만 보면 `using ScoreList = sw::vector<int32>;` 로 적은 프로퍼티를 모르는 타입 이름으로 남겨, 직렬화가 그 값을 쓰지 못한다.
 */
SW_TEST_CASE( ReflectionTest, AliasedContainerPropertyIsAContainer )
{
    const sw::TypeInfo* pType = sw::engine::getSubsystem<sw::TypeRegistry>().findType( sw::hashed_string( "sw::AliasContainerActor" ) );
    SW_ASSERT_NOT_NULL( pType );
    const sw::PropertyInfo* pScores = pType->findProperty( sw::hashed_string( "_aliasScores" ) );
    const sw::PropertyInfo* pCount  = pType->findProperty( sw::hashed_string( "_aliasCount" ) );
    SW_ASSERT_NOT_NULL( pScores );
    SW_ASSERT_NOT_NULL( pCount );
    SW_EXPECT_TRUE( pScores->_bIsContainer );
    SW_EXPECT_TRUE( pScores->_containerKind == sw::ContainerKind::Sequence );
    SW_EXPECT_FALSE( pCount->_bIsContainer );
    // 원소로 적은 별칭도 벗긴다. 모르는 원소 이름이면 자기 기술형 바이너리 읽기가 실패한다.
    const sw::PropertyInfo* pAliasID = pType->findProperty( sw::hashed_string( "_listAliasID" ) );
    SW_ASSERT_NOT_NULL( pAliasID );
    SW_EXPECT_TRUE( pAliasID->_elementTypeName == sw::hashed_string( "uint64" ) );
    // 프로퍼티의 기록 타입 이름도 별칭을 벗긴다. 적힌 대로(`vector<TestAliasElementID>`)면 `vector<uint64>` 로 쓴 레코드를 읽지 못한다.
    SW_EXPECT_TRUE_MSG( pAliasID->_typeName == sw::hashed_string( "vector<uint64>" ), pAliasID->_typeName.c_str() );

    sw::AliasContainerActor source;
    source._aliasScores          = { 3, 1, 4 };
    source._aliasCount           = 7;
    source._listAliasID          = { 11, 22 };
    const sw::string        json = sw::JSONSerializer::serialize( &source, *pType );
    sw::AliasContainerActor restored;
    SW_ASSERT_TRUE_MSG( sw::JSONSerializer::deserialize( &restored, *pType, json ), json.c_str() );
    SW_EXPECT_EQUAL( size_t( 3 ), restored._aliasScores.size() );
    SW_EXPECT_TRUE_MSG( restored._aliasScores.size() == 3 && restored._aliasScores[2] == 4, json.c_str() );
    SW_EXPECT_EQUAL( 7, restored._aliasCount );

    sw::vector<uint8> bytes;
    SW_EXPECT_TRUE( sw::BinarySerializer::serialize( &source, *pType, bytes ) );
    sw::AliasContainerActor fromBinary;
    SW_ASSERT_TRUE( sw::BinarySerializer::deserialize( &fromBinary, *pType, bytes.data(), bytes.size() ) );
    SW_EXPECT_TRUE( fromBinary._listAliasID.size() == 2 && fromBinary._listAliasID[1] == 22 );
}

/**
 * @brief [TypedComponentHandleTest] FUNCTION 의 타입 핸들 인자는 타입 없는 핸들로 받고, 생성한 썽크가 그 컴포넌트의 씬에서 대상 타입을 확인한 뒤에만 부른다
 * @details 변이: 썽크가 확인 없이 바이트를 옮기면 다른 타입(`TestOtherComponent`)을 가리킨 호출도 `_linkCount` 를 올려 두 번째 비교가 진다.
 *          인자 정보는 `ComponentHandle` 과 대상 타입 이름(프로퍼티와 같은 표기)을 실어 인스펙터가 선택기를 그린다.
 */
SW_TEST_CASE( TypedComponentHandleTest, FunctionArgumentIsCheckedByThunk )
{
    sw::GameObjectManager manager;
    const ProbeObject     source = makeProbeObject( manager, "ProbeCall" );
    SW_ASSERT_TRUE( source._pObject != nullptr && source._pLink != nullptr && source._pFirstProbe != nullptr );
    const sw::TypeInfo* pType = sw::TestProbeLinkComponent::StaticType();
    SW_ASSERT_NOT_NULL( pType );
    const sw::FunctionInfo* pLink      = pType->findMethod( sw::hashed_string( "linkProbe" ) );
    const sw::PropertyInfo* pProbeProp = pType->findProperty( sw::hashed_string( "_probe" ) );
    SW_ASSERT_TRUE( pLink != nullptr && pProbeProp != nullptr );
    SW_ASSERT_EQUAL( 2u, pLink->getParameterCount() );
    SW_EXPECT_EQUAL( sw::string( "ComponentHandle" ), pLink->_listParameter[0]._typeName );
    SW_EXPECT_TRUE( pLink->_listParameter[0]._handleTargetTypeName == pProbeProp->_handleTargetTypeName );
    SW_EXPECT_TRUE( pLink->_listParameter[1]._handleTargetTypeName.empty() );

    source._pLink->_probe = {};
    SW_EXPECT_TRUE( sw::ReflectionInvoke::call( *pLink, source._pLink, { sw::ReflectValue::make( source._pFirstProbe->getHandle() ), sw::ReflectValue::make( int32{ 3 } ) } ) ==
                    sw::ReflectCallResult::Ok );
    SW_EXPECT_EQUAL( 3, source._pLink->_linkCount );
    SW_EXPECT_TRUE( manager.resolveComponent( source._pLink->_probe ) == source._pFirstProbe );

    SW_TEST_DEFENSIVE_SCOPE( "a handle to a component of another type is refused before the method runs" );
    const sw::TestOtherComponent* pOther = source._pObject->getComponent<sw::TestOtherComponent>();
    SW_ASSERT_NOT_NULL( pOther );
    (void)sw::ReflectionInvoke::call( *pLink, source._pLink, { sw::ReflectValue::make( pOther->getHandle() ), sw::ReflectValue::make( int32{ 5 } ) } );
    SW_EXPECT_EQUAL( 3, source._pLink->_linkCount );
    SW_EXPECT_TRUE( manager.resolveComponent( source._pLink->_probe ) == source._pFirstProbe );
}
