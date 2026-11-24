#include "pch.h"

#include "Core/Testing/TestRunner.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/EnumArray.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Foundation/Constants.h"
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
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Testing/ThreadTestAccess.h"
#include "Core/Time/MonotonicClock.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>

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

            /**
             * @brief 케이스 실패의 종류입니다 — 보고(판 4)의 `failureKind` 와 종류별 수. 첫 실패 줄의 종류가 그 케이스의 종류다.
             * @details 같은 "실패" 가 틀린 단언 · 단언 없음 · 선언하지 않은 로그 · 기준 파일 없음을 함께 담으면, 빌드 없이 쓴 테스트를 통합에서 고칠 때 줄마다 읽어 갈라야 했다.
             */
            enum class TestFailureKind : uint8
            {
                Assertion,
                NoAssertion,
                UndeclaredLog,
                ExpectedLogMissing,
                SnapshotMissing,
                SnapshotDiffers,
                Timeout,
                Crash,
                Count,
            };

            static const utf8* getFailureKindName( TestFailureKind kind )
            {
                static constexpr auto kArrName = makeEnumTable<TestFailureKind, const utf8*>( "Assertion", "NoAssertion", "UndeclaredLog", "ExpectedLogMissing", "SnapshotMissing",
                                                                                              "SnapshotDiffers", "Timeout", "Crash" );
                return kArrName[kind];
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
                TestFailureKind             _firstFailureKind{ TestFailureKind::Assertion }; ///< 첫 실패 줄의 종류(`_listFailure` 가 비면 뜻이 없다)
                vector<string>              _listUpdatedSnapshot;
                string                      _skipReason; ///< `SW_TEST_SKIP` 의 까닭과 자리(`파일(줄)`) — 비면 건너뛰지 않았다
                AbsolutePath                _scratchFolder;
                string                      _childArgument;               ///< 자식 역할로 띄워졌을 때 `--test-child-arg` 의 값
                uint32                      _declaredErrorLineCount{ 0 }; ///< 실행 전체에서 선언과 맞은 Error 줄 수(`kDeclaredErrorLinesPrefix` 줄로 알린다)
                bool                        _bIsolatedRun{ false };       ///< 격리 케이스의 자식 프로세스로 띄워졌다(`--test-isolated-case`) NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
                uint32                      _assertionCount{ 0 };
                SnapshotWriteMode           _snapshotWriteMode{ SnapshotWriteMode::Off };
                bool                        _bActive{ SW_FALSE }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
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

            /** @brief 실패 하나를 종류 @p kind 와 함께 적고 바로 찍습니다(잠금을 잡은 채 부르지 않는다 — 표준 출력은 잠금 밖). */
            static void addFailure( string text, TestFailureKind kind )
            {
                writeLine( text );
                CaseState&        state = getState();
                ScopedLock<mutex> lock{ state._mutex };
                if ( state._listFailure.empty() )
                    state._firstFailureKind = kind;
                state._listFailure.push_back( std::move( text ) );
            }

            /** @brief `파일(줄): error: 글` 실패 하나를 종류 @p kind 로 적습니다. */
            static void reportFailureOfKind( const utf8* pFile, int32 line, string_view message, TestFailureKind kind )
            {
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "%#(%#): error: %#", string( pFile ), line, string( message ) );
                addFailure( string( textBuilder.view() ), kind );
            }

            /** @brief 실행의 마지막 표준 출력 줄 `TEST_RESULT=<결과> run=… failed=… skipped=…` 입니다(`WINDOWS_OK=` 와 같은 꼴 — 출력을 잘라 본 쪽도 결과를 읽는다). */
            static void writeResultLine( const utf8* pResult, uint32 ranCount, uint32 failedCount, uint32 skippedCount )
            {
                utf8 arrBuffer[constant::buffer::kMax256];
                formatstring( arrBuffer, constant::buffer::kMax256, "TEST_RESULT=%# run=%# failed=%# skipped=%#", pResult, ranCount, failedCount, skippedCount );
                writeLine( arrBuffer );
            }

            /** @brief 거르개의 쉼표로 가른 패턴마다 @p visit( 패턴, 빼기인지 )를 부릅니다(빈 패턴은 건너뛴다). */
            template <typename Visit>
            static void forEachPattern( string_view filter, Visit&& visit )
            {
                size_t start = 0;
                while ( start <= filter.size() )
                {
                    const size_t      end     = MathUtil::min( filter.find( ',', start ), filter.size() );
                    const string_view pattern = filter.substr( start, end - start );
                    if ( pattern.empty() == false )
                    {
                        if ( pattern.front() == '-' )
                            visit( pattern.substr( 1 ), true );
                        else
                            visit( pattern, false );
                    }
                    start = end + 1;
                }
            }

            /** @brief 케이스 이름 패턴에 올 수 있는 글자인지입니다 — 이름 글자 · `.` · `*` · `?` · `/`. */
            static bool isNamePatternCharacter( utf8 character )
            {
                return ( 'a' <= character && character <= 'z' ) || ( 'A' <= character && character <= 'Z' ) || ( '0' <= character && character <= '9' ) || character == '_' ||
                       character == '.' || character == '*' || character == '?' || character == '/';
            }

            /**
             * @brief 케이스 동안 남은 로그를 봅니다. 선언한 id 는 수준과 상관없이 세고(설계가 정보 로그로 정한 사실도 기대할 수 있게),
             *        선언하지 않은 Warning · Error 는 실패로 적는다.
             */
            static void onLogWritten( const LogEntry& entry )
            {
                const bool bProblem = entry._level == LogLevel::Warning || entry._level == LogLevel::Error;
                if ( bProblem == false && entry._pID == nullptr )
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
                    if ( bProblem == false )
                        return;
                    StringBuilder<constant::buffer::kMax1024> failureBuilder;
                    failureBuilder.appendFormat( "%#(%#): error: unexpected %# log", entry._file, entry._line, entry._level == LogLevel::Error ? "Error" : "Warning" );
                    if ( entry._pID != nullptr )
                        failureBuilder.appendFormat( " [id:%#]", entry._pID );
                    else
                        failureBuilder.append( " (no id - give it one to declare it with SW_EXPECT_LOG)" );
                    failureBuilder.appendFormat( ": %#", entry._message );
                    // 붙일 글을 그대로 낸다 — 이 로그가 케이스가 밟는 새 길의 사실이면 이 한 줄을 케이스에 붙인다.
                    if ( entry._pID != nullptr && state._pCase != nullptr )
                        failureBuilder.appendFormat( " - if it is expected, add SW_EXPECT_LOG( \"%#\" ) to %#(%#)", entry._pID, state._pCase->_pFile, state._pCase->_line );
                    failure = string( failureBuilder.view() );
                }
                addFailure( std::move( failure ), TestFailureKind::UndeclaredLog );
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
                atomic<uint32>      _ranCount{ 0 }; ///< 지금 실행이 끝낸 케이스 수 — 시한에 끝낼 때 끝 줄(`TEST_RESULT=TIMEOUT`)에 적는다
                atomic<uint32>      _failedCount{ 0 };
                atomic<uint32>      _skippedCount{ 0 };
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
                    // 끝 줄 — 멈춘 케이스도 돈 것으로 센다(`writeResultLine` 과 같은 꼴).
                    std::fprintf( stdout, "TEST_RESULT=TIMEOUT run=%u failed=%u skipped=%u\n", static_cast<unsigned>( watchdog._ranCount.load() + 1 ),
                                  static_cast<unsigned>( watchdog._failedCount.load() + 1 ), static_cast<unsigned>( watchdog._skippedCount.load() ) );
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
        SourceLocation                existing{};
        {
            ScopedLock<mutex> lock{ registry._mutex };
            result = registry._registration.add( pRegistration, pRegistration->_pName );
            if ( const TestCaseRegistration* pExisting = result == RegistrationResult::DuplicateName ? registry._registration.findByName( pRegistration->_pName ) : nullptr; pExisting != nullptr )
                existing = SourceLocation{ pExisting->_pFile, pExisting->_line };
        }
        // 케이스는 늘 정적 등록이다 — 겹친 이름은 두 자리를 찍고 시작 단계(`run`)가 로드 오류로 멈춘다.
        if ( result == RegistrationResult::DuplicateName )
            reportDuplicateRegistration( "Test case", pRegistration->_pName, existing, SourceLocation{ pRegistration->_pFile, pRegistration->_line } );
        else
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
        TestRunnerInternal::reportFailureOfKind( pFile, line, message, TestRunnerInternal::TestFailureKind::Assertion );
    }

    void countAssertion()
    {
        TestRunnerInternal::CaseState& state = TestRunnerInternal::getState();
        ScopedLock<mutex>              lock{ state._mutex };
        ++state._assertionCount;
    }

    void skipCase( string_view reason, const utf8* pFile, int32 line )
    {
        if ( reason.empty() )
        {
            reportFailure( pFile, line, "SW_TEST_SKIP needs a reason - a skip without one cannot be told apart in the report" );
            return;
        }
        StringBuilder<constant::buffer::kMax256> textBuilder;
        textBuilder.appendFormat( "%# (%#(%#))", string( reason ), string( pFile ), line );
        TestRunnerInternal::CaseState& state = TestRunnerInternal::getState();
        ScopedLock<mutex>              lock{ state._mutex };
        state._skipReason = string( textBuilder.view() );
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

    AbsolutePath makeScratchPath( string_view name )
    {
        TestRunnerInternal::CaseState& state = TestRunnerInternal::getState();
        AbsolutePath                   folder;
        {
            ScopedLock<mutex> lock{ state._mutex };
            folder = state._scratchFolder;
        }
        SW_ASSERT( folder.empty() == false && "makeScratchPath is only valid inside a running test case" );
        if ( FileUtil::ensureDirectoryExists( folder ) == false )
        {
            StringBuilder<constant::buffer::kMax1024> message;
            message.appendFormat( "could not create the scratch folder %#", folder.str() );
            reportFailure( __FILE__, __LINE__, message.view() );
        }
        return folder.join( name );
    }

    bool checkSnapshot( string_view name, string_view actual, const utf8* pFile, int32 line )
    {
        countAssertion();
        TestRunnerInternal::CaseState& state = TestRunnerInternal::getState();
        string                         caseName;
        SnapshotWriteMode              recording = SnapshotWriteMode::Off;
        {
            ScopedLock<mutex> lock{ state._mutex };
            caseName  = state._pCase != nullptr ? state._pCase->_pName : "";
            recording = state._snapshotWriteMode;
        }
        StringBuilder<constant::buffer::kMax256> snapshotName;
        snapshotName.appendFormat( "%#.%#.txt", TestRunnerInternal::toFileName( caseName ), name );
        const AbsolutePath path = FileUtil::toDiskPath( FileUtil::joinPath( FileUtil::joinPath( FileUtil::getDirectoryPart( pFile ), "Snapshots" ), snapshotName.view() ) );
        string             normalized( actual );
        normalized.erase( std::remove( normalized.begin(), normalized.end(), '\r' ), normalized.end() );
        string     expected;
        const bool bHasSnapshot = FileUtil::readOptionalTextFile( path, expected ) == OptionalFileRead::Read;
        expected.erase( std::remove( expected.begin(), expected.end(), '\r' ), expected.end() );
        if ( bHasSnapshot && expected == normalized )
            return true;
        // 없는 것만 쓰는 실행(`Missing`)은 있는 스냅숏을 덮지 않는다 — 다르면 아래에서 진다.
        const bool bWrite = recording == SnapshotWriteMode::All || ( recording == SnapshotWriteMode::Missing && bHasSnapshot == false );
        if ( bWrite )
        {
            const bool bWritten = FileUtil::ensureDirectoryExists( path.getParent() ) && FileUtil::writeTextFile( path, normalized );
            if ( bWritten == false )
            {
                StringBuilder<constant::buffer::kMax1024> message;
                message.appendFormat( "could not write the snapshot %#", path.str() );
                reportFailure( pFile, line, message.view() );
                return false;
            }
            {
                ScopedLock<mutex> lock{ state._mutex };
                state._listUpdatedSnapshot.push_back( path.str() );
            }
            if ( recording == SnapshotWriteMode::All )
                return true;
            // 처음 쓴 스냅숏은 아직 사람이 보지 않았다 — 지고 목록에 남아, 보고 커밋한 뒤 기록 없이 다시 돌려 통과해야 한다.
            StringBuilder<constant::buffer::kMax1024> message;
            message.appendFormat( "no snapshot yet: recorded %# - review it, commit it and run again without recording", path.str() );
            TestRunnerInternal::reportFailureOfKind( pFile, line, message.view(), TestRunnerInternal::TestFailureKind::SnapshotMissing );
            return false;
        }
        StringBuilder<constant::buffer::kMax1024> message;
        message.appendFormat( "%#%# (run App --test --record-missing-snapshots for a new one, --update-snapshots to rewrite, and review the diff)",
                              bHasSnapshot ? "snapshot differs: " : "no snapshot yet: ", path.str() );
        TestRunnerInternal::reportFailureOfKind( pFile, line, message.view(),
                                                 bHasSnapshot ? TestRunnerInternal::TestFailureKind::SnapshotDiffers : TestRunnerInternal::TestFailureKind::SnapshotMissing );
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
            StringBuilder<constant::buffer::kMax256> message;
            message.appendFormat( "no child role named %# is registered (declare it with SW_TEST_CHILD_ROLE)", roleName );
            reportFailure( __FILE__, __LINE__, message.view() );
            return result;
        }
        // 자식의 로그 파일은 이 케이스의 쓰기 폴더에 둔다 — 자식마다 `Saved/Logs` 에 파일이 쌓이지 않게.
        StringBuilder<constant::buffer::kMax256> roleArgument;
        roleArgument.appendFormat( "-test-child-role=%#", roleName );
        StringBuilder<constant::buffer::kMax256> logFileName;
        logFileName.appendFormat( "%#.child.log", TestRunnerInternal::toFileName( roleName ) );
        StringBuilder<constant::buffer::kMax1024> logFileArgument;
        logFileArgument.appendFormat( "-log-file=%#", makeScratchPath( logFileName.view() ).str() );
        vector<string> listArgument{ "-test", string( roleArgument.view() ), "-unattended", string( logFileArgument.view() ) };
        if ( argument.empty() == false )
        {
            StringBuilder<constant::buffer::kMax1024> childArgument;
            childArgument.appendFormat( "-test-child-arg=%#", argument );
            listArgument.push_back( string( childArgument.view() ) );
        }
        TestRunnerInternal::launchSelf( std::move( listArgument ), timeoutSeconds, result );
        return result;
    }

    void TestRunnerInternal::launchSelf( vector<string> listArgument, float32 timeoutSeconds, ChildRunResult& outResult )
    {
        ChildRunResult&      result  = outResult;
        const ProcessCommand command = ProcessCommand::at( FileUtil::getExecutablePath(), std::move( listArgument ) );
        ProcessOptions       options;
        options._workingDirectory = FileUtil::getCurrentPath().str();
        Process process;
        result._bLaunched = process.launch( command, options );
        if ( result._bLaunched == false )
        {
            StringBuilder<constant::buffer::kMax1024> message;
            message.appendFormat( "could not launch the child %#", command.toCommandLine() );
            reportFailure( __FILE__, __LINE__, message.view() );
            return;
        }
        // 출력을 읽는 이 스레드는 자식이 말없이 멈추면 깨지 않는다 — 감시 스레드가 시한에 죽인다(`terminate` 는 다른 스레드가 읽는 중에도 안전한 계약).
        atomic<bool>   bTimedOut{ false };
        const Deadline deadline = Deadline::afterMilliseconds( static_cast<int64>( static_cast<float64>( timeoutSeconds ) * static_cast<float64>( constant::time::kMillisecondsPerSecond ) ) );
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
        {
            StringBuilder<constant::buffer::kMax512> waitError;
            waitError.appendFormat( "[test runner] could not wait for the child: %# %#\n", waited.error().message(), waited.error().detail() );
            result._output += waitError.view();
        }
        watchThread       = thread{};                                    // 정지를 요청하고 합류한다
        result._bTimedOut = bTimedOut.load( std::memory_order_acquire ); // 실패로 적지 않는다 — 시한에 죽이는 것을 보는 케이스도 있다. 죽은 자식은 종료 코드가 0 이 아니다
    }

    void TestRunnerInternal::runIsolated( const TestCaseRegistration& testCase, float32 timeoutSeconds )
    {
        // 자식은 이 케이스 하나만 같은 판정 규칙으로 돈다(`--test-isolated-case`). 크래시 · 교착 · 강제 종료는 종료 코드로 이 케이스의 실패가 되고 부모 실행은 이어진다.
        StringBuilder<constant::buffer::kMax256> caseArgument;
        caseArgument.appendFormat( "-test-isolated-case=%#", testCase._pName );
        StringBuilder<constant::buffer::kMax256> logFileName;
        logFileName.appendFormat( "%#.isolated.log", toFileName( testCase._pName ) );
        StringBuilder<constant::buffer::kMax1024> logFileArgument;
        logFileArgument.appendFormat( "-log-file=%#", makeScratchPath( logFileName.view() ).str() );
        vector<string> listArgument{ "-test", string( caseArgument.view() ), "-unattended", string( logFileArgument.view() ) };
        // 스냅숏 기록 범위를 자식에 넘긴다 — 넘기지 않으면 격리 케이스의 스냅숏은 기록 실행에서도 쓰이지 않고 진다.
        SnapshotWriteMode recording = SnapshotWriteMode::Off;
        {
            CaseState&        state = getState();
            ScopedLock<mutex> lock{ state._mutex };
            recording = state._snapshotWriteMode;
        }
        if ( recording == SnapshotWriteMode::Missing )
            listArgument.push_back( "-record-missing-snapshots" );
        else if ( recording == SnapshotWriteMode::All )
            listArgument.push_back( "-update-snapshots" );
        ChildRunResult child;
        launchSelf( std::move( listArgument ), timeoutSeconds, child );
        countAssertion();
        if ( child._bLaunched == false )
            return; // 띄우지 못한 까닭은 위에서 적었다
        if ( child._exitCode.has_value() && *child._exitCode == 0 )
            return;
        StringBuilder<constant::buffer::kMax256> textBuilder;
        textBuilder.appendFormat( "the isolated case %# (", child._bTimedOut ? "timed out and was killed" : "failed" );
        if ( child._exitCode.has_value() )
            textBuilder.appendFormat( "exit code %#", *child._exitCode );
        else
            textBuilder.append( "not waited for" );
        textBuilder.appendFormat( ") - child output:\n%#", child.getOutputTail( 20 ) );
        // 자식이 끝 줄(`TEST_RESULT=`)까지 갔으면 판정으로 진 것이고, 못 갔으면 죽은 것이다.
        const TestFailureKind kind = child._bTimedOut                                     ? TestFailureKind::Timeout
                                   : child._output.find( "TEST_RESULT=" ) != string::npos ? TestFailureKind::Assertion
                                                                                          : TestFailureKind::Crash;
        reportFailureOfKind( testCase._pFile, testCase._line, string( textBuilder.view() ), kind );
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
        {
            StringBuilder<constant::buffer::kMax1024> failure;
            failure.appendFormat( "%# — %#", pExpression, message );
            reportFailure( pFile, line, failure.view() );
        }
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

    string TestRunner::findFilterError( string_view filter )
    {
        string error;
        TestRunnerInternal::forEachPattern( filter, [&error]( string_view pattern, [[maybe_unused]] bool bExclude )
        {
            if ( error.empty() == false )
                return;
            const bool bTag      = pattern.size() >= 2 && pattern.front() == '[' && pattern.back() == ']';
            const auto isAllowed = [bTag]( utf8 character )
            { return bTag ? TestRunnerInternal::isNamePatternCharacter( character ) || character == '-' : TestRunnerInternal::isNamePatternCharacter( character ); };
            const string_view body = bTag ? pattern.substr( 1, pattern.size() - 2 ) : pattern;
            if ( body.empty() || std::all_of( body.begin(), body.end(), isAllowed ) == false )
            {
                StringBuilder<constant::buffer::kMax256> errorBuilder;
                errorBuilder.appendFormat( "--test-filter: '%#' is not a case pattern (name characters . * ? / or [tag], a leading - excludes) - separate patterns with a comma", pattern );
                error = string( errorBuilder.view() );
            }
        } );
        return error;
    }

    vector<string> TestRunner::findUnmatchedPatterns( string_view filter )
    {
        vector<string>                listUnmatched;
        TestRunnerInternal::Registry& registry = TestRunnerInternal::getRegistry();
        ScopedLock<mutex>             lock{ registry._mutex };
        TestRunnerInternal::forEachPattern( filter, [&]( string_view pattern, bool bExclude )
        {
            if ( bExclude )
                return; // 빼기 패턴은 맞는 케이스가 없어도 된다(뺄 것이 없을 뿐이다)
            const auto& listCase = registry._registration.getItems();
            const bool  bMatched = std::any_of( listCase.begin(), listCase.end(), [pattern]( const TestCaseRegistration* pCase )
             { return pCase->_bChildRole == false && matchesFilter( pattern, pCase->_pName, pCase->_options._tags ); } );
            if ( bMatched == false )
                listUnmatched.emplace_back( pattern );
        } );
        return listUnmatched;
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
        bool bIncluded    = false;
        bool bExcluded    = false;
        bool bHasPositive = false;
        TestRunnerInternal::forEachPattern( filter, [&]( string_view pattern, bool bExclude )
        {
            if ( bExclude )
            {
                bExcluded = bExcluded || matchesPattern( pattern );
                return;
            }
            bHasPositive = true;
            bIncluded    = bIncluded || matchesPattern( pattern );
        } );
        if ( bExcluded )
            return false;
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
        using Internal          = TestRunnerInternal;
        const bool bChildRun    = options._childRole.empty() == false;
        const bool bIsolatedRun = options._isolatedCase.empty() == false; // 부모가 격리 케이스 하나를 돌리라고 띄운 자식
        // 시작 때 겹친 정적 등록(케이스 · 감시 값 · 단계 …)이 있으면 돌지 않는다 — 뒤의 등록이 조용히 빠진 채 도는 실행은 통과해도 그 등록을 본 적이 없다.
        if ( const vector<string> listRejected = collectRejectedStaticRegistrations(); listRejected.empty() == false )
        {
            for ( const string& line : listRejected )
            {
                StringBuilder<constant::buffer::kMax512> output;
                output.appendFormat( "[ LOAD ERROR ] %#", line );
                Internal::writeLine( output.view() );
            }
            Internal::writeLine( "[ LOAD ERROR ] a static registration was rejected at startup - rename one of the two (both sites are above)" );
            Internal::writeResultLine( "LOAD_ERROR", 0, 0, 0 );
            return false;
        }
        // 거르개 문법 — 쉼표 밖의 구분자(`:` · `;` · 공백)는 "맞는 케이스 없음" 과 같은 길로 새지 않고 인자 오류다.
        const bool bFiltered = bChildRun == false && bIsolatedRun == false;
        if ( const string filterError = bFiltered ? findFilterError( options._filter ) : string{}; filterError.empty() == false )
        {
            StringBuilder<constant::buffer::kMax512> output;
            output.appendFormat( "[ ARGUMENT ] %#", filterError );
            Internal::writeLine( output.view() );
            Internal::writeResultLine( "ARGUMENT_ERROR", 0, 0, 0 );
            return false;
        }
        // 더하기 패턴마다 등록된 케이스 하나는 맞아야 한다(샤드 · 폴더로 거르기 전) — `A.*,Typo.*` 가 A 만 돌고 통과하지 않게.
        const vector<string>                listUnmatched = bFiltered ? findUnmatchedPatterns( options._filter ) : vector<string>{};
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
            StringBuilder<constant::buffer::kMax256> output;
            output.appendFormat( "[  NO TESTS ] no child role (SW_TEST_CHILD_ROLE) is named '%#'", options._childRole );
            Internal::writeLine( output.view() );
            Internal::writeResultLine( "NO_TESTS", 0, 0, 0 );
            return false;
        }
        if ( bIsolatedRun && listCase.empty() )
        {
            StringBuilder<constant::buffer::kMax256> output;
            output.appendFormat( "[  NO TESTS ] no case is named '%#' for --test-isolated-case", options._isolatedCase );
            Internal::writeLine( output.view() );
            Internal::writeResultLine( "NO_TESTS", 0, 0, 0 );
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
        for ( const string& pattern : listUnmatched )
        {
            StringBuilder<constant::buffer::kMax256> output;
            output.appendFormat( "[  NO TESTS ] the pattern '%#' matches no registered case - a typo runs nothing", pattern );
            Internal::writeLine( output.view() );
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
            StringBuilder<constant::buffer::kMax256> output;
            output.appendFormat( "[ DISABLED ] %# - %#", pCase->_pName, pCase->_options._disabledReason );
            Internal::writeLine( output.view() );
        }
        // 빈 샤드는 실패가 아니다 — 케이스가 몫보다 적으면 빈 몫이 생긴다. 빈 거르개는 실패다.
        if ( listCase.empty() && options._shardCount <= 1 )
        {
            StringBuilder<constant::buffer::kMax256> output;
            output.appendFormat( "[  NO TESTS ] no case matches the filter '%#' - an empty run is not a pass", options._filter );
            Internal::writeLine( output.view() );
            Internal::writeResultLine( "NO_TESTS", 0, 0, 0 );
            return false;
        }

        const uint32 baseSeed = Internal::pickSeed( options._seed );
        // 쓰기 폴더는 이 실행만의 이름이다(`<프로세스 id>-<시작 시각 ns>`) — Windows 가 프로세스 id 를 다시 써도 옛 실행의 폴더를 이어 쓰지 않는다.
        // 미리 만들지 않는다: 케이스가 처음 쓸 때 만든다(`makeScratchPath`). 그래서 쓰기 폴더를 안 쓰고 끝나는 실행(일부러 멈추는 자식 프로세스)이 빈 폴더를 남기지 않는다.
        string baseRoot;
        if ( FileUtil::makeAbsolutePath( options._scratchRoot.empty() ? FileUtil::joinPath( ProcessRoots::get()._saved.str(), "Test" ) : options._scratchRoot, baseRoot ) == false )
        {
            Internal::writeLine( "[  ERROR   ] the scratch folder cannot be made absolute - no case runs" );
            Internal::writeResultLine( "FAILED", 0, 0, 0 );
            return false;
        }
        StringBuilder<constant::buffer::kMax64> runName;
        runName.appendFormat( "%#-%#", Process::getCurrentProcessID(), MonotonicClock::nowNanoseconds() );
        const AbsolutePath                 scratchRoot = FileUtil::toDiskPath( FileUtil::joinPath( baseRoot, runName.view() ) );
        TestRunnerInternal::ActiveRunState runState; // 리스너보다 먼저 서고 늦게 사라진다
        Subscription                       logListener = Logger::registerGlobalListener( SW_DELEGATE_FUNCTION( LogWrittenDelegate, &Internal::onLogWritten ) );
        thread                             watchThread;
        if ( Process::isDebuggerPresent() == false )
            watchThread = thread( ThreadTestAccess::getKey(), &Internal::watchCaseDeadline );
        const uint32                                 repeatCount  = ( bChildRun || bIsolatedRun ) ? 1u : MathUtil::max( options._repeatCount, 1u );
        uint32                                       failedCount  = 0;
        uint32                                       skippedCount = 0;
        uint32                                       ranCount     = 0;
        EnumArray<Internal::TestFailureKind, uint32> arrKindCount{}; // 종류별 진 케이스 수(보고의 `failureKinds`)
        StringBuilder<constant::buffer::kMax8192>    caseReport;     // 보고 JSON 의 `cases` 칸 — 잘리면 안 되니 힙으로 자라는 빌더에 모은다
        Internal::Watchdog&                          runWatchdog = Internal::getWatchdog();
        runWatchdog._ranCount.store( 0, std::memory_order_relaxed );
        runWatchdog._failedCount.store( 0, std::memory_order_relaxed );
        runWatchdog._skippedCount.store( 0, std::memory_order_relaxed );
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
            fixed_string<constant::buffer::kMax16> roundFolder;
            formatstring( roundFolder.data(), roundFolder.capacity(), "%#", roundIndex );
            for ( const TestCaseRegistration* pCase : listRound )
            {
                TestRunnerInternal::CaseState& state = TestRunnerInternal::getState();
                {
                    ScopedLock<mutex> lock{ state._mutex };
                    state._pCase = pCase;
                    state._listExpectedLog.clear();
                    state._listFailure.clear();
                    state._firstFailureKind = Internal::TestFailureKind::Assertion;
                    state._listUpdatedSnapshot.clear();
                    state._skipReason.clear();
                    state._assertionCount    = 0;
                    state._snapshotWriteMode = options._snapshotWriteMode;
                    state._scratchFolder     = scratchRoot.join( Internal::toFileName( pCase->_pName ) ).join( roundFolder.view() );
                    state._childArgument     = options._childArgument;
                    state._bIsolatedRun      = bIsolatedRun;
                    state._bActive           = SW_TRUE;
                }
                LogOnce::beginScope(); // 한 번만 알리는 로그(`LogOnce`)도 케이스마다 처음이다 — 앞 케이스 · 앞 회차가 알렸다고 기대 로그가 사라지지 않게
                formatstring( arrBuffer, constant::buffer::kMax1024, "[ RUN      ] %#", pCase->_pName );
                Internal::writeLine( arrBuffer );
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

                struct LateFailure
                {
                    string                    _text;
                    Internal::TestFailureKind _kind;
                };
                vector<LateFailure> listLateFailure;
                string              skipReason;
                {
                    ScopedLock<mutex> lock{ state._mutex };
                    state._bActive = SW_FALSE;
                    skipReason     = state._skipReason;
                    // 건너뛴 케이스는 돌지 않은 것이다 — 단언 0 건 · 나오지 않은 기대 로그를 묻지 않는다(건너뜀이 곧 판정이다).
                    if ( skipReason.empty() && state._assertionCount == 0 )
                    {
                        StringBuilder<constant::buffer::kMax256> textBuilder2;
                        textBuilder2.appendFormat( "%#(%#): error: the case made no assertion - a case that checks nothing is not a pass", string( pCase->_pFile ), pCase->_line );
                        listLateFailure.push_back( LateFailure{ string( textBuilder2.view() ), Internal::TestFailureKind::NoAssertion } );
                    }
                    for ( const TestRunnerInternal::ExpectedLog& expected : state._listExpectedLog )
                    {
                        if ( expected._seenCount == 0 && skipReason.empty() )
                        {
                            StringBuilder<constant::buffer::kMax256> textBuilder;
                            textBuilder.appendFormat( "%#(%#): error: expected log [id:%#] never appeared", string( expected._pFile ), expected._line, expected._id );
                            listLateFailure.push_back( LateFailure{ string( textBuilder.view() ), Internal::TestFailureKind::ExpectedLogMissing } );
                        }
                    }
                }
                for ( LateFailure& failure : listLateFailure )
                {
                    Internal::addFailure( std::move( failure._text ), failure._kind );
                }

                vector<string>            listFailure;
                Internal::TestFailureKind failureKind = Internal::TestFailureKind::Assertion;
                {
                    ScopedLock<mutex> lock{ state._mutex };
                    listFailure = state._listFailure;
                    failureKind = state._firstFailureKind;
                    listUpdatedSnapshot.insert( listUpdatedSnapshot.end(), state._listUpdatedSnapshot.begin(), state._listUpdatedSnapshot.end() );
                    state._pCase = nullptr;
                }
                // 건너뛰기 전에 진 단언이 있으면 실패다 — 건너뜀이 실패를 가리지 않는다.
                const bool bSkipped = skipReason.empty() == false && listFailure.empty();
                const bool bPassed  = listFailure.empty() && bSkipped == false;
                for ( const string& failure : listFailure )
                {
                    if ( pOutListFailure != nullptr )
                    {
                        StringBuilder<constant::buffer::kMax1024> failureLine;
                        failureLine.appendFormat( "%#: %#", pCase->_pName, failure );
                        pOutListFailure->push_back( string( failureLine.view() ) );
                    }
                }
                ++ranCount;
                if ( bSkipped )
                    ++skippedCount;
                else if ( bPassed == false )
                {
                    ++failedCount;
                    ++arrKindCount[failureKind];
                }
                runWatchdog._ranCount.store( ranCount, std::memory_order_relaxed );
                runWatchdog._failedCount.store( failedCount, std::memory_order_relaxed );
                runWatchdog._skippedCount.store( skippedCount, std::memory_order_relaxed );
                if ( bSkipped )
                    formatstring( arrBuffer, constant::buffer::kMax1024, "[  SKIPPED ] %# (%# ms) - %#", pCase->_pName, caseMilliseconds, skipReason.c_str() );
                else
                    formatstring( arrBuffer, constant::buffer::kMax1024, "%# %# (%# ms)", bPassed ? "[       OK ]" : "[  FAILED  ]", pCase->_pName, caseMilliseconds );
                Internal::writeLine( arrBuffer );

                caseReport.append( caseReport.size() == 0 ? "\n" : ",\n" );
                caseReport.appendFormat( "    {\"name\": \"%#\", \"file\": \"%#\", \"line\": %#, \"round\": %#, \"isolated\": %#, \"passed\": %#, \"failureKind\": \"%#\", "
                                         "\"skipped\": %#, \"skipReason\": \"%#\", \"milliseconds\": %#, \"failures\": [",
                                         Internal::escapeJSON( pCase->_pName ), Internal::escapeJSON( pCase->_pFile ), pCase->_line, roundIndex, bIsolate ? "true" : "false",
                                         bPassed ? "true" : "false", bPassed || bSkipped ? "" : Internal::getFailureKindName( failureKind ), bSkipped ? "true" : "false",
                                         Internal::escapeJSON( bSkipped ? skipReason : string{} ), caseMilliseconds );
                for ( size_t failureIndex = 0; failureIndex < listFailure.size(); ++failureIndex )
                {
                    caseReport.appendFormat( failureIndex == 0 ? "\"%#\"" : ", \"%#\"", Internal::escapeJSON( listFailure[failureIndex] ) );
                }
                caseReport.append( "]}" );
            }
        }
        logListener.reset();
        watchThread = thread{}; // 감시를 세우고 합류한다
        if ( options._pCrashHandler != nullptr )
            options._pCrashHandler->setContextValue( "TestCase", "" );

        const int64 runMilliseconds = ( MonotonicClock::nowNanoseconds() - runStart ) / constant::time::kNanosecondsPerMillisecond;
        formatstring( arrBuffer, constant::buffer::kMax1024, "[==========] %# case run(s) (%# ms), %# passed, %# failed, %# skipped, seed %#", ranCount, runMilliseconds,
                      ranCount - failedCount - skippedCount, failedCount, skippedCount, baseSeed );
        Internal::writeLine( arrBuffer );
        {
            TestRunnerInternal::CaseState& state = TestRunnerInternal::getState();
            ScopedLock<mutex>              lock{ state._mutex };
            formatstring( arrBuffer, constant::buffer::kMax1024, "%#%# error log line(s) declared with SW_EXPECT_LOG", test::kDeclaredErrorLinesPrefix, state._declaredErrorLineCount );
        }
        Internal::writeLine( arrBuffer );
        for ( const string& path : listUpdatedSnapshot )
        {
            formatstring( arrBuffer, constant::buffer::kMax1024, "[ SNAPSHOT ] updated %#", path );
            Internal::writeLine( arrBuffer );
        }

        // 모두 통과한 실행의 쓰기 폴더는 지운다 — 남기면 실행마다 쌓였다(`Saved/Test` 아래 수백 개). 실패한 실행은 들여다보도록 남기고 보고도 그 안에 쓴다(언리얼 자동화의 실패 산출물).
        // 보고 경로를 준 실행(`--test-report`)은 늘 그 경로에 쓴다.
        const bool bKeepScratch = failedCount > 0 || listUnmatched.empty() == false;
        if ( bKeepScratch == false && FileUtil::removeDirectory( scratchRoot ) == false )
        {
            formatstring( arrBuffer, constant::buffer::kMax1024, "[ WARNING  ] the scratch folder could not be removed: %#", scratchRoot.str() );
            Internal::writeLine( arrBuffer );
        }
        const AbsolutePath reportPath = options._reportPath.empty() == false ? FileUtil::toDiskPath( options._reportPath ) : bKeepScratch ? scratchRoot.join( "report.json" )
                                                                                                                                          : AbsolutePath{};
        if ( reportPath.empty() == false )
        {
            StringBuilder<constant::buffer::kMax4096> report; // 직렬화 글이라 잘리면 안 된다 — 넘치면 힙으로 자란다
            report.appendFormat( "{\n  \"schemaVersion\": %#,\n  \"seed\": %#,\n  \"filter\": \"%#\",\n  \"selectedCount\": %#,\n  \"runCount\": %#,\n  \"failedCount\": %#,\n"
                                 "  \"skippedCount\": %#,\n  \"milliseconds\": %#,\n  \"shard\": \"%#/%#\",\n  \"disabled\": [",
                                 kReportSchemaVersion, baseSeed, Internal::escapeJSON( options._filter ), static_cast<uint64>( listCase.size() ), ranCount, failedCount, skippedCount, runMilliseconds,
                                 options._shardIndex, options._shardCount );
            for ( size_t index = 0; index < listDisabled.size(); ++index )
            {
                report.appendFormat( "%#{\"name\": \"%#\", \"reason\": \"%#\"}", index == 0 ? "" : ", ", Internal::escapeJSON( listDisabled[index]->_pName ),
                                     Internal::escapeJSON( listDisabled[index]->_options._disabledReason ) );
            }
            report.append( "],\n  \"unmatchedPatterns\": [" );
            for ( size_t index = 0; index < listUnmatched.size(); ++index )
                report.appendFormat( index == 0 ? "\"%#\"" : ", \"%#\"", Internal::escapeJSON( listUnmatched[index] ) );
            report.append( "],\n  \"failureKinds\": {" );
            for ( const Internal::TestFailureKind kind : EnumRange<Internal::TestFailureKind>{} )
                report.appendFormat( kind == static_cast<Internal::TestFailureKind>( 0 ) ? "\"%#\": %#" : ", \"%#\": %#", Internal::getFailureKindName( kind ), arrKindCount[kind] );
            report.append( "},\n  \"updatedSnapshots\": [" );
            for ( size_t index = 0; index < listUpdatedSnapshot.size(); ++index )
            {
                report.appendFormat( index == 0 ? "\"%#\"" : ", \"%#\"", Internal::escapeJSON( listUpdatedSnapshot[index] ) );
            }
            report.append( "],\n  \"cases\": [" );
            report.append( caseReport.view() );
            report.append( "\n  ]\n}\n" );
            if ( FileUtil::ensureDirectoryExists( reportPath.getParent() ) == false || FileUtil::writeTextFile( reportPath, report.view() ) == false )
            {
                formatstring( arrBuffer, constant::buffer::kMax1024, "[  FAILED  ] could not write the test report %#", reportPath.str() );
                Internal::writeLine( arrBuffer );
                Internal::writeResultLine( "FAILED", ranCount, failedCount, skippedCount );
                return false;
            }
            formatstring( arrBuffer, constant::buffer::kMax1024, "[  REPORT  ] %#", reportPath.str() );
            Internal::writeLine( arrBuffer );
        }
        // 안 맞는 패턴은 돈 케이스가 모두 통과해도 실패다 — 끝 줄에 그 사실을 다시 적는다.
        for ( const string& pattern : listUnmatched )
        {
            formatstring( arrBuffer, constant::buffer::kMax1024, "[  FAILED  ] the pattern '%#' matched no registered case", pattern );
            Internal::writeLine( arrBuffer );
        }
        const bool bPassedRun = failedCount == 0 && listUnmatched.empty();
        Internal::writeResultLine( bPassedRun ? "PASSED" : "FAILED", ranCount, failedCount, skippedCount );
        return bPassedRun;
    }
} // namespace sw::test
