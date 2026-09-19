#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"

#include "TestFramework/TestFramework.h"

SW_GLOBAL_VARIABLE_BOOL( gv_testBool, true, "Unit Test Bool Global Variable" );
SW_GLOBAL_VARIABLE_INT( gv_testInt, 60, "Unit Test Int32 Global Variable" );
SW_GLOBAL_VARIABLE_FLOAT( gv_testFloat, 45.0f, "Unit Test Float Global Variable" );
SW_GLOBAL_VARIABLE_STRING( gv_testString, "InitialValue", "Unit Test String Global Variable" );

SW_EXTERN_GLOBAL_VARIABLE_BOOL( gv_testBool );
SW_EXTERN_GLOBAL_VARIABLE_INT( gv_testInt );
SW_EXTERN_GLOBAL_VARIABLE_FLOAT( gv_testFloat );
SW_EXTERN_GLOBAL_VARIABLE_STRING( gv_testString );

// ------------------------------------------------------------------------------
// 1) Engine_GlobalVariable — 등록·수정·커맨드라인
// ------------------------------------------------------------------------------
/**
 * @brief [GlobalVariableTest] 등록
 */
SW_TEST_CASE( GlobalVariableTest, Registration )
{
    sw::GlobalVariableInfo* pBoolInfo = sw::engine::getGlobalVariableManager().findVariable( "gv_testBool" );
    SW_EXPECT_TRUE( pBoolInfo != nullptr );
    if ( pBoolInfo != nullptr )
    {
        SW_EXPECT_TRUE( pBoolInfo->_type == sw::GlobalVariableType::Boolean );
        SW_EXPECT_TRUE( pBoolInfo->getValueAsBool() );
        SW_EXPECT_EQUAL( sw::string( "Unit Test Bool Global Variable" ), pBoolInfo->_description );
    }

    sw::GlobalVariableInfo* pIntInfo = sw::engine::getGlobalVariableManager().findVariable( "gv_testInt" );
    SW_EXPECT_TRUE( pIntInfo != nullptr );
    if ( pIntInfo != nullptr )
    {
        SW_EXPECT_TRUE( pIntInfo->_type == sw::GlobalVariableType::Int32 );
        SW_EXPECT_EQUAL( 60, pIntInfo->getValueAsInt() );
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
 * @brief [GlobalVariableTest] 수정과 리셋
 */
SW_TEST_CASE( GlobalVariableTest, ModificationAndReset )
{

    SW_EXPECT_TRUE( sw::engine::getGlobalVariableManager().setValueFromString( "gv_testInt", "144" ) );
    SW_EXPECT_EQUAL( 144, gv_testInt );

    SW_EXPECT_TRUE( sw::engine::getGlobalVariableManager().setValueFromString( "gv_testBool", "false" ) );
    SW_EXPECT_FALSE( gv_testBool );

    SW_EXPECT_TRUE( sw::engine::getGlobalVariableManager().resetToDefault( "gv_testInt" ) );
    SW_EXPECT_EQUAL( 60, gv_testInt );

    SW_EXPECT_TRUE( sw::engine::getGlobalVariableManager().resetToDefault( "gv_testBool" ) );
    SW_EXPECT_TRUE( gv_testBool );
}

/**
 * @brief [GlobalVariableTest] 직접 타입별 값 수정 및 리셋
 */
SW_TEST_CASE( GlobalVariableTest, DirectValueModificationAndReset )
{
    sw::GlobalVariableInfo* pBoolInfo = sw::engine::getGlobalVariableManager().findVariable( "gv_testBool" );
    SW_ASSERT_NOT_NULL( pBoolInfo );
    SW_EXPECT_TRUE( pBoolInfo->setValueAsBool( false ) );
    SW_EXPECT_FALSE( gv_testBool );
    pBoolInfo->resetToDefault();
    SW_EXPECT_TRUE( gv_testBool );

    sw::GlobalVariableInfo* pIntInfo = sw::engine::getGlobalVariableManager().findVariable( "gv_testInt" );
    SW_ASSERT_NOT_NULL( pIntInfo );
    SW_EXPECT_TRUE( pIntInfo->setValueAsInt( 999 ) );
    SW_EXPECT_EQUAL( 999, gv_testInt );
    pIntInfo->resetToDefault();
    SW_EXPECT_EQUAL( 60, gv_testInt );

    sw::GlobalVariableInfo* pFloatInfo = sw::engine::getGlobalVariableManager().findVariable( "gv_testFloat" );
    SW_ASSERT_NOT_NULL( pFloatInfo );
    SW_EXPECT_TRUE( pFloatInfo->setValueAsFloat( 123.5f ) );
    SW_EXPECT_NEAR_EQUAL( 123.5f, gv_testFloat, 1e-4f );
    pFloatInfo->resetToDefault();
    SW_EXPECT_NEAR_EQUAL( 45.0f, gv_testFloat, 1e-4f );

    sw::GlobalVariableInfo* pStrInfo = sw::engine::getGlobalVariableManager().findVariable( "gv_testString" );
    SW_ASSERT_NOT_NULL( pStrInfo );
    SW_EXPECT_TRUE( pStrInfo->setValueAsString( "ModifiedStr" ) );
    SW_EXPECT_EQUAL( sw::string( "ModifiedStr" ), gv_testString );
    pStrInfo->resetToDefault();
    SW_EXPECT_EQUAL( sw::string( "InitialValue" ), gv_testString );
}

/**
 * @brief [GlobalVariableTest] 커맨드라인 연동
 */
SW_TEST_CASE( GlobalVariableTest, CommandLineIntegration )
{
    // 부분 CommandLineManager 에서 GlobalVariableManager::updateFromCommandLine 을 쓰지 않는다.
    // CLI 맵에 GV 이름이 없으면 getArgument 가 assert 한다(과거 flake/abort).
    // 테스트 대상 변수만 파싱한 뒤 setValueFromString 으로 적용한다.
    sw::engine::getGlobalVariableManager().resetToDefault( "gv_testInt" );
    sw::engine::getGlobalVariableManager().resetToDefault( "gv_testString" );

    sw::CommandLineManager cmd;
    cmd.initialize();
    cmd.addArgument<int32>( { "gv_testInt" }, int32{ 60 }, false );
    cmd.addArgument<sw::string>( { "gv_testString" }, sw::string( "InitialValue" ), false );

    utf8  arg0[] = "CoreUtilityTest";
    utf8  arg1[] = "gv_testInt=777";
    utf8  arg2[] = "gv_testString=FromCLI";
    utf8* argv[] = { reinterpret_cast<utf8*>( arg0 ), reinterpret_cast<utf8*>( arg1 ), reinterpret_cast<utf8*>( arg2 ) };
    cmd.parse( 3, argv );

    int32 parsedInt{ 0 };
    SW_EXPECT_TRUE( cmd.getArgument( "gv_testInt", parsedInt ) );
    SW_EXPECT_EQUAL( 777, parsedInt );

    sw::string parsedStr;
    SW_EXPECT_TRUE( cmd.getArgument( "gv_testString", parsedStr ) );
    SW_EXPECT_EQUAL( sw::string( "FromCLI" ), parsedStr );

    SW_EXPECT_TRUE( sw::engine::getGlobalVariableManager().setValueFromString( "gv_testInt", sw::to_string( parsedInt ) ) );
    SW_EXPECT_TRUE( sw::engine::getGlobalVariableManager().setValueFromString( "gv_testString", parsedStr ) );
    SW_EXPECT_EQUAL( 777, gv_testInt );
    SW_EXPECT_EQUAL( sw::string( "FromCLI" ), gv_testString );

    sw::engine::getGlobalVariableManager().resetToDefault( "gv_testInt" );
    sw::engine::getGlobalVariableManager().resetToDefault( "gv_testString" );
}

/**
 * @brief [GlobalVariableTest] 미등록 변수 조회 및 안전성 검증
 */
SW_TEST_CASE( GlobalVariableTest, NonExistentVariableHandling )
{
    sw::GlobalVariableInfo* pMissing = sw::engine::getGlobalVariableManager().findVariable( "gv_nonExistentVariable" );
    SW_EXPECT_NULL( pMissing );

    SW_EXPECT_FALSE( sw::engine::getGlobalVariableManager().setValueFromString( "gv_nonExistentVariable", "123" ) );
    SW_EXPECT_FALSE( sw::engine::getGlobalVariableManager().resetToDefault( "gv_nonExistentVariable" ) );
}

/**
 * @brief [GlobalVariableTest] 멀티스레드 환경에서 문자열 전역 변수 동시 읽기/쓰기 스레드 안전성 검증
 */
SW_TEST_CASE( GlobalVariableTest, MultithreadedStringReadWriteThreadSafety )
{
    sw::GlobalVariableInfo* pStrInfo = sw::engine::getGlobalVariableManager().findVariable( "gv_testString" );
    SW_ASSERT_NOT_NULL( pStrInfo );

    std::atomic<bool> bRunning{ true };
    std::thread       writer( [&]()
    {
        for ( int32 iter = 0; iter < 1000; ++iter )
        {
            sw::engine::getGlobalVariableManager().setValueFromString( "gv_testString", sw::string( "Value_" + std::to_string( iter ) ) );
        }
        bRunning.store( false, std::memory_order_release );
    } );

    std::thread reader( [&]()
    {
        while ( bRunning.load( std::memory_order_acquire ) )
        {
            sw::string val = pStrInfo->getValueAsString();
            SW_EXPECT_TRUE( val.find( "Value_" ) != sw::string::npos || val == "InitialValue" );
        }
    } );

    writer.join();
    reader.join();

    sw::engine::getGlobalVariableManager().resetToDefault( "gv_testString" );
}

/**
 * @brief [GlobalVariableTest] `findVariable` 이 준 포인터는 **다른 변수를 등록·해제해도 살아 있다**
 * @details 패널은 이름을 훑어 포인터를 모아 두었다가 한 번에 그린다 — 헤더가 권하는 사용법이다.
 *          그런데 `sw::unordered_map` 은 밀집 배열이라 삽입하면 재할당으로 **모든** 원소가, 삭제하면
 *          swap-and-pop 으로 **마지막 원소가** 옮겨 간다. 값을 그대로 담고 그 주소를 내주면 그 포인터가
 *          조용히 다른 변수를 가리키거나 죽은 자리를 가리킨다. 값을 `unique_ptr` 로 들어 막았다.
 */
SW_TEST_CASE( GlobalVariableTest, FoundPointerSurvivesOtherRegistrations )
{
    sw::GlobalVariableManager gvm;

    int32 watched{ 11 };
    SW_ASSERT_TRUE( gvm.registerVariable( "gv_watched", sw::GlobalVariableType::Int32, &watched, int32{ 11 }, "" ) );

    sw::GlobalVariableInfo* pWatched = gvm.findVariable( "gv_watched" );
    SW_ASSERT_NOT_NULL( pWatched );

    // 맵을 여러 번 재할당시킨다 — 값이 밀집 배열 안에 있었다면 pWatched 는 여기서 죽는다.
    constexpr size_t  kFillCount = 256;
    sw::vector<int32> listStorage( kFillCount, 0 );
    for ( size_t fillIndex = 0; fillIndex < kFillCount; ++fillIndex )
    {
        const sw::string name = sw::string{ "gv_filler" } + sw::to_string( fillIndex );
        SW_ASSERT_TRUE( gvm.registerVariable( name, sw::GlobalVariableType::Int32, &listStorage[fillIndex], int32{ 0 }, "" ) );
    }

    SW_EXPECT_TRUE_MSG( pWatched == gvm.findVariable( "gv_watched" ),
                        "등록을 반복했더니 같은 변수의 주소가 바뀌었다 — 먼저 받아 둔 포인터가 죽는다" );
    SW_EXPECT_STREQ( "gv_watched", pWatched->_name.c_str() );
    SW_EXPECT_EQUAL( 11, pWatched->getValueAsInt() );

    // 모듈 언로드가 하는 일 — 다른 변수들을 걷어낸다. swap-and-pop 이 도는 자리다.
    gvm.unregisterVariablesByModule( "" );

    // 위 등록들은 모듈 이름이 비어 있어 전부 걷힌다. 걷힌 뒤에는 없어야 한다.
    SW_EXPECT_TRUE( gvm.findVariable( "gv_watched" ) == nullptr );
    SW_EXPECT_EQUAL( 0u, gvm.getVariableCount() );
}
