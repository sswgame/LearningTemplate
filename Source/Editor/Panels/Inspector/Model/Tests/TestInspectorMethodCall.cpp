#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Widgets/PropertyGrid/Model/InspectorBuiltinValue.h"
#include "Editor/Common/Widgets/PropertyGrid/Model/InspectorFavorites.h"
#include "Editor/Common/Widgets/PropertyGrid/Model/PropertyGridViewModel.h"

#include "Engine/Reflection/ReflectionInvoke.h"
#include "Engine/Reflection/Tests/ReflectionTestTypes.h"
#include "Engine/Reflection/TypeInfo.h"

// InspectorMethodCallTest — 인스펙터의 FUNCTION 호출. 인자 칸(내장 표 위젯 값)은 `ReflectedValue` 로 넘어가 인자 타입으로 바뀌고,
// 결과 갈래(`ReflectCallResult`) · 반환 글 · 실패 수/대상 수가 결과 칸에 남는다(엔진 테스트 타입 `InvokeDemoActor` 로 본다 — 씬 밖 대상이라 바로 부른다).

namespace
{
    struct InspectorMethodCallTestInternal
    {
        static const sw::FunctionInfo* findMethod( const utf8* pName )
        {
            const sw::TypeInfo* pType = sw::InvokeDemoActor::StaticType();
            return pType != nullptr ? pType->findMethod( sw::hashed_string( pName ) ) : nullptr;
        }

        /** @brief @p actor 를 씬 밖 대상으로 묶습니다. */
        static bool bind( sw::editor::PropertyGridViewModel& model, sw::InvokeDemoActor& actor )
        {
            sw::editor::EditorPropertyGridTarget target{};
            target._pInstance = &actor;
            target._pType     = sw::InvokeDemoActor::StaticType();
            return model.beginTarget( target );
        }
    };
} // namespace

/**
 * @brief [InspectorMethodCallTest] 인자 칸은 칸 타입 이름이 붙은 값으로 묶이고, 부르면 인자 타입으로 바뀌어 들어가 반환 글이 결과 칸에 남는다
 */
SW_TEST_CASE( InspectorMethodCallTest, SlotsConvertToArgumentsAndResultCarriesReturnText )
{
    const sw::FunctionInfo* pHeal = InspectorMethodCallTestInternal::findMethod( "heal" );
    SW_REQUIRE_NOT_NULL( pHeal );

    sw::vector<sw::editor::InspectorMethodArgSlot> listSlot;
    SW_REQUIRE_TRUE( sw::editor::InspectorBuiltinValue::prepareMethodArgs( *pHeal, listSlot ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listSlot.size() );
    listSlot[0]._value = sw::TaskValue{ int32{ 10 } };
    listSlot[1]._value = sw::TaskValue{ float32{ 2.0f } };

    const sw::vector<sw::ReflectedValue> listArg = sw::editor::InspectorBuiltinValue::makeMethodArgs( listSlot );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listArg.size() );
    SW_EXPECT_TRUE( listArg[0]._typeName == sw::hashed_string( "int32" ) );
    SW_EXPECT_TRUE( listArg[1]._typeName == sw::hashed_string( "float32" ) );

    sw::InvokeDemoActor               actor;
    sw::editor::PropertyGridViewModel model;
    SW_REQUIRE_TRUE( InspectorMethodCallTestInternal::bind( model, actor ) );
    model.invokeMethod( *pHeal, listArg );
    SW_EXPECT_EQUAL( 120, actor._hp );

    const sw::editor::InspectorInvokeResult& result = model.getLastInvokeResult();
    SW_EXPECT_EQUAL( sw::string( "heal" ), result._methodName );
    SW_EXPECT_FALSE( result.hasFailure() );
    SW_EXPECT_EQUAL( 1u, result._targetCount );
    SW_EXPECT_EQUAL( sw::string( "120" ), result._returnText );

    // void 반환은 빈 글이다(패널은 대시로 보인다).
    const sw::FunctionInfo* pReport = InspectorMethodCallTestInternal::findMethod( "reportHp" );
    SW_REQUIRE_NOT_NULL( pReport );
    model.invokeMethod( *pReport, { sw::ReflectedValue::make( int32{ 7 } ) } );
    SW_EXPECT_EQUAL( 7, actor._lastReported );
    SW_EXPECT_FALSE( model.getLastInvokeResult().hasFailure() );
    SW_EXPECT_TRUE( model.getLastInvokeResult()._returnText.empty() );
    model.endTarget();
}

/**
 * @brief [InspectorMethodCallTest] 부를 수 없는 호출은 값을 바꾸지 않고 갈래로 남는다 — 빠진 인자 · 바꿀 수 없는 인자 · 많은 인자 · 없는 함수
 */
SW_TEST_CASE( InspectorMethodCallTest, FailedCallsLeaveTheReasonAndDoNotRun )
{
    const sw::FunctionInfo* pHeal     = InspectorMethodCallTestInternal::findMethod( "heal" );
    const sw::FunctionInfo* pDescribe = InspectorMethodCallTestInternal::findMethod( "describe" );
    SW_REQUIRE_NOT_NULL( pHeal );
    SW_REQUIRE_NOT_NULL( pDescribe );

    sw::InvokeDemoActor               actor;
    sw::editor::PropertyGridViewModel model;
    SW_REQUIRE_TRUE( InspectorMethodCallTestInternal::bind( model, actor ) );

    model.invokeMethod( *pHeal, {} ); // amount 는 기본 인자가 없다
    SW_EXPECT_TRUE( model.getLastInvokeResult()._firstFailure == sw::ReflectCallResult::MissingArgument );
    SW_EXPECT_EQUAL( 1u, model.getLastInvokeResult()._failureCount );

    model.invokeMethod( *pHeal, { sw::ReflectedValue::makeText( "not a number" ) } );
    SW_EXPECT_TRUE( model.getLastInvokeResult()._firstFailure == sw::ReflectCallResult::ArgumentMismatch );

    model.invokeMethod( *pDescribe, { sw::ReflectedValue::makeText( "a" ), sw::ReflectedValue::makeText( "b" ) } );
    SW_EXPECT_TRUE( model.getLastInvokeResult()._firstFailure == sw::ReflectCallResult::TooManyArguments );

    // 모듈을 다시 올려 함수가 사라진 경우 — 부를 때 이름으로 다시 찾으므로 옛 정보로 부르지 않는다.
    sw::FunctionInfo gone = *pHeal;
    gone._name            = "removedMethod";
    gone._hashName        = sw::hashed_string( "removedMethod" );
    model.invokeMethod( gone, { sw::ReflectedValue::make( int32{ 5 } ) } );
    SW_EXPECT_TRUE( model.getLastInvokeResult()._firstFailure == sw::ReflectCallResult::NotBound );
    SW_EXPECT_EQUAL( sw::string( "removedMethod" ), model.getLastInvokeResult()._methodName );

    SW_EXPECT_EQUAL( 100, actor._hp );
    model.endTarget();
}

/**
 * @brief [InspectorMethodCallTest] 대상 여럿의 결과는 첫 실패 갈래와 실패 수/대상 수, 첫 성공 대상의 반환 글을 든다
 */
SW_TEST_CASE( InspectorMethodCallTest, ResultAggregatesTargets )
{
    sw::editor::InspectorInvokeResult result;
    result.begin( "heal" );
    result.record( sw::ReflectCallResult::ArgumentMismatch, {} );
    result.record( sw::ReflectCallResult::Ok, sw::ReflectedValue::make( int32{ 3 } ) );
    result.record( sw::ReflectCallResult::Ok, sw::ReflectedValue::make( int32{ 4 } ) );
    result.record( sw::ReflectCallResult::NotBound, {} );
    SW_EXPECT_EQUAL( 4u, result._targetCount );
    SW_EXPECT_EQUAL( 2u, result._failureCount );
    SW_EXPECT_TRUE( result._firstFailure == sw::ReflectCallResult::ArgumentMismatch );
    SW_EXPECT_EQUAL( sw::string( "3" ), result._returnText );

    result.begin( "describe" );
    SW_EXPECT_EQUAL( 0u, result._targetCount );
    SW_EXPECT_FALSE( result.hasFailure() );
    SW_EXPECT_TRUE( result._returnText.empty() );
}

/**
 * @brief [InspectorMethodCallTest] 액션 즐겨찾기 키는 메서드를 선언한 타입 이름 + 메서드 이름이고, 그리드는 그 집합으로 별 · 맨 위 정렬을 판정한다
 */
SW_TEST_CASE( InspectorMethodCallTest, ActionFavoriteKeyAndLookup )
{
    const sw::TypeInfo*     pType = sw::InvokeDemoActor::StaticType();
    const sw::FunctionInfo* pHeal = InspectorMethodCallTestInternal::findMethod( "heal" );
    SW_REQUIRE_NOT_NULL( pType );
    SW_REQUIRE_NOT_NULL( pHeal );
    const sw::string key = sw::editor::InspectorFavorites::makeMethodKey( *pType, *pHeal );
    SW_EXPECT_EQUAL( sw::editor::InspectorFavorites::makeKey( pType->_name.c_str(), "heal" ), key );

    // 프로퍼티 즐겨찾기와 다른 파일이다 — 섞으면 메서드만 즐겨찾은 타입에 빈 프로퍼티 구역이 선다.
    SW_EXPECT_FALSE( sw::string_view( sw::editor::InspectorFavorites::kPropertyFileName ) == sw::string_view( sw::editor::InspectorFavorites::kActionFileName ) );

    sw::editor::InspectorFavorites    actions( sw::editor::InspectorFavorites::kActionFileName );
    sw::editor::PropertyGridViewModel model;
    SW_EXPECT_FALSE( model.hasActionFavorites() );
    SW_EXPECT_FALSE( model.isActionFavorite( *pType, *pHeal ) );
    model.setActionFavorites( &actions );
    SW_EXPECT_TRUE( model.hasActionFavorites() );
    SW_EXPECT_FALSE( model.isActionFavorite( *pType, *pHeal ) );
    SW_EXPECT_TRUE( actions.add( key ) );
    SW_EXPECT_TRUE( model.isActionFavorite( *pType, *pHeal ) );
    model.setActionFavorites( nullptr );
}
