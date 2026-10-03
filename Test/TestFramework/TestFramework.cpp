#include "pch.h"

#include "TestFramework/TestFramework.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <random>

#if defined( SW_PLATFORM_WINDOWS )
    #include <process.h>
#else
    #include <unistd.h>
#endif

namespace test
{
    namespace
    {
        /** @brief 이 프로세스의 id 입니다. 임시 파일 이름을 프로세스마다 다르게 하는 데 씁니다. */
        uint32 currentProcessId()
        {
#if defined( SW_PLATFORM_WINDOWS )
            return static_cast<uint32>( ::_getpid() );
#else
            return static_cast<uint32>( ::getpid() );
#endif
        }

        /** @brief 파일 이름에 넣어도 되는 글자만 남깁니다. */
        sw::string toFileNameSafe( sw::string_view text )
        {
            sw::string safe;
            safe.reserve( text.size() );
            for ( const utf8 ch : text )
            {
                const bool bSafe = ( 'a' <= ch && ch <= 'z' ) || ( 'A' <= ch && ch <= 'Z' ) || ( '0' <= ch && ch <= '9' );
                safe.push_back( bSafe ? ch : '_' );
            }
            return safe;
        }

        /** @brief 이 프로세스의 임시 폴더 — `<임시 폴더>/sw_<pid>`. 케이스 폴더가 전부 그 아래에 생긴다. */
        sw::string getProcessTempDirectory()
        {
            sw::StringBuilder<sw::constant::kMaxBuffer64> name;
            name.append( "sw_" );
            name.append( static_cast<int32>( currentProcessId() ) );
            return sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), name.c_str() );
        }

        /** @brief 이 케이스의 임시 폴더 — `<임시 폴더>/sw_<pid>/<스위트_케이스>`(케이스 밖이면 프로세스 폴더). */
        sw::string getCaseTempDirectory( const sw::string& testName )
        {
            const sw::string processDirectory = getProcessTempDirectory();
            return testName.empty() ? processDirectory : sw::FileUtil::joinPath( processDirectory, toFileNameSafe( testName ) );
        }

        /**
         * @brief 실패 · 건너뜀 기록을 한 줄로 세우는 락.
         * @details 단언은 작업 스레드에서도 불린다(`runParallel` 본문 · `std::thread` 람다 — 테스트 열 곳 남짓). 예전에는 기록이
         *          벡터 `push_back` 이라 **둘이 동시에 실패하면** 그 벡터가 깨졌다 — 병렬 코드가 틀렸다는 것을 알려야 할 바로 그 순간에.
         *          gtest 도 단언을 스레드 안전하게 보장한다. 엔진의 `sw::mutex` 를 쓰지 않는 것은 그것이 Debug 에서 데드락 탐지기를
         *          타기 때문이다 — 그 탐지기를 시험하는 케이스가 있다.
         */
        std::mutex& getRecordMutex()
        {
            static std::mutex s_mutex;
            return s_mutex;
        }

        /** @brief 이 스레드가 지금 `addFailure` 안에 있는가 — 거기서 남기는 `[FAILED]` 줄은 예상 밖 Error 로 세지 않는다. */
        thread_local bool s_bReportingFailure = false;

        /** @brief 방어 시험 구간(`SW_TEST_DEFENSIVE_SCOPE`)이 Error · Warning 앞에 붙이는 표식입니다. */
        constexpr sw::string_view kDefensiveLogPrefix = "[Expected Defensive Test]";

        /**
         * @brief 호스트 스위트 케이스 하나 동안 남은 Error 로그를 셉니다.
         * @details GPU 시험은 검증 레이어 · 드라이버 오류를 Error 로그로만 남기고 단언은 통과할 수 있다. 그래서 호스트 스위트의
         *          케이스는 예상 밖 Error 가 하나라도 있으면 진다. 의도된 Error 는 `SW_TEST_DEFENSIVE_SCOPE` 안에서 남기면 표식이
         *          붙어 세지 않고, `SW_TEST_SUPPRESS_LOGS` 안의 로그는 아예 남지 않는다. 그 스위트에 선언된 알려진 Error
         *          (`SW_TEST_KNOWN_ERROR_LOG`)는 세지 않고 그 선언의 `_hitCount` 에 센다.
         */
        class UnexpectedErrorLogWatch
        {
        public:
            /** @param pListKnownErrorLog 이 케이스의 스위트에 선언된 알려진 Error. 널이면 세지 않는다(호스트 스위트가 아님). */
            explicit UnexpectedErrorLogWatch( sw::vector<KnownErrorLog*>* pListKnownErrorLog )
                : _pListKnownErrorLog{ pListKnownErrorLog }
            {
                if ( _pListKnownErrorLog == nullptr )
                    return;
                _handle = sw::Logger::addGlobalListener( SW_DELEGATE_LAMBDA( sw::LogWrittenDelegate, [this]( const sw::LogEntry& entry )
                {
                    if ( entry._level != sw::LogLevel::Error || s_bReportingFailure )
                        return;
                    const sw::string_view message{ entry._message.c_str(), entry._message.size() };
                    if ( message.substr( 0, kDefensiveLogPrefix.size() ) == kDefensiveLogPrefix )
                        return;
                    const std::lock_guard<std::mutex> lock( _mutex );
                    for ( KnownErrorLog* pKnown : *_pListKnownErrorLog )
                    {
                        if ( message.find( sw::string_view{ pKnown->_substring.c_str(), pKnown->_substring.size() } ) == sw::string_view::npos )
                            continue;
                        ++pKnown->_hitCount;
                        return;
                    }
                    ++_count;
                    if ( _listMessage.size() < kShownMessageCount )
                        _listMessage.push_back( entry._message );
                } ) );
            }

            ~UnexpectedErrorLogWatch() { stop(); }

            UnexpectedErrorLogWatch( const UnexpectedErrorLogWatch& )            = delete;
            UnexpectedErrorLogWatch& operator=( const UnexpectedErrorLogWatch& ) = delete;

            /** @brief 세기를 멈춥니다. 케이스 정리가 끝난 뒤, 결과를 기록하기 전에 부릅니다. */
            void stop()
            {
                if ( _handle.isValid() == false )
                    return;
                sw::Logger::removeGlobalListener( _handle );
                _handle = {};
            }

            uint32 getCount() const
            {
                const std::lock_guard<std::mutex> lock( _mutex );
                return _count;
            }

            /** @brief 센 수와 앞쪽 몇 줄 — 실패 메시지에 붙여 "무엇이 나왔나" 를 보입니다. */
            sw::string describe() const
            {
                const std::lock_guard<std::mutex> lock( _mutex );
                sw::string                        result = sw::to_string( _count );
                result += " unexpected [Error] log line(s); wrap intended ones in SW_TEST_DEFENSIVE_SCOPE";
                for ( const sw::string& message : _listMessage )
                {
                    result += "\n      ";
                    result += message;
                }
                return result;
            }

        private:
            static constexpr size_t kShownMessageCount = 5;

            mutable std::mutex          _mutex;
            sw::vector<sw::string>      _listMessage;
            sw::vector<KnownErrorLog*>* _pListKnownErrorLog{ nullptr };
            sw::DelegateHandle          _handle;
            uint32                      _count{ 0 };
        };

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

    void reportFailure( const utf8* pCondition, const utf8* pFile, int32 line, const utf8* pMessage )
    {
        TestRegistry::getInstance().addFailure( pCondition, pFile, line, pMessage != nullptr ? pMessage : "" );
    }

    void reportFailure( const utf8* pCondition, const utf8* pFile, int32 line, const sw::string& message )
    {
        TestRegistry::getInstance().addFailure( pCondition, pFile, line, message );
    }

    void expectSameText( const utf8* pCondition, const utf8* pFile, int32 line, const ComparableText& expected, const ComparableText& actual )
    {
        if ( expected._bNull == actual._bNull && expected._text == actual._text )
            return;

        std::ostringstream oss;
        oss << "Expected [" << ( expected._bNull ? sw::string_view( "<null>" ) : expected._text ) << "], Actual ["
            << ( actual._bNull ? sw::string_view( "<null>" ) : actual._text ) << "]";
        reportFailure( pCondition, pFile, line, oss.str().c_str() );
    }

    sw::string makeTempPath( sw::string_view fileName )
    {
        TestContext* pContext = TestRegistry::getInstance().getCurrentContext();
        if ( pContext->getTestName().empty() == false )
            pContext->markTempPathUsed();

        const sw::string caseDirectory = getCaseTempDirectory( pContext->getTestName() );
        sw::FileUtil::ensureDirectoryExists( caseDirectory );
        return sw::FileUtil::joinPath( caseDirectory, fileName );
    }

    sw::string makeTempDirectory( sw::string_view directoryName )
    {
        const sw::string directory = makeTempPath( directoryName );
        sw::FileUtil::ensureDirectoryExists( directory );
        return directory;
    }

    TestRegistry& TestRegistry::getInstance()
    {
        static TestRegistry s_instance;
        return s_instance;
    }

    void TestRegistry::registerTest( const utf8* pSuiteName, const utf8* pTestName, sw::Delegate<void()> func )
    {
        _listTest.push_back( { pSuiteName, pTestName, func } );
    }

    void TestRegistry::registerHostSuite( const utf8* pSuiteName, const utf8* pReason )
    {
        _mapHostSuiteReason[pSuiteName] = pReason;
    }

    void TestRegistry::registerKnownErrorLog( const utf8* pSuiteName, const utf8* pSubstring, const utf8* pReason )
    {
        _listKnownErrorLog.push_back( { pSuiteName, pSubstring, pReason, 0 } );
    }

    void TestRegistry::printKnownErrorLogSummary() const
    {
        if ( _listKnownErrorLog.empty() )
            return;

        std::fprintf( stdout, " Known error logs (SW_TEST_KNOWN_ERROR_LOG - fix the cause, then delete the declaration):\n" );
        for ( const KnownErrorLog& known : _listKnownErrorLog )
        {
            if ( known._hitCount > 0 )
                std::fprintf( stdout, "   %s: tolerated %u line(s) containing '%s' - %s\n", known._suiteName.c_str(), known._hitCount,
                              known._substring.c_str(), known._reason.c_str() );
            else
                std::fprintf( stdout, "   %s: '%s' did not appear in this run - delete the declaration if it is fixed\n", known._suiteName.c_str(),
                              known._substring.c_str() );
        }
    }

    bool TestRegistry::isSelected( const TestCaseInfo& testInfo ) const
    {
        if ( _filter.matches( testInfo.fullName() ) == false )
            return false;

        const bool bHostSuite = _mapHostSuiteReason.find( testInfo._groupName ) != _mapHostSuiteReason.end();
        if ( _hostSuiteMode == HostSuiteMode::Exclude )
            return bHostSuite == false;
        if ( _hostSuiteMode == HostSuiteMode::Only )
            return bHostSuite;
        return true;
    }

    int32 TestRegistry::countHostSuiteMismatch() const
    {
        // 선언한 스위트에 케이스가 하나도 없으면 그 선언은 아무것도 빼지 않는다 — 대개 스위트 이름을 바꾸고 선언을
        // 놓친 것이고, 그 순간 이름이 바뀐 스위트는 **CI 로 들어간다.** 그래서 조용히 넘기지 않는다.
        int32 mismatchCount{ 0 };
        for ( const auto& [suiteName, reason] : _mapHostSuiteReason )
        {
            bool bHasCase{ false };
            for ( const TestCaseInfo& testInfo : _listTest )
            {
                if ( testInfo._groupName == suiteName )
                {
                    bHasCase = true;
                    break;
                }
            }
            if ( bHasCase )
                continue;

            ++mismatchCount;
            std::fprintf( stdout, " SW_TEST_REQUIRES_HOST( %s ) names a suite with no cases in this executable\n", suiteName.c_str() );
            SW_LOG_ERROR( "SW_TEST_REQUIRES_HOST( %# ) names a suite with no cases in this executable", suiteName.c_str() );
        }
        return mismatchCount;
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

        // 샤드 — gtest 의 환경 변수도 받는다(아래 `--test_shard=` 가 있으면 그쪽이 이긴다).
        const auto setShard = [this]( std::string_view indexText, std::string_view countText )
        {
            const int32 index = std::atoi( sw::string( indexText ).c_str() );
            const int32 count = std::atoi( sw::string( countText ).c_str() );
            if ( count < 1 || index < 0 || index >= count )
            {
                _bInvalidArgument = true;
                std::fprintf( stdout, "Invalid test shard '%s/%s' (expected <index>/<count> with 0 <= index < count)\n", sw::string( indexText ).c_str(),
                              sw::string( countText ).c_str() );
                return;
            }
            _shardIndex = static_cast<uint32>( index );
            _shardCount = static_cast<uint32>( count );
        };
        const utf8* pShardIndexEnv = std::getenv( "GTEST_SHARD_INDEX" );
        const utf8* pShardCountEnv = std::getenv( "GTEST_TOTAL_SHARDS" );
        if ( pShardIndexEnv != nullptr && pShardCountEnv != nullptr )
            setShard( pShardIndexEnv, pShardCountEnv );

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

            // 되풀이 · 섞기 — gtest 의 이름(`--gtest_repeat` · `--gtest_shuffle` · `--gtest_random_seed`)도 받는다.
            constexpr std::string_view kRepeatPrefixA = "--test_repeat=";
            constexpr std::string_view kRepeatPrefixB = "--gtest_repeat=";
            if ( arg.substr( 0, kRepeatPrefixA.size() ) == kRepeatPrefixA || arg.substr( 0, kRepeatPrefixB.size() ) == kRepeatPrefixB )
            {
                const std::string_view value = arg.substr( arg.find( '=' ) + 1 );
                const int32            count = std::atoi( sw::string( value ).c_str() );
                if ( count < 1 )
                {
                    _bInvalidArgument = true;
                    std::fprintf( stdout, "Invalid --test_repeat value '%s' (expected a count of 1 or more)\n", sw::string( value ).c_str() );
                }
                else
                    _repeatCount = static_cast<uint32>( count );
                continue;
            }

            constexpr std::string_view kShufflePrefix = "--test_shuffle=";
            constexpr std::string_view kSeedPrefix    = "--gtest_random_seed=";
            if ( arg == "--test_shuffle" || arg == "--gtest_shuffle" )
            {
                _bShuffle = true;
                if ( _shuffleSeed == 0 )
                    _shuffleSeed = static_cast<uint32>( std::chrono::steady_clock::now().time_since_epoch().count() % 100000 ) + 1;
                continue;
            }
            if ( arg.substr( 0, kShufflePrefix.size() ) == kShufflePrefix || arg.substr( 0, kSeedPrefix.size() ) == kSeedPrefix )
            {
                _bShuffle    = arg.substr( 0, kShufflePrefix.size() ) == kShufflePrefix || _bShuffle;
                _shuffleSeed = static_cast<uint32>( std::strtoul( sw::string( arg.substr( arg.find( '=' ) + 1 ) ).c_str(), nullptr, 10 ) );
                continue;
            }

            constexpr std::string_view kShardPrefix = "--test_shard=";
            if ( arg.substr( 0, kShardPrefix.size() ) == kShardPrefix )
            {
                const std::string_view value = arg.substr( kShardPrefix.size() );
                const size_t           slash = value.find( '/' );
                if ( slash == std::string_view::npos )
                    setShard( value, "" ); // 형식 오류로 보고된다
                else
                    setShard( value.substr( 0, slash ), value.substr( slash + 1 ) );
                continue;
            }

            constexpr std::string_view kHostSuitesPrefix = "--host_suites=";
            if ( arg.substr( 0, kHostSuitesPrefix.size() ) == kHostSuitesPrefix )
            {
                const std::string_view mode = arg.substr( kHostSuitesPrefix.size() );
                if ( mode == "exclude" )
                    _hostSuiteMode = HostSuiteMode::Exclude;
                else if ( mode == "only" )
                    _hostSuiteMode = HostSuiteMode::Only;
                else if ( mode == "all" )
                    _hostSuiteMode = HostSuiteMode::All;
                else
                {
                    // 오타를 "전부" 로 읽으면 CI 가 GPU 스위트를 돌린다 — 실행을 실패로 끝낸다.
                    _bInvalidArgument = true;
                    std::fprintf( stdout, "Unknown --host_suites value '%s' (expected exclude, only or all)\n", sw::string( mode ).c_str() );
                }
                continue;
            }

            listApplicationArg.push_back( argv[argIndex] );
        }

        return listApplicationArg;
    }

    void TestRegistry::addFailure( const sw::string& condition, const sw::string& file, int32 line, const sw::string& message )
    {
        const std::lock_guard<std::mutex> lock( getRecordMutex() );
        if ( _pFailureCapture != nullptr )
        {
            _pFailureCapture->push_back( { condition, file, line, message } );
            return;
        }

        s_bReportingFailure = true;

        _currentContext.addFailure( condition, file, line, message );
        std::fprintf( stdout, "\n  [FAILED] %s:%d\n    Condition: %s\n", file.c_str(), line, condition.c_str() );
        if ( message.empty() == false )
            std::fprintf( stdout, "    Message  : %s\n", message.c_str() );
        std::fflush( stdout );
        SW_LOG_ERROR( "\n  [FAILED] %#:%#", file.c_str(), line );
        SW_LOG_ERROR( "    Condition: %#", condition.c_str() );
        if ( message.empty() == false )
            SW_LOG_ERROR( "    Message  : %#", message.c_str() );
        s_bReportingFailure = false;
    }

    void TestRegistry::skipCurrentTest( [[maybe_unused]] const sw::string& reason, [[maybe_unused]] const sw::string& file, [[maybe_unused]] int32 line )
    {
        const std::lock_guard<std::mutex> lock( getRecordMutex() );
        _currentContext.skip( reason, file, line );
        SW_LOG_INFO( "\n  [SKIPPED] %#:%# — %#", file.c_str(), line, reason.c_str() );
    }

    void TestRegistry::listTests() const
    {
        // **필터를 적용해서 센다.** 예전에는 `_listTest` 를 통째로 찍어서, `--test_list` 와
        // `--test_filter` 를 같이 주면 필터가 조용히 무시됐다. 하필 "내 필터가 무엇을 고르나" 를
        // 확인할 때 쓰는 기능이라, 틀린 답을 주면 그걸 믿고 필터를 잘못 적는다.
        sw::vector<sw::string> listSelected;
        listSelected.reserve( _listTest.size() );
        for ( const TestCaseInfo* pTestInfo : selectCasesForThisShard() )
            listSelected.push_back( pTestInfo->fullName() );

        const uint32 selectedCount = static_cast<uint32>( listSelected.size() );
        const uint32 totalCount    = static_cast<uint32>( _listTest.size() );
        if ( selectedCount == totalCount )
        {
            std::fprintf( stdout, "Registered tests (%u):\n", totalCount );
            SW_LOG_INFO( "Registered tests (%#):", totalCount );
        }
        else
        {
            std::fprintf( stdout, "Registered tests (%u selected / %u total):\n", selectedCount, totalCount );
            SW_LOG_INFO( "Registered tests (%# selected / %# total):", selectedCount, totalCount );
        }

        for ( const sw::string& fullName : listSelected )
        {
            std::fprintf( stdout, "  %s\n", fullName.c_str() );
            SW_LOG_INFO( "  %#", fullName.c_str() );
        }

        if ( _mapHostSuiteReason.empty() == false )
        {
            std::fprintf( stdout, "Host suites - CI cannot run these (%u):\n", static_cast<uint32>( _mapHostSuiteReason.size() ) );
            for ( const auto& [suiteName, reason] : _mapHostSuiteReason )
                std::fprintf( stdout, "  %s - %s\n", suiteName.c_str(), reason.c_str() );
        }
        std::fflush( stdout );
    }

    sw::vector<const TestCaseInfo*> TestRegistry::selectCasesForThisShard() const
    {
        sw::vector<const TestCaseInfo*> listSelected;
        listSelected.reserve( _listTest.size() );
        sw::map<sw::string, sw::pair<uint32, uint32>> mapSuiteOrdinalAndNextIndex; // 스위트 번호, 그 스위트의 다음 순번
        for ( const TestCaseInfo& testInfo : _listTest )
        {
            if ( isSelected( testInfo ) == false )
                continue;
            if ( _shardCount <= 1 )
            {
                listSelected.push_back( &testInfo );
                continue;
            }
            auto suiteIt = mapSuiteOrdinalAndNextIndex.find( testInfo._groupName );
            if ( suiteIt == mapSuiteOrdinalAndNextIndex.end() )
            {
                const uint32 suiteOrdinal = static_cast<uint32>( mapSuiteOrdinalAndNextIndex.size() );
                suiteIt                   = mapSuiteOrdinalAndNextIndex.emplace( testInfo._groupName, sw::pair<uint32, uint32>{ suiteOrdinal, 0u } ).first;
            }
            const uint32 indexInSuite = suiteIt->second.second++;
            if ( ( suiteIt->second.first + indexInSuite ) % _shardCount == _shardIndex )
                listSelected.push_back( &testInfo );
        }
        return listSelected;
    }

    sw::vector<const TestCaseInfo*> TestRegistry::buildRunOrder( uint32 iteration ) const
    {
        sw::vector<const TestCaseInfo*> listRun = selectCasesForThisShard();
        if ( _bShuffle == false )
            return listRun;

        // 스위트 순서를 섞고 스위트 안의 케이스를 섞는다(스위트는 붙어 있다 — gtest 와 같다).
        sw::vector<sw::string>                               listSuiteOrder;
        sw::map<sw::string, sw::vector<const TestCaseInfo*>> mapSuiteCase;
        for ( const TestCaseInfo* pTestInfo : listRun )
        {
            sw::vector<const TestCaseInfo*>& listCase = mapSuiteCase[pTestInfo->_groupName];
            if ( listCase.empty() )
                listSuiteOrder.push_back( pTestInfo->_groupName );
            listCase.push_back( pTestInfo );
        }

        std::mt19937 random( _shuffleSeed + iteration );
        std::shuffle( listSuiteOrder.begin(), listSuiteOrder.end(), random );
        listRun.clear();
        for ( const sw::string& suiteName : listSuiteOrder )
        {
            sw::vector<const TestCaseInfo*>& listCase = mapSuiteCase[suiteName];
            std::shuffle( listCase.begin(), listCase.end(), random );
            listRun.insert( listRun.end(), listCase.begin(), listCase.end() );
        }
        return listRun;
    }

    CaseResult TestRegistry::runCase( const TestCaseInfo& testInfo, float64& outElapsedMs )
    {
        _currentContext.begin( testInfo.fullName() );
        std::fprintf( stdout, "[ RUN      ] %s\n", testInfo.fullName().c_str() );
        std::fflush( stdout );
        SW_LOG_INFO( "%#", testInfo.fullName().c_str() );

        const std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();

        // 호스트 스위트(GPU · 창 · DXC)는 예상 밖 Error 로그를 실패로 친다 — 검증 레이어 오류가 단언 없이 지나가지 않게.
        const bool                 bHostSuite = _mapHostSuiteReason.find( testInfo._groupName ) != _mapHostSuiteReason.end();
        sw::vector<KnownErrorLog*> listKnownErrorLog;
        for ( KnownErrorLog& known : _listKnownErrorLog )
        {
            if ( known._suiteName == testInfo._groupName )
                listKnownErrorLog.push_back( &known );
        }
        UnexpectedErrorLogWatch errorLogWatch( bHostSuite ? &listKnownErrorLog : nullptr );

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

        errorLogWatch.stop();
        if ( errorLogWatch.getCount() > 0 )
            addFailure( "no unexpected [Error] log in a host suite case", testInfo.fullName(), 0, errorLogWatch.describe() );

        // 정리(핸들 닫기 · 등록 해제)가 끝난 뒤에 케이스 폴더를 통째로 지운다. 못 지웠다면 그 케이스가 파일을 연 채로
        // 두었다는 뜻이다 — Windows 에서는 열린 파일을 지울 수 없다. 핸들 누수는 결함이므로 그 케이스의 실패로 남긴다.
        if ( _currentContext.isTempPathUsed() )
        {
            const sw::string caseDirectory = getCaseTempDirectory( testInfo.fullName() );
            if ( sw::FileUtil::removeDirectory( caseDirectory ) == false )
                addFailure( "temp directory removed after the case", caseDirectory, 0, "still exists - is a file handle left open?" );
        }

        outElapsedMs = std::chrono::duration<float64, std::milli>( std::chrono::high_resolution_clock::now() - start ).count();

        if ( _currentContext.isSkipped() && _currentContext.hasFailed() == false )
        {
            std::fprintf( stdout, "[  SKIPPED ] %s\n", testInfo.fullName().c_str() );
            std::fflush( stdout );
            SW_LOG_INFO( "%#", testInfo.fullName().c_str() );
            return CaseResult::Skipped;
        }
        if ( _currentContext.hasFailed() )
        {
            std::fprintf( stdout, "[  FAILED  ] %s (%.2f ms)\n", testInfo.fullName().c_str(), outElapsedMs );
            std::fflush( stdout );
            const sw::string testName = testInfo.fullName();
            SW_LOG_ERROR( "%# (%# ms)", testName.c_str(), sw::Fmt( outElapsedMs, sw::Format().precision( 2 ) ) );
            return CaseResult::Failed;
        }
        std::fprintf( stdout, "[       OK ] %s (%.2f ms)\n", testInfo.fullName().c_str(), outElapsedMs );
        std::fflush( stdout );
        SW_LOG_INFO( "%# (%# ms)", testInfo.fullName().c_str(), sw::Fmt( outElapsedMs, sw::Format().precision( 2 ) ) );
        return CaseResult::Passed;
    }

    int32 TestRegistry::runAllTests()
    {
        if ( _bInvalidArgument )
            return 1;

        if ( _listOnly )
        {
            listTests();
            return 0;
        }

        const int32 hostSuiteMismatchCount = countHostSuiteMismatch();

        int32   passedCount{ 0 };
        int32   failedCount{ 0 };
        int32   skippedCount{ 0 };
        float64 totalMs{ 0.0 };

        const uint32 runnableCount = static_cast<uint32>( buildRunOrder( 0 ).size() );
        const uint32 filteredOut   = static_cast<uint32>( _listTest.size() ) - runnableCount;

        SW_LOG_INFO( "====================================================" );
        SW_LOG_INFO( " Running %# / %# Test Cases...", runnableCount, static_cast<uint32>( _listTest.size() ) );
        if ( filteredOut > 0 )
            SW_LOG_INFO( " Filtered out: %#", filteredOut );
        SW_LOG_INFO( "====================================================" );
        if ( _shardCount > 1 )
        {
            std::fprintf( stdout, "Running shard %u of %u (%u cases)\n", _shardIndex + 1, _shardCount, runnableCount );
            std::fflush( stdout );
        }
        if ( _bShuffle )
        {
            // 순서에 기대는 테스트를 찾으려고 섞는다 — 진 순서를 다시 만들 수 있어야 고칠 수 있다.
            std::fprintf( stdout, "Shuffling test order with seed %u (replay: --test_shuffle=%u)\n", _shuffleSeed, _shuffleSeed );
            std::fflush( stdout );
        }

        sw::vector<sw::string> listFailedTestName;

        // 스위트별 실행/스킵 — "고르긴 했는데 하나도 안 돌아간" 스위트를 끝에서 잡는다(아래 참고).
        sw::vector<sw::string>                      listSuiteOrder;
        sw::map<sw::string, sw::pair<int32, int32>> mapSuiteRanSkipped;

        // 오래 걸린 케이스 — 끝에 몇 개를 찍는다. 테스트가 느려지는 것은 조용히 일어난다. 되풀이하면 케이스마다 가장 오래 걸린 회차
        // 하나로 센다(예전에는 회차마다 따로 들어가 같은 케이스 하나가 목록을 다 채웠다).
        sw::map<const TestCaseInfo*, float64> mapSlowestElapsed;

        for ( uint32 iteration = 0; iteration < _repeatCount; ++iteration )
        {
            if ( _repeatCount > 1 )
            {
                std::fprintf( stdout, "\nRepeating all tests (iteration %u / %u) . . .\n\n", iteration + 1, _repeatCount );
                std::fflush( stdout );
            }

            for ( const TestCaseInfo* pTestInfo : buildRunOrder( iteration ) )
            {
                const TestCaseInfo& testInfo = *pTestInfo;
                if ( mapSuiteRanSkipped.find( testInfo._groupName ) == mapSuiteRanSkipped.end() )
                {
                    mapSuiteRanSkipped[testInfo._groupName] = { 0, 0 };
                    listSuiteOrder.push_back( testInfo._groupName );
                }

                float64          elapsed = 0.0;
                const CaseResult result  = runCase( testInfo, elapsed );
                totalMs += elapsed;
                float64& slowestElapsed = mapSlowestElapsed[pTestInfo];
                slowestElapsed          = std::max( slowestElapsed, elapsed );

                if ( result == CaseResult::Skipped )
                {
                    ++skippedCount;
                    ++mapSuiteRanSkipped[testInfo._groupName].second;
                    continue;
                }
                ++mapSuiteRanSkipped[testInfo._groupName].first;
                if ( result == CaseResult::Failed )
                {
                    ++failedCount;
                    sw::string failedName = testInfo.fullName();
                    if ( _repeatCount > 1 )
                        failedName += " (iteration " + sw::to_string( iteration + 1 ) + ")";
                    listFailedTestName.push_back( std::move( failedName ) );
                }
                else
                    ++passedCount;
            }
        }

        // 스위트를 골라 놓고 **하나도 실행되지 않았다면** 그 스위트는 아무것도 검증하지 않았다.
        //
        // 스킵은 실패가 아니라서 예전에는 이런 실행이 그냥 초록이었다. DXC 가 사라지거나 구운 셰이더가
        // 없어지면 케이스가 스스로 SW_TEST_SKIP 하고, CI 는 "통과" 를 보고한다 — 무엇이 사라졌는지
        // 아무도 모른 채로. 검증 공백은 통과가 아니므로 여기서 실패로 만든다.
        // (2026-09-13 기준 Debug·Release·Shipping 어디에도 통째로 스킵되는 스위트는 없다. 그래서
        //  예외 목록이 없다 — 정말 필요해지면 `--allow_empty_suite` 로 그 실행만 열어 준다.)
        //
        // 샤드로 나눴으면 **여러 샤드에 갈린 스위트는 여기서 판단하지 않는다** — 이 샤드가 받은 케이스가 마침 모두 건너뛰는 것이어도 다른
        // 샤드에서는 검증했을 수 있다. 통째로 이 샤드에 온 스위트만 본다. 갈린 스위트가 "전부 건너뜀" 을 막아야 하면 그 전제를 단언하는
        // 케이스를 따로 둔다(`ReflectionParserTest.ParserExecutableIsBuilt`).
        sw::map<sw::string, uint32> mapSuiteSelectedCount;
        if ( _shardCount > 1 )
        {
            for ( const TestCaseInfo& testInfo : _listTest )
            {
                if ( isSelected( testInfo ) )
                    ++mapSuiteSelectedCount[testInfo._groupName];
            }
        }
        sw::vector<sw::string> listEmptySuite;
        for ( const sw::string& suiteName : listSuiteOrder )
        {
            const sw::pair<int32, int32>& ranSkipped = mapSuiteRanSkipped[suiteName];
            const uint32                  casesHere  = static_cast<uint32>( ranSkipped.first + ranSkipped.second ) / _repeatCount;
            const bool                    bSplit     = _shardCount > 1 && mapSuiteSelectedCount[suiteName] > casesHere;
            if ( ranSkipped.first == 0 && ranSkipped.second > 0 && bSplit == false )
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

        // 케이스 밖에서 만든 임시 경로와 빈 프로세스 폴더를 거둔다. 케이스 폴더는 케이스마다 따졌으니 여기 남는 것은 케이스 밖에서 연 경로다.
        if ( sw::FileUtil::removeDirectory( getProcessTempDirectory() ) == false )
            SW_LOG_WARNING( "Could not remove the process temp directory %# - is a file handle left open?", getProcessTempDirectory().c_str() );

        // 호스트 스위트만 고른 실행이 아무것도 안 돌았다면 그 ctest 항목(`<타깃>_HostOnly`)은 빈 그물이다.
        const bool bHostOnlyRanNothing = _hostSuiteMode == HostSuiteMode::Only && runnableCount == 0;

        std::fprintf( stdout, "====================================================\n" );
        if ( _hostSuiteMode == HostSuiteMode::Exclude && _mapHostSuiteReason.empty() == false )
        {
            std::fprintf( stdout, " Host suites left out (run them with --host_suites=only):" );
            for ( const auto& [suiteName, reason] : _mapHostSuiteReason )
                std::fprintf( stdout, " %s", suiteName.c_str() );
            std::fprintf( stdout, "\n" );
        }
        if ( bHostOnlyRanNothing )
            std::fprintf( stdout, " --host_suites=only selected no test - no SW_TEST_REQUIRES_HOST suite matched\n" );

        constexpr size_t                                   kSlowestShown = 5;
        sw::vector<sw::pair<float64, const TestCaseInfo*>> listElapsed;
        listElapsed.reserve( mapSlowestElapsed.size() );
        for ( const auto& [pTestInfo, slowestElapsed] : mapSlowestElapsed )
            listElapsed.push_back( { slowestElapsed, pTestInfo } );
        if ( listElapsed.size() > kSlowestShown )
        {
            std::partial_sort( listElapsed.begin(), listElapsed.begin() + kSlowestShown, listElapsed.end(),
                               []( const sw::pair<float64, const TestCaseInfo*>& lhs, const sw::pair<float64, const TestCaseInfo*>& rhs )
            {
                return lhs.first > rhs.first;
            } );
            std::fprintf( stdout, " Slowest cases:\n" );
            for ( size_t index = 0; index < kSlowestShown; ++index )
                std::fprintf( stdout, "   %9.2f ms  %s\n", listElapsed[index].first, listElapsed[index].second->fullName().c_str() );
        }

        printKnownErrorLogSummary();
        std::fprintf( stdout, " Tests passed: %d / %d (%d skipped, %.2f ms total)\n", passedCount, passedCount + failedCount + skippedCount, skippedCount, totalMs );
        if ( failedCount > 0 )
        {
            std::fprintf( stdout, " Tests failed (%d):\n", failedCount );
            for ( const auto& name : listFailedTestName )
            {
                std::fprintf( stdout, "   - %s\n", name.c_str() );
            }
            if ( _bShuffle )
                std::fprintf( stdout, " Replay this order with --test_shuffle=%u\n", _shuffleSeed );
        }
        std::fprintf( stdout, "====================================================\n" );
        std::fflush( stdout );

        const bool bPassed = failedCount == 0 && bEmptySuiteIsFailure == false && hostSuiteMismatchCount == 0 && bHostOnlyRanNothing == false;
        return bPassed ? 0 : 1;
    }
} // namespace test
