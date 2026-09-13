#include "pch.h"

#include "TestFramework/TestFramework.h"

namespace test
{
    namespace
    {
        /** @brief CLI 인자 값의 따옴표를 제거합니다. */
        sw::string trimArgValue( std::string_view value )
        {
            while ( value.empty() == false && ( value.front() == '"' || value.front() == '\'' ) )
                value.remove_prefix( 1 );
            while ( value.empty() == false && ( value.back() == '"' || value.back() == '\'' ) )
                value.remove_suffix( 1 );
            return sw::string( value );
        }

    } // namespace

    TestRegistry& TestRegistry::getInstance()
    {
        static TestRegistry s_instance;
        return s_instance;
    }

    void TestRegistry::registerTest( const sw::string& suiteName, const sw::string& testName, sw::Delegate<void()> func )
    {
        _listTest.push_back( { suiteName, testName, func } );
    }

    void TestRegistry::setFilter( const sw::string& filter )
    {
        _filter.setPattern( filter );
    }

    sw::vector<utf8*> TestRegistry::configureFromArgs( int32 argc, utf8* argv[] )
    {
        sw::vector<utf8*> listApplicationArg;
        listApplicationArg.reserve( static_cast<size_t>( argc ) );
        if ( argc > 0 )
            listApplicationArg.push_back( argv[0] );

        for ( int32 argIndex = 1; argIndex < argc; ++argIndex )
        {
            const std::string_view arg = argv[argIndex] != nullptr ? argv[argIndex] : "";
            if ( arg == "--test_list" || arg == "--gtest_list_tests" )
            {
                _listOnly = true;
                continue;
            }

            constexpr std::string_view kFilterPrefixA = "--test_filter=";
            constexpr std::string_view kFilterPrefixB = "--gtest_filter=";
            if ( arg.substr( 0, kFilterPrefixA.size() ) == kFilterPrefixA )
            {
                setFilter( sw::string( trimArgValue( arg.substr( kFilterPrefixA.size() ) ) ) );
                continue;
            }
            if ( arg.substr( 0, kFilterPrefixB.size() ) == kFilterPrefixB )
            {
                setFilter( sw::string( trimArgValue( arg.substr( kFilterPrefixB.size() ) ) ) );
                continue;
            }

            if ( arg == "--test_filter" || arg == "--gtest_filter" )
            {
                if ( argIndex + 1 < argc && argv[argIndex + 1] != nullptr )
                    setFilter( sw::string( trimArgValue( argv[++argIndex] ) ) );
                continue;
            }

            if ( arg == "--allow_empty_suite" )
            {
                _bAllowEmptySuite = true;
                continue;
            }

            listApplicationArg.push_back( argv[argIndex] );
        }

        return listApplicationArg;
    }

    void TestRegistry::addFailure( const sw::string& condition, const sw::string& file, int32 line, const sw::string& message )
    {
        _currentContext.addFailure( condition, file, line, message );
        std::fprintf( stdout, "\n  [FAILED] %s:%d\n    Condition: %s\n", file.c_str(), line, condition.c_str() );
        if ( message.empty() == false )
            std::fprintf( stdout, "    Message  : %s\n", message.c_str() );
        std::fflush( stdout );
        SW_LOG_ERROR( "\n  [FAILED] %#:%#", file.c_str(), line );
        SW_LOG_ERROR( "    Condition: %#", condition.c_str() );
        if ( message.empty() == false )
            SW_LOG_ERROR( "    Message  : %#", message.c_str() );
    }

    void TestRegistry::skipCurrentTest( [[maybe_unused]] const sw::string& reason, [[maybe_unused]] const sw::string& file, [[maybe_unused]] int32 line )
    {
        _currentContext.skip( reason, file, line );
        SW_LOG_INFO( "\n  [SKIPPED] %#:%# — %#", file.c_str(), line, reason.c_str() );
    }

    void TestRegistry::listTests() const
    {
        std::fprintf( stdout, "Registered tests (%u):\n", static_cast<uint32>( _listTest.size() ) );
        SW_LOG_INFO( "Registered tests (%#):", static_cast<uint32>( _listTest.size() ) );
        for ( const TestCaseInfo& testInfo : _listTest )
        {
            std::fprintf( stdout, "  %s\n", testInfo.fullName().c_str() );
            SW_LOG_INFO( "  %#", testInfo.fullName().c_str() );
        }
        std::fflush( stdout );
    }

    int32 TestRegistry::runAllTests()
    {
        if ( _listOnly )
        {
            listTests();
            return 0;
        }

        int32   passedCount{ 0 };
        int32   failedCount{ 0 };
        int32   skippedCount{ 0 };
        float64 totalMs{ 0.0 };

        uint32 runnableCount{ 0 };
        for ( const TestCaseInfo& testInfo : _listTest )
        {
            if ( _filter.matches( testInfo.fullName() ) )
                ++runnableCount;
        }

        const uint32 filteredOut = static_cast<uint32>( _listTest.size() ) - runnableCount;

        SW_LOG_INFO( "====================================================" );
        SW_LOG_INFO( " Running %# / %# Test Cases...", runnableCount, static_cast<uint32>( _listTest.size() ) );
        if ( filteredOut > 0 )
            SW_LOG_INFO( " Filtered out: %#", filteredOut );
        SW_LOG_INFO( "====================================================" );

        sw::vector<sw::string> listFailedTestName;

        // 스위트별 실행/스킵 — "고르긴 했는데 하나도 안 돌아간" 스위트를 끝에서 잡는다(아래 참고).
        sw::vector<sw::string>                      listSuiteOrder;
        sw::map<sw::string, sw::pair<int32, int32>> mapSuiteRanSkipped;

        for ( const TestCaseInfo& testInfo : _listTest )
        {
            if ( _filter.matches( testInfo.fullName() ) == false )
                continue;

            if ( mapSuiteRanSkipped.find( testInfo._groupName ) == mapSuiteRanSkipped.end() )
            {
                mapSuiteRanSkipped[testInfo._groupName] = { 0, 0 };
                listSuiteOrder.push_back( testInfo._groupName );
            }

            _currentContext.begin( testInfo.fullName() );
            std::fprintf( stdout, "[ RUN      ] %s\n", testInfo.fullName().c_str() );
            std::fflush( stdout );
            SW_LOG_INFO( "%#", testInfo.fullName().c_str() );

            const std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();

            try
            {
                testInfo._func();
            }
            catch ( const std::exception& exception )
            {
                addFailure( "test exception", "TestFramework", 0, exception.what() );
            }
            catch ( ... )
            {
                addFailure( "test exception", "TestFramework", 0, "Unknown test exception" );
            }

            try
            {
                _environment.tearDown();
            }
            catch ( const std::exception& exception )
            {
                addFailure( "environment teardown", "TestFramework", 0, exception.what() );
            }
            catch ( ... )
            {
                addFailure( "environment teardown", "TestFramework", 0, "Unknown teardown exception" );
            }

            _currentContext.runCleanup();

            const std::chrono::high_resolution_clock::time_point end     = std::chrono::high_resolution_clock::now();
            const float64                                        elapsed = std::chrono::duration<float64, std::milli>( end - start ).count();
            totalMs += elapsed;

            if ( _currentContext.isSkipped() && _currentContext.hasFailed() == false )
            {
                ++skippedCount;
                ++mapSuiteRanSkipped[testInfo._groupName].second;
                std::fprintf( stdout, "[  SKIPPED ] %s\n", testInfo.fullName().c_str() );
                std::fflush( stdout );
                SW_LOG_INFO( "%#", testInfo.fullName().c_str() );
            }
            else if ( _currentContext.hasFailed() )
            {
                ++mapSuiteRanSkipped[testInfo._groupName].first;
                ++failedCount;
                listFailedTestName.push_back( testInfo.fullName() );
                std::fprintf( stdout, "[  FAILED  ] %s (%.2f ms)\n", testInfo.fullName().c_str(), elapsed );
                std::fflush( stdout );
                const sw::string testName = testInfo.fullName();
                SW_LOG_ERROR( "%# (%# ms)", testName.c_str(), sw::Fmt( elapsed, sw::Format().precision( 2 ) ) );
            }
            else
            {
                ++mapSuiteRanSkipped[testInfo._groupName].first;
                ++passedCount;
                std::fprintf( stdout, "[       OK ] %s (%.2f ms)\n", testInfo.fullName().c_str(), elapsed );
                std::fflush( stdout );
                SW_LOG_INFO( "%# (%# ms)", testInfo.fullName().c_str(), sw::Fmt( elapsed, sw::Format().precision( 2 ) ) );
            }
        }

        // 스위트를 골라 놓고 **하나도 실행되지 않았다면** 그 스위트는 아무것도 검증하지 않았다.
        //
        // 스킵은 실패가 아니라서 예전에는 이런 실행이 그냥 초록이었다. DXC 가 사라지거나 구운 셰이더가
        // 없어지면 케이스가 스스로 SW_TEST_SKIP 하고, CI 는 "통과" 를 보고한다 — 무엇이 사라졌는지
        // 아무도 모른 채로. 검증 공백은 통과가 아니므로 여기서 실패로 만든다.
        // (2026-09-13 기준 Debug·Release·Shipping 어디에도 통째로 스킵되는 스위트는 없다. 그래서
        //  예외 목록이 없다 — 정말 필요해지면 `--allow_empty_suite` 로 그 실행만 열어 준다.)
        sw::vector<sw::string> listEmptySuite;
        for ( const sw::string& suiteName : listSuiteOrder )
        {
            const sw::pair<int32, int32>& ranSkipped = mapSuiteRanSkipped[suiteName];
            if ( ranSkipped.first == 0 && ranSkipped.second > 0 )
                listEmptySuite.push_back( suiteName );
        }

        const bool bEmptySuiteIsFailure = listEmptySuite.empty() == false && _bAllowEmptySuite == false;
        if ( listEmptySuite.empty() == false )
        {
            std::fprintf( stdout, "====================================================\n" );
            std::fprintf( stdout, " %s (%d):\n",
                          bEmptySuiteIsFailure ? "Suites that verified nothing - every case skipped"
                                               : "Suites that verified nothing (allowed)",
                          static_cast<int32>( listEmptySuite.size() ) );
            for ( const sw::string& suiteName : listEmptySuite )
            {
                std::fprintf( stdout, "   - %s (%d skipped)\n", suiteName.c_str(), mapSuiteRanSkipped[suiteName].second );
                if ( bEmptySuiteIsFailure )
                    SW_LOG_ERROR( "Suite verified nothing - every case skipped: %#", suiteName.c_str() );
                else
                    SW_LOG_INFO( "Suite verified nothing - every case skipped (allowed): %#", suiteName.c_str() );
            }
            if ( bEmptySuiteIsFailure )
                std::fprintf( stdout, " Pass --allow_empty_suite if this is expected here.\n" );
            std::fflush( stdout );
        }

        SW_LOG_INFO( "====================================================" );
        SW_LOG_INFO( " Test Summary: %# Passed, %# Failed, %# Skipped (%# ms total)",
                     passedCount,
                     failedCount,
                     skippedCount,
                     totalMs );
        SW_LOG_INFO( "====================================================" );

        std::fprintf( stdout, "====================================================\n" );
        std::fprintf( stdout, " Tests passed: %d / %d (%d skipped, %.2f ms total)\n", passedCount, passedCount + failedCount + skippedCount, skippedCount, totalMs );
        if ( failedCount > 0 )
        {
            std::fprintf( stdout, " Tests failed (%d):\n", failedCount );
            for ( const auto& name : listFailedTestName )
            {
                std::fprintf( stdout, "   - %s\n", name.c_str() );
            }
        }
        std::fprintf( stdout, "====================================================\n" );
        std::fflush( stdout );

        return ( failedCount == 0 && bEmptySuiteIsFailure == false ) ? 0 : 1;
    }
} // namespace test
