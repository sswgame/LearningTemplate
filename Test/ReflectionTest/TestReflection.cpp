#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectAny.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionCore.h"

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
    prop.bindOnChanged( SW_DELEGATE_LAMBDA( sw::PropertyInfo::PropertyBindingDelegate, [&bCalled]( const sw::PropertyInfo& p, const void* pInst )
    {
        (void)p;
        (void)pInst;
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
    // 수동 연결 invoker (레거시 경로도 여전히 지원).
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
    funcInfo._name                  = "takeDamage";
    funcInfo._hashName              = sw::hashed_string( "takeDamage" );
    funcInfo._returnTypeName        = "void";
    funcInfo._listParameterTypeName = { "sw::int32" };
    funcInfo._invoker               = SW_DELEGATE_LAMBDA( sw::Delegate<sw::TaskValue( void*, const sw::TaskArgs& )>, []( void* pObjPtr, const sw::TaskArgs& args ) -> sw::TaskValue
                  {
        static_cast<FunctionAnnotatedActor*>( pObjPtr )->takeDamage( args.get<int32>( 0 ) );
        return sw::TaskValue{};
    } );

    sw::TypeInfo info;
    info._name               = sw::hashed_string( "FunctionAnnotatedActor" );
    info._fullyQualifiedName = sw::hashed_string( "sw::FunctionAnnotatedActor" );
    info._size               = sizeof( FunctionAnnotatedActor );
    info._listMethod.push_back( funcInfo );

    sw::engine::getTypeRegistry().registerClass( info );

    sw::TaskArgs args;
    args.add( int32{ 35 } );
    sw::engine::getTypeRegistry().invokeMethod( &actor, sw::hashed_string( "sw::FunctionAnnotatedActor" ), sw::hashed_string( "takeDamage" ), args );

    SW_EXPECT_EQUAL( 65, actor._health );
}

/**
 * @brief [ReflectionFunctionMacroTest] 코드젠 메서드 호출
 */
SW_TEST_CASE( ReflectionFunctionMacroTest, CodegenMethodInvoke )
{
    // SampleTestActor::takeDamage / getHp 는 ReflectionParser 코드젠이 출력한다.
    sw::SampleTestActor actor;
    SW_ASSERT_NOT_NULL( sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::SampleTestActor" ) ) );

    const sw::TypeInfo* typeInfo = sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::SampleTestActor" ) );
    SW_ASSERT_NOT_NULL( typeInfo );
    const sw::PropertyInfo* nameProp = typeInfo->findProperty( sw::hashed_string( "_name" ) );
    SW_ASSERT_NOT_NULL( nameProp );
    SW_EXPECT_EQUAL( sw::string( "string" ), sw::string( nameProp->_typeName.c_str() ) );
    const sw::FunctionInfo* takeDamageFn = typeInfo->findMethod( sw::hashed_string( "takeDamage" ) );
    const sw::FunctionInfo* getHpFn      = typeInfo->findMethod( sw::hashed_string( "getHp" ) );
    SW_ASSERT_NOT_NULL( takeDamageFn );
    SW_ASSERT_NOT_NULL( getHpFn );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( takeDamageFn->_metadata._bConst ) );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( getHpFn->_metadata._bConst ) );

    sw::TaskArgs damageArgs;
    damageArgs.add( int32{ 40 } );
    sw::engine::getTypeRegistry().invokeMethod( &actor, sw::hashed_string( "sw::SampleTestActor" ),
                                                sw::hashed_string( "takeDamage" ), damageArgs );
    SW_EXPECT_EQUAL( 60, actor._hp );

    sw::TaskValue hp = sw::engine::getTypeRegistry().invokeMethod( &actor, sw::hashed_string( "sw::SampleTestActor" ),
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

    manager.tick( 1.0f );
    manager.tick( 1.0f );

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

    manager.tick( 0.016f );
    manager.tick( 0.016f );

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
    const sw::TypeInfo* pType = sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::TestScriptComponent" ) );
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
 * @brief [ReflectionGenericQueryTest] TypeRegistry 템플릿 조회 및 isA 헬퍼 검증
 */
SW_TEST_CASE( ReflectionGenericQueryTest, FindTypeAndIsA )
{
    const sw::TypeInfo* pType = sw::engine::getTypeRegistry().findType<sw::TestDerivedScriptComponent>();
    SW_ASSERT_NOT_NULL( pType );
    SW_EXPECT_TRUE( pType->_name == sw::hashed_string( "TestDerivedScriptComponent" ) );

    sw::TestDerivedScriptComponent comp;
    SW_EXPECT_TRUE( sw::isA<sw::TestDerivedScriptComponent>( &comp ) );
    SW_EXPECT_TRUE( sw::isA<sw::TestScriptComponent>( &comp ) );
    SW_EXPECT_TRUE( sw::isA<sw::Component>( &comp ) );
    SW_EXPECT_TRUE( sw::isA<sw::DummyActor>( &comp ) == false );
}

/**
 * @brief [ReflectionGenericQueryTest] REFLECT() 베이스가 첫 번째로 선언된 다중 상속 액터의 부모 채택을
 *        검증합니다. IPlainMixinTestActor(REFLECT() 없는 순수 인터페이스)는 두 번째 베이스로 조용히
 *        무시되고, EmptyReflectedBaseTestActor(REFLECT() 있음, 첫 번째 베이스)가 부모로 채택되어야
 *        합니다. GameFramework::TurnBattleSaveGame : public SaveGame, public IFlagStore 실사례의
 *        축소판이며, ReflectionParser/AstVisitor.cpp 의 baseClassVisitor 회귀 테스트입니다.
 * @note 프로퍼티 오프셋은 파생 클래스 자신에게 직접 선언된 것만 안전합니다(offsetof가 그 파생
 *       클래스 자체의 실제 레이아웃으로 계산되므로). 그래서 리플렉션 부모(EmptyReflectedBaseTestActor)
 *       는 실제 SaveGame처럼 프로퍼티가 없고, _ownValue 는 파생 클래스 자신에 선언되어 있습니다 —
 *       서로 다른 다형성을 가진 베이스를 섞으면 상속받은(파생 클래스에 없는) 프로퍼티의 오프셋이
 *       ABI상 안전하지 않을 수 있으므로, 리플렉션 프로퍼티 병합은 여전히 단일 상속에서만 신뢰할 수
 *       있습니다(getPropertiesWithBase() 문서 참고).
 */
SW_TEST_CASE( ReflectionGenericQueryTest, MultiInheritanceSafeOrderParentAndProperties )
{
    const sw::TypeInfo* pType = sw::engine::getTypeRegistry().findType<sw::MultiBaseOrderTestActor>();
    SW_ASSERT_NOT_NULL( pType );

    // 부모는 IPlainMixinTestActor(REFLECT() 없음)가 아니라 첫 번째로 선언된 REFLECT() 베이스여야 합니다.
    SW_EXPECT_TRUE( pType->_parentFQN == sw::hashed_string( "sw::EmptyReflectedBaseTestActor" ) );
    SW_EXPECT_TRUE( pType->isDerivedFrom( sw::hashed_string( "sw::EmptyReflectedBaseTestActor" ) ) );

    // 다중 상속과 무관하게, 파생 클래스 자신에게 직접 선언된 프로퍼티는 정상적으로 round-trip 되어야
    // 합니다 (TurnBattleSaveGame 의 5개 필드가 실제로 의존하는 보장입니다).
    sw::MultiBaseOrderTestActor actor;
    const sw::PropertyInfo*     pOwnProp = pType->findProperty( sw::hashed_string( "_ownValue" ) );
    SW_ASSERT_NOT_NULL( pOwnProp );
    pOwnProp->setValue<int32>( &actor, 555 );
    SW_EXPECT_TRUE( actor._ownValue == 555 );
}

/**
 * @brief [ReflectionGenericQueryTest] TypeRegistry forEachType 및 getDerivedTypes 검증
 */
SW_TEST_CASE( ReflectionGenericQueryTest, ForEachTypeAndDerivedTypes )
{
    uint32 typeCount{ 0 };
    sw::engine::getTypeRegistry().forEachType(
        [&typeCount]( const sw::TypeInfo& )
    {
        ++typeCount;
    } );
    SW_EXPECT_TRUE( typeCount > 0 );

    const auto derivedComponents = sw::engine::getTypeRegistry().getDerivedTypes<sw::TestScriptComponent>();
    SW_EXPECT_TRUE( derivedComponents.size() >= 2 ); // TestDerivedScriptComponent, TestGrandChildScriptComponent
}

/**
 * @brief [ReflectionGenericQueryTest] PropertyInfo getRawPtr 및 findPropertyInHierarchy 검증
 */
SW_TEST_CASE( ReflectionGenericQueryTest, HierarchyPropertyLookupAndRawPtr )
{
    const sw::TypeInfo* pGrandChildType = sw::engine::getTypeRegistry().findType<sw::TestGrandChildScriptComponent>();
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
    const sw::TypeInfo* pTypeInfo = sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::PolyPayloadA" ) );
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
    const sw::TypeInfo* pWrongType = sw::engine::getTypeRegistry().findType( sw::hashed_string( "sw::SampleTestActor" ) );
    SW_ASSERT_NOT_NULL( pWrongType );
    sw::SampleTestActor wrongTarget;
    SW_EXPECT_FALSE( anyValue.tryGetFrom( *pWrongType, &wrongTarget ) );
}
