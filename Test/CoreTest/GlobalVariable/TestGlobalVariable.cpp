#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Time/MonotonicClock.h"

#include "TestFramework/TestFramework.h"

namespace
{
    /**
     * @brief 지역 `GlobalVariableManager` 하나에 네 종류(불리언 · 정수 · 실수 · 문자열) 변수를 등록한 시험 바탕입니다.
     * @details 프로세스의 매니저(Engine 이 든다)를 쓰지 않으므로 시험끼리 값을 나누지 않습니다. 정의 매크로가 정적 등록자를 거쳐 기동 때
     *          등록되는 경로는 EngineTest 의 `GlobalVariableMacroTest` 가 봅니다.
     */
    struct GlobalVariableFixture
    {
        sw::GlobalVariableManager _manager;
        sw::string                _stringValue;
        float32                   _floatValue;
        int32                     _intValue;
        bool                      _bValue;
        bool                      _bAllRegistered;

        /** @brief 기본값으로 네 변수를 등록합니다. */
        GlobalVariableFixture()
            : _manager{}
            , _stringValue{ "InitialValue" }
            , _floatValue{ 45.0f }
            , _intValue{ 60 }
            , _bValue{ true }
            , _bAllRegistered{ false }
        {
            const bool bBoolRegistered   = _manager.registerVariable( "gv_testBool", sw::GlobalVariableType::Boolean, &_bValue, true, "Unit Test Bool Global Variable" );
            const bool bIntRegistered    = _manager.registerVariable( "gv_testInt", sw::GlobalVariableType::Int32, &_intValue, int32{ 60 }, "Unit Test Int32 Global Variable" );
            const bool bFloatRegistered  = _manager.registerVariable( "gv_testFloat", sw::GlobalVariableType::Float, &_floatValue, 45.0f, "Unit Test Float Global Variable" );
            const bool bStringRegistered = _manager.registerVariable( "gv_testString", sw::GlobalVariableType::String, &_stringValue, sw::string{ "InitialValue" }, "Unit Test String Global Variable" );
            _bAllRegistered              = bBoolRegistered && bIntRegistered && bFloatRegistered && bStringRegistered;
        }
    };
} // namespace

// ------------------------------------------------------------------------------
// 1) GlobalVariableTest — 등록 정보 · 수정 · 커맨드라인 (지역 매니저)
// ------------------------------------------------------------------------------
/**
 * @brief [GlobalVariableTest] 등록하면 타입 · 설명 · 현재 값을 그대로 돌려준다
 */
SW_TEST_CASE( GlobalVariableTest, Registration )
{
    GlobalVariableFixture fixture;
    SW_ASSERT_TRUE( fixture._bAllRegistered );

    sw::GlobalVariableInfo* pBoolInfo = fixture._manager.findVariable( "gv_testBool" );
    SW_ASSERT_NOT_NULL( pBoolInfo );
    SW_EXPECT_TRUE( pBoolInfo->_type == sw::GlobalVariableType::Boolean );
    SW_EXPECT_TRUE( pBoolInfo->getValueAsBool() );
    SW_EXPECT_EQUAL( sw::string( "Unit Test Bool Global Variable" ), pBoolInfo->_description );
    SW_EXPECT_FALSE( pBoolInfo->_bTestOnly );

    sw::GlobalVariableInfo* pIntInfo = fixture._manager.findVariable( "gv_testInt" );
    SW_ASSERT_NOT_NULL( pIntInfo );
    SW_EXPECT_TRUE( pIntInfo->_type == sw::GlobalVariableType::Int32 );
    SW_EXPECT_EQUAL( 60, pIntInfo->getValueAsInt() );

    sw::GlobalVariableInfo* pFloatInfo = fixture._manager.findVariable( "gv_testFloat" );
    SW_ASSERT_NOT_NULL( pFloatInfo );
    SW_EXPECT_NEAR_EQUAL( 45.0f, pFloatInfo->getValueAsFloat(), 1e-4f );

    sw::GlobalVariableInfo* pStrInfo = fixture._manager.findVariable( "gv_testString" );
    SW_ASSERT_NOT_NULL( pStrInfo );
    SW_EXPECT_EQUAL( sw::string( "InitialValue" ), pStrInfo->getValueAsString() );

    SW_EXPECT_EQUAL( 4u, fixture._manager.getVariableCount() );
    SW_EXPECT_FALSE( fixture._manager.registerVariable( "gv_testInt", sw::GlobalVariableType::Int32, &fixture._intValue, int32{ 0 }, "duplicate" ) );
}

/**
 * @brief [GlobalVariableTest] 수정과 리셋
 */
SW_TEST_CASE( GlobalVariableTest, ModificationAndReset )
{
    GlobalVariableFixture fixture;
    SW_EXPECT_TRUE( fixture._manager.setValueFromString( "gv_testInt", "144" ) );
    SW_EXPECT_EQUAL( 144, fixture._intValue );

    SW_EXPECT_TRUE( fixture._manager.setValueFromString( "gv_testBool", "false" ) );
    SW_EXPECT_FALSE( fixture._bValue );

    SW_EXPECT_TRUE( fixture._manager.resetToDefault( "gv_testInt" ) );
    SW_EXPECT_EQUAL( 60, fixture._intValue );

    SW_EXPECT_TRUE( fixture._manager.resetToDefault( "gv_testBool" ) );
    SW_EXPECT_TRUE( fixture._bValue );
}

/**
 * @brief [GlobalVariableTest] 불리언이 아닌 글은 불리언 변수를 바꾸지 않는다
 * @details 불리언도 정수 · 실수처럼 읽지 못하면 실패를 돌려주고 값은 그대로다(명령줄 적용은 실패를 경고한다). `parseBool( text, false )` 로
 *          무엇이든 받으면 `-gv_x=ture` · 프리셋의 "enabled" 가 조용히 그 변수를 끈다.
 */
SW_TEST_CASE( GlobalVariableTest, BooleanTextThatIsNotABooleanIsRefused )
{
    GlobalVariableFixture      fixture;
    sw::GlobalVariableManager& manager = fixture._manager;
    SW_ASSERT_TRUE( manager.resetToDefault( "gv_testBool" ) );
    SW_EXPECT_FALSE( manager.setValueFromString( "gv_testBool", "ture" ) );
    SW_EXPECT_TRUE( fixture._bValue );
    SW_EXPECT_TRUE( manager.setValueFromString( "gv_testBool", "Off" ) );
    SW_EXPECT_FALSE( fixture._bValue );
    SW_EXPECT_TRUE( manager.resetToDefault( "gv_testBool" ) );
}

/**
 * @brief [GlobalVariableTest] 직접 타입별 값 수정 및 리셋
 */
SW_TEST_CASE( GlobalVariableTest, DirectValueModificationAndReset )
{
    GlobalVariableFixture   fixture;
    sw::GlobalVariableInfo* pBoolInfo = fixture._manager.findVariable( "gv_testBool" );
    SW_ASSERT_NOT_NULL( pBoolInfo );
    SW_EXPECT_TRUE( pBoolInfo->setValueAsBool( false ) );
    SW_EXPECT_FALSE( fixture._bValue );
    pBoolInfo->resetToDefault();
    SW_EXPECT_TRUE( fixture._bValue );

    sw::GlobalVariableInfo* pIntInfo = fixture._manager.findVariable( "gv_testInt" );
    SW_ASSERT_NOT_NULL( pIntInfo );
    SW_EXPECT_TRUE( pIntInfo->setValueAsInt( 999 ) );
    SW_EXPECT_EQUAL( 999, fixture._intValue );
    pIntInfo->resetToDefault();
    SW_EXPECT_EQUAL( 60, fixture._intValue );

    sw::GlobalVariableInfo* pFloatInfo = fixture._manager.findVariable( "gv_testFloat" );
    SW_ASSERT_NOT_NULL( pFloatInfo );
    SW_EXPECT_TRUE( pFloatInfo->setValueAsFloat( 123.5f ) );
    SW_EXPECT_NEAR_EQUAL( 123.5f, fixture._floatValue, 1e-4f );
    pFloatInfo->resetToDefault();
    SW_EXPECT_NEAR_EQUAL( 45.0f, fixture._floatValue, 1e-4f );

    sw::GlobalVariableInfo* pStrInfo = fixture._manager.findVariable( "gv_testString" );
    SW_ASSERT_NOT_NULL( pStrInfo );
    SW_EXPECT_TRUE( pStrInfo->setValueAsString( "ModifiedStr" ) );
    SW_EXPECT_EQUAL( sw::string( "ModifiedStr" ), fixture._stringValue );
    pStrInfo->resetToDefault();
    SW_EXPECT_EQUAL( sw::string( "InitialValue" ), fixture._stringValue );
}

/**
 * @brief [GlobalVariableTest] 커맨드라인 연동
 */
SW_TEST_CASE( GlobalVariableTest, CommandLineIntegration )
{
    GlobalVariableFixture fixture;
    // 부분 CommandLineManager 에서 GlobalVariableManager::updateFromCommandLine 을 쓰지 않는다.
    // CLI 맵에 GV 이름이 없으면 getArgument 가 assert 한다.
    // 테스트 대상 변수만 파싱한 뒤 setValueFromString 으로 적용한다.
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

    SW_EXPECT_TRUE( fixture._manager.setValueFromString( "gv_testInt", sw::to_string( parsedInt ) ) );
    SW_EXPECT_TRUE( fixture._manager.setValueFromString( "gv_testString", parsedStr ) );
    SW_EXPECT_EQUAL( 777, fixture._intValue );
    SW_EXPECT_EQUAL( sw::string( "FromCLI" ), fixture._stringValue );
}

/**
 * @brief [GlobalVariableTest] enum 변수는 명령줄에서 열거자 이름을 받고, 모르는 이름이면 적용을 실패로 알린다
 * @details 명령줄은 리플렉션보다 먼저 파싱되므로 이름은 받아 두었다가 파서가 걸린 뒤 적용한다(`applyPendingEnumText`). 숫자만 받으면
 *          `-gv_rhiBackend=Vulkan` 이 경고 한 줄과 함께 기본 백엔드로 돈다.
 */
SW_TEST_CASE( GlobalVariableTest, EnumTakesEnumeratorNamesFromTheCommandLine )
{
    SW_TEST_DEFENSIVE_SCOPE( "an unknown enumerator name on the command line is reported" );
    sw::GlobalVariableManager manager;
    int32                     namedValue{ 0 };
    int32                     unknownValue{ 0 };
    SW_ASSERT_TRUE( manager.registerVariable( "gv_testEnumByName", sw::GlobalVariableType::Enum, &namedValue, int32{ 0 }, "", "TestMode" ) );
    SW_ASSERT_TRUE( manager.registerVariable( "gv_testEnumUnknown", sw::GlobalVariableType::Enum, &unknownValue, int32{ 0 }, "", "TestMode" ) );

    sw::CommandLineManager cmd;
    cmd.initialize();
    manager.registerToCommandLine( &cmd );
    utf8  arg0[] = "CoreTest";
    utf8  arg1[] = "gv_testEnumByName=Second";
    utf8  arg2[] = "gv_testEnumUnknown=Bogus";
    utf8* argv[] = { arg0, arg1, arg2 };
    cmd.parse( 3, argv );
    manager.updateFromCommandLine( &cmd );
    SW_EXPECT_EQUAL( 0, namedValue ); // 파서가 걸리기 전에는 이름을 적용하지 않는다

    sw::GlobalVariableManager::setEnumTextParser( SW_DELEGATE_LAMBDA( sw::GlobalVariableEnumTextParser, []( sw::string_view enumType, sw::string_view text, int32& outValue ) -> bool
    {
        if ( enumType != "TestMode" || text != "Second" )
            return false;
        outValue = 2;
        return true;
    } ) );
    SW_TEST_DEFER_CLEANUP( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, []()
    {
        sw::GlobalVariableManager::setEnumTextParser( sw::GlobalVariableEnumTextParser{} );
    } ) );

    SW_EXPECT_FALSE( manager.applyPendingEnumText() ); // Bogus
    SW_EXPECT_EQUAL( 2, namedValue );
    SW_EXPECT_EQUAL( 0, unknownValue );
    SW_EXPECT_TRUE( manager.applyPendingEnumText() ); // 한 번 적용하면 비운다

    // 콘솔 · 에디터 입력도 같은 이름과 숫자를 받는다.
    SW_EXPECT_TRUE( manager.setValueFromString( "gv_testEnumUnknown", "Second" ) );
    SW_EXPECT_EQUAL( 2, unknownValue );
    SW_EXPECT_TRUE( manager.setValueFromString( "gv_testEnumUnknown", "1" ) );
    SW_EXPECT_EQUAL( 1, unknownValue );
    SW_EXPECT_FALSE( manager.setValueFromString( "gv_testEnumUnknown", "Bogus" ) );
    SW_EXPECT_EQUAL( 1, unknownValue );
}

/**
 * @brief [GlobalVariableTest] 미등록 변수 조회 및 안전성 검증
 */
SW_TEST_CASE( GlobalVariableTest, NonExistentVariableHandling )
{
    GlobalVariableFixture   fixture;
    sw::GlobalVariableInfo* pMissing = fixture._manager.findVariable( "gv_nonExistentVariable" );
    SW_EXPECT_NULL( pMissing );

    SW_EXPECT_FALSE( fixture._manager.setValueFromString( "gv_nonExistentVariable", "123" ) );
    SW_EXPECT_FALSE( fixture._manager.resetToDefault( "gv_nonExistentVariable" ) );
}

/**
 * @brief [GlobalVariableTest] 멀티스레드 환경에서 문자열 전역 변수 동시 읽기/쓰기 스레드 안전성 검증
 * @details 읽는 스레드가 늦게 뜨면 쓰기 1000 번이 먼저 끝나 읽기가 한 번도 겹치지 않는다 — 그러면 아무것도 시험하지 않고 통과한다.
 *          읽는 쪽이 돌기 시작한 뒤에 쓰고, 쓰는 동안 겹친 읽기를 세어 모자라면 더 쓰며, 겹친 횟수를 단언한다(`FileTest.ReadersNeverObserveHalfWrittenFile` 와 같은 모양).
 */
SW_TEST_CASE( GlobalVariableTest, MultithreadedStringReadWriteThreadSafety )
{
    GlobalVariableFixture   fixture;
    sw::GlobalVariableInfo* pStrInfo = fixture._manager.findVariable( "gv_testString" );
    SW_ASSERT_NOT_NULL( pStrInfo );

    std::atomic<bool>   bWriterDone{ false };
    std::atomic<uint32> readCount{ 0 };
    std::atomic<uint32> badReadCount{ 0 };
    std::thread         reader( [&]()
    {
        while ( bWriterDone.load( std::memory_order_acquire ) == false )
        {
            const sw::string val = pStrInfo->getValueAsString();
            if ( val.find( "Value_" ) == sw::string::npos && val != "InitialValue" )
                badReadCount.fetch_add( 1 );
            readCount.fetch_add( 1 );
        }
    } );

    // 읽는 쪽이 돌기 시작한 뒤에 쓴다(위 설명).
    const sw::Deadline waitDeadline = sw::Deadline::afterMilliseconds( 10000 );
    while ( readCount.load() == 0 && waitDeadline.isExpired() == false )
    {
        std::this_thread::yield();
    }
    const uint32 readBeforeWrite = readCount.load();

    // 쓰는 동안 겹친 읽기가 충분해야 한다 — 1000 번을 쓰고도 모자라면 더 쓴다(상한이 있다).
    constexpr uint32 kMinOverlappedRead = 100;
    for ( uint32 iter = 0; iter < 200000; ++iter )
    {
        if ( iter >= 1000 && readCount.load() - readBeforeWrite >= kMinOverlappedRead )
            break;
        fixture._manager.setValueFromString( "gv_testString", sw::string( "Value_" ) + sw::to_string( iter ) );
    }
    const uint32 overlappedRead = readCount.load() - readBeforeWrite;
    bWriterDone.store( true, std::memory_order_release );
    reader.join();

    SW_EXPECT_EQUAL( 0u, badReadCount.load() );
    SW_EXPECT_TRUE_MSG( overlappedRead >= kMinOverlappedRead, "읽기가 쓰기와 거의 겹치지 않았다 — 아무것도 시험하지 않은 것이다" );
}

/**
 * @brief [GlobalVariableTest] `findVariable` 이 준 포인터는 **다른 변수를 등록·해제해도 살아 있다**
 * @details 패널은 이름을 훑어 포인터를 모아 두었다가 한 번에 그린다 — 헤더가 권하는 사용법이다.
 *          그런데 `sw::unordered_map` 은 밀집 배열이라 삽입하면 재할당으로 **모든** 원소가, 삭제하면
 *          swap-and-pop 으로 **마지막 원소가** 옮겨 간다. 값을 그대로 담고 그 주소를 내주면 그 포인터가
 *          조용히 다른 변수를 가리키거나 죽은 자리를 가리킨다. 그래서 값을 `unique_ptr` 로 든다.
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
