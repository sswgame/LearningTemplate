#include "pch.h"

#include "Core/Testing/TestRunner.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/StringUtil.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/File/FileUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/XorShift32.h"
#include "Core/Process/Process.h"
#include "Core/Process/StandardOutput.h"
#include "Core/String/RegistrationList.h"
#include "Core/String/StringBuilder.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/MonotonicClock.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace sw::test
{
    namespace
    {
        struct TestRunnerInternal
        {
            /** @brief @p pFile(정의한 파일)에 @p folder 가 들었으면 true 입니다. 구분자는 `/` 로 맞춰 비교한다. 폴더가 비면 늘 true 입니다. */
            static bool isInSourceFolder( const utf8* pFile, const string& folder )
            {
                if ( folder.empty() )
                    return true;
                if ( pFile == nullptr )
                    return false;
                string path{ pFile };
                std::replace( path.begin(), path.end(), '\\', '/' );
                return path.find( folder ) != string::npos;
            }

            /** @brief 일부러 내는 로그 하나의 선언과 나온 횟수입니다. */
            struct ExpectedLog
            {
                string      _id;
                const utf8* _pFile;
                int32       _line;
                uint32      _seenCount;
            };

            /** @brief 지금 도는 케이스의 기록입니다. 로그 리스너는 다른 스레드에서도 오므로 `_mutex` 로 지킨다. */
            struct CaseState
            {
                mutex                       _mutex;
                const TestCaseRegistration* _pCase{ nullptr };
                vector<ExpectedLog>         _listExpectedLog;
                vector<string>              _listFailure;
                vector<string>              _listUpdatedSnapshot;
                string                      _scratchFolder;
                uint32                      _assertionCount{ 0 };
                bool                        _bUpdateSnapshots{ false };
                bool                        _bActive{ false };
            };

            /** @brief 케이스 등록표입니다. 정적 초기화(모듈 이미지 로드)에서 채우고 내릴 때 뺀다 — 함수 안 정적이다(초기화 순서). */
            struct Registry
            {
                mutex                                        _mutex;
                RegistrationList<const TestCaseRegistration> _registration{ "TestCaseRegistry", RegistrationOrder::Insertion, true };
            };

            static Registry& getRegistry()
            {
                static Registry s_registry;
                return s_registry;
            }

            static CaseState& getState()
            {
                static CaseState s_state;
                return s_state;
            }

            static void writeLine( string_view text )
            {
                (void)StandardOutput::write( text );
                (void)StandardOutput::write( "\n" );
            }

            /** @brief 실패 하나를 적고 바로 찍습니다(잠금을 잡은 채 부르지 않는다 — 표준 출력은 잠금 밖). */
            static void addFailure( string text )
            {
                writeLine( text );
                CaseState&        state = getState();
                ScopedLock<mutex> lock{ state._mutex };
                state._listFailure.push_back( std::move( text ) );
            }

            /** @brief 케이스 동안 남은 로그를 봅니다. Warning · Error 는 선언한 id 면 세고, 아니면 실패로 적는다. */
            static void onLogWritten( const LogEntry& entry )
            {
                const bool bProblem = entry._level == LogLevel::Warning || entry._level == LogLevel::Error;
                if ( bProblem == false )
                    return;
                CaseState& state = getState();
                string     failure;
                {
                    ScopedLock<mutex> lock{ state._mutex };
                    if ( state._bActive == false )
                        return;
                    for ( ExpectedLog& expected : state._listExpectedLog )
                    {
                        if ( entry._pID != nullptr && expected._id == entry._pID )
                        {
                            ++expected._seenCount;
                            return;
                        }
                    }
                    failure = string( entry._file ) + "(" + to_string( entry._line ) + "): error: unexpected " + ( entry._level == LogLevel::Error ? "Error" : "Warning" ) +
                              " log" + ( entry._pID != nullptr ? string( " [id:" ) + entry._pID + "]" : string( " (no id - give it one to declare it with SW_EXPECT_LOG)" ) ) + ": " +
                              entry._message;
                }
                addFailure( std::move( failure ) );
            }

            /**
             * @brief 케이스 시한 감시입니다. 실행기가 케이스마다 기한을 넣고, 감시 스레드가 넘긴 것을 보면 이름을 찍고 프로세스를 끝낸다.
             * @details 멈춘 케이스(무한 루프 · 교착)는 그 스레드에서 되살릴 수 없다 — 이름도 없이 CTest 시한(600 초)에 죽는 대신 바로 어느 케이스인지 남긴다.
             *          디버거가 붙어 있으면 끈다(중단점에 선 것을 멈춤으로 보지 않는다).
             */
            struct Watchdog
            {
                atomic<int64>       _deadlineNanoseconds{ 0 }; ///< 0 이면 보는 케이스가 없다
                atomic<int64>       _timeoutMilliseconds{ 0 };
                atomic<const utf8*> _pCaseName{ nullptr };
            };

            static Watchdog& getWatchdog()
            {
                static Watchdog s_watchdog;
                return s_watchdog;
            }

            static void watchCaseDeadline( StopToken stopToken )
            {
                Watchdog& watchdog = getWatchdog();
                while ( stopToken.isStopRequested() == false )
                {
                    MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + 100 * constant::kNanosecondsPerMillisecond );
                    const int64 deadline = watchdog._deadlineNanoseconds.load( std::memory_order_acquire );
                    if ( deadline == 0 || MonotonicClock::nowNanoseconds() < deadline )
                        continue;
                    // 할당 · 잠금 없이 바로 쓴다 — 멈춘 케이스가 잠금을 쥐고 있을 수 있다.
                    const utf8* pName = watchdog._pCaseName.load( std::memory_order_acquire );
                    std::fprintf( stdout, "\n[ TIMEOUT  ] %s did not finish within %lld ms - the run stops here (a stuck case cannot be resumed; raise ._timeoutSeconds if it is just slow)\n",
                                  pName != nullptr ? pName : "(unknown)", static_cast<long long>( watchdog._timeoutMilliseconds.load() ) );
                    std::fflush( stdout );
                    Logger::flushGlobalForCrash();
                    std::_Exit( kTestTimeoutExitCode );
                }
            }

            /** @brief 스냅숏 · 쓰기 폴더 이름에 쓰는 케이스 이름 — 매개변수 케이스의 `/` 를 `.` 으로(`Suite.Name/2` → `Suite.Name.2`). */
            static string toFileName( string_view caseName )
            {
                string name( caseName );
                std::replace( name.begin(), name.end(), '/', '.' );
                return name;
            }

            static uint32 pickSeed( uint32 requested )
            {
                if ( requested != 0 )
                    return requested;
                const uint32 fromClock = static_cast<uint32>( MonotonicClock::nowNanoseconds() ) | 1u; // XorShift 는 0 상태에서 멈춘다
                return fromClock;
            }

            /** @brief 피셔-예이츠 섞기(XorShift32 — 표준 라이브러리마다 다른 std::shuffle 을 쓰지 않는다. 같은 씨앗이면 어느 빌드에서나 같은 순서다). */
            static void shuffle( vector<const TestCaseRegistration*>& inoutList, uint32 seed )
            {
                uint32 state = seed;
                for ( size_t index = inoutList.size(); index > 1; --index )
                {
                    state             = XorShift32::next( state );
                    const size_t pick = state % index;
                    std::swap( inoutList[index - 1], inoutList[pick] );
                }
            }

            static string escapeJSON( string_view text )
            {
                string out;
                out.reserve( text.size() + 8 );
                for ( const char ch : text )
                {
                    if ( ch == '"' || ch == '\\' )
                    {
                        out += '\\';
                        out += ch;
                    }
                    else if ( ch == '\n' )
                        out += "\\n";
                    else if ( ch == '\r' )
                        continue;
                    else
                        out += ch;
                }
                return out;
            }
        };
    } // namespace
} // namespace sw::test

namespace sw::test
{
    TestCaseRegistrar::TestCaseRegistrar( const TestCaseRegistration* pRegistration )
        : _pRegistration{ pRegistration }
        , _bRegistered{ false }
    {
        TestRunnerInternal::Registry& registry = TestRunnerInternal::getRegistry();
        RegistrationResult            result   = RegistrationResult::Added;
        {
            ScopedLock<mutex> lock{ registry._mutex };
            result = registry._registration.add( pRegistration, pRegistration->_pName );
        }
        reportRegistrationRejected( result, "Test case", pRegistration->_pName );
        _bRegistered = result == RegistrationResult::Added;
    }

    TestCaseRegistrar::~TestCaseRegistrar()
    {
        if ( _bRegistered == false )
            return;
        TestRunnerInternal::Registry& registry = TestRunnerInternal::getRegistry();
        ScopedLock<mutex>             lock{ registry._mutex };
        (void)registry._registration.remove( _pRegistration );
    }

    TestCaseEachRegistrar::TestCaseEachRegistrar( const utf8* pBaseName, size_t count, TestIndexedFunction pfnRun, const utf8* pFile, int32 line,
                                                  const TestCaseOptions& options )
    {
        _listName.reserve( count );
        _listRegistration.reserve( count ); // 크기를 먼저 정한다 — 등록표가 원소의 주소를 쥔다
        for ( size_t index = 0; index < count; ++index )
        {
            _listName.push_back( formatToString( "%#/%#", string( pBaseName ), index ) );
        }
        for ( size_t index = 0; index < count; ++index )
        {
            _listRegistration.push_back( TestCaseRegistration{ _listName[index].c_str(), nullptr, pFile, line, options, pfnRun, index } );
        }
        for ( const TestCaseRegistration& registration : _listRegistration )
        {
            _listRegistrar.push_back( make_unique<TestCaseRegistrar>( &registration ) );
        }
    }

    TestCaseEachRegistrar::~TestCaseEachRegistrar()
    {
        _listRegistrar.clear(); // 등록표에서 먼저 뺀 뒤 이름 · 등록 줄을 놓는다
    }

    void reportFailure( const utf8* pFile, int32 line, string_view message )
    {
        TestRunnerInternal::addFailure( formatToString( "%#(%#): error: %#", string( pFile ), line, string( message ) ) );
    }

    void countAssertion()
    {
        TestRunnerInternal::CaseState& state = TestRunnerInternal::getState();
        ScopedLock<mutex>              lock{ state._mutex };
        ++state._assertionCount;
    }

    void expectLog( const utf8* pLogID, const utf8* pFile, int32 line )
    {
        TestRunnerInternal::CaseState& state = TestRunnerInternal::getState();
        ScopedLock<mutex>              lock{ state._mutex };
        ++state._assertionCount; // 선언 자체가 확인이다(끝까지 나오지 않으면 실패)
        state._listExpectedLog.push_back( TestRunnerInternal::ExpectedLog{ string( pLogID ), pFile, line, 0 } );
    }

    string makeScratchPath( string_view name )
    {
        TestRunnerInternal::CaseState& state = TestRunnerInternal::getState();
        string                         folder;
        {
            ScopedLock<mutex> lock{ state._mutex };
            folder = state._scratchFolder;
        }
        SW_ASSERT( folder.empty() == false && "makeScratchPath is only valid inside a running test case" );
        if ( FileUtil::ensureDirectoryExists( folder ) == false )
            reportFailure( __FILE__, __LINE__, "could not create the scratch folder " + folder );
        return FileUtil::joinPath( folder, name );
    }

    bool checkSnapshot( string_view name, string_view actual, const utf8* pFile, int32 line )
    {
        countAssertion();
        TestRunnerInternal::CaseState& state = TestRunnerInternal::getState();
        string                         caseName;
        bool                           bUpdate = false;
        {
            ScopedLock<mutex> lock{ state._mutex };
            caseName = state._pCase != nullptr ? state._pCase->_pName : "";
            bUpdate  = state._bUpdateSnapshots;
        }
        const string path = FileUtil::joinPath( FileUtil::joinPath( FileUtil::getDirectoryPart( pFile ), "Snapshots" ),
                                                TestRunnerInternal::toFileName( caseName ) + "." + string( name ) + ".txt" );
        string       normalized( actual );
        normalized.erase( std::remove( normalized.begin(), normalized.end(), '\r' ), normalized.end() );
        string     expected;
        const bool bHasSnapshot = FileUtil::exists( path ) && FileUtil::readTextFile( path, expected );
        expected.erase( std::remove( expected.begin(), expected.end(), '\r' ), expected.end() );
        if ( bHasSnapshot && expected == normalized )
            return true;
        if ( bUpdate )
        {
            const bool bWritten = FileUtil::ensureDirectoryExists( FileUtil::getDirectoryPart( path ) ) && FileUtil::writeTextFile( path, normalized );
            if ( bWritten == false )
            {
                reportFailure( pFile, line, "could not write the snapshot " + path );
                return false;
            }
            ScopedLock<mutex> lock{ state._mutex };
            state._listUpdatedSnapshot.push_back( path );
            return true;
        }
        reportFailure( pFile, line,
                       string( bHasSnapshot ? "snapshot differs: " : "no snapshot yet: " ) + path + " (run App --test --update-snapshots and review the diff)" );
        return false;
    }

    bool checkNear( float64 expected, float64 actual, float64 tolerance, const utf8* pExpression, const utf8* pFile, int32 line )
    {
        countAssertion();
        // NaN 은 어떤 비교도 거짓이라 `<=` 가 거짓 — 통과로 새지 않는다.
        if ( std::fabs( expected - actual ) <= tolerance )
            return true;
        utf8 arrBuffer[constant::kMaxBuffer1024];
        formatstring( arrBuffer, constant::kMaxBuffer1024, "%# — expected %#, actual %#, tolerance %#", pExpression, expected, actual, tolerance );
        reportFailure( pFile, line, arrBuffer );
        return false;
    }

    bool checkConditionWithMessage( bool bPassed, const utf8* pExpression, string_view message, const utf8* pFile, int32 line )
    {
        countAssertion();
        if ( bPassed == false )
            reportFailure( pFile, line, string( pExpression ) + " — " + string( message ) );
        return bPassed;
    }

    bool checkCondition( bool bPassed, const utf8* pExpression, const utf8* pFile, int32 line )
    {
        countAssertion();
        if ( bPassed == false )
            reportFailure( pFile, line, pExpression );
        return bPassed;
    }

    bool TestRunner::matchesFilter( string_view filter, string_view name )
    {
        return matchesFilter( filter, name, {} );
    }

    bool TestRunner::matchesFilter( string_view filter, string_view name, string_view tags )
    {
        const auto matchesPattern = [name, tags]( string_view pattern )
        {
            if ( pattern.size() >= 2 && pattern.front() == '[' && pattern.back() == ']' )
            {
                const string_view tag = pattern.substr( 1, pattern.size() - 2 );
                for ( const string_view word : StringUtil::split( tags, " ", { ._bTrim = true, ._bDropEmpty = true } ) )
                {
                    if ( word == tag )
                        return true;
                }
                return false;
            }
            return StringUtil::matchesWildcard( pattern, name );
        };
        // 쉼표로 가른 패턴 — 앞에 `-` 가 붙은 것은 빼기다. 더하기 패턴이 하나도 없으면 모두를 더한 것으로 본다.
        bool   bIncluded    = false;
        bool   bHasPositive = false;
        size_t start        = 0;
        while ( start <= filter.size() )
        {
            const size_t      end     = MathUtil::min( filter.find( ',', start ), filter.size() );
            const string_view pattern = filter.substr( start, end - start );
            if ( pattern.empty() == false )
            {
                if ( pattern.front() == '-' )
                {
                    if ( matchesPattern( pattern.substr( 1 ) ) )
                        return false;
                }
                else
                {
                    bHasPositive = true;
                    bIncluded    = bIncluded || matchesPattern( pattern );
                }
            }
            start = end + 1;
        }
        return bHasPositive ? bIncluded : true;
    }

    bool TestRunner::parseShard( string_view text, uint32& outIndex, uint32& outCount )
    {
        const size_t slash = text.find( '/' );
        int64        index = -1;
        int64        count = 0;
        if ( slash == string_view::npos || StringUtil::parseInt64( text.substr( 0, slash ), index ) == false ||
             StringUtil::parseInt64( text.substr( slash + 1 ), count ) == false || count < 1 || index < 0 || index >= count )
            return false;
        outIndex = static_cast<uint32>( index );
        outCount = static_cast<uint32>( count );
        return true;
    }

    uint32 TestRunner::getShardOf( string_view name, uint32 shardCount )
    {
        uint32 hash = HashUtil::kFNVOffset32;
        for ( const char ch : name )
        {
            hash = HashUtil::fnvStep32( hash, static_cast<uint8>( ch ) );
        }
        return shardCount <= 1 ? 0 : hash % shardCount;
    }

    vector<string> TestRunner::collectCaseNames()
    {
        vector<string>                listName;
        TestRunnerInternal::Registry& registry = TestRunnerInternal::getRegistry();
        {
            ScopedLock<mutex> lock{ registry._mutex };
            for ( const TestCaseRegistration* pCase : registry._registration.getItems() )
            {
                listName.emplace_back( pCase->_pName );
            }
        }
        std::sort( listName.begin(), listName.end() );
        return listName;
    }

    bool TestRunner::run( const TestRunOptions& options )
    {
        using Internal = TestRunnerInternal;
        vector<const TestCaseRegistration*> listCase;
        {
            Internal::Registry& registry = Internal::getRegistry();
            ScopedLock<mutex>   lock{ registry._mutex };
            for ( const TestCaseRegistration* pCase : registry._registration.getItems() )
            {
                if ( matchesFilter( options._filter, pCase->_pName, pCase->_options._tags ) && getShardOf( pCase->_pName, options._shardCount ) == options._shardIndex &&
                     Internal::isInSourceFolder( pCase->_pFile, options._sourceFolder ) )
                    listCase.push_back( pCase );
            }
        }
        // 이름 순으로 놓은 뒤 섞는다 — 등록 순서(링크 순서)에 기대지 않고, 씨앗이 같으면 어느 빌드에서나 같은 순서다.
        std::sort( listCase.begin(), listCase.end(), []( const TestCaseRegistration* pA, const TestCaseRegistration* pB )
        { return StringUtil::compare( pA->_pName, pB->_pName ) < 0; } );
        if ( options._bListOnly )
        {
            for ( const TestCaseRegistration* pCase : listCase )
            {
                Internal::writeLine( pCase->_pName );
            }
            return true;
        }
        // 비활성 케이스는 돌지 않고 까닭과 함께 센다(지우지 않고 끈 자리가 보고에 남는다).
        vector<const TestCaseRegistration*> listDisabled;
        if ( options._bRunDisabled == false )
        {
            const auto isDisabled = []( const TestCaseRegistration* pCase )
            { return pCase->_options._disabledReason != nullptr; };
            std::copy_if( listCase.begin(), listCase.end(), std::back_inserter( listDisabled ), isDisabled );
            listCase.erase( std::remove_if( listCase.begin(), listCase.end(), isDisabled ), listCase.end() );
        }
        for ( const TestCaseRegistration* pCase : listDisabled )
        {
            Internal::writeLine( string( "[ DISABLED ] " ) + pCase->_pName + " - " + pCase->_options._disabledReason );
        }
        // 빈 샤드는 실패가 아니다 — 케이스가 몫보다 적으면 빈 몫이 생긴다. 빈 거르개는 실패다.
        if ( listCase.empty() && options._shardCount <= 1 )
        {
            Internal::writeLine( string( "[  NO TESTS ] no case matches the filter '" ) + options._filter + "' - an empty run is not a pass" );
            return false;
        }

        const uint32 baseSeed    = Internal::pickSeed( options._seed );
        const string scratchRoot = FileUtil::joinPath( options._scratchRoot.empty() ? string( "Saved/Test" ) : options._scratchRoot, to_string( Process::getCurrentProcessID() ) );
        Subscription logListener = Logger::registerGlobalListener( SW_DELEGATE_FUNCTION( LogWrittenDelegate, &Internal::onLogWritten ) );
        thread       watchThread;
        if ( Process::isDebuggerPresent() == false )
            watchThread = thread( &Internal::watchCaseDeadline );
        const uint32   repeatCount = MathUtil::max( options._repeatCount, 1u );
        uint32         failedCount = 0;
        uint32         ranCount    = 0;
        string         caseReport;
        vector<string> listUpdatedSnapshot;
        const int64    runStart = MonotonicClock::nowNanoseconds();
        utf8           arrBuffer[constant::kMaxBuffer1024];

        formatstring( arrBuffer, constant::kMaxBuffer1024, "[==========] %# of %# registered case(s), seed %# (repeat with --test-seed=%#), %# round(s), shard %#/%#",
                      static_cast<uint32>( listCase.size() ), static_cast<uint32>( collectCaseNames().size() ), baseSeed, baseSeed, repeatCount, options._shardIndex,
                      options._shardCount );
        Internal::writeLine( arrBuffer );
        for ( uint32 roundIndex = 0; roundIndex < repeatCount; ++roundIndex )
        {
            vector<const TestCaseRegistration*> listRound = listCase;
            Internal::shuffle( listRound, baseSeed + roundIndex );
            for ( const TestCaseRegistration* pCase : listRound )
            {
                Internal::CaseState& state = Internal::getState();
                {
                    ScopedLock<mutex> lock{ state._mutex };
                    state._pCase = pCase;
                    state._listExpectedLog.clear();
                    state._listFailure.clear();
                    state._listUpdatedSnapshot.clear();
                    state._assertionCount   = 0;
                    state._bUpdateSnapshots = options._bUpdateSnapshots;
                    state._scratchFolder    = FileUtil::joinPath( FileUtil::joinPath( scratchRoot, Internal::toFileName( pCase->_pName ) ), to_string( roundIndex ) );
                    state._bActive          = true;
                }
                Internal::writeLine( string( "[ RUN      ] " ) + pCase->_pName );
                CrashHandler::setContextValue( "TestCase", pCase->_pName ); // 크래시 보고에 어느 케이스였는지 남는다
                const float32       timeoutSeconds = pCase->_options._timeoutSeconds > 0.0f ? pCase->_options._timeoutSeconds : kDefaultCaseTimeoutSeconds;
                const int64         timeoutNanos   = static_cast<int64>( static_cast<float64>( timeoutSeconds ) * 1.0e9 );
                const int64         caseStart      = MonotonicClock::nowNanoseconds();
                Internal::Watchdog& watchdog       = Internal::getWatchdog();
                watchdog._pCaseName.store( pCase->_pName, std::memory_order_release );
                watchdog._timeoutMilliseconds.store( timeoutNanos / constant::kNanosecondsPerMillisecond, std::memory_order_release );
                watchdog._deadlineNanoseconds.store( caseStart + timeoutNanos, std::memory_order_release );
                if ( pCase->_pfnRunIndexed != nullptr )
                    pCase->_pfnRunIndexed( pCase->_index );
                else
                    pCase->_pfnRun();
                watchdog._deadlineNanoseconds.store( 0, std::memory_order_release );
                const int64 caseMilliseconds = ( MonotonicClock::nowNanoseconds() - caseStart ) / constant::kNanosecondsPerMillisecond;

                vector<string> listLateFailure;
                {
                    ScopedLock<mutex> lock{ state._mutex };
                    state._bActive = false;
                    if ( state._assertionCount == 0 )
                        listLateFailure.push_back( formatToString( "%#(%#): error: the case made no assertion - a case that checks nothing is not a pass", string( pCase->_pFile ), pCase->_line ) );
                    for ( const Internal::ExpectedLog& expected : state._listExpectedLog )
                    {
                        if ( expected._seenCount == 0 )
                            listLateFailure.push_back( formatToString( "%#(%#): error: expected log [id:%#] never appeared", string( expected._pFile ), expected._line, expected._id ) );
                    }
                }
                for ( string& failure : listLateFailure )
                {
                    Internal::addFailure( std::move( failure ) );
                }

                vector<string> listFailure;
                {
                    ScopedLock<mutex> lock{ state._mutex };
                    listFailure = state._listFailure;
                    listUpdatedSnapshot.insert( listUpdatedSnapshot.end(), state._listUpdatedSnapshot.begin(), state._listUpdatedSnapshot.end() );
                    state._pCase = nullptr;
                }
                const bool bPassed = listFailure.empty();
                ++ranCount;
                if ( bPassed == false )
                    ++failedCount;
                formatstring( arrBuffer, constant::kMaxBuffer1024, "%# %# (%# ms)", bPassed ? "[       OK ]" : "[  FAILED  ]", pCase->_pName, caseMilliseconds );
                Internal::writeLine( arrBuffer );

                caseReport += caseReport.empty() ? "\n" : ",\n";
                caseReport += string( "    {\"name\": \"" ) + Internal::escapeJSON( pCase->_pName ) + "\", \"round\": " + to_string( roundIndex ) + ", \"passed\": " +
                              formatToString( "%#, \"milliseconds\": %#, \"failures\": [", ( bPassed ? "true" : "false" ), caseMilliseconds );
                for ( size_t failureIndex = 0; failureIndex < listFailure.size(); ++failureIndex )
                {
                    caseReport += ( failureIndex == 0 ? "\"" : ", \"" ) + Internal::escapeJSON( listFailure[failureIndex] ) + "\"";
                }
                caseReport += "]}";
            }
        }
        logListener.reset();
        watchThread = thread{}; // 감시를 세우고 합류한다
        CrashHandler::setContextValue( "TestCase", "" );

        const int64 runMilliseconds = ( MonotonicClock::nowNanoseconds() - runStart ) / constant::kNanosecondsPerMillisecond;
        formatstring( arrBuffer, constant::kMaxBuffer1024, "[==========] %# case run(s) (%# ms), %# passed, %# failed, seed %#", ranCount, runMilliseconds, ranCount - failedCount, failedCount, baseSeed );
        Internal::writeLine( arrBuffer );
        for ( const string& path : listUpdatedSnapshot )
        {
            Internal::writeLine( string( "[ SNAPSHOT ] updated " ) + path );
        }

        // 보고는 늘 쓴다 — 주지 않으면 이 실행의 폴더(프로세스마다 다르다)에 쓴다. 다음 실행 · 나란한 실행이 덮어쓰지 않는다.
        const string reportPath = options._reportPath.empty() ? FileUtil::joinPath( scratchRoot, "report.json" ) : options._reportPath;
        {
            string report = "{\n  \"seed\": " + to_string( baseSeed ) + ",\n  \"filter\": \"" + Internal::escapeJSON( options._filter ) + "\",\n  \"selectedCount\": " +
                            to_string( listCase.size() ) + ",\n  \"runCount\": " + to_string( ranCount ) + ",\n  \"failedCount\": " + to_string( failedCount ) +
                            ",\n  \"milliseconds\": " + to_string( runMilliseconds ) + ",\n  \"shard\": \"" + to_string( options._shardIndex ) + "/" +
                            formatToString( "%#\",\n  \"disabled\": [", options._shardCount );
            for ( size_t index = 0; index < listDisabled.size(); ++index )
            {
                report += string( index == 0 ? "" : ", " ) + "{\"name\": \"" + Internal::escapeJSON( listDisabled[index]->_pName ) + "\", \"reason\": \"" +
                          Internal::escapeJSON( listDisabled[index]->_options._disabledReason ) + "\"}";
            }
            report += "],\n  \"updatedSnapshots\": [";
            for ( size_t index = 0; index < listUpdatedSnapshot.size(); ++index )
            {
                report += ( index == 0 ? "\"" : ", \"" ) + Internal::escapeJSON( listUpdatedSnapshot[index] ) + "\"";
            }
            report += "],\n  \"cases\": [" + caseReport + "\n  ]\n}\n";
            if ( FileUtil::ensureDirectoryExists( FileUtil::getDirectoryPart( reportPath ) ) == false || FileUtil::writeTextFile( reportPath, report ) == false )
            {
                Internal::writeLine( "[  FAILED  ] could not write the test report " + reportPath );
                return false;
            }
            Internal::writeLine( "[  REPORT  ] " + reportPath );
        }
        return failedCount == 0;
    }
} // namespace sw::test
