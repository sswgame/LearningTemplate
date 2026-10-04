#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionCore.h"

#include "ReflectionTest/TestSampleActor.h"

#include "TestFramework/TestFramework.h"

// 이름으로 부르기 · 인자 변환 · 기본 인자 · 이벤트 — 타입을 모르는 쪽(콘솔 · 비주얼 스크립팅 · 기믹 배선)이 쓰는 길.

namespace
{
    const sw::TypeInfo& getInvokeDemoType()
    {
        const sw::TypeInfo* pType = sw::InvokeDemoActor::StaticType();
        SW_ASSERT( pType != nullptr );
        return *pType;
    }

    const sw::FunctionInfo& getInvokeDemoMethod( const sw::hashed_string& name )
    {
        const sw::FunctionInfo* pMethod = getInvokeDemoType().findMethod( name );
        SW_ASSERT( pMethod != nullptr );
        return *pMethod;
    }
} // namespace

/**
 * @brief [ReflectionInvokeTest] 함수 정보가 인자 이름 · 정규 타입 · 기본 인자(C++ 식 그대로) · 반환 타입을 든다
 */
SW_TEST_CASE( ReflectionInvokeTest, FunctionInfoCarriesParameterNamesAndDefaults )
{
    const sw::FunctionInfo& heal = getInvokeDemoMethod( "heal" );
    SW_ASSERT_EQUAL( 2u, heal.getParameterCount() );
    SW_EXPECT_EQUAL( sw::string( "amount" ), heal._listParameter[0]._name );
    SW_EXPECT_EQUAL( sw::string( "int32" ), heal._listParameter[0]._typeName );
    SW_EXPECT_FALSE( heal._listParameter[0].hasDefaultValue() );
    SW_EXPECT_EQUAL( sw::string( "multiplier" ), heal._listParameter[1]._name );
    SW_EXPECT_EQUAL( sw::string( "float32" ), heal._listParameter[1]._typeName );
    SW_EXPECT_EQUAL( sw::string( "1.5f" ), heal._listParameter[1]._defaultValue );
    SW_ASSERT_NOT_NULL( heal._pReturnType );
    SW_EXPECT_TRUE( heal._pReturnType->_pGetTypeName() == sw::hashed_string( "int32" ) );

    const sw::FunctionInfo& describe = getInvokeDemoMethod( "describe" );
    SW_ASSERT_EQUAL( 1u, describe.getParameterCount() );
    SW_EXPECT_EQUAL( sw::string( "\"hp\"" ), describe._listParameter[0]._defaultValue );

    const sw::FunctionInfo& setStatus = getInvokeDemoMethod( "setStatus" );
    SW_ASSERT_EQUAL( 1u, setStatus.getParameterCount() );
    SW_EXPECT_EQUAL( sw::string( "SampleStatus::Moving" ), setStatus._listParameter[0]._defaultValue );
    SW_EXPECT_NULL( setStatus._pReturnType );
}

/**
 * @brief [ReflectionInvokeTest] 인자는 인자 타입으로 바뀌어 들어가고(글 · 다른 숫자 타입), 빠진 뒤쪽 인자는 기본 인자로 찬다
 */
SW_TEST_CASE( ReflectionInvokeTest, CallConvertsArgumentsAndFillsDefaults )
{
    sw::InvokeDemoActor     actor;
    const sw::FunctionInfo& heal = getInvokeDemoMethod( "heal" );

    // 글 "10" → int32, 배율은 기본 인자 1.5f
    sw::ReflectValue result;
    SW_EXPECT_TRUE( sw::ReflectionInvoke::call( heal, &actor, { sw::ReflectValue::makeText( "10" ) }, &result ) == sw::ReflectCallResult::Ok );
    SW_EXPECT_EQUAL( 115, actor._hp );
    const int32* pResult = result.findValue<int32>();
    SW_ASSERT_NOT_NULL( pResult );
    SW_EXPECT_EQUAL( 115, *pResult );

    // float64 4.0(소수부 없음) → int32, int32 2 → float32
    SW_EXPECT_TRUE( sw::ReflectionInvoke::call( heal, &actor, { sw::ReflectValue::make( 4.0 ), sw::ReflectValue::make( int32{ 2 } ) }, &result ) ==
                    sw::ReflectCallResult::Ok );
    SW_EXPECT_EQUAL( 123, actor._hp );

    // 반환 글
    const sw::FunctionInfo& describe = getInvokeDemoMethod( "describe" );
    SW_EXPECT_TRUE( sw::ReflectionInvoke::call( describe, &actor, {}, &result ) == sw::ReflectCallResult::Ok );
    SW_EXPECT_EQUAL( sw::string( "hp:123" ), result.toText() );
    SW_EXPECT_TRUE( sw::ReflectionInvoke::callWithText( describe, &actor, { "life" }, &result ) == sw::ReflectCallResult::Ok );
    SW_EXPECT_EQUAL( sw::string( "life:123" ), result.toText() );
}

/**
 * @brief [ReflectionInvokeTest] 열거형 인자는 이름 글 · 같은 열거형 · 알려진 숫자로 받고, 기본 인자(`SampleStatus::Moving`)도 읽는다
 */
SW_TEST_CASE( ReflectionInvokeTest, EnumArgumentReadsNameValueAndDefault )
{
    sw::InvokeDemoActor     actor;
    const sw::FunctionInfo& setStatus = getInvokeDemoMethod( "setStatus" );

    SW_EXPECT_TRUE( sw::ReflectionInvoke::call( setStatus, &actor, {} ) == sw::ReflectCallResult::Ok );
    SW_EXPECT_TRUE( actor._status == sw::SampleStatus::Moving );

    SW_EXPECT_TRUE( sw::ReflectionInvoke::callWithText( setStatus, &actor, { "Attacking" } ) == sw::ReflectCallResult::Ok );
    SW_EXPECT_TRUE( actor._status == sw::SampleStatus::Attacking );

    SW_EXPECT_TRUE( sw::ReflectionInvoke::call( setStatus, &actor, { sw::ReflectValue::make( sw::SampleStatus::Idle ) } ) == sw::ReflectCallResult::Ok );
    SW_EXPECT_TRUE( actor._status == sw::SampleStatus::Idle );

    SW_EXPECT_TRUE( sw::ReflectionInvoke::call( setStatus, &actor, { sw::ReflectValue::make( int32{ 1 } ) } ) == sw::ReflectCallResult::Ok );
    SW_EXPECT_TRUE( actor._status == sw::SampleStatus::Moving );

    // 모르는 이름 · 모르는 값은 거절하고 값을 건드리지 않는다
    SW_EXPECT_TRUE( sw::ReflectionInvoke::callWithText( setStatus, &actor, { "Flying" } ) == sw::ReflectCallResult::ArgumentMismatch );
    SW_EXPECT_TRUE( sw::ReflectionInvoke::call( setStatus, &actor, { sw::ReflectValue::make( int32{ 9 } ) } ) == sw::ReflectCallResult::ArgumentMismatch );
    SW_EXPECT_TRUE( actor._status == sw::SampleStatus::Moving );
}

/**
 * @brief [ReflectionInvokeTest] 넘길 수 없는 인자는 부르지 않고 이유를 돌려준다(조용히 0 이나 깎은 값으로 부르지 않는다)
 */
SW_TEST_CASE( ReflectionInvokeTest, CallRejectsArgumentsItCannotPass )
{
    sw::InvokeDemoActor     actor;
    const sw::FunctionInfo& heal = getInvokeDemoMethod( "heal" );

    SW_EXPECT_TRUE( sw::ReflectionInvoke::call( heal, &actor, {} ) == sw::ReflectCallResult::MissingArgument );
    SW_EXPECT_TRUE( sw::ReflectionInvoke::callWithText( heal, &actor, { "1", "2", "3" } ) == sw::ReflectCallResult::TooManyArguments );
    SW_EXPECT_TRUE( sw::ReflectionInvoke::callWithText( heal, &actor, { "ten" } ) == sw::ReflectCallResult::ArgumentMismatch );
    SW_EXPECT_TRUE( sw::ReflectionInvoke::call( heal, &actor, { sw::ReflectValue::make( 2.5f ) } ) == sw::ReflectCallResult::ArgumentMismatch );
    SW_EXPECT_TRUE( sw::ReflectionInvoke::call( heal, &actor, { sw::ReflectValue::make( int64{ 1 } << 40 ) } ) == sw::ReflectCallResult::ArgumentMismatch );
    SW_EXPECT_TRUE( sw::ReflectionInvoke::call( heal, nullptr, { sw::ReflectValue::make( int32{ 1 } ) } ) == sw::ReflectCallResult::NullInstance );
    SW_EXPECT_EQUAL( 100, actor._hp );

    SW_EXPECT_TRUE( sw::ReflectionInvoke::callByName( getInvokeDemoType(), &actor, "noSuchFunction", {} ) == sw::ReflectCallResult::NotBound );
}

/**
 * @brief [ReflectionInvokeTest] 멀티캐스트 델리게이트 PROPERTY 는 이벤트로 등록된다 — 인자 이름 · 타입 · 표시 메타, 프로퍼티 목록에는 없다
 */
SW_TEST_CASE( ReflectionInvokeTest, MulticastDelegatePropertyIsReflectedAsEvent )
{
    const sw::TypeInfo&  type   = getInvokeDemoType();
    const sw::EventInfo* pEvent = type.findEventInHierarchy( "_onHpChanged" );
    SW_ASSERT_NOT_NULL( pEvent );
    SW_ASSERT_EQUAL( 2u, pEvent->getParameterCount() );
    SW_EXPECT_EQUAL( sw::string( "newHp" ), pEvent->_listParameter[0]._name );
    SW_EXPECT_EQUAL( sw::string( "int32" ), pEvent->_listParameter[0]._typeName );
    SW_EXPECT_EQUAL( sw::string( "reason" ), pEvent->_listParameter[1]._name );
    SW_EXPECT_EQUAL( sw::string( "string" ), pEvent->_listParameter[1]._typeName );
    SW_EXPECT_NOT_NULL( pEvent->_listParameter[1]._pType );
#if !defined( SW_SHIPPING )
    SW_EXPECT_EQUAL( sw::string( "Events" ), pEvent->_metadata._category );
#endif

    const sw::EventInfo* pDied = type.findEvent( "_onDied" );
    SW_ASSERT_NOT_NULL( pDied );
    SW_EXPECT_EQUAL( 0u, pDied->getParameterCount() );

    // 값이 아니다 — 직렬화 · 인스펙터 값 편집이 도는 프로퍼티 목록에는 없다
    SW_EXPECT_NULL( type.findPropertyInHierarchy( "_onHpChanged" ) );
}

/**
 * @brief [ReflectionInvokeTest] 이름으로 찾은 이벤트에 타입을 모른 채 묶고 · 부르고 · 뗀다
 */
SW_TEST_CASE( ReflectionInvokeTest, EventBindsBroadcastsAndUnbindsByName )
{
    sw::InvokeDemoActor  actor;
    const sw::EventInfo* pEvent = getInvokeDemoType().findEventInHierarchy( "_onHpChanged" );
    SW_ASSERT_NOT_NULL( pEvent );

    sw::vector<sw::ReflectValue> listReceived;
    uint32                       callCount = 0;
    const sw::DelegateHandle     handle    = sw::ReflectionInvoke::bindEvent( *pEvent, &actor, sw::ReflectEventHandler::create( [&listReceived, &callCount]( const sw::vector<sw::ReflectValue>& listArg )
    {
        listReceived = listArg;
        ++callCount;
    } ) );
    SW_ASSERT_TRUE( handle.isValid() );
    SW_EXPECT_TRUE( sw::ReflectionInvoke::isEventBound( *pEvent, &actor ) );

    // 네이티브 broadcast 가 리플렉션 받는 쪽에 이름 붙은 값으로 닿는다
    actor._onHpChanged.broadcast( 42, sw::string( "potion" ) );
    SW_ASSERT_EQUAL( 1u, callCount );
    SW_ASSERT_EQUAL( static_cast<size_t>( 2 ), listReceived.size() );
    SW_ASSERT_NOT_NULL( listReceived[0].findValue<int32>() );
    SW_EXPECT_EQUAL( 42, *listReceived[0].findValue<int32>() );
    SW_EXPECT_EQUAL( sw::string( "potion" ), listReceived[1].toText() );

    // 리플렉션 broadcast 는 인자를 이벤트 타입으로 바꿔 네이티브 받는 쪽까지 부른다
    using HpChangedDelegate = sw::Delegate<void( int32, const sw::string& )>;
    int32      nativeHp     = 0;
    sw::string nativeReason;
    actor._onHpChanged.add( SW_DELEGATE_LAMBDA( HpChangedDelegate, [&nativeHp, &nativeReason]( int32 hp, const sw::string& reason )
    {
        nativeHp     = hp;
        nativeReason = reason;
    } ) );
    SW_EXPECT_TRUE( sw::ReflectionInvoke::broadcastEvent( *pEvent, &actor, { sw::ReflectValue::makeText( "7" ), sw::ReflectValue::makeText( "trap" ) } ) ==
                    sw::ReflectCallResult::Ok );
    SW_EXPECT_EQUAL( 7, nativeHp );
    SW_EXPECT_EQUAL( sw::string( "trap" ), nativeReason );
    SW_EXPECT_EQUAL( 2u, callCount );

    // 인자가 맞지 않으면 아무도 부르지 않는다
    SW_EXPECT_TRUE( sw::ReflectionInvoke::broadcastEvent( *pEvent, &actor, { sw::ReflectValue::makeText( "7" ) } ) == sw::ReflectCallResult::MissingArgument );
    SW_EXPECT_TRUE( sw::ReflectionInvoke::broadcastEvent( *pEvent, &actor, { sw::ReflectValue::makeText( "x" ), sw::ReflectValue::makeText( "y" ) } ) ==
                    sw::ReflectCallResult::ArgumentMismatch );
    SW_EXPECT_EQUAL( 2u, callCount );

    sw::ReflectionInvoke::unbindEvent( *pEvent, &actor, handle );
    actor._onHpChanged.broadcast( 1, sw::string( "after" ) );
    SW_EXPECT_EQUAL( 2u, callCount );
}

/**
 * @brief [ReflectionInvokeTest] 이벤트를 다른 오브젝트의 함수에 이름으로 묶는다(기믹 배선) — 넘길 수 없는 시그니처는 묶지 않는다
 */
SW_TEST_CASE( ReflectionInvokeTest, EventBindsToFunctionByName )
{
    sw::InvokeDemoActor  source;
    sw::InvokeDemoActor  target;
    const sw::TypeInfo&  type   = getInvokeDemoType();
    const sw::EventInfo* pEvent = type.findEventInHierarchy( "_onHpChanged" );
    const sw::EventInfo* pDied  = type.findEventInHierarchy( "_onDied" );
    SW_ASSERT_NOT_NULL( pEvent );
    SW_ASSERT_NOT_NULL( pDied );

    // (int32, string) → reportHp(int32): 앞쪽 인자만 넘긴다
    SW_EXPECT_TRUE( sw::ReflectionInvoke::canBindEventToFunction( *pEvent, getInvokeDemoMethod( "reportHp" ) ) );
    const sw::DelegateHandle handle = sw::ReflectionInvoke::bindEventToFunction( *pEvent, &source, type, "reportHp", &target );
    SW_ASSERT_TRUE( handle.isValid() );
    source._onHpChanged.broadcast( 33, sw::string( "hit" ) );
    SW_EXPECT_EQUAL( 33, target._lastReported );

    // () → reportHp(int32): 넘길 인자가 없고 기본 인자도 없다 / () → setStatus(=Moving): 기본 인자로 채운다
    SW_EXPECT_FALSE( sw::ReflectionInvoke::canBindEventToFunction( *pDied, getInvokeDemoMethod( "reportHp" ) ) );
    SW_EXPECT_FALSE( sw::ReflectionInvoke::bindEventToFunction( *pDied, &source, type, "reportHp", &target ).isValid() );
    SW_EXPECT_TRUE( sw::ReflectionInvoke::bindEventToFunction( *pDied, &source, type, "setStatus", &target ).isValid() );
    source._onDied.broadcast();
    SW_EXPECT_TRUE( target._status == sw::SampleStatus::Moving );

    // (int32, string) → describe(string): int32 를 글로 넘길 수 있다 / 없는 함수는 묶지 않는다
    SW_EXPECT_TRUE( sw::ReflectionInvoke::canBindEventToFunction( *pEvent, getInvokeDemoMethod( "describe" ) ) );
    SW_EXPECT_FALSE( sw::ReflectionInvoke::bindEventToFunction( *pEvent, &source, type, "noSuchFunction", &target ).isValid() );
}

/**
 * @brief [ReflectionInvokeTest] 등록 내용 진단(`describeType`)이 인자 이름 · 기본 인자 · 이벤트를 보인다
 */
SW_TEST_CASE( ReflectionInvokeTest, DescribeTypeShowsParametersAndEvents )
{
    const sw::string text = sw::engine::getTypeRegistry().describeType( "sw::InvokeDemoActor" );
    SW_EXPECT_TRUE_MSG( text.find( "FUNCTION heal(int32 amount, float32 multiplier = 1.5f) -> int32" ) != sw::string::npos, text.c_str() );
    SW_EXPECT_TRUE_MSG( text.find( "EVENT _onHpChanged(int32 newHp, string reason)" ) != sw::string::npos, text.c_str() );
}
