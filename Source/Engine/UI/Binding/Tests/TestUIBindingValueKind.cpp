// UIBindingValueKind — UI 바인딩의 값 종류는 내장 타입 표의 갈래에서 나온다. 성분이 하나인 압축 타입은 숫자 바인딩이고 여럿이면 그 밖이다(nogpu).
#include "pch.h"

#include "Core/String/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/UI/Binding/UIBindingValue.h"

using namespace sw;

/**
 * @brief [UIBindingValueKindTest] 종류는 내장 타입 표의 갈래를 따른다 — unorm8 · half · float32 는 숫자, half2 는 그 밖, bool · string 은 제 갈래
 */
SW_TEST_CASE( UIBindingValueKindTest, KindFollowsTheBuiltinClass )
{
    SW_EXPECT_TRUE( UIBindingValueUtil::classifyType( hashed_string( "unorm8" ) ) == UIBindingValueKind::Number );
    SW_EXPECT_TRUE( UIBindingValueUtil::classifyType( hashed_string( "half" ) ) == UIBindingValueKind::Number );
    SW_EXPECT_TRUE( UIBindingValueUtil::classifyType( hashed_string( "half2" ) ) == UIBindingValueKind::Other );
    SW_EXPECT_TRUE( UIBindingValueUtil::classifyType( hashed_string( "float32" ) ) == UIBindingValueKind::Number );
    SW_EXPECT_TRUE( UIBindingValueUtil::classifyType( hashed_string( "bool" ) ) == UIBindingValueKind::Bool );
    SW_EXPECT_TRUE( UIBindingValueUtil::classifyType( hashed_string( "string" ) ) == UIBindingValueKind::Text );
}
