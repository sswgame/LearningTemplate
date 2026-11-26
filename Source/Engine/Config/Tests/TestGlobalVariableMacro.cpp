// 정의 매크로(`SW_TEST_GLOBAL_VARIABLE` · `SW_GLOBAL_VARIABLE`)는 정적 등록자를 전역 리스트에 매달고, 엔진 기동이
// 그 리스트를 프로세스 매니저(`engine::getSubsystem<GlobalVariableManager>()`)에 등록한다. 매니저 자체의 동작은 `GlobalVariableTest` 가 지역 매니저로 본다(nogpu).
// 이 파일의 변수는 Engine 이미지에 오르므로 테스트용 매크로로만 정의한다 — 일반 매크로로 두면 에디터 전역 변수 목록에 테스트 변수가 섞인다.
// 일반 매크로의 "표시 없음" 은 엔진의 `gv_rhiBackend` 로 본다.
#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Config/RHIBackendType.h"
#include "Engine/Foundation/EngineSubsystems.h"

enum class TestGlobalVariableMode : uint8
{
    First,
    Second
};

SW_TEST_GLOBAL_VARIABLE( bool, gv_testMacroBool, true, "Unit Test Bool Global Variable" );
SW_TEST_GLOBAL_VARIABLE( int32, gv_testMacroInt, 60, "Unit Test Int32 Global Variable" );
SW_TEST_GLOBAL_VARIABLE( float32, gv_testMacroFloat, 45.0f, "Unit Test Float Global Variable" );
SW_TEST_GLOBAL_VARIABLE( sw::GlobalString, gv_testMacroString, "InitialValue", "Unit Test String Global Variable" );
SW_TEST_GLOBAL_VARIABLE( TestGlobalVariableMode, gv_testMacroEnum, TestGlobalVariableMode::Second, "Unit Test Enum Global Variable" );

// 참조 매크로는 정의의 종류와 상관없이 같은 `extern` 이다.
SW_EXTERN_TEST_GLOBAL_VARIABLE( sw::GlobalString, gv_testMacroString );
SW_EXTERN_TEST_GLOBAL_VARIABLE( TestGlobalVariableMode, gv_testMacroEnum );

/**
 * @brief [GlobalVariableMacroTest] 정의 매크로로 만든 변수는 기동 때 프로세스 매니저에 등록되고, 테스트용 표시가 남는다
 */
SW_TEST_CASE( GlobalVariableMacroTest, DefinedVariablesAreRegisteredAtStartup )
{
    sw::GlobalVariableManager& manager = sw::engine::getSubsystem<sw::GlobalVariableManager>();

    sw::GlobalVariableInfo* pBoolInfo = manager.findVariable( "gv_testMacroBool" );
    SW_REQUIRE_NOT_NULL( pBoolInfo );
    SW_EXPECT_TRUE( pBoolInfo->_type == sw::GlobalVariableType::Boolean );
    SW_EXPECT_TRUE( pBoolInfo->getValueAsBool() );
    SW_EXPECT_EQUAL( sw::string( "Unit Test Bool Global Variable" ), pBoolInfo->_description );
    SW_EXPECT_TRUE( pBoolInfo->_pData == &gv_testMacroBool );
    SW_EXPECT_TRUE( pBoolInfo->_bTestOnly );

    sw::GlobalVariableInfo* pIntInfo = manager.findVariable( "gv_testMacroInt" );
    SW_REQUIRE_NOT_NULL( pIntInfo );
    SW_EXPECT_TRUE( pIntInfo->_type == sw::GlobalVariableType::Int32 );
    SW_EXPECT_EQUAL( 60, pIntInfo->getValueAsInt() );
    SW_EXPECT_TRUE( pIntInfo->_pData == &gv_testMacroInt );
    SW_EXPECT_TRUE( pIntInfo->_bTestOnly ); // Shipping 에도 남는 테스트용도 표시는 같다

    sw::GlobalVariableInfo* pFloatInfo = manager.findVariable( "gv_testMacroFloat" );
    SW_REQUIRE_NOT_NULL( pFloatInfo );
    SW_EXPECT_NEAR( 45.0f, pFloatInfo->getValueAsFloat(), 1e-4 );

    sw::GlobalVariableInfo* pStrInfo = manager.findVariable( "gv_testMacroString" );
    SW_REQUIRE_NOT_NULL( pStrInfo );
    SW_EXPECT_EQUAL( sw::string( "InitialValue" ), pStrInfo->getValueAsString() );

    // 일반 매크로(`SW_GLOBAL_VARIABLE`)에는 표시가 없다.
    const sw::GlobalVariableInfo* pRuntime = manager.findVariable( "gv_rhiBackend" );
    SW_REQUIRE_NOT_NULL( pRuntime );
    SW_EXPECT_FALSE( pRuntime->_bTestOnly );
}

/**
 * @brief [GlobalVariableMacroTest] 정의 매크로는 첫 인자(타입)에서 저장 타입 · enum 이름 · 크기를, 변수에서 기본값을 얻는다
 * @details enum 이름은 `#type` 이라 에디터 · 명령줄이 리플렉션 enum 표를 찾는 열쇠가 되고, 크기는 `writeEnumValue` 가 쓰는 바이트 수다.
 *          스칼라는 enum 이름을 남기지 않는다.
 */
SW_TEST_CASE( GlobalVariableMacroTest, DefinitionTakesStorageFromTypeAndDefaultFromVariable )
{
    sw::GlobalVariableManager& manager = sw::engine::getSubsystem<sw::GlobalVariableManager>();

    sw::GlobalVariableInfo* pEnumInfo = manager.findVariable( "gv_testMacroEnum" );
    SW_REQUIRE_NOT_NULL( pEnumInfo );
    SW_EXPECT_TRUE( pEnumInfo->_type == sw::GlobalVariableType::Enum );
    SW_EXPECT_EQUAL( sw::string( "TestGlobalVariableMode" ), pEnumInfo->_enumType );
    SW_EXPECT_EQUAL( 1u, pEnumInfo->_typeSize );
    SW_EXPECT_TRUE( pEnumInfo->_pData == &gv_testMacroEnum );
    SW_EXPECT_TRUE( std::holds_alternative<int32>( pEnumInfo->_defaultValue ) );
    SW_EXPECT_EQUAL( static_cast<int32>( TestGlobalVariableMode::Second ), std::get<int32>( pEnumInfo->_defaultValue ) );

    // 쓰고 되돌리면 등록 때 변수에서 읽은 기본값으로 돌아온다. 1 바이트 enum 에 1 바이트만 쓴다.
    SW_EXPECT_TRUE( pEnumInfo->setValueAsInt( static_cast<int32>( TestGlobalVariableMode::First ) ) );
    SW_EXPECT_TRUE( gv_testMacroEnum == TestGlobalVariableMode::First );
    pEnumInfo->resetToDefault();
    SW_EXPECT_TRUE( gv_testMacroEnum == TestGlobalVariableMode::Second );

    sw::GlobalVariableInfo* pStringInfo = manager.findVariable( "gv_testMacroString" );
    SW_REQUIRE_NOT_NULL( pStringInfo );
    SW_EXPECT_TRUE( pStringInfo->_enumType.empty() );
    SW_EXPECT_TRUE( std::holds_alternative<sw::string>( pStringInfo->_defaultValue ) );
    SW_EXPECT_EQUAL( sw::string( "InitialValue" ), std::get<sw::string>( pStringInfo->_defaultValue ) );

    sw::GlobalVariableInfo* pFloatInfo = manager.findVariable( "gv_testMacroFloat" );
    SW_REQUIRE_NOT_NULL( pFloatInfo );
    SW_EXPECT_TRUE( pFloatInfo->_type == sw::GlobalVariableType::Float );
    SW_EXPECT_TRUE( pFloatInfo->_enumType.empty() );
    SW_EXPECT_TRUE( std::holds_alternative<float32>( pFloatInfo->_defaultValue ) );
}

namespace
{
    /** @brief 엔진 리플렉션 enum(`RHIBackend`)을 형식으로 둔 케이스 전용 변수의 저장소입니다 — 실행 중인 `gv_rhiBackend` 는 교체기가 변경 콜백을 걸어 두어 건드리지 않는다. */
    sw::RHIBackend s_testEngineEnumVariable = sw::RHIBackend::DirectX12;
    /** @brief 변경 콜백이 불린 횟수입니다. */
    int32 s_testEngineEnumChangedCount = 0;

    void onTestEngineEnumChanged( sw::GlobalVariableInfo* )
    {
        ++s_testEngineEnumChangedCount;
    }

    constexpr const utf8* kTestEngineEnumVariable = "gv_testEngineEnumVariable";
    constexpr const utf8* kTestEngineEnumModule   = "GlobalVariableMacroTest";
} // namespace

/**
 * @brief [GlobalVariableMacroTest] 엔진의 enum 전역 변수는 리플렉션 열거자 이름을 받는다
 * @details 기동이 리플렉션 enum 표를 파서로 건다(`engine::bindGlobalVariableEnumNames`). 걸리지 않으면 `-gv_rhiBackend=Vulkan` ·
 *          콘솔의 `gv_rhiBackend Vulkan` 이 숫자만 받아 거절된다. 같은 enum 형식(`RHIBackend`)의 케이스 전용 변수로 본다.
 */
SW_TEST_CASE( GlobalVariableMacroTest, EngineEnumVariableTakesEnumeratorNames )
{
    sw::GlobalVariableManager& manager = sw::engine::getSubsystem<sw::GlobalVariableManager>();
    SW_REQUIRE_TRUE( manager.registerVariable( kTestEngineEnumVariable, sw::GlobalVariableType::Enum, &s_testEngineEnumVariable, static_cast<int32>( sw::RHIBackend::DirectX12 ),
                                               "engine enum test variable", "RHIBackend", kTestEngineEnumModule, static_cast<uint32>( sizeof( sw::RHIBackend ) ), true ) );
    sw::GlobalVariableInfo* pInfo = manager.findVariable( kTestEngineEnumVariable );
    SW_EXPECT_NOT_NULL( pInfo );
    if ( pInfo != nullptr )
    {
        SW_EXPECT_TRUE( pInfo->setValueFromString( "Vulkan" ) );
        SW_EXPECT_EQUAL( static_cast<int32>( sw::RHIBackend::Vulkan ), pInfo->getValueAsInt() );
        SW_EXPECT_TRUE( pInfo->setValueFromString( "DirectX11" ) );
        SW_EXPECT_EQUAL( static_cast<int32>( sw::RHIBackend::DirectX11 ), pInfo->getValueAsInt() );
        SW_EXPECT_FALSE( pInfo->setValueFromString( "NoSuchBackend" ) );
        SW_EXPECT_EQUAL( static_cast<int32>( sw::RHIBackend::DirectX11 ), pInfo->getValueAsInt() );
    }
    manager.unregisterVariablesByModule( kTestEngineEnumModule );
}

/**
 * @brief [GlobalVariableMacroTest] 변경 콜백은 구독이 쥐고, 콜백 없는 쓰기(`setValueAsIntWithoutNotify`)는 값만 바꾼다 — 변경 콜백 안의 되돌리기가 같은 콜백을 다시 부르지 않는 근거다
 * @details 구독이 풀리면 칸이 비어 다음 쓰기는 콜백을 부르지 않는다. 변수가 먼저 내려가도(모듈 내리기) 구독은 무효가 될 뿐이다.
 */
SW_TEST_CASE( GlobalVariableMacroTest, ChangeHookIsASubscriptionAndSilentWriteSkipsIt )
{
    sw::GlobalVariableManager& manager = sw::engine::getSubsystem<sw::GlobalVariableManager>();
    SW_REQUIRE_TRUE( manager.registerVariable( kTestEngineEnumVariable, sw::GlobalVariableType::Enum, &s_testEngineEnumVariable, static_cast<int32>( sw::RHIBackend::DirectX12 ),
                                               "engine enum test variable", "RHIBackend", kTestEngineEnumModule, static_cast<uint32>( sizeof( sw::RHIBackend ) ), true ) );
    sw::GlobalVariableInfo* pInfo = manager.findVariable( kTestEngineEnumVariable );
    SW_EXPECT_NOT_NULL( pInfo );
    if ( pInfo != nullptr )
    {
        s_testEngineEnumChangedCount  = 0;
        sw::Subscription subscription = pInfo->bindOnValueChanged( SW_DELEGATE_FUNCTION( sw::GlobalVariableChangedDelegate, &onTestEngineEnumChanged ) );
        SW_EXPECT_TRUE( pInfo->setValueAsInt( static_cast<int32>( sw::RHIBackend::Vulkan ) ) );
        SW_EXPECT_EQUAL( 1, s_testEngineEnumChangedCount );
        SW_EXPECT_TRUE( pInfo->setValueAsIntWithoutNotify( static_cast<int32>( sw::RHIBackend::DirectX11 ) ) );
        SW_EXPECT_TRUE( s_testEngineEnumVariable == sw::RHIBackend::DirectX11 );
        SW_EXPECT_EQUAL( 1, s_testEngineEnumChangedCount ); // 콜백 없는 쓰기
        subscription.reset();
        SW_EXPECT_TRUE( pInfo->setValueAsInt( static_cast<int32>( sw::RHIBackend::Vulkan ) ) );
        SW_EXPECT_EQUAL( 1, s_testEngineEnumChangedCount ); // 풀린 구독은 부르지 않는다

        // 변수가 먼저 내려가면 남은 구독은 무효가 될 뿐이다.
        subscription = pInfo->bindOnValueChanged( SW_DELEGATE_FUNCTION( sw::GlobalVariableChangedDelegate, &onTestEngineEnumChanged ) );
        manager.unregisterVariablesByModule( kTestEngineEnumModule );
        SW_EXPECT_FALSE( subscription.isActive() );
    }
    manager.unregisterVariablesByModule( kTestEngineEnumModule );
}
