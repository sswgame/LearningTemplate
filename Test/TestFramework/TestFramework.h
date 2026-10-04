#pragma once
/**
 * @file TestFramework.h
 * @brief 자동 등록 단위 테스트 프레임워크
 */
#include "Engine/EngineMinimal.h"

#include "TestFramework/TestContext.h"
#include "TestFramework/TestEnvironment.h"
#include "TestFramework/TestFilter.h"

namespace test
{
    // ------------------------------------------------------------------------------
    // 1) 결과 — 실패 기록·케이스 메타
    // ------------------------------------------------------------------------------
    /** @brief 등록된 테스트 케이스 메타데이터. */
    struct TestCaseInfo
    {
        sw::string           _groupName;
        sw::string           _testName;
        sw::Delegate<void()> _func;

        /** @brief `Suite.Test` 전체 이름을 반환합니다. */
        sw::string fullName() const { return _groupName + "." + _testName; }
    };
} // namespace test

namespace test
{
    /**
     * @brief 호스트 스위트에서 아직 고치지 못한 Error 로그 하나(`SW_TEST_KNOWN_ERROR_LOG`).
     * @details 그 스위트의 케이스가 남긴 Error 중 `_substring` 을 담은 줄은 예상 밖 Error 로 세지 않고 `_hitCount` 에 센다.
     *          실행 요약이 견딘 줄 수를 찍으므로 조용히 묻히지 않는다.
     */
    struct KnownErrorLog
    {
        sw::string _suiteName;
        sw::string _substring;
        sw::string _reason;
        uint32     _hitCount{ 0 };
    };
} // namespace test

namespace test
{
    /** @brief 케이스 하나를 돌린 결과. */
    enum class CaseResult : uint8
    {
        Passed,
        Failed,
        Skipped,
    };

    /**
     * @brief 호스트 스위트(CI 러너가 못 돌리는 것)를 이번 실행에서 어떻게 다루는지.
     * @details `--host_suites=exclude` 는 CI 가 도는 집합(`<타깃>_NoGPU`), `--host_suites=only` 는 그 나머지
     *          (`<타깃>_HostOnly`)다. 두 ctest 항목은 `sw_addTestExecutable( ... HOST_SPLIT )` 가 등록한다.
     */
    enum class HostSuiteMode : uint8
    {
        All,     /**< 전부 돈다(기본 — 개발자가 실행 파일을 직접 돌릴 때). */
        Exclude, /**< 호스트 스위트를 뺀다. */
        Only,    /**< 호스트 스위트만 돈다. */
    };

    // ------------------------------------------------------------------------------
    // 2) 레지스트리 — 등록·필터·실행
    // ------------------------------------------------------------------------------
    class TestRegistry
    {
    public:
        /** @brief 프로세스 전역 레지스트리를 반환합니다. */
        static TestRegistry& getInstance();

        /** @brief 스위트·이름·함수로 테스트를 등록합니다. */
        void registerTest( const utf8* pSuiteName, const utf8* pTestName, sw::Delegate<void()> func );
        /** @brief 스위트가 GPU · 창 · DXC 같은 호스트 자원을 요구한다고 등록합니다(`SW_TEST_REQUIRES_HOST`). */
        void registerHostSuite( const utf8* pSuiteName, const utf8* pReason );

        /** @brief 호스트 스위트의 케이스가 남겨도 실패로 치지 않을 Error 로그(부분 문자열)를 등록합니다(`SW_TEST_KNOWN_ERROR_LOG`). */
        void registerKnownErrorLog( const utf8* pSuiteName, const utf8* pSubstring, const utf8* pReason );

        /** @brief 테스트 전용 인자를 파싱하고 나머지 인자를 반환합니다. */
        sw::vector<utf8*> configureFromArgs( int32 argc, utf8* argv[] );

        /** @brief glob 필터를 설정합니다. "Suite.*", 쉼표 include, "-RHI*" exclude. */
        void setFilter( const sw::string& filter );

        /** @brief 필터에 맞는 테스트를 모두 실행하고 실패 개수를 반환합니다. */
        int32 runAllTests();
        /** @brief 등록된 테스트 이름을 나열합니다. */
        void listTests() const;
        /** @brief 이번 실행이 이 케이스를 고르는지 — 이름 필터와 호스트 스위트 모드를 함께 봅니다. */
        bool isSelected( const TestCaseInfo& testInfo ) const;

        /** @brief 현재 테스트의 실패를 기록합니다. */
        void addFailure( const sw::string& condition, const sw::string& file, int32 line, const sw::string& message = "" );
        /** @brief 현재 테스트를 실패 없이 건너뜁니다. */
        void skipCurrentTest( const sw::string& reason, const sw::string& file, int32 line );

        /** @brief 현재 테스트의 상세 실행 컨텍스트를 반환합니다. */
        TestContext*       getCurrentContext() { return &_currentContext; }
        const TestContext* getCurrentContext() const { return &_currentContext; }

        /** @brief 실패를 현재 케이스 대신 이 목록에 모읍니다(널이면 되돌림). `ScopedFailureCapture` 만 부릅니다. */
        void                     setFailureCapture( sw::vector<TestFailure>* pCapture ) { _pFailureCapture = pCapture; }
        sw::vector<TestFailure>* getFailureCapture() const { return _pFailureCapture; }

        /**
         * @brief 이번 실행의 케이스 순서 — 고른 케이스를 등록 순서로, `--test_shuffle` 이면 섞어서.
         * @details 섞을 때는 gtest 처럼 **스위트 순서를 섞고 스위트 안의 케이스를 섞는다**(스위트는 붙어 있다). 회차마다 씨앗에 회차를
         *          더한다 — 같은 `--test_shuffle=<씨앗>` 이면 같은 순서가 다시 나온다.
         */
        sw::vector<const TestCaseInfo*> buildRunOrder( uint32 iteration ) const;

        /**
         * @brief 이 실행이 맡은 케이스 — 고른 케이스(`isSelected`) 중 이 샤드의 것을 등록 순서로.
         * @details `--test_shard=INDEX/COUNT`(또는 gtest 의 `GTEST_SHARD_INDEX` · `GTEST_TOTAL_SHARDS`)이면 **스위트 안에서 번갈아** 나눈다
         *          (스위트 번호 + 스위트 안 순번을 COUNT 로 나눈 나머지). 느린 스위트 하나가 실행 시간을 다 차지할 때(ReflectionTest 의 파서 시험)
         *          그 스위트가 반씩 갈려야 샤드가 의미 있다. 샤드가 하나면 고른 케이스 전부다.
         */
        sw::vector<const TestCaseInfo*> selectCasesForThisShard() const;

    private:
        /** @brief 호스트 스위트 선언이 실제 케이스와 맞는지 보고, 어긋난 수를 반환합니다. */
        int32 countHostSuiteMismatch() const;
        /** @brief 견딘 알려진 Error 로그를 실행 요약에 찍습니다. */
        void printKnownErrorLogSummary() const;
        /** @brief 케이스 하나를 돌리고(정리 · 임시 폴더 지우기까지) 결과 줄을 찍습니다. */
        CaseResult runCase( const TestCaseInfo& testInfo, float64& outElapsedMs );

        sw::vector<TestCaseInfo>        _listTest;
        sw::map<sw::string, sw::string> _mapHostSuiteReason;
        sw::vector<KnownErrorLog>       _listKnownErrorLog;
        TestFilter                      _filter;
        TestContext                     _currentContext;
        TestEnvironment                 _environment;
        sw::vector<TestFailure>*        _pFailureCapture{ nullptr };
        HostSuiteMode                   _hostSuiteMode{ HostSuiteMode::All };
        uint32                          _repeatCount{ 1 };
        uint32                          _shuffleSeed{ 0 };
        bool                            _bShuffle{ false };
        bool                            _listOnly{ false };
        bool                            _bAllowEmptySuite{ false };
        uint32                          _shardIndex{ 0 }; /**< 이 실행의 샤드(0 부터) — `--test_shard=INDEX/COUNT` */
        uint32                          _shardCount{ 1 }; /**< 샤드 수(1 = 나누지 않는다) */
        bool                            _bInvalidArgument{ false };
    };
} // namespace test

namespace test
{
    /**
     * @brief 스코프 안의 단언 실패를 현재 케이스 대신 여기에 모읍니다 — **단언 자체를 검사할 때만** 씁니다.
     * @details 실패하는 단언을 일부러 불러 "실패를 기록하는가 · 무엇이라 찍는가 · ASSERT 가 멈추는가" 를 보려면 그 실패가
     *          케이스를 떨어뜨리면 안 된다. gtest 의 `EXPECT_FATAL_FAILURE`(`ScopedFakeTestPartResultReporter`)가 같은 일을 한다.
     *          모인 실패는 찍지도 로그로 남기지도 않는다. 겹쳐 쓰면 안쪽이 이긴다.
     */
    class ScopedFailureCapture
    {
    public:
        ScopedFailureCapture()
            : _pPreviousCapture{ TestRegistry::getInstance().getFailureCapture() }
        {
            TestRegistry::getInstance().setFailureCapture( &_listFailure );
        }

        ~ScopedFailureCapture() { TestRegistry::getInstance().setFailureCapture( _pPreviousCapture ); }

        ScopedFailureCapture( const ScopedFailureCapture& )            = delete;
        ScopedFailureCapture& operator=( const ScopedFailureCapture& ) = delete;

        /** @brief 지금까지 모인 실패. */
        const sw::vector<TestFailure>& getListFailure() const { return _listFailure; }

    private:
        sw::vector<TestFailure>  _listFailure;
        sw::vector<TestFailure>* _pPreviousCapture{ nullptr };
    };
} // namespace test

namespace test
{
    /** @brief 스코프 내에서 전역 로그 출력을 임시 억제하는 RAII 헬퍼 */
    class ScopedLogSuppressor
    {
    public:
        ScopedLogSuppressor()
            : _pOldSink{ sw::Logger::getGlobalSink() }
        {
            sw::Logger::setGlobalSink( nullptr );
        }

        ~ScopedLogSuppressor()
        {
            sw::Logger::setGlobalSink( _pOldSink );
        }

        ScopedLogSuppressor( const ScopedLogSuppressor& )            = delete;
        ScopedLogSuppressor& operator=( const ScopedLogSuppressor& ) = delete;

    private:
        sw::ILogSink* _pOldSink;
    };
} // namespace test

namespace test
{
    /** @brief 스코프 내에서 Error/Warning 로그에 [Expected Defensive Test] 표기를 부착하는 테스트용 싱크 프록시 */
    class DefensiveTestLogSink final : public sw::ILogSink
    {
    public:
        DefensiveTestLogSink( sw::ILogSink* pWrappedSink, const utf8* pReason = nullptr )
            : _pWrappedSink{ pWrappedSink }
            , _reason{ pReason != nullptr ? pReason : "" }
        {
        }

        void initialize() override
        {
            if ( _pWrappedSink != nullptr )
                _pWrappedSink->initialize();
        }

        void shutdown() override
        {
            if ( _pWrappedSink != nullptr )
                _pWrappedSink->shutdown();
        }

        void writeLog( sw::LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pMessage, const utf8* pFile, int32 line ) override
        {
            if ( _pWrappedSink == nullptr )
                return;

            if ( level == sw::LogLevel::Error || level == sw::LogLevel::Warning )
            {
                sw::fixed_string<sw::constant::kMaxBuffer4096> decoratedMsg{};
                sw::formatstring( decoratedMsg.data(), decoratedMsg.capacity(), "[Expected Defensive Test] %#", pMessage != nullptr ? pMessage : "" );
                _pWrappedSink->writeLog( level, pTag, pCaller, decoratedMsg.c_str(), pFile, line );
            }
            else
            {
                _pWrappedSink->writeLog( level, pTag, pCaller, pMessage, pFile, line );
            }
        }

        sw::DelegateHandle addLogWrittenListener( const sw::LogWrittenDelegate& listener ) override
        {
            return _pWrappedSink != nullptr ? _pWrappedSink->addLogWrittenListener( listener ) : sw::DelegateHandle{};
        }

        void removeLogWrittenListener( const sw::DelegateHandle& handle ) override
        {
            if ( _pWrappedSink != nullptr )
                _pWrappedSink->removeLogWrittenListener( handle );
        }

        uint32 releaseListenerCodeWithin( const void* pBegin, const void* pEnd ) override
        {
            return _pWrappedSink != nullptr ? _pWrappedSink->releaseListenerCodeWithin( pBegin, pEnd ) : 0;
        }

        const sw::string& getLogFolderPath() override
        {
            static const sw::string s_emptyPath{};
            return _pWrappedSink != nullptr ? _pWrappedSink->getLogFolderPath() : s_emptyPath;
        }

        void flushForCrash() override
        {
            if ( _pWrappedSink != nullptr )
                _pWrappedSink->flushForCrash();
        }

    private:
        sw::ILogSink* _pWrappedSink{ nullptr };
        sw::string    _reason;
    };
} // namespace test

namespace test
{
    /** @brief 스코프 내에서 발생하는 Error/Warning 로그를 의도된 방어/예외 테스트(Expected)로 마킹하는 RAII 헬퍼 */
    class ScopedDefensiveTestLog
    {
    public:
        explicit ScopedDefensiveTestLog( const utf8* pReason = nullptr )
            : _pOldSink{ sw::Logger::getGlobalSink() }
            , _defensiveSink{ _pOldSink, pReason }
        {
            if ( _pOldSink != nullptr )
            {
                if ( pReason != nullptr && pReason[0] != '\0' )
                {
                    sw::fixed_string<sw::constant::kMaxBuffer256> notice{};
                    sw::formatstring( notice.data(), notice.capacity(), ">>> [Defensive Test] Expected Error/Warning validation: '%#' <<<", pReason );
                    _pOldSink->writeLog( sw::LogLevel::Info, "Test", nullptr, notice.c_str(), __FILE__, __LINE__ );
                }
                else
                {
                    _pOldSink->writeLog( sw::LogLevel::Info, "Test", nullptr, ">>> [Defensive Test] Expected Error/Warning validation scope began <<<", __FILE__, __LINE__ );
                }
            }
            sw::Logger::setGlobalSink( &_defensiveSink );
        }

        ~ScopedDefensiveTestLog()
        {
            sw::Logger::setGlobalSink( _pOldSink );
            if ( _pOldSink != nullptr )
                _pOldSink->writeLog( sw::LogLevel::Info, "Test", nullptr, "<<< [Defensive Test] Expected Error/Warning validation scope ended <<<", __FILE__, __LINE__ );
        }

        ScopedDefensiveTestLog( const ScopedDefensiveTestLog& )            = delete;
        ScopedDefensiveTestLog& operator=( const ScopedDefensiveTestLog& ) = delete;

    private:
        sw::ILogSink*        _pOldSink{ nullptr };
        DefensiveTestLogSink _defensiveSink;
    };
} // namespace test

namespace test
{
    /**
     * @brief 스코프 동안 단언(`SW_ASSERT` · `SW_LOG_ASSERT`)이 멈추지 않고 세어지게 하는 RAII 헬퍼입니다.
     * @details Debug 의 단언은 디버거에서 멈추므로(`SW_DEBUG_BREAK`) 단언이 걸리는 입력을 시험하면 프로세스가 죽는다. 이 스코프 안에서는
     *          단언이 세어지기만 하고 코드는 그 뒤로 이어진다(배포본과 같은 길). `getCount()` 로 "단언이 걸렸나" 를 묻는다.
     *          단언은 Debug 에서만 있으므로 그 밖의 구성에서 `getCount()` 는 늘 0 이다 — 시험은 `kAssertsAreActive` 로 갈라 묻는다.
     *          `SW_LOG_ASSERT` 는 Error 로그도 남기므로 `SW_TEST_DEFENSIVE_SCOPE` 와 같이 쓴다.
     */
    class ScopedAssertCapture
    {
    public:
#if defined( SW_DEBUG )
        static constexpr bool kAssertsAreActive = true;

        ScopedAssertCapture()
            : _startCount{ sw::internal::getCapturedAssertCount() }
        {
            sw::internal::beginAssertCapture();
        }
        ~ScopedAssertCapture() { sw::internal::endAssertCapture(); }
        ScopedAssertCapture( const ScopedAssertCapture& )            = delete;
        ScopedAssertCapture& operator=( const ScopedAssertCapture& ) = delete;

        /** @brief 이 스코프가 시작한 뒤 가로챈 단언 수입니다(다른 스레드의 단언도 센다). */
        uint32 getCount() const { return sw::internal::getCapturedAssertCount() - _startCount; }

    private:
        uint32 _startCount;
#else
        // 단언이 멈추지 않는 구성에는 가로채기 창구(`sw::internal::beginAssertCapture`)가 없다 — 이 스코프는 아무것도 하지 않는다.
        static constexpr bool kAssertsAreActive = false;

        ScopedAssertCapture()                                        = default;
        ScopedAssertCapture( const ScopedAssertCapture& )            = delete;
        ScopedAssertCapture& operator=( const ScopedAssertCapture& ) = delete;

        /** @brief 단언이 없는 구성이라 늘 0 입니다. */
        uint32 getCount() const { return 0; }
#endif
    };
} // namespace test

namespace test
{
    /**
     * @brief 스코프 동안 남은 Warning · Error 로그를 모아 "이 경고가 나왔나" 를 묻는 RAII 헬퍼입니다.
     * @details 시험마다 `Logger::addGlobalListener` 리스너를 손으로 만들지 않게 한 자리에 둔다. 리스너는 남기는 스레드에서
     *          곧바로 불리므로 같은 스레드의 경고는 그 호출이 돌아오면 이미 모여 있다. `ScopedDefensiveTestLog` 와 같이 써도 된다 — 그때 메시지 앞에
     *          `[Expected Defensive Test]` 가 붙지만 `countContaining` 은 부분 문자열로 찾는다.
     */
    class ScopedLogCollector
    {
    public:
        ScopedLogCollector()
        {
            _handle = sw::Logger::addGlobalListener( SW_DELEGATE_LAMBDA( sw::LogWrittenDelegate, [this]( const sw::LogEntry& entry )
            {
                if ( entry._level != sw::LogLevel::Error && entry._level != sw::LogLevel::Warning )
                    return;
                std::scoped_lock<sw::mutex> lock{ _mutex };
                _listMessage.push_back( entry._message );
            } ) );
        }

        ~ScopedLogCollector() { sw::Logger::removeGlobalListener( _handle ); }

        ScopedLogCollector( const ScopedLogCollector& )            = delete;
        ScopedLogCollector& operator=( const ScopedLogCollector& ) = delete;

        /** @brief 모은 Warning · Error 중 `text` 를 담은 줄의 수입니다. */
        uint32 countContaining( sw::string_view text ) const
        {
            std::scoped_lock<sw::mutex> lock{ _mutex };
            uint32                      count = 0;
            for ( const sw::string& message : _listMessage )
            {
                if ( sw::string_view{ message.c_str(), message.size() }.find( text ) != sw::string_view::npos )
                    ++count;
            }
            return count;
        }

        /** @brief 모은 줄을 한 줄에 하나씩 이어 붙입니다 — 실패 메시지에 붙여 "무엇이 나왔나" 를 보입니다. */
        sw::string joined() const
        {
            std::scoped_lock<sw::mutex> lock{ _mutex };
            sw::string                  result;
            for ( const sw::string& message : _listMessage )
            {
                result += "\n  ";
                result += message;
            }
            return result;
        }

    private:
        mutable sw::mutex      _mutex;
        sw::vector<sw::string> _listMessage;
        sw::DelegateHandle     _handle;
    };
} // namespace test

namespace test
{
    // ------------------------------------------------------------------------------
    // 실패 보고 — 단언 매크로가 부르는 **바깥** 함수들
    //
    // 단언 자리에는 비교와 이 호출 하나만 남긴다. 실패 경로(문자열 둘 · `ostringstream` · 서식)를 단언마다 인라인으로
    // 펼치면 단언이 수천 곳이라 그것이 테스트 바이너리의 대부분이 되고, 최적화 빌드는 그것을 더 펼쳐 **Release 바이너리가
    // Debug 보다 커진다.** 실패는 드물고 느려도 되는 경로다.
    // ------------------------------------------------------------------------------
    /** @brief 단언 실패 하나를 기록합니다. @param pMessage 덧붙일 말(널이면 없음). */
    SW_NOINLINE void reportFailure( const utf8* pCondition, const utf8* pFile, int32 line, const utf8* pMessage );
    /** @brief 단언 실패 하나를 덧붙일 말과 함께 기록합니다. */
    SW_NOINLINE void reportFailure( const utf8* pCondition, const utf8* pFile, int32 line, const sw::string& message );

    /** @brief 같아야 할 두 값이 다를 때 — 두 값을 찍어 실패를 기록합니다. */
    template <typename TExpected, typename TActual>
    SW_NOINLINE void reportNotEqual( const utf8* pCondition, const utf8* pFile, int32 line, const TExpected& expected, const TActual& actual )
    {
        std::ostringstream oss;
        oss << "Expected [" << expected << "], Actual [" << actual << "]";
        reportFailure( pCondition, pFile, line, oss.str().c_str() );
    }

    /** @brief 달라야 할 두 값이 같을 때 — 그 값을 찍어 실패를 기록합니다. */
    template <typename TValue>
    SW_NOINLINE void reportEqual( const utf8* pCondition, const utf8* pFile, int32 line, const TValue& value )
    {
        std::ostringstream oss;
        oss << "Expected not equal to [" << value << "]";
        reportFailure( pCondition, pFile, line, oss.str().c_str() );
    }

    /** @brief 허용 오차를 넘었을 때 — 차이와 오차를 찍어 실패를 기록합니다. */
    template <typename TDifference, typename TTolerance>
    SW_NOINLINE void reportNotNear( const utf8* pCondition, const utf8* pFile, int32 line, const TDifference& difference, const TTolerance& tolerance )
    {
        std::ostringstream oss;
        oss << "Diff [" << difference << "] exceeds tolerance [" << tolerance << "]";
        reportFailure( pCondition, pFile, line, oss.str().c_str() );
    }

    /**
     * @brief `SW_EXPECT_STREQ` 가 비교하는 글 — 복사하지 않는 뷰와, 그것이 널 포인터였는지.
     * @details 널 `const utf8*` 로 `sw::string` 을 만들면 그 자리에서 죽는다. 그러면 실패를 찍어 보기도 전에
     *          **테스트 바이너리 전체가 내려가고**, 같은 파일의 뒤쪽 케이스가 통째로 사라진다. 그런데 "널을 돌려주기
     *          시작한 회귀" 야말로 이 매크로가 가장 잡아야 할 것이다. 그리고 널을 `"<null>"` 로 찍는 것만으로는
     *          부족하다 — 진짜 `"<null>"` 문자열과 널이 같다고 나온다. 그래서 널 여부를 따로 든다.
     */
    struct ComparableText
    {
        sw::string_view _text;
        bool            _bNull{ false };
    };
} // namespace test

namespace test
{
    /** @brief 널에도 안전하게 비교용 글을 만듭니다. 받는 형태는 이 셋이다 — 나머지는 이 중 하나로 바뀌어 들어온다. */
    inline ComparableText toComparableText( const utf8* pText ) { return pText != nullptr ? ComparableText{ pText, false } : ComparableText{ {}, true }; }
    inline ComparableText toComparableText( const sw::string& text ) { return ComparableText{ text, false }; }
    inline ComparableText toComparableText( sw::string_view text ) { return ComparableText{ text, false }; }

    /** @brief 두 글이 같은지 보고, 다르면 두 글을 찍어 실패를 기록합니다(`SW_EXPECT_STREQ`). */
    void expectSameText( const utf8* pCondition, const utf8* pFile, int32 line, const ComparableText& expected, const ComparableText& actual );

    /**
     * @brief 이 프로세스·이 케이스만 쓰는 임시 파일 경로를 만듭니다.
     * @param fileName 쓰려는 파일 이름. 확장자는 그대로 남는다(로더가 그것으로 형식을 고른다).
     * @return `<임시 폴더>/sw_<pid>/<스위트_케이스>/<fileName>` — 그 폴더는 만들어 둔다.
     * @details `%TEMP%/test_malformed.wav` 처럼 **고정된 이름**에 쓰면 안 된다. 같은 `EngineTest` 가 네 프리셋에서
     *          각각 돌고 CI 는 그것들을 나란히 돌리므로, 한쪽의 `removeFile` 이 다른 쪽이 방금 쓴 파일을 지워
     *          간헐 실패가 된다. 프로세스 id 를 섞으면 그 충돌이 사라지고, 케이스 이름까지 섞으면
     *          **한 프로세스 안에서 같은 이름을 쓰는 두 케이스**도 서로를 안 밟는다.
     *
     *          **케이스가 끝나면 프레임워크가 그 케이스 폴더를 통째로 지운다** — `SW_TEST_DEFER_CLEANUP` 이 돈 뒤에. 엔진이 옆에
     *          쿠킹해 둔 `.bin` · `.meta` 도 같은 폴더라 함께 지워진다. 케이스가 끝에서 `removeFile` 을 손으로 부르면 단언으로 일찍
     *          빠질 때 그 줄에 닿지 않아 남는다.
     *          못 지우면(열린 핸들) 그 케이스가 진다. 폴더를 케이스마다 두는 것은 정리가 **훑기 없이** 끝나게 하려는 것이다
     *          (임시 폴더 전체를 이름으로 훑으면 케이스마다 수십 ms 가 든다).
     * @note 케이스 밖에서 부르면 프로세스 폴더(`sw_<pid>`)에 생기고, 실행이 끝날 때 지워진다.
     */
    sw::string makeTempPath( sw::string_view fileName );

    /**
     * @brief `makeTempPath` 로 이름을 지은 **폴더**를 만들어 돌려줍니다. 케이스가 끝나면 통째로 지워진다.
     * @details 파일 이름 자체가 뜻을 갖는 경우(팩 이름이 우선순위를 정한다 — `engine_` · `game_` · `patch_`, 파서 산출물 이름이
     *          헤더 이름을 따른다) 그 이름을 지킨 채 이 폴더 안에 둔다. 같은 이름으로 다시 부르면 같은 폴더다.
     */
    sw::string makeTempDirectory( sw::string_view directoryName );

    /** @brief 정적 초기화로 테스트를 레지스트리에 붙입니다. */
    class TestRegistrar
    {
    public:
        /** @brief 정적 초기화 시점에 테스트를 레지스트리에 등록합니다. */
        TestRegistrar( const utf8* pSuiteName, const utf8* pTestName, sw::Delegate<void()> func )
        {
            TestRegistry::getInstance().registerTest( pSuiteName, pTestName, func );
        }
    };
} // namespace test

namespace test
{
    /** @brief 정적 초기화로 알려진 Error 로그 선언을 레지스트리에 붙입니다. */
    class KnownErrorLogRegistrar
    {
    public:
        /** @brief 정적 초기화 시점에 알려진 Error 로그를 등록합니다. */
        KnownErrorLogRegistrar( const utf8* pSuiteName, const utf8* pSubstring, const utf8* pReason )
        {
            TestRegistry::getInstance().registerKnownErrorLog( pSuiteName, pSubstring, pReason );
        }
    };
} // namespace test

namespace test
{
    /** @brief 정적 초기화로 호스트 스위트 선언을 레지스트리에 붙입니다. */
    class HostSuiteRegistrar
    {
    public:
        /** @brief 정적 초기화 시점에 호스트 스위트를 등록합니다. */
        HostSuiteRegistrar( const utf8* pSuiteName, const utf8* pReason )
        {
            TestRegistry::getInstance().registerHostSuite( pSuiteName, pReason );
        }
    };
} // namespace test

// ------------------------------------------------------------------------------
// 3) 매크로 — 케이스 등록·스킵·어서션
// ------------------------------------------------------------------------------
/** @brief 현재 스코프 동안 의도된 실패로 인한 로그 출력을 억제합니다. */
#define SW_TEST_SUPPRESS_LOGS() test::ScopedLogSuppressor SW_CONCAT( logSuppressor_, __LINE__ )

/** @brief 현재 스코프를 의도된 방어/예외 테스트 구간으로 마킹하여 Error/Warning 로그에 [Expected Defensive Test]를 표기합니다. */
#define SW_TEST_DEFENSIVE_SCOPE( ... ) test::ScopedDefensiveTestLog SW_CONCAT( defensiveLog_, __LINE__ )( "" __VA_ARGS__ )

/** @brief 테스트 케이스를 등록하고 함수를 정의합니다. */
#define SW_TEST_CASE( SuiteName, TestName )                                                                                                                              \
    void                       test_##SuiteName##_##TestName();                                                                                                          \
    static test::TestRegistrar registrar_##SuiteName##_##TestName( #SuiteName, #TestName, SW_DELEGATE_FUNCTION( sw::Delegate<void()>, test_##SuiteName##_##TestName ) ); \
    void                       test_##SuiteName##_##TestName()

/**
 * @brief 이 스위트는 CI 러너가 돌릴 수 없다고 선언합니다 — GPU · 디스플레이 · DXC 처럼 호스트에만 있는 것이 필요할 때.
 * @param SuiteName 그 스위트. **이 파일에 사는 스위트**여야 하고, 이 파일에는 다른 스위트를 두지 않는다(`CheckTestSuites`).
 * @param reason    왜 CI 가 못 돌리는지(영문 — `--test_list` 와 실행 요약에 찍힌다).
 * @details 선언이 곧 분류다. `sw_addTestExecutable( ... HOST_SPLIT )` 가 `<타깃>_NoGPU`(`--host_suites=exclude`,
 *          라벨 `nogpu`)와 `<타깃>_HostOnly`(`--host_suites=only`, 라벨 `hostgpu`)를 등록하므로 새 호스트 스위트는
 *          **이 한 줄**로 CI 에서 빠지고 호스트 실행에 들어간다. CMake 에는 스위트 이름을 적지 않는다.
 */
#define SW_TEST_REQUIRES_HOST( SuiteName, reason ) static test::HostSuiteRegistrar hostSuite_##SuiteName( #SuiteName, reason )

/**
 * @brief 호스트 스위트가 남기는 Error 로그 가운데 **아직 고치지 못한 것** 하나를 견딥니다.
 * @param SuiteName 그 스위트(`SW_TEST_REQUIRES_HOST` 로 선언한 것).
 * @param substring 그 Error 줄에 든 고정 문구(영문 원문).
 * @param reason    왜 견디는지 · 어디서 고칠지(영문 — 실행 요약에 찍힌다).
 * @details 호스트 스위트의 케이스는 예상 밖 Error 로그 하나로 진다(검증 레이어 · 드라이버 오류가 단언 없이 지나가지 않게).
 *          의도된 Error 는 `SW_TEST_DEFENSIVE_SCOPE` 로 감싸고, 이 선언은 **원인이 시험 밖(엔진)에 있고 아직 못 고친 것**에만 쓴다.
 *          실행 요약이 견딘 줄 수를 찍고, 한 번도 나오지 않았으면 "지워도 된다" 고 알린다. 고쳤으면 이 줄을 지운다.
 */
#define SW_TEST_KNOWN_ERROR_LOG( SuiteName, substring, reason ) \
    static test::KnownErrorLogRegistrar SW_CONCAT( knownErrorLog_##SuiteName##_, __LINE__ )( #SuiteName, substring, reason )

/** @brief 현재 테스트를 사유와 함께 건너뜁니다(스위트 실패로 치지 않음). */
#define SW_TEST_SKIP( reason )                                                               \
    do                                                                                       \
    {                                                                                        \
        test::TestRegistry::getInstance().skipCurrentTest( ( reason ), __FILE__, __LINE__ ); \
        return;                                                                              \
    } while ( 0 )

/** @brief 현재 테스트 종료 후 역순으로 실행할 정리 함수를 등록합니다. */
#define SW_TEST_DEFER_CLEANUP( cleanup ) test::TestRegistry::getInstance().getCurrentContext()->deferCleanup( cleanup )

// 단언은 전부 아래 둘 중 하나의 뼈대다 — `onFail` 이 `(void)0` 이면 EXPECT(기록하고 계속), `return` 이면 ASSERT(기록하고
// 그 케이스를 끝낸다). 조건 글(`#cond`)은 **바깥 매크로에서**
// 만든다 — 안쪽 뼈대로 넘어간 인자는 이미 매크로가 풀린 뒤라, 거기서 만들면 `SW_TRUE` 가 `1` 로 찍힌다.

/** @brief 조건 단언의 뼈대 — `bPassed` 가 거짓이면 실패를 기록하고 `onFail` 을 실행합니다. */
#define SW_TEST_CHECK_IMPL( bPassed, pCondition, message, onFail )          \
    do                                                                      \
    {                                                                       \
        if ( !( bPassed ) )                                                 \
        {                                                                   \
            test::reportFailure( pCondition, __FILE__, __LINE__, message ); \
            onFail;                                                         \
        }                                                                   \
    } while ( 0 )

/** @brief 값 비교 단언의 뼈대 — 두 값을 한 번씩만 평가하고, 다르면 두 값을 찍어 기록한 뒤 `onFail` 을 실행합니다. */
#define SW_TEST_CHECK_EQUAL_IMPL( expected, actual, pCondition, onFail )                    \
    do                                                                                      \
    {                                                                                       \
        const auto _swExpected = ( expected );                                              \
        const auto _swActual   = ( actual );                                                \
        if ( !( _swExpected == _swActual ) )                                                \
        {                                                                                   \
            test::reportNotEqual( pCondition, __FILE__, __LINE__, _swExpected, _swActual ); \
            onFail;                                                                         \
        }                                                                                   \
    } while ( 0 )

/** @brief 약한 기대 — 실패를 기록하고 계속합니다. */
#define SW_EXPECT_TRUE( cond ) SW_TEST_CHECK_IMPL( cond, #cond, nullptr, (void)0 )

/** @brief 약한 기대 — 실패 시 메시지를 함께 기록합니다. */
#define SW_EXPECT_TRUE_MSG( cond, msg ) SW_TEST_CHECK_IMPL( cond, #cond, msg, (void)0 )

/** @brief 조건이 거짓이어야 합니다. */
#define SW_EXPECT_FALSE( cond ) SW_TEST_CHECK_IMPL( !( cond ), "!(" #cond ")", nullptr, (void)0 )

/**
 * @brief 조건이 거짓이어야 하며, 실패 시 메시지를 함께 기록합니다.
 * @details **실패했을 때 가장 설명이 필요한 쪽이 부정 단언**이다(무엇이 열려 있으면 안 되는지). 그래서
 *          `SW_EXPECT_TRUE_MSG( x == false, ... )` 로 뒤집어 쓰지 않게 짝을 맞춘다.
 */
#define SW_EXPECT_FALSE_MSG( cond, msg ) SW_TEST_CHECK_IMPL( !( cond ), "!(" #cond ")", msg, (void)0 )

/** @brief 두 값이 같아야 합니다. */
#define SW_EXPECT_EQUAL( expected, actual ) SW_TEST_CHECK_EQUAL_IMPL( expected, actual, #actual " == " #expected, (void)0 )

/** @brief 두 값이 달라야 합니다. */
#define SW_EXPECT_NOT_EQUAL( expected, actual )                                             \
    do                                                                                      \
    {                                                                                       \
        const auto _swExpected = ( expected );                                              \
        const auto _swActual   = ( actual );                                                \
        if ( _swExpected == _swActual )                                                     \
            test::reportEqual( #actual " != " #expected, __FILE__, __LINE__, _swExpected ); \
    } while ( 0 )

/** @brief 허용 오차 안에서 두 값이 가까워야 합니다. */
#define SW_EXPECT_NEAR_EQUAL( expected, actual, tolerance )                                                                         \
    do                                                                                                                              \
    {                                                                                                                               \
        const auto _swTolerance  = ( tolerance );                                                                                   \
        const auto _swDifference = sw::MathUtil::abs( ( expected ) - ( actual ) );                                                  \
        if ( _swDifference > _swTolerance )                                                                                         \
            test::reportNotNear( "|" #actual " - " #expected "| <= " #tolerance, __FILE__, __LINE__, _swDifference, _swTolerance ); \
    } while ( 0 )

/** @brief 포인터가 null 이 아니어야 합니다. */
#define SW_EXPECT_NOT_NULL( ptr ) SW_TEST_CHECK_IMPL( ( ptr ) != nullptr, #ptr " != nullptr", nullptr, (void)0 )

/** @brief 포인터가 null 이어야 합니다. */
#define SW_EXPECT_NULL( ptr ) SW_TEST_CHECK_IMPL( ( ptr ) == nullptr, #ptr " == nullptr", nullptr, (void)0 )

/** @brief null 종료/문자열 유사 값을 비교합니다 — 널은 널끼리만 같습니다. */
#define SW_EXPECT_STREQ( expected, actual ) \
    test::expectSameText( #actual " == " #expected, __FILE__, __LINE__, test::toComparableText( expected ), test::toComparableText( actual ) )

/** @brief 컨테이너/문자열이 비어 있어야 합니다. */
#define SW_EXPECT_EMPTY( value ) SW_TEST_CHECK_IMPL( ( value ).empty(), #value ".empty()", nullptr, (void)0 )

/** @brief 강한 어서션 — 실패를 기록하고 현재 테스트를 중단합니다. */
#define SW_ASSERT_TRUE( cond ) SW_TEST_CHECK_IMPL( cond, #cond, nullptr, return )

/** @brief 강한 어서션 — 실패 시 메시지(대개 실제로 받은 텍스트)를 함께 기록하고 중단합니다. */
#define SW_ASSERT_TRUE_MSG( cond, msg ) SW_TEST_CHECK_IMPL( cond, #cond, msg, return )

/** @brief 조건이 거짓이어야 하며, 아니면 테스트를 중단합니다. */
#define SW_ASSERT_FALSE( cond ) SW_TEST_CHECK_IMPL( !( cond ), "!(" #cond ")", nullptr, return )

/** @brief 두 값이 같아야 하며, 아니면 테스트를 중단합니다. */
#define SW_ASSERT_EQUAL( expected, actual ) SW_TEST_CHECK_EQUAL_IMPL( expected, actual, #actual " == " #expected, return )

/** @brief 포인터가 null 이 아니어야 하며, 아니면 테스트를 중단합니다. */
#define SW_ASSERT_NOT_NULL( ptr ) SW_TEST_CHECK_IMPL( ( ptr ) != nullptr, #ptr " != nullptr", nullptr, return )

// `SW_ASSERT_NULL` 은 **일부러 없다.** 짝을 맞추려고 만들 수는 있지만 부를 자리가 없다
// (`SW_EXPECT_NULL` 을 쓰는 자리는 실패해도 계속 가는 게 맞는 자리들이다).
// 쓰는 곳이 생기면 그때 위 뼈대로 한 줄 넣는다.
