#include "pch.h"

#include "Core/Container/ComponentHandle.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/SlotHandle.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/TagID.h"

#include "Editor/Panels/Inspector/InspectorBuiltinValue.h"

#include "Engine/Reflection/ReflectionTypes.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 인자 하나를 받는 메서드 정보입니다. */
    FunctionInfo makeMethodTaking( const utf8* pParamType )
    {
        FunctionInfo method;
        method._name           = "Probe";
        method._returnTypeName = "void";
        method._listParameterTypeName.push_back( pParamType );
        return method;
    }

    /** @brief 인스펙터가 채운 인자 칸을 생성 호출기처럼 `args.get<T>( 0 )` 의 타입으로 꺼낼 수 있는지 봅니다. */
    template <typename T>
    bool doesMethodArgHoldCppType( const utf8* pTypeName )
    {
        vector<InspectorMethodArgSlot> listSlot;
        if ( InspectorBuiltinValueUtil::prepareMethodArgs( makeMethodTaking( pTypeName ), listSlot ) == false )
            return false;
        const TaskArgs args = InspectorBuiltinValueUtil::makeMethodArgs( listSlot );
        return args.getCount() == 1 && args.getPtr<T>( 0 ) != nullptr;
    }

    /** @brief CallInEditor 인자로 받을 수 있는지(인자 칸을 모두 채울 수 있는지)입니다. */
    bool isAcceptedAsArg( const utf8* pTypeName )
    {
        vector<InspectorMethodArgSlot> listSlot;
        return InspectorBuiltinValueUtil::prepareMethodArgs( makeMethodTaking( pTypeName ), listSlot );
    }

    /** @brief CallInEditor 반환으로 형식화할 수 있는지입니다. 값은 그 타입의 기본값(내장 타입이 아니면 int32 하나)입니다. */
    bool isAcceptedAsReturn( const utf8* pTypeName )
    {
        const InspectorBuiltinValue*          pRow  = InspectorBuiltinValueUtil::findBuiltin( pTypeName );
        const TaskValue                       value = ( pRow != nullptr ) ? pRow->_pMakeDefault() : TaskValue{ int32{ 7 } };
        fixed_string<constant::kMaxBuffer512> buf;
        return InspectorBuiltinValueUtil::formatMethodResult( value, pTypeName, buf.data(), buf.capacity() );
    }
} // namespace

/**
 * @brief [InspectorBuiltinValueTest] `ReflectBuiltins.xxx` 의 내장 타입마다 표 줄이 있고 위젯 갈래가 정해져 있다
 * @details 직렬화는 내장 타입 23 개를 다 읽는데 인스펙터는 13 개만 위젯을 등록해, int8 · int16 · uint16 · uint64 · float4x4 · quaternion ·
 *          TagID · 핸들 프로퍼티는 "No inspector for X" 로 떴다. 위젯 등록(`InspectorPropertyManager::registerDefaults`)은 이 표와 같은
 *          파일을 include 해서 줄마다 `InspectorWidgetFor<T>` 로 위젯을 만든다.
 */
SW_TEST_CASE( InspectorBuiltinValueTest, EveryBuiltinTypeHasWidget )
{
    SW_EXPECT_EQUAL( InspectorBuiltinValueUtil::kBuiltinCount, InspectorBuiltinValueUtil::getBuiltinCount() );

    uint32 checkedCount = 0;
#define SW_REFLECT_BUILTIN_TYPE( Canon, CppType, TextConv, Ns, ... )                                                       \
    {                                                                                                                      \
        const InspectorBuiltinValue* pRow = InspectorBuiltinValueUtil::findBuiltin( #Canon );                              \
        SW_EXPECT_TRUE_MSG( pRow != nullptr, #Canon );                                                                     \
        if ( pRow != nullptr )                                                                                             \
        {                                                                                                                  \
            SW_EXPECT_TRUE_MSG( pRow->_widget != InspectorValueWidget::None, #Canon );                                     \
            SW_EXPECT_TRUE_MSG( pRow->_widget == InspectorWidgetFor<InspectorBuiltinCppTypeT<CppType>>::kWidget, #Canon ); \
            SW_EXPECT_TRUE_MSG( &InspectorBuiltinValueUtil::getBuiltin( pRow->_index ) == pRow, #Canon );                  \
        }                                                                                                                  \
        ++checkedCount;                                                                                                    \
    }
#define SW_REFLECT_BUILTIN_CONTAINER( ... )
#include "Engine/Reflection/ReflectBuiltins.xxx"
#undef SW_REFLECT_BUILTIN_TYPE
#undef SW_REFLECT_BUILTIN_CONTAINER
    SW_EXPECT_EQUAL( InspectorBuiltinValueUtil::getBuiltinCount(), checkedCount );

    // 표 밖의 이름은 내장 타입이 아니다.
    SW_EXPECT_NULL( InspectorBuiltinValueUtil::findBuiltin( "SceneComponent" ) );
    SW_EXPECT_NULL( InspectorBuiltinValueUtil::findBuiltin( "" ) );
}

/**
 * @brief [InspectorBuiltinValueTest] 내장 타입은 CallInEditor 인자와 반환에서 같은 판정을 받는다
 * @details `InspectorBuiltinValue` 표 한 줄이 인자 지원 판정 · 인자 위젯 · `TaskArgs::add` · 반환 형식화 넷을 모두 정한다. 따로 된 if-체인이면
 *          한쪽에만 있는 타입이 생긴다(인자로는 못 받고 반환만 되는 float64 같은).
 */
SW_TEST_CASE( InspectorBuiltinValueTest, MethodArgAndReturnShareOneJudgment )
{
    for ( uint32 index = 0; index < InspectorBuiltinValueUtil::getBuiltinCount(); ++index )
    {
        const utf8* pTypeName = InspectorBuiltinValueUtil::getBuiltin( index )._pTypeName;
        SW_EXPECT_TRUE_MSG( isAcceptedAsArg( pTypeName ), pTypeName );
        SW_EXPECT_TRUE_MSG( isAcceptedAsReturn( pTypeName ), pTypeName );
        SW_EXPECT_TRUE_MSG( InspectorBuiltinValueUtil::supportsMethodValue( pTypeName ), pTypeName );
    }

    // 내장 타입이 아닌 것은 양쪽 모두 거절한다.
    for ( const utf8* pTypeName : { "SceneComponent", "vector<int32>", "RenderPassType" } )
    {
        SW_EXPECT_FALSE( isAcceptedAsArg( pTypeName ) );
        SW_EXPECT_FALSE( isAcceptedAsReturn( pTypeName ) );
        SW_EXPECT_FALSE( InspectorBuiltinValueUtil::supportsMethodValue( pTypeName ) );
    }

    // 반환값 표시는 표의 형식을 쓴다.
    fixed_string<constant::kMaxBuffer128> buf;
    SW_EXPECT_TRUE( InspectorBuiltinValueUtil::formatMethodResult( TaskValue{ float64{ 2.5 } }, "float64", buf.data(), buf.capacity() ) );
    SW_EXPECT_STREQ( "2.500000", buf.c_str() );
    SW_EXPECT_TRUE( InspectorBuiltinValueUtil::formatMethodResult( TaskValue{ true }, "bool", buf.data(), buf.capacity() ) );
    SW_EXPECT_STREQ( "true", buf.c_str() );
    SW_EXPECT_TRUE( InspectorBuiltinValueUtil::formatMethodResult( TaskValue{ GameObjectHandle::make( 42 ) }, "GameObjectHandle", buf.data(), buf.capacity() ) );
    SW_EXPECT_STREQ( "object 42", buf.c_str() );
    SW_EXPECT_TRUE( InspectorBuiltinValueUtil::formatMethodResult( TaskValue{}, "void", buf.data(), buf.capacity() ) );
}

/**
 * @brief [InspectorBuiltinValueTest] 인자 칸의 값은 인자의 C++ 타입 그대로 호출 인자에 들어간다
 * @details 생성 호출기는 `args.get<T>( index )` 로 정확히 그 타입을 꺼낸다. int64 인자에 int32 를 넣으면 Debug 에서는 assert, Release 에서는
 *          엉뚱한 바이트를 읽는다. 내장 타입마다 `ReflectBuiltins.xxx` 의 C++ 타입으로 꺼내 본다.
 */
SW_TEST_CASE( InspectorBuiltinValueTest, MethodArgKeepsExactCppType )
{
#define SW_REFLECT_BUILTIN_TYPE( Canon, CppType, TextConv, Ns, ... ) \
    SW_EXPECT_TRUE_MSG( doesMethodArgHoldCppType<InspectorBuiltinCppTypeT<CppType>>( #Canon ), #Canon );
#define SW_REFLECT_BUILTIN_CONTAINER( ... )
#include "Engine/Reflection/ReflectBuiltins.xxx"
#undef SW_REFLECT_BUILTIN_TYPE
#undef SW_REFLECT_BUILTIN_CONTAINER
}

/**
 * @brief [InspectorBuiltinValueTest] 인자 칸은 타입이 같으면 값을 지키고, 타입이 바뀌면 기본값으로 되돌린다 — 칸은 여덟까지다
 */
SW_TEST_CASE( InspectorBuiltinValueTest, PrepareMethodArgsKeepsOrResetsSlots )
{
    FunctionInfo method = makeMethodTaking( "float64" );
    method._listParameterTypeName.push_back( "TagID" );

    vector<InspectorMethodArgSlot> listSlot;
    SW_ASSERT_TRUE( InspectorBuiltinValueUtil::prepareMethodArgs( method, listSlot ) );
    SW_ASSERT_EQUAL( size_t{ 2 }, listSlot.size() );
    listSlot[0]._value = TaskValue{ float64{ 3.25 } };

    SW_ASSERT_TRUE( InspectorBuiltinValueUtil::prepareMethodArgs( method, listSlot ) );
    SW_EXPECT_EQUAL( 3.25, listSlot[0]._value.getValue<float64>() );

    // 같은 칸의 인자 타입이 바뀌면(핫 리로드로 시그니처가 바뀐 경우 등) 새 타입의 기본값이 된다.
    method._listParameterTypeName[0] = "int16";
    SW_ASSERT_TRUE( InspectorBuiltinValueUtil::prepareMethodArgs( method, listSlot ) );
    SW_EXPECT_NOT_NULL( listSlot[0]._value.getPtr<int16>() );
    SW_EXPECT_EQUAL( int16{ 0 }, listSlot[0]._value.getValue<int16>() );

    // 내장 타입이 아닌 인자가 하나라도 있으면 부를 수 없다.
    method._listParameterTypeName.push_back( "SceneComponent" );
    SW_EXPECT_FALSE( InspectorBuiltinValueUtil::prepareMethodArgs( method, listSlot ) );
    SW_EXPECT_FALSE( listSlot[2]._value.hasValue() );

    FunctionInfo manyArg;
    for ( uint32 index = 0; index <= InspectorBuiltinValueUtil::kMaxMethodArgCount; ++index )
        manyArg._listParameterTypeName.push_back( "int32" );
    SW_EXPECT_FALSE( InspectorBuiltinValueUtil::prepareMethodArgs( manyArg, listSlot ) );
    SW_EXPECT_EQUAL( size_t{ InspectorBuiltinValueUtil::kMaxMethodArgCount }, listSlot.size() );
}
