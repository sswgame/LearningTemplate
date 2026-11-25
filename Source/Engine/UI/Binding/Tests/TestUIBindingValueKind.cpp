// UIBindingValueKind — UI 바인딩의 값 종류는 내장 타입 표의 갈래에서 나온다. 성분이 하나인 압축 타입은 숫자 바인딩이고 여럿이면 그 밖이다(nogpu).
#include "pch.h"

#include "Core/Name/hashed_string.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Reflection/ReflectedValue.h"
#include "Engine/UI/Binding/UIBindingValue.h"

using namespace sw;

/**
 * @brief [UIBindingValueKindTest] 값 비교는 글 비교와 같은 답이다 — 종류 · 불리언 · 숫자(비트) · 글
 */
SW_TEST_CASE( UIBindingValueKindTest, SameValueMatchesTextComparison )
{
    SW_EXPECT_TRUE( UIBindingValue::makeBool( true ).isSameValue( UIBindingValue::makeBool( true ) ) );
    SW_EXPECT_FALSE( UIBindingValue::makeBool( true ).isSameValue( UIBindingValue::makeBool( false ) ) );
    SW_EXPECT_TRUE( UIBindingValue::makeNumber( 0.1, false ).isSameValue( UIBindingValue::makeNumber( 0.1, false ) ) );
    SW_EXPECT_FALSE( UIBindingValue::makeNumber( 0.1, false ).isSameValue( UIBindingValue::makeNumber( 0.2, false ) ) );
    SW_EXPECT_FALSE( UIBindingValue::makeNumber( 0.0, false ).isSameValue( UIBindingValue::makeNumber( -0.0, false ) ) ); // 글이 "0" 과 "-0" 으로 다르다
    SW_EXPECT_TRUE( UIBindingValue::makeText( "a" ).isSameValue( UIBindingValue::makeText( "a" ) ) );
    SW_EXPECT_FALSE( UIBindingValue::makeText( "a" ).isSameValue( UIBindingValue::makeText( "b" ) ) );
    SW_EXPECT_FALSE( UIBindingValue::makeText( "1" ).isSameValue( UIBindingValue::makeNumber( 1.0, true ) ) );
    SW_EXPECT_TRUE( UIBindingValue{}.isSameValue( UIBindingValue{} ) );
}

/**
 * @brief [UIBindingValueKindTest] 종류는 내장 타입 표의 갈래를 따른다 — unorm8 · half · float32 는 숫자, half2 는 그 밖, bool · string 은 제 갈래
 */
SW_TEST_CASE( UIBindingValueKindTest, KindFollowsTheBuiltinClass )
{
    SW_EXPECT_TRUE( UIBindingValue::classifyType( ReflectBuiltinIndex::kunorm8 ) == UIBindingValueKind::Number );
    SW_EXPECT_TRUE( UIBindingValue::classifyType( ReflectBuiltinIndex::khalf ) == UIBindingValueKind::Number );
    SW_EXPECT_TRUE( UIBindingValue::classifyType( ReflectBuiltinIndex::khalf2 ) == UIBindingValueKind::Other );
    SW_EXPECT_TRUE( UIBindingValue::classifyType( ReflectBuiltinIndex::kfloat32 ) == UIBindingValueKind::Number );
    SW_EXPECT_TRUE( UIBindingValue::classifyType( ReflectBuiltinIndex::kbool ) == UIBindingValueKind::Bool );
    SW_EXPECT_TRUE( UIBindingValue::classifyType( ReflectBuiltinIndex::kstring ) == UIBindingValueKind::Text );
}

/**
 * @brief [UIBindingValueKindTest] 종류는 줄 차례가 아니라 줄마다의 갈래에서 나온다 — 표의 모든 줄에서 바인딩 종류가 그 줄의 갈래와 같고, Layout 이 num 인 줄은 숫자다
 * @details 숫자 줄(int8 .. float64)이 이어져 있다고 가정하던 때는 그 사이에 줄을 끼우면 숫자 바인딩이 엉뚱한 줄을 읽었다.
 */
SW_TEST_CASE( UIBindingValueKindTest, EveryRowKindComesFromItsOwnClass )
{
    for ( size_t index = 0; index < static_cast<size_t>( ReflectBuiltinIndex::kCount ); ++index )
    {
        const ReflectBuiltinIndex builtinIndex = static_cast<ReflectBuiltinIndex>( index );
        const hashed_string&      typeName     = ReflectedValueUtil::getBuiltinTypeName( builtinIndex );
        const UIBindingValueKind  kind         = UIBindingValue::classifyType( builtinIndex );
        const ReflectBuiltinClass builtinClass = ReflectedValueUtil::getBuiltinClass( builtinIndex );
        const bool                bSameKind    = ( builtinClass == ReflectBuiltinClass::Number ) == ( kind == UIBindingValueKind::Number ) &&
                               ( builtinClass == ReflectBuiltinClass::Bool ) == ( kind == UIBindingValueKind::Bool ) &&
                               ( builtinClass == ReflectBuiltinClass::Text ) == ( kind == UIBindingValueKind::Text );
        SW_EXPECT_TRUE_MSG( bSameKind, typeName.c_str() );
        if ( ReflectBuiltinTraitTable::get( builtinIndex )._layout == ReflectBuiltinLayout::Number )
            SW_EXPECT_TRUE_MSG( kind == UIBindingValueKind::Number, typeName.c_str() );
    }
}
