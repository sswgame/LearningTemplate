#include "pch.h"

#include "Core/Testing/TestRunner.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/XorShift32.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Process/Process.h"
#include "Core/Process/StandardOutput.h"
#include "Core/Registry/RegistrationList.h"
#include "Core/String/JSONStringUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Testing/ThreadTestAccess.h"
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
                for ( const string_view one : StringUtil::split( folder, ",", { ._bTrim = true, ._bDropEmpty = true } ) )
                {
                    if ( path.find( one.data(), 0, one.size() ) != string::npos )
                        return true;
                }
                return false;
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
                string                      _childArgument;               ///< 자식 역할로 띄워졌을 때 `--test-child-arg` 의 값
                uint32                      _declaredErrorLineCount{ 0 }; ///< 실행 전체에서 선언과 맞은 Error 줄 수(`kDeclaredErrorLinesPrefix` 줄로 알린다)
                bool                        _bIsolatedRun{ false };       ///< 격리 케이스의 자식 프로세스로 띄워졌다(`--test-isolated-case`) NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
                uint32                      _assertionCount{ 0 };
                bool                        _bUpdateSnapshots{ false }; ///< NOLINT(sw-bool-flag): 참조(`bool&`)로 넘긴다
                bool                        _bActive{ SW_FALSE };       ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
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

            /// @brief 지금 도는 실행의 케이스 상태입니다(`ActiveRunState` 가 건다). 실행 밖에서는 nullptr 이다.
            static inline atomic<CaseState*> s_pActiveState{ nullptr };

            /**
             * @brief 실행 하나가 소유하는 케이스 상태입니다 — 실행이 끝나면 함께 사라진다.
             * @details 예전에는 함수 안 static 이라 실행 밖(시나리오의 로그 경로)에서도 만들어지고, 실행 뒤에도 남아 종료 누수 보고에 찍혔다(MSVC Debug 컨테이너 프록시까지).
             */
            /** @brief 지금 도는 케이스가 `SW_EXPECT_LOG` 로 적은 id 이면 true 입니다. 시나리오 안에서 단위 테스트를 돌릴 때(`editor.unitTests`) 로그 게이트가 묻는다(`test::isExpectedLog`). */
            static bool isExpectedByActiveCase( const utf8* pLogID )
            {
                CaseState* const pState = s_pActiveState.load( std::memory_order_acquire );
                if ( pLogID == nullptr || pState == nullptr )
                    return false;
                CaseState&        state = *pState;
                ScopedLock<mutex> lock{ state._mutex };
                return state._bActive && VectorUtil::containsIf( state._listExpectedLog, [&]( const ExpectedLog& expected )
                { return expected._id == pLogID; } );
            }

            struct ActiveRunState
            {
                CaseState _state;
                ActiveRunState()
                {
                    s_pActiveState.store( &_state, std::memory_order_release );
                }
                ~ActiveRunState()
                {
                    s_pActiveState.store( nullptr, std::memory_order_release );
                }
                ActiveRunState( const ActiveRunState& )            = delete;
                ActiveRunState& operator=( const ActiveRunState& ) = delete;
            };

            /** @brief 지금 실행의 케이스 상태입니다. 테스트 API(`SW_EXPECT` · `expectLog` …)는 실행 안에서만 부른다. */
            static CaseState& getState()
            {
                CaseState* const pState = s_pActiveState.load( std::memory_order_acquire );
                SW_ASSERT( pState != nullptr && "test API used outside TestRunner::run" );
                return *pState;
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
                    // 같은 id 를 여러 번 선언하면 선언마다 한 번씩 채운다 - 아직 못 본 선언이 먼저, 다 보았으면 첫 선언이 더 센다.
                    ExpectedLog* pMatch = nullptr;
                    for ( ExpectedLog& expected : state._listExpectedLog )
                    {
                        if ( entry._pID == nullptr || expected._id != entry._pID )
                            continue;
                        if ( pMatch == nullptr )
                            pMatch = &expected;
                        if ( expected._seenCount == 0 )
                        {
                            pMatch = &expected;
                            break;
                        }
                    }
                    if ( pMatch != nullptr )
                    {
                        ++pMatch->_seenCount;
                        if ( entry._level == LogLevel::Error )
                            ++state._declaredErrorLineCount;
                        return;
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
                    MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + 100 * constant::time::kNanosecondsPerMillisecond );
                    const int64 deadline = watchdog._deadlineNanoseconds.load( std::memory_order_acquire );
                    if ( deadline == 0 || MonotonicClock::nowNanoseconds() < deadline )
                        continue;
                    // 할당 · 잠금 없이 바로 쓴다 — 멈춘 케이스가 잠금을 쥐고 있을 수 있다.
                    const utf8* pName = watchdog._pCaseName.load( std::memory_order_acquire );
                    std::fprintf( stdout, "\n[ TIMEOUT  ] %s did not finish within %lld ms - the run stops here (a stuck case cannot be resumed; raise ._timeoutSeconds if it is just slow)\n",
                                  pName != nullptr ? pName : "(unknown)", static_cast<long long>( watchdog._timeoutMilliseconds.load() ) );
                    std::fflush( stdout );
                    Logger::flushGlobalForCrash();
                    Process::exitImmediately( kTestTimeoutExitCode );
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

            /** @brief 이 실행 파일을 @p listArgument 로 자식으로 띄워 끝까지 기다립니다. 시한을 넘기면 죽이고 `_bTimedOut` 이다. 띄우지 못하면 실패로 적는다(케이스 안에서만 부른다). */
            static void launchSelf( vector<string> listArgument, float32 timeoutSeconds, ChildRunResult& outResult );
            /** @brief 격리 케이스(`._bIsolated`) 하나를 자식에서 돌리고 결과를 지금 케이스에 적습니다. 자식이 0 이 아닌 종료 코드면 출력 꼬리와 함께 실패다. */
            static void runIsolated( const TestCaseRegistration& testCase, float32 timeoutSeconds );

            static string escapeJSON( string_view text )
            {
                string out;
                JSONStringUtil::appendEscaped( out, text );
                return out;
            }
        };
    } // namespace
} // namespace sw::test

namespace sw::test
{
    TestCaseRegistrar::TestCaseRegistrar( const TestCaseRegistration* pRegistration )
        : _pRegistration{ pRegistration }
        , _bRegistered{ SW_FALSE }
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

    TestCaseEachRegistrar::TestCaseEachRegistrar( const utf8* pBaseName, size_t count, TestIndexedFunction pfnRun, TestParamNameFunction pfnName, const utf8* pFile, int32 line,
                                                  const TestCaseOptions& options )
    {
        _listName.reserve( count );
        _listRegistration.reserve( count ); // 크기를 먼저 정한다 — 등록표가 원소의 주소를 쥔다
        for ( size_t index = 0; index < count; ++index )
        {
            // 안정 이름(`_pTestName`)이 있으면 그것 — 배열 순서가 바뀌어도 이름이 같은 입력을 가리킨다. 비면 등록표가 이름 없는 줄로 거절한다.
            const utf8* const pParamName = pfnName != nullptr ? pfnName( index ) : nullptr;
            if ( pfnName != nullptr && pfnName( 0 ) != nullptr )
            {
                StringBuilder<constant::buffer::kMax256> textBuilder2;
                textBuilder2.appendFormat( "%#/%#", string( pBaseName ), string( pParamName != nullptr ? pParamName : "" ) );
                _listName.push_back( string( textBuilder2.view() ) );
            }
            else
            {
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "%#/%#", string( pBaseName ), index );
                _listName.push_back( string( textBuilder.view() ) );
            }
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
        StringBuilder<constant::buffer::kMax256> textBuilder;
        textBuilder.appendFormat( "%#(%#): error: %#", string( pFile ), line, string( message ) );
        TestRunnerInternal::addFailure( string( textBuilder.view() ) );
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

    bool isExpectedLog( const utf8* pLogID )
    {
        return TestRunnerInternal::isExpectedByActiveCase( pLogID );
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
        const bool bHasSnapshot = FileUtil::readOptionalTextFile( path, expected ) == OptionalFileRead::Read;
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
        utf8 arrBuffer[constant::buffer::kMax1024];
        formatstring( arrBuffer, constant::buffer::kMax1024, "%# — expected %#, actual %#, tolerance %#", pExpression, expected, actual, tolerance );
        reportFailure( pFile, line, arrBuffer );
        return false;
    }

    string ChildRunResult::getOutputTail( uint32 lineCount ) const
    {
        size_t start = _output.size();
        for ( uint32 line = 0; line <= lineCount && start > 0; ++line )
        {
            const size_t newline = _output.rfind( '\n', start - 1 );
            if ( newline == string::npos )
            {
                start = 0;
                break;
            }
            start = newline;
        }
        return _output.substr( start );
    }

    ChildRunResult runChildRole( string_view roleName, string_view argument, float32 timeoutSeconds )
    {
        ChildRunResult result;
        bool           bRegistered = false;
        {
            TestRunnerInternal::Registry& registry = TestRunnerInternal::getRegistry();
            ScopedLock<mutex>             lock{ registry._mutex };
            for ( const TestCaseRegistration* pCase : registry._registration.getItems() )
                bRegistered = bRegistered || ( pCase->_bChildRole && roleName == pCase->_pName );
        }
        if ( bRegistered == false )
        {
            reportFailure( __FILE__, __LINE__, "no child role named " + string( roleName ) + " is registered (declare it with SW_TEST_CHILD_ROLE)" );
            return result;
        }
        // 자식의 로그 파일은 이 케이스의 쓰기 폴더에 둔다 — 자식마다 `Saved/Logs` 에 파일이 쌓이지 않게.
        vector<string> listArgument{ "-test", "-test-child-role=" + string( roleName ), "-unattended",
                                     "-log-file=" + makeScratchPath( TestRunnerInternal::toFileName( roleName ) + ".child.log" ) };
        if ( argument.empty() == false )
            listArgument.push_back( "-test-child-arg=" + string( argument ) );
        TestRunnerInternal::launchSelf( std::move( listArgument ), timeoutSeconds, result );
        return result;
    }

    void TestRunnerInternal::launchSelf( vector<string> listArgument, float32 timeoutSeconds, ChildRunResult& outResult )
    {
        ChildRunResult&              result     = outResult;
        const optional<AbsolutePath> executable = AbsolutePath::tryMake( FileUtil::getExecutablePath() );
        if ( executable.has_value() == false )
        {
            reportFailure( __FILE__, __LINE__, "the test executable path is not absolute: " + FileUtil::getExecutablePath() );
            return;
        }
        const ProcessCommand command = ProcessCommand::at( *executable, std::move( listArgument ) );
        ProcessOptions       options;
        options._workingDirectory = FileUtil::getCurrentPath();
        Process process;
        result._bLaunched = process.launch( command, options );
        if ( result._bLaunched == false )
        {
            reportFailure( __FILE__, __LINE__, "could not launch the child " + command.toCommandLine() );
            return;
        }
        // 출력을 읽는 이 스레드는 자식이 말없이 멈추면 깨지 않는다 — 감시 스레드가 시한에 죽인다(`terminate` 는 다른 스레드가 읽는 중에도 안전한 계약).
        atomic<bool>   bTimedOut{ false };
        const Deadline deadline = Deadline::afterMilliseconds( static_cast<int64>( static_cast<float64>( timeoutSeconds ) * 1000.0 ) );
        thread         watchThread( ThreadTestAccess::getKey(), [&process, &bTimedOut, deadline]( const StopToken& stopToken )
                {
            while ( stopToken.isStopRequested() == false )
            {
                if ( deadline.isExpired() )
                {
                    bTimedOut.store( true, std::memory_order_release );
                    (void)process.terminate( 1 );
                    return;
                }
                MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + 20 * constant::time::kNanosecondsPerMillisecond );
            }
        } );
        string         line;
        while ( process.readOutputLine( line ) )
        {
            result._output += line;
            result._output.push_back( '\n' );
        }
        const expected<int32> waited = process.waitForExit();
        if ( waited.has_value() )
            result._exitCode = *waited;
        else
            result._output += "[test runner] could not wait for the child: " + string( waited.error().message() ) + " " + string( waited.error().detail() ) + "\n";
        watchThread       = thread{};                                    // 정지를 요청하고 합류한다
        result._bTimedOut = bTimedOut.load( std::memory_order_acquire ); // 실패로 적지 않는다 — 시한에 죽이는 것을 보는 케이스도 있다. 죽은 자식은 종료 코드가 0 이 아니다
    }

    void TestRunnerInternal::runIsolated( const TestCaseRegistration& testCase, float32 timeoutSeconds )
    {
        // 자식은 이 케이스 하나만 같은 판정 규칙으로 돈다(`--test-isolated-case`). 크래시 · 교착 · 강제 종료는 종료 코드로 이 케이스의 실패가 되고 부모 실행은 이어진다.
        vector<string> listArgument{ "-test", "-test-isolated-case=" + string( testCase._pName ), "-unattended",
                                     "-log-file=" + makeScratchPath( toFileName( testCase._pName ) + ".isolated.log" ) };
        ChildRunResult child;
        launchSelf( std::move( listArgument ), timeoutSeconds, child );
        countAssertion();
        if ( child._bLaunched == false )
            return; // 띄우지 못한 까닭은 위에서 적었다
        if ( child._exitCode.has_value() && *child._exitCode == 0 )
            return;
        StringBuilder<constant::buffer::kMax256> textBuilder;
        const string                             exitText = child._exitCode.has_value() ? "exit code " + to_string( *child._exitCode ) : string( "not waited for" );
        textBuilder.appendFormat( "the isolated case %# (%#) - child output:\n%#", child._bTimedOut ? "timed out and was killed" : "failed", exitText, child.getOutputTail( 20 ) );
        reportFailure( testCase._pFile, testCase._line,
                       string( textBuilder.view() ) );
    }

    bool isRunningIsolated()
    {
        TestRunnerInternal::CaseState& state = TestRunnerInternal::getState();
        ScopedLock<mutex>              lock{ state._mutex };
        return state._bIsolatedRun;
    }

    string getChildRoleArgument()
    {
        TestRunnerInternal::CaseState& state      = TestRunnerInternal::getState();
        bool                           bChildRole = false;
        string                         argument;
        {
            ScopedLock<mutex> lock{ state._mutex };
            bChildRole = state._pCase != nullptr && state._pCase->_bChildRole;
            argument   = state._childArgument;
        }
        if ( bChildRole == false )
            reportFailure( __FILE__, __LINE__, "getChildRoleArgument is only valid inside a child role (SW_TEST_CHILD_ROLE)" );
        return argument;
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

    vector<string> TestRunner::collectCaseNamesOutside( std::initializer_list<string_view> listFolder )
    {
        vector<string>                listName;
        TestRunnerInternal::Registry& registry = TestRunnerInternal::getRegistry();
        {
            ScopedLock<mutex> lock{ registry._mutex };
            for ( const TestCaseRegistration* pCase : registry._registration.getItems() )
            {
                string file( pCase->_pFile );
                std::replace( file.begin(), file.end(), '\\', '/' );
                const bool bExcluded = std::any_of( listFolder.begin(), listFolder.end(), [&file]( string_view folder )
                { return string_view( file ).find( folder ) != string_view::npos; } );
                if ( pCase->_bChildRole == false && bExcluded == false )
                    listName.emplace_back( pCase->_pName );
            }
        }
        VectorUtil::sort( listName );
        return listName;
    }

    vector<string> TestRunner::collectCaseNames()
    {
        vector<string>                listName;
        TestRunnerInternal::Registry& registry = TestRunnerInternal::getRegistry();
        {
            ScopedLock<mutex> lock{ registry._mutex };
            for ( const TestCaseRegistration* pCase : registry._registration.getItems() )
            {
                if ( pCase->_bChildRole == false )
                    listName.emplace_back( pCase->_pName );
            }
        }
        VectorUtil::sort( listName );
        return listName;
    }

    bool TestRunner::run( const TestRunOptions& options, vector<string>* pOutListFailure )
    {
        using Internal                                   = TestRunnerInternal;
        const bool                          bChildRun    = options._childRole.empty() == false;
        const bool                          bIsolatedRun = options._isolatedCase.empty() == false; // 부모가 격리 케이스 하나를 돌리라고 띄운 자식
        vector<const TestCaseRegistration*> listCase;
        {
            Internal::Registry& registry = Internal::getRegistry();
            ScopedLock<mutex>   lock{ registry._mutex };
            for ( const TestCaseRegistration* pCase : registry._registration.getItems() )
            {
                // 자식 실행은 그 역할 하나만, 평소 실행은 자식 역할을 빼고 고른다.
                const bool bSelected = bChildRun    ? ( pCase->_bChildRole && options._childRole == pCase->_pName )
                                     : bIsolatedRun ? ( pCase->_bChildRole == false && options._isolatedCase == pCase->_pName )
                                                    : ( pCase->_bChildRole == false && matchesFilter( options._filter, pCase->_pName, pCase->_options._tags ) &&
                                                        getShardOf( pCase->_pName, options._shardCount ) == options._shardIndex &&
                                                        Internal::isInSourceFolder( pCase->_pFile, options._sourceFolder ) );
                if ( bSelected )
                    listCase.push_back( pCase );
            }
        }
        if ( bChildRun && listCase.empty() )
        {
            Internal::writeLine( string( "[  NO TESTS ] no child role (SW_TEST_CHILD_ROLE) is named '" ) + options._childRole + "'" );
            return false;
        }
        if ( bIsolatedRun && listCase.empty() )
        {
            Internal::writeLine( string( "[  NO TESTS ] no case is named '" ) + options._isolatedCase + "' for --test-isolated-case" );
            return false;
        }
        // 이름 순으로 놓은 뒤 섞는다 — 등록 순서(링크 순서)에 기대지 않고, 씨앗이 같으면 어느 빌드에서나 같은 순서다.
        VectorUtil::sort( listCase, []( const TestCaseRegistration* pA, const TestCaseRegistration* pB )
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

        const uint32 baseSeed = Internal::pickSeed( options._seed );
        // 쓰기 폴더는 이 실행만의 이름이다(`<프로세스 id>-<시작 시각 ns>`) — Windows 가 프로세스 id 를 다시 써도 옛 실행의 폴더를 이어 쓰지 않는다.
        // 미리 만들지 않는다: 케이스가 처음 쓸 때 만든다(`makeScratchPath`). 그래서 쓰기 폴더를 안 쓰고 끝나는 실행(일부러 멈추는 자식 프로세스)이 빈 폴더를 남기지 않는다.
        string baseRoot;
        if ( FileUtil::makeAbsolutePath( options._scratchRoot.empty() ? FileUtil::joinPath( ProcessRoots::get()._saved.str(), "Test" ) : options._scratchRoot, baseRoot ) == false )
        {
            Internal::writeLine( "[  ERROR   ] the scratch folder cannot be made absolute - no case runs" );
            return false;
        }
        const string                       scratchRoot = FileUtil::joinPath( baseRoot, to_string( Process::getCurrentProcessID() ) + "-" + to_string( MonotonicClock::nowNanoseconds() ) );
        TestRunnerInternal::ActiveRunState runState; // 리스너보다 먼저 서고 늦게 사라진다
        Subscription                       logListener = Logger::registerGlobalListener( SW_DELEGATE_FUNCTION( LogWrittenDelegate, &Internal::onLogWritten ) );
        thread                             watchThread;
        if ( Process::isDebuggerPresent() == false )
            watchThread = thread( ThreadTestAccess::getKey(), &Internal::watchCaseDeadline );
        const uint32   repeatCount = ( bChildRun || bIsolatedRun ) ? 1u : MathUtil::max( options._repeatCount, 1u );
        uint32         failedCount = 0;
        uint32         ranCount    = 0;
        string         caseReport;
        vector<string> listUpdatedSnapshot;
        const int64    runStart = MonotonicClock::nowNanoseconds();
        utf8           arrBuffer[constant::buffer::kMax1024];

        formatstring( arrBuffer, constant::buffer::kMax1024, "[==========] %# of %# registered case(s), seed %# (repeat with --test-seed=%#), %# round(s), shard %#/%#",
                      static_cast<uint32>( listCase.size() ), static_cast<uint32>( collectCaseNames().size() ), baseSeed, baseSeed, repeatCount, options._shardIndex,
                      options._shardCount );
        Internal::writeLine( arrBuffer );
        for ( uint32 roundIndex = 0; roundIndex < repeatCount; ++roundIndex )
        {
            vector<const TestCaseRegistration*> listRound = listCase;
            Internal::shuffle( listRound, baseSeed + roundIndex );
            for ( const TestCaseRegistration* pCase : listRound )
            {
                TestRunnerInternal::CaseState& state = TestRunnerInternal::getState();
                {
                    ScopedLock<mutex> lock{ state._mutex };
                    state._pCase = pCase;
                    state._listExpectedLog.clear();
                    state._listFailure.clear();
                    state._listUpdatedSnapshot.clear();
                    state._assertionCount   = 0;
                    state._bUpdateSnapshots = options._bUpdateSnapshots;
                    state._scratchFolder    = FileUtil::joinPath( FileUtil::joinPath( scratchRoot, Internal::toFileName( pCase->_pName ) ), to_string( roundIndex ) );
                    state._childArgument    = options._childArgument;
                    state._bIsolatedRun     = bIsolatedRun;
                    state._bActive          = SW_TRUE;
                }
                LogOnce::beginScope(); // 한 번만 알리는 로그(`LogOnce`)도 케이스마다 처음이다 — 앞 케이스 · 앞 회차가 알렸다고 기대 로그가 사라지지 않게
                Internal::writeLine( string( "[ RUN      ] " ) + pCase->_pName );
                if ( options._pCrashHandler != nullptr )
                    options._pCrashHandler->setContextValue( "TestCase", pCase->_pName ); // 크래시 보고에 어느 케이스였는지 남는다
                const float32 timeoutSeconds = pCase->_options._timeoutSeconds > 0.0f ? pCase->_options._timeoutSeconds : kDefaultCaseTimeoutSeconds;
                const bool    bIsolate       = pCase->_options._bIsolated && bIsolatedRun == false;
                // 격리 케이스의 부모는 자식 기동(Debug 에서 몇 초)까지 기다린다 — 자식 시한은 케이스 시한이고 부모 시한은 그 두 배에 기동 몫을 더한다.
                const float32       parentSeconds = bIsolate ? timeoutSeconds * 2.0f + 60.0f : timeoutSeconds;
                const int64         timeoutNanos  = static_cast<int64>( static_cast<float64>( parentSeconds ) * 1.0e9 );
                const int64         caseStart     = MonotonicClock::nowNanoseconds();
                Internal::Watchdog& watchdog      = Internal::getWatchdog();
                watchdog._pCaseName.store( pCase->_pName, std::memory_order_release );
                watchdog._timeoutMilliseconds.store( timeoutNanos / constant::time::kNanosecondsPerMillisecond, std::memory_order_release );
                watchdog._deadlineNanoseconds.store( caseStart + timeoutNanos, std::memory_order_release );
                if ( bIsolate )
                    Internal::runIsolated( *pCase, timeoutSeconds );
                else if ( pCase->_pfnRunIndexed != nullptr )
                    pCase->_pfnRunIndexed( pCase->_index );
                else
                    pCase->_pfnRun();
                watchdog._deadlineNanoseconds.store( 0, std::memory_order_release );
                const int64 caseMilliseconds = ( MonotonicClock::nowNanoseconds() - caseStart ) / constant::time::kNanosecondsPerMillisecond;

                vector<string> listLateFailure;
                {
                    ScopedLock<mutex> lock{ state._mutex };
                    state._bActive = SW_FALSE;
                    if ( state._assertionCount == 0 )
                    {
                        StringBuilder<constant::buffer::kMax256> textBuilder2;
                        textBuilder2.appendFormat( "%#(%#): error: the case made no assertion - a case that checks nothing is not a pass", string( pCase->_pFile ), pCase->_line );
                        listLateFailure.push_back( string( textBuilder2.view() ) );
                    }
                    for ( const TestRunnerInternal::ExpectedLog& expected : state._listExpectedLog )
                    {
                        if ( expected._seenCount == 0 )
                        {
                            StringBuilder<constant::buffer::kMax256> textBuilder;
                            textBuilder.appendFormat( "%#(%#): error: expected log [id:%#] never appeared", string( expected._pFile ), expected._line, expected._id );
                            listLateFailure.push_back( string( textBuilder.view() ) );
                        }
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
                for ( const string& failure : listFailure )
                {
                    if ( pOutListFailure != nullptr )
                        pOutListFailure->push_back( string( pCase->_pName ) + ": " + failure );
                }
                ++ranCount;
                if ( bPassed == false )
                    ++failedCount;
                formatstring( arrBuffer, constant::buffer::kMax1024, "%# %# (%# ms)", bPassed ? "[       OK ]" : "[  FAILED  ]", pCase->_pName, caseMilliseconds );
                Internal::writeLine( arrBuffer );

                caseReport += caseReport.empty() ? "\n" : ",\n";
                caseReport += string( "    {\"name\": \"" ) + Internal::escapeJSON( pCase->_pName ) + "\", \"file\": \"" + Internal::escapeJSON( pCase->_pFile ) +
                              "\", \"line\": " + to_string( pCase->_line ) + ", \"round\": " + to_string( roundIndex ) + ", \"isolated\": " + ( bIsolate ? "true" : "false" ) +
                              ", \"passed\": " + ( bPassed ? "true" : "false" ) + ", \"milliseconds\": " + to_string( caseMilliseconds ) + ", \"failures\": [";
                for ( size_t failureIndex = 0; failureIndex < listFailure.size(); ++failureIndex )
                {
                    caseReport += ( failureIndex == 0 ? "\"" : ", \"" ) + Internal::escapeJSON( listFailure[failureIndex] ) + "\"";
                }
                caseReport += "]}";
            }
        }
        logListener.reset();
        watchThread = thread{}; // 감시를 세우고 합류한다
        if ( options._pCrashHandler != nullptr )
            options._pCrashHandler->setContextValue( "TestCase", "" );

        const int64 runMilliseconds = ( MonotonicClock::nowNanoseconds() - runStart ) / constant::time::kNanosecondsPerMillisecond;
        formatstring( arrBuffer, constant::buffer::kMax1024, "[==========] %# case run(s) (%# ms), %# passed, %# failed, seed %#", ranCount, runMilliseconds, ranCount - failedCount, failedCount, baseSeed );
        Internal::writeLine( arrBuffer );
        {
            TestRunnerInternal::CaseState& state = TestRunnerInternal::getState();
            ScopedLock<mutex>              lock{ state._mutex };
            formatstring( arrBuffer, constant::buffer::kMax1024, "%#%# error log line(s) declared with SW_EXPECT_LOG", test::kDeclaredErrorLinesPrefix, state._declaredErrorLineCount );
        }
        Internal::writeLine( arrBuffer );
        for ( const string& path : listUpdatedSnapshot )
        {
            Internal::writeLine( string( "[ SNAPSHOT ] updated " ) + path );
        }

        // 모두 통과한 실행의 쓰기 폴더는 지운다 — 남기면 실행마다 쌓였다(`Saved/Test` 아래 수백 개). 실패한 실행은 들여다보도록 남기고 보고도 그 안에 쓴다(언리얼 자동화의 실패 산출물).
        // 보고 경로를 준 실행(`--test-report`)은 늘 그 경로에 쓴다.
        const bool bKeepScratch = failedCount > 0;
        if ( bKeepScratch == false && FileUtil::removeDirectory( scratchRoot ) == false )
            Internal::writeLine( "[ WARNING  ] the scratch folder could not be removed: " + scratchRoot );
        const string reportPath = options._reportPath.empty() == false ? options._reportPath : bKeepScratch ? FileUtil::joinPath( scratchRoot, "report.json" )
                                                                                                            : string{};
        if ( reportPath.empty() == false )
        {
            StringBuilder<constant::buffer::kMax256> textBuilder;
            textBuilder.appendFormat( "%#\",\n  \"disabled\": [", options._shardCount );
            string report = "{\n  \"schemaVersion\": " + to_string( kReportSchemaVersion ) + ",\n  \"seed\": " + to_string( baseSeed ) + ",\n  \"filter\": \"" + Internal::escapeJSON( options._filter ) + "\",\n  \"selectedCount\": " +
                            to_string( listCase.size() ) + ",\n  \"runCount\": " + to_string( ranCount ) + ",\n  \"failedCount\": " + to_string( failedCount ) +
                            ",\n  \"milliseconds\": " + to_string( runMilliseconds ) + ",\n  \"shard\": \"" + to_string( options._shardIndex ) + "/" +
                            string( textBuilder.view() );
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
