#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/RHIBackendType.h"

#include "TestFramework/TestFramework.h"

// 정의 매크로(`SW_GLOBAL_VARIABLE` · `SW_TEST_GLOBAL_VARIABLE`)는 정적 등록자를 전역 리스트에 매달고, 기동(테스트 main · `EngineLoop`)이
// 그 리스트를 프로세스 매니저(`engine::getGlobalVariableManager`)에 "Engine" 으로 등록한다. 매니저 자체의 동작은 CoreTest 의 `GlobalVariableTest` 가
// 지역 매니저로 본다.

enum class TestGlobalVariableMode : uint8
{
    First,
    Second
};

SW_GLOBAL_VARIABLE( bool, gv_testBool, true, "Unit Test Bool Global Variable" );
SW_GLOBAL_VARIABLE( int32, gv_testInt, 60, "Unit Test Int32 Global Variable" );
SW_GLOBAL_VARIABLE( float32, gv_testFloat, 45.0f, "Unit Test Float Global Variable" );
SW_GLOBAL_VARIABLE( sw::string, gv_testString, "InitialValue", "Unit Test String Global Variable" );
SW_GLOBAL_VARIABLE( TestGlobalVariableMode, gv_testEnum, TestGlobalVariableMode::Second, "Unit Test Enum Global Variable" );

// 참조 매크로는 정의의 종류와 상관없이 같은 `extern` 이다 — 일반 · 테스트용 하나씩 컴파일한다.
SW_EXTERN_GLOBAL_VARIABLE( sw::string, gv_testString );
SW_EXTERN_GLOBAL_VARIABLE( TestGlobalVariableMode, gv_testOnlyEnum );

// 테스트용 매크로 — 다섯 종류에 "Shipping 에서 빠짐 · 남음" 을 섞는다.
SW_TEST_GLOBAL_VARIABLE( bool, gv_testOnlyBool, true, "Test-only Bool (dropped in Shipping)" );
SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_testOnlyInt, 7, "Test-only Int32 (kept in Shipping)" );
SW_TEST_GLOBAL_VARIABLE( float32, gv_testOnlyFloat, 1.5f, "Test-only Float (dropped in Shipping)" );
SW_TEST_GLOBAL_VARIABLE_SHIPPED( sw::string, gv_testOnlyString, "Probe", "Test-only String (kept in Shipping)" );
SW_TEST_GLOBAL_VARIABLE( TestGlobalVariableMode, gv_testOnlyEnum, TestGlobalVariableMode::Second, "Test-only Enum (dropped in Shipping)" );

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
 * @brief [GlobalVariableMacroTest] 정의 매크로는 첫 인자(타입)에서 저장 타입 · enum 이름 · 크기를, 변수에서 기본값을 얻는다
 * @details enum 이름은 `#type` 이라 에디터 · 명령줄이 리플렉션 enum 표를 찾는 열쇠가 되고, 크기는 `writeEnumValue` 가 쓰는 바이트 수다.
 *          스칼라는 enum 이름을 남기지 않는다.
 */
SW_TEST_CASE( GlobalVariableMacroTest, DefinitionTakesStorageFromTypeAndDefaultFromVariable )
{
    sw::GlobalVariableManager& manager = sw::engine::getGlobalVariableManager();

    sw::GlobalVariableInfo* pEnumInfo = manager.findVariable( "gv_testEnum" );
    SW_ASSERT_NOT_NULL( pEnumInfo );
    SW_EXPECT_TRUE( pEnumInfo->_type == sw::GlobalVariableType::Enum );
    SW_EXPECT_EQUAL( sw::string( "TestGlobalVariableMode" ), pEnumInfo->_enumType );
    SW_EXPECT_EQUAL( 1u, pEnumInfo->_typeSize );
    SW_EXPECT_TRUE( pEnumInfo->_pData == &gv_testEnum );
    SW_EXPECT_TRUE( std::holds_alternative<int32>( pEnumInfo->_defaultValue ) );
    SW_EXPECT_EQUAL( static_cast<int32>( TestGlobalVariableMode::Second ), std::get<int32>( pEnumInfo->_defaultValue ) );

    // 쓰고 되돌리면 등록 때 변수에서 읽은 기본값으로 돌아온다. 1 바이트 enum 에 1 바이트만 쓴다.
    SW_EXPECT_TRUE( pEnumInfo->setValueAsInt( static_cast<int32>( TestGlobalVariableMode::First ) ) );
    SW_EXPECT_TRUE( gv_testEnum == TestGlobalVariableMode::First );
    pEnumInfo->resetToDefault();
    SW_EXPECT_TRUE( gv_testEnum == TestGlobalVariableMode::Second );

    sw::GlobalVariableInfo* pStringInfo = manager.findVariable( "gv_testString" );
    SW_ASSERT_NOT_NULL( pStringInfo );
    SW_EXPECT_TRUE( pStringInfo->_enumType.empty() );
    SW_EXPECT_TRUE( std::holds_alternative<sw::string>( pStringInfo->_defaultValue ) );
    SW_EXPECT_EQUAL( sw::string( "InitialValue" ), std::get<sw::string>( pStringInfo->_defaultValue ) );

    sw::GlobalVariableInfo* pFloatInfo = manager.findVariable( "gv_testFloat" );
    SW_ASSERT_NOT_NULL( pFloatInfo );
    SW_EXPECT_TRUE( pFloatInfo->_type == sw::GlobalVariableType::Float );
    SW_EXPECT_TRUE( pFloatInfo->_enumType.empty() );
    SW_EXPECT_TRUE( std::holds_alternative<float32>( pFloatInfo->_defaultValue ) );
}

/**
 * @brief [GlobalVariableMacroTest] 테스트용 매크로는 등록 정보에 표시를 남기고, Shipping 에서는 등록되지 않는다
 * @details 에디터 목록 · 프리셋이 이 표시(`_bTestOnly`)로 거른다. `SW_TEST_GLOBAL_VARIABLE_SHIPPED` 로 정의한 것만 배포 빌드에도 등록되고,
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

/**
 * @brief [GlobalVariableMacroTest] 엔진의 enum 전역 변수는 리플렉션 열거자 이름을 받는다
 * @details 기동이 리플렉션 enum 표를 파서로 건다(`engine::bindGlobalVariableEnumNames`). 걸리지 않으면 `-gv_rhiBackend=Vulkan` ·
 *          콘솔의 `gv_rhiBackend Vulkan` 이 숫자만 받아 거절된다.
 */
SW_TEST_CASE( GlobalVariableMacroTest, EngineEnumVariableTakesEnumeratorNames )
{
    sw::GlobalVariableInfo* pInfo = sw::engine::getGlobalVariableManager().findVariable( "gv_rhiBackend" );
    SW_ASSERT_NOT_NULL( pInfo );
    const int32 valueBefore = pInfo->getValueAsInt();
    SW_TEST_DEFER_CLEANUP( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [pInfo, valueBefore]()
    {
        (void)pInfo->setValueAsInt( valueBefore );
    } ) );

    SW_EXPECT_TRUE( pInfo->setValueFromString( "Vulkan" ) );
    SW_EXPECT_EQUAL( static_cast<int32>( sw::RHIBackend::Vulkan ), pInfo->getValueAsInt() );
    SW_EXPECT_TRUE( pInfo->setValueFromString( "DirectX11" ) );
    SW_EXPECT_EQUAL( static_cast<int32>( sw::RHIBackend::DirectX11 ), pInfo->getValueAsInt() );
    SW_EXPECT_FALSE( pInfo->setValueFromString( "NoSuchBackend" ) );
    SW_EXPECT_EQUAL( static_cast<int32>( sw::RHIBackend::DirectX11 ), pInfo->getValueAsInt() );
}
