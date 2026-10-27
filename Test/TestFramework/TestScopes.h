/**
 * @file Test/TestFramework/TestScopes.h
 * @brief 케이스 안에서 쓰는 범위 도우미 — 실패 · 로그 · 엔진 단언 가로채기, 방어 테스트 표식, 실패 추적
 */
#pragma once
#include "Engine/Automation/AutomationLogPolicy.h"
#include "Engine/EngineMinimal.h"

#include "TestFramework/TestRegistry.h"

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
    /**
     * @brief 이 범위 동안 이 스레드에서 진 단언에 맥락 한 줄을 붙입니다 — gtest `SCOPED_TRACE` 와 같다.
     * @details 한 케이스가 여러 조건(백엔드 · 씨앗 · 입력 파일)을 도는 곳에서 "어느 조건에서 졌나" 를 실패 줄과 재현 요약에 남긴다
     *          (`test::RHIBackendSweep` 은 백엔드마다 건다). 겹치면 바깥부터 ` > ` 로 잇는다. 다른 스레드의 단언에는 붙지 않는다.
     */
    class ScopedTrace
    {
    public:
        explicit ScopedTrace( sw::string text );
        ~ScopedTrace();

        ScopedTrace( const ScopedTrace& )            = delete;
        ScopedTrace& operator=( const ScopedTrace& ) = delete;

        /** @brief 이 스레드에 지금 걸린 추적 — 바깥부터 ` > ` 로 이은 글(없으면 빈 글). */
        static sw::string makeCurrentTrace();
    };
} // namespace test

namespace test
{
    /**
     * @brief 스코프 내에서 Error/Warning 로그에 [Expected Defensive Test] 표기를 부착하는 테스트용 싱크 프록시
     * @param listNeedle 비어 있지 않으면 문구에 이 글 가운데 하나가 든 줄에만 표기한다 — 나머지 Warning · Error 는 표식 없이 지나가 예상 밖으로 세어진다.
     */
    class DefensiveTestLogSink final : public sw::ILogSink
    {
    public:
        DefensiveTestLogSink( sw::ILogSink* pWrappedSink, const utf8* pReason = nullptr, sw::vector<sw::string> listNeedle = {} )
            : _pWrappedSink{ pWrappedSink }
            , _reason{ pReason != nullptr ? pReason : "" }
            , _listNeedle{ std::move( listNeedle ) }
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

        void writeLogWithID( sw::LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pMessage, const utf8* pFile, int32 line, const utf8* pID ) override
        {
            if ( _pWrappedSink == nullptr )
                return;

            if ( ( level == sw::LogLevel::Error || level == sw::LogLevel::Warning ) && isExpectedMessage( pMessage ) )
            {
                sw::fixed_string<sw::constant::kMaxBuffer4096> decoratedMsg{};
                sw::formatstring( decoratedMsg.data(), decoratedMsg.capacity(), "%# %#", sw::AutomationLogPolicy::kDefensivePrefix, pMessage != nullptr ? pMessage : "" );
                _pWrappedSink->writeLogWithID( level, pTag, pCaller, decoratedMsg.c_str(), pFile, line, pID );
            }
            else
            {
                _pWrappedSink->writeLogWithID( level, pTag, pCaller, pMessage, pFile, line, pID );
            }
        }

        sw::Subscription registerLogWrittenListener( const sw::LogWrittenDelegate& listener ) override
        {
            // 감싼 싱크의 구독을 그대로 돌려준다 — 이 스코프가 먼저 끝나도 구독은 감싼 싱크에서 풀린다.
            return _pWrappedSink != nullptr ? _pWrappedSink->registerLogWrittenListener( listener ) : sw::Subscription{};
        }

        uint32 removeListenerCodeWithin( const void* pBegin, const void* pEnd ) override
        {
            return _pWrappedSink != nullptr ? _pWrappedSink->removeListenerCodeWithin( pBegin, pEnd ) : 0;
        }

        sw::string_view getLogFolderPath() override
        {
            if ( _pWrappedSink == nullptr )
                return {};
            return _pWrappedSink->getLogFolderPath();
        }

        void flushForCrash() override
        {
            if ( _pWrappedSink != nullptr )
                _pWrappedSink->flushForCrash();
        }

    private:
        /** @brief 거름 글이 없으면 모든 줄이 의도된 것이고, 있으면 그 글 하나가 든 줄만 의도된 것이다. */
        bool isExpectedMessage( const utf8* pMessage ) const
        {
            if ( _listNeedle.empty() )
                return true;
            const sw::string_view message{ pMessage != nullptr ? pMessage : "" };
            for ( const sw::string& needle : _listNeedle )
            {
                if ( message.find( sw::string_view{ needle.c_str(), needle.size() } ) != sw::string_view::npos )
                    return true;
            }
            return false;
        }

        sw::ILogSink*          _pWrappedSink{ nullptr };
        sw::string             _reason;
        sw::vector<sw::string> _listNeedle;
    };
} // namespace test

namespace test
{
    /**
     * @brief 스코프 내에서 전역 로그 출력을 임시 억제하는 RAII 헬퍼
     * @details 전역 싱크를 비우지 않고 줄을 버리는 싱크로 바꾼다. 싱크가 비면 `Logger::registerGlobalListener` 가 순서 위반으로 멈추는데(시나리오 로그 포착이
     *          억제 구간에서 리스너를 붙인다), 버리는 싱크는 리스너 붙임을 받아 빈 구독을 돌려준다.
     */
    class ScopedLogSuppressor
    {
    public:
        ScopedLogSuppressor()
            : _pOldSink{ sw::Logger::getGlobalSink() }
            , _discardSink{ nullptr }
        {
            sw::Logger::setGlobalSink( &_discardSink );
        }

        ~ScopedLogSuppressor() { sw::Logger::setGlobalSink( _pOldSink ); }

        ScopedLogSuppressor( const ScopedLogSuppressor& )            = delete;
        ScopedLogSuppressor& operator=( const ScopedLogSuppressor& ) = delete;

    private:
        sw::ILogSink*        _pOldSink;
        DefensiveTestLogSink _discardSink; ///< 감싼 싱크가 없어 모든 줄을 버린다
    };
} // namespace test

namespace test
{
    /**
     * @brief 스코프 내에서 발생하는 Error/Warning 로그를 의도된 방어/예외 테스트(Expected)로 마킹하는 RAII 헬퍼
     * @param listNeedle 주면 문구에 그 글 하나가 든 줄만 마킹한다(예: `of unknown type '게임타입'`). 나머지 줄은 예상 밖으로 남는다 — 범위를 통째로 덮지 않으려는 쓰임이다.
     */
    class ScopedDefensiveTestLog
    {
    public:
        explicit ScopedDefensiveTestLog( const utf8* pReason = nullptr, sw::vector<sw::string> listNeedle = {} )
            : _pOldSink{ sw::Logger::getGlobalSink() }
            , _defensiveSink{ _pOldSink, pReason, std::move( listNeedle ) }
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
     * @brief 스코프 동안 글 조각이 든 Warning · Error 줄을 케이스 판정(`CaseLogJudge`)에서 빼는 RAII 헬퍼입니다 — 줄에는 **표식을 붙이지 않는다**.
     * @details `SW_TEST_DEFENSIVE_SCOPE` 의 표식은 시나리오 실행기의 로그 게이트도 그 줄을 건너뛰게 한다. 게이트 자체를 시험하는 테스트는 실행기가 표식 없는 줄을
     *          처리하지 않은 줄로 세는지를 봐야 하므로 표식을 쓸 수 없다. 이 스코프는 판정기만 그 줄을 모르게 한다. 그 밖의 쓰임에는 `SW_TEST_DEFENSIVE_SCOPE` 를 쓴다.
     */
    class ScopedJudgeIgnoredLog
    {
    public:
        ScopedJudgeIgnoredLog( sw::string needle, const utf8* pReason )
            : _needle{ std::move( needle ) }
        {
            TestRegistry::getInstance().getCaseLogJudge().addIgnoredNeedle( sw::string_view{ _needle.c_str(), _needle.size() } );
            sw::ILogSink* pSink = sw::Logger::getGlobalSink();
            if ( pSink != nullptr )
            {
                sw::fixed_string<sw::constant::kMaxBuffer256> notice{};
                sw::formatstring( notice.data(), notice.capacity(), ">>> [Judge Ignored] lines containing '%#': %#", _needle, pReason != nullptr ? pReason : "" );
                pSink->writeLog( sw::LogLevel::Info, "Test", nullptr, notice.c_str(), __FILE__, __LINE__ );
            }
        }

        ~ScopedJudgeIgnoredLog() { TestRegistry::getInstance().getCaseLogJudge().removeIgnoredNeedle( sw::string_view{ _needle.c_str(), _needle.size() } ); }

        ScopedJudgeIgnoredLog( const ScopedJudgeIgnoredLog& )            = delete;
        ScopedJudgeIgnoredLog& operator=( const ScopedJudgeIgnoredLog& ) = delete;

    private:
        sw::string _needle;
    };
} // namespace test

namespace test
{
    /**
     * @brief 스코프 동안 단언(`SW_ASSERT` · `SW_LOG_ASSERT`)이 멈추지 않고 세어지게 하는 RAII 헬퍼입니다.
     * @details Debug 의 단언은 디버거에서 멈추므로(`SW_DEBUG_BREAK`) 단언이 걸리는 입력을 테스트하면 프로세스가 죽는다. 이 스코프 안에서는
     *          단언이 세어지기만 하고 코드는 그 뒤로 이어진다(배포본과 같은 길). `getCount()` 로 "단언이 걸렸나" 를 묻는다.
     *          단언은 Debug 에서만 있으므로 그 밖의 구성에서 `getCount()` 는 늘 0 이다 — 테스트는 `SW_TEST_REQUIRES_FEATURE( DebugAsserts )` 로 가른다.
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
     * @details 테스트마다 `Logger::registerGlobalListener` 리스너를 손으로 만들지 않게 한 자리에 둔다. 리스너는 남기는 스레드에서
     *          곧바로 불리므로 같은 스레드의 경고는 그 호출이 돌아오면 이미 모여 있다. `ScopedDefensiveTestLog` 와 같이 써도 된다 — 그때 메시지 앞에
     *          `[Expected Defensive Test]` 가 붙지만 `countWithID` 는 문장이 아니라 id 로 찾으므로 상관없다.
     */
    class ScopedLogCollector
    {
    public:
        ScopedLogCollector()
        {
            _subscription = sw::Logger::registerGlobalListener( SW_DELEGATE_LAMBDA( sw::LogWrittenDelegate, [this]( const sw::LogEntry& entry )
            {
                if ( entry._level != sw::LogLevel::Error && entry._level != sw::LogLevel::Warning )
                    return;
                std::scoped_lock<sw::mutex> lock{ _mutex };
                _listMessage.push_back( entry._message );
                _listID.push_back( entry._pID != nullptr ? sw::string( entry._pID ) : sw::string{} );
            } ) );
        }

        ScopedLogCollector( const ScopedLogCollector& )            = delete;
        ScopedLogCollector& operator=( const ScopedLogCollector& ) = delete;

        /** @brief 모은 Warning · Error 의 총 수입니다 — "아무 줄도 남지 않았다" 를 확인할 때 씁니다(문구를 맞추지 않는다). */
        uint32 count() const
        {
            std::scoped_lock<sw::mutex> lock{ _mutex };
            return static_cast<uint32>( _listMessage.size() );
        }

        /**
         * @brief 모은 Warning · Error 중 id 가 `id` 인 줄(`SW_LOG_ERROR_ID` · `SW_LOG_WARNING_ID`)의 수입니다.
         * @details 문장이 아니라 사건으로 찾는다 — 로그 문구를 고쳐도 테스트가 지지 않는다. 로그 문장을 맞추는 `countContaining` 은 없앴다(재발 방지):
         *          로그에 id 가 없으면 소스에서 `SW_LOG_*_ID` 로 id 를 붙인다. 빈 id 는 id 없는 줄을 세는 것이라 쓸 수 없다.
         */
        uint32 countWithID( sw::string_view id ) const
        {
            std::scoped_lock<sw::mutex> lock{ _mutex };
            uint32                      count = 0;
            for ( const sw::string& lineID : _listID )
            {
                if ( sw::string_view{ lineID.c_str(), lineID.size() } == id )
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
        sw::vector<sw::string> _listID;       ///< `_listMessage` 와 같은 순서. id 없는 줄은 빈 글
        sw::Subscription       _subscription; ///< 마지막 멤버라 먼저 풀린다 — 콜백이 만지는 목록보다 먼저
    };
} // namespace test

/** @brief 현재 스코프 동안 의도된 실패로 인한 로그 출력을 억제합니다. */
#define SW_TEST_SUPPRESS_LOGS() test::ScopedLogSuppressor SW_CONCAT( logSuppressor_, __LINE__ )

/** @brief 현재 스코프를 의도된 방어/예외 테스트 구간으로 마킹하여 Error/Warning 로그에 [Expected Defensive Test]를 표기합니다. */
#define SW_TEST_DEFENSIVE_SCOPE( ... ) test::ScopedDefensiveTestLog SW_CONCAT( defensiveLog_, __LINE__ )( "" __VA_ARGS__ )

/** @brief 현재 스코프 동안 `needle` 이 든 Warning · Error 줄을 케이스 판정에서만 뺍니다(표식 없음 — 시나리오 로그 게이트를 시험하는 케이스용). */
#define SW_TEST_JUDGE_IGNORE_LINES( needle, reason ) test::ScopedJudgeIgnoredLog SW_CONCAT( judgeIgnoredLog_, __LINE__ )( needle, reason )
