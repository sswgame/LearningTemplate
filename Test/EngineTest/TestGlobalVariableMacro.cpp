#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"

#include "TestFramework/TestFramework.h"

// 정의 매크로(`SW_GLOBAL_VARIABLE_*` · `SW_TEST_GLOBAL_VARIABLE_*`)는 정적 등록자를 전역 리스트에 매달고, 기동(테스트 main · `EngineLoop`)이
// 그 리스트를 프로세스 매니저(`engine::getGlobalVariableManager`)에 "Engine" 으로 등록한다. 매니저 자체의 동작은 CoreTest 의 `GlobalVariableTest` 가
// 지역 매니저로 본다.

SW_GLOBAL_VARIABLE_BOOL( gv_testBool, true, "Unit Test Bool Global Variable" );
SW_GLOBAL_VARIABLE_INT( gv_testInt, 60, "Unit Test Int32 Global Variable" );
SW_GLOBAL_VARIABLE_FLOAT( gv_testFloat, 45.0f, "Unit Test Float Global Variable" );
SW_GLOBAL_VARIABLE_STRING( gv_testString, "InitialValue", "Unit Test String Global Variable" );

SW_EXTERN_GLOBAL_VARIABLE_BOOL( gv_testBool );
SW_EXTERN_GLOBAL_VARIABLE_INT( gv_testInt );
SW_EXTERN_GLOBAL_VARIABLE_FLOAT( gv_testFloat );
SW_EXTERN_GLOBAL_VARIABLE_STRING( gv_testString );

// 테스트용 매크로 — 다섯 종류에 "Shipping 에서 빠짐 · 남음" 을 섞어 정의 · 참조의 두 선택 경로를 모두 컴파일한다.
enum class TestGlobalVariableMode : uint8
{
    First,
    Second
};

SW_TEST_GLOBAL_VARIABLE_BOOL( gv_testOnlyBool, true, "Test-only Bool (dropped in Shipping)" );
SW_TEST_GLOBAL_VARIABLE_INT( gv_testOnlyInt, 7, "Test-only Int32 (kept in Shipping)", SW_KEEP_IN_SHIPPING );
SW_TEST_GLOBAL_VARIABLE_FLOAT( gv_testOnlyFloat, 1.5f, "Test-only Float (dropped in Shipping)" );
SW_TEST_GLOBAL_VARIABLE_STRING( gv_testOnlyString, "Probe", "Test-only String (kept in Shipping)", SW_KEEP_IN_SHIPPING );
SW_TEST_GLOBAL_VARIABLE_ENUM( gv_testOnlyEnum, TestGlobalVariableMode, TestGlobalVariableMode::Second, "Test-only Enum (dropped in Shipping)" );

SW_EXTERN_TEST_GLOBAL_VARIABLE_BOOL( gv_testOnlyBool );
SW_EXTERN_TEST_GLOBAL_VARIABLE_INT( gv_testOnlyInt, SW_KEEP_IN_SHIPPING );
SW_EXTERN_TEST_GLOBAL_VARIABLE_FLOAT( gv_testOnlyFloat );
SW_EXTERN_TEST_GLOBAL_VARIABLE_STRING( gv_testOnlyString, SW_KEEP_IN_SHIPPING );
SW_EXTERN_TEST_GLOBAL_VARIABLE_ENUM( gv_testOnlyEnum, TestGlobalVariableMode );

// ------------------------------------------------------------------------------
// 1) GlobalVariableMacroTest — 정의 매크로 → 기동 등록 · 테스트용 표시 · Shipping 에서 빠짐
// ------------------------------------------------------------------------------
/**
 * @brief [GlobalVariableMacroTest] 정의 매크로로 만든 변수는 기동 때 프로세스 매니저에 등록된다
 */
SW_TEST_CASE( GlobalVariableMacroTest, DefinedVariablesAreRegisteredAtStartup )
{
    sw::GlobalVariableInfo* pBoolInfo = sw::engine::getGlobalVariableManager().findVariable( "gv_testBool" );
    SW_EXPECT_TRUE( pBoolInfo != nullptr );
    if ( pBoolInfo != nullptr )
    {
        SW_EXPECT_TRUE( pBoolInfo->_type == sw::GlobalVariableType::Boolean );
        SW_EXPECT_TRUE( pBoolInfo->getValueAsBool() );
        SW_EXPECT_EQUAL( sw::string( "Unit Test Bool Global Variable" ), pBoolInfo->_description );
        SW_EXPECT_TRUE( pBoolInfo->_pData == &gv_testBool );
    }

    sw::GlobalVariableInfo* pIntInfo = sw::engine::getGlobalVariableManager().findVariable( "gv_testInt" );
    SW_EXPECT_TRUE( pIntInfo != nullptr );
    if ( pIntInfo != nullptr )
    {
        SW_EXPECT_TRUE( pIntInfo->_type == sw::GlobalVariableType::Int32 );
        SW_EXPECT_EQUAL( 60, pIntInfo->getValueAsInt() );
        SW_EXPECT_TRUE( pIntInfo->_pData == &gv_testInt );
    }

    sw::GlobalVariableInfo* pFloatInfo = sw::engine::getGlobalVariableManager().findVariable( "gv_testFloat" );
    SW_EXPECT_TRUE( pFloatInfo != nullptr );
    if ( pFloatInfo != nullptr )
        SW_EXPECT_NEAR_EQUAL( 45.0f, pFloatInfo->getValueAsFloat(), 1e-4f );

    sw::GlobalVariableInfo* pStrInfo = sw::engine::getGlobalVariableManager().findVariable( "gv_testString" );
    SW_EXPECT_TRUE( pStrInfo != nullptr );
    if ( pStrInfo != nullptr )
        SW_EXPECT_EQUAL( sw::string( "InitialValue" ), pStrInfo->getValueAsString() );

    const uint32 varCount = sw::engine::getGlobalVariableManager().getVariableCount();
    SW_EXPECT_TRUE( varCount >= 4u );
}

/**
 * @brief [GlobalVariableMacroTest] 테스트용 매크로는 등록 정보에 표시를 남기고, Shipping 에서는 등록되지 않는다
 * @details 에디터 목록 · 프리셋이 이 표시(`_bTestOnly`)로 거른다. `SW_KEEP_IN_SHIPPING` 을 준 것만 배포 빌드에도 등록되고,
 *          나머지는 등록되지 않은 채 기본값으로 읽힌다.
 */
SW_TEST_CASE( GlobalVariableMacroTest, TestOnlyVariablesAreMarkedAndDroppedInShipping )
{
    sw::GlobalVariableManager& manager = sw::engine::getGlobalVariableManager();

    // 값은 어느 빌드에서나 읽힌다(Shipping 에서는 등록되지 않은 기본값이다).
    SW_EXPECT_TRUE( gv_testOnlyBool );
    SW_EXPECT_NEAR_EQUAL( 1.5f, gv_testOnlyFloat, 1e-6f );
    SW_EXPECT_TRUE( gv_testOnlyEnum == TestGlobalVariableMode::Second );

    // 남기는 것: 어느 빌드에서나 등록되고 테스트용으로 표시된다.
    const sw::GlobalVariableInfo* pKeptInt = manager.findVariable( "gv_testOnlyInt" );
    SW_ASSERT_TRUE( pKeptInt != nullptr );
    SW_EXPECT_TRUE( pKeptInt->_bTestOnly );
    SW_EXPECT_EQUAL( 7, pKeptInt->getValueAsInt() );

    const sw::GlobalVariableInfo* pKeptString = manager.findVariable( "gv_testOnlyString" );
    SW_ASSERT_TRUE( pKeptString != nullptr );
    SW_EXPECT_TRUE( pKeptString->_bTestOnly );
    SW_EXPECT_EQUAL( sw::string( "Probe" ), pKeptString->getValueAsString() );

    // 빠지는 것: 개발 빌드에서는 표시와 함께 등록되고, Shipping 에서는 등록 자체가 없다.
    for ( const utf8* pDroppedName : { "gv_testOnlyBool", "gv_testOnlyFloat", "gv_testOnlyEnum" } )
    {
        const sw::GlobalVariableInfo* pDropped = manager.findVariable( pDroppedName );
#if defined( SW_SHIPPING )
        SW_EXPECT_TRUE_MSG( pDropped == nullptr, pDroppedName );
#else
        SW_ASSERT_TRUE( pDropped != nullptr );
        SW_EXPECT_TRUE_MSG( pDropped->_bTestOnly, pDroppedName );
#endif
    }

    // 일반 매크로에는 표시가 없다.
    const sw::GlobalVariableInfo* pRuntime = manager.findVariable( "gv_testInt" );
    SW_ASSERT_TRUE( pRuntime != nullptr );
    SW_EXPECT_FALSE( pRuntime->_bTestOnly );
}
