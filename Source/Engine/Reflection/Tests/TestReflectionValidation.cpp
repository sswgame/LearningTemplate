#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/ReflectionValidation.h"
#include "Engine/Reflection/Tests/ReflectionTestTypes.h"

// ReflectionValidationTest — 검증 함수. PROPERTY( Validate = fn ) · REFLECT( Validate = fn ) 를 돌려 결과를 모으는 길.

namespace
{
    /** @brief 결과 중 메시지에 @p pText 가 든 것의 수입니다. */
    uint32 countIssues( const sw::vector<sw::ValidationIssue>& listIssue, const utf8* pText )
    {
        uint32 count = 0;
        for ( const sw::ValidationIssue& issue : listIssue )
        {
            if ( issue._message.find( pText ) != sw::string::npos )
                ++count;
        }
        return count;
    }
} // namespace

/**
 * @brief [ReflectionValidationTest] 프로퍼티 · 타입 검증이 돌고, 값 구조체와 그 시퀀스의 원소까지 내려간다 — 결과는 타입 · 프로퍼티 이름을 단다
 */
SW_TEST_CASE( ReflectionValidationTest, ValidatorsRunOnPropertiesTypesAndNestedValues )
{
    const sw::TypeInfo* pType = sw::ValidatedActor::StaticType();
    SW_REQUIRE_NOT_NULL( pType );
    SW_EXPECT_TRUE( sw::ReflectionValidation::hasValidator( *pType ) );

    sw::ValidatedActor clean;
    clean._listPart.resize( 2 );
    sw::ValidationContext cleanContext;
    SW_EXPECT_EQUAL( 0u, sw::ReflectionValidation::validateObject( *pType, &clean, cleanContext ) );

    sw::ValidatedActor broken;
    broken._name         = "";
    broken._min          = 20;
    broken._part._weight = -1.0f;
    broken._listPart.resize( 3 );
    broken._listPart[2]._weight = -5.0f;
    sw::ValidationContext context;
    context.setSource( 7, "BrokenActor" );
    SW_EXPECT_EQUAL( 4u, sw::ReflectionValidation::validateObject( *pType, &broken, context ) );
    SW_EXPECT_TRUE( context.hasError() );

    const sw::vector<sw::ValidationIssue>& listIssue = context.getIssues();
    SW_EXPECT_EQUAL( 1u, countIssues( listIssue, "name is empty" ) );
    SW_EXPECT_EQUAL( 2u, countIssues( listIssue, "weight is negative" ) ); // 값 구조체 하나 + 시퀀스 원소 하나
    SW_EXPECT_EQUAL( 1u, countIssues( listIssue, "min is greater than max" ) );
    for ( const sw::ValidationIssue& issue : listIssue )
    {
        SW_EXPECT_EQUAL( sw::string( "BrokenActor" ), issue._sourceLabel );
        if ( issue._message == "name is empty" )
        {
            SW_EXPECT_TRUE( issue._severity == sw::ValidationSeverity::Warning );
            SW_EXPECT_TRUE( issue._typeName == sw::hashed_string( "sw::ValidatedActor" ) );
            SW_EXPECT_TRUE( issue._propertyName == sw::hashed_string( "_name" ) );
        }
        if ( issue._message == "min is greater than max" )
            SW_EXPECT_TRUE( issue._propertyName.empty() );
    }

    // 검증 함수는 상속된다
    const sw::TypeInfo* pChild = sw::ValidatedChildActor::StaticType();
    SW_REQUIRE_NOT_NULL( pChild );
    sw::ValidatedChildActor child;
    child._min = 99;
    sw::ValidationContext childContext;
    SW_EXPECT_EQUAL( 1u, sw::ReflectionValidation::validateObject( *pChild, &child, childContext ) );

    // 검증 함수가 없는 타입은 돌 것이 없다
    const sw::TypeInfo* pPlain = sw::SampleTestActor::StaticType();
    SW_REQUIRE_NOT_NULL( pPlain );
    SW_EXPECT_FALSE( sw::ReflectionValidation::hasValidator( *pPlain ) );
}
