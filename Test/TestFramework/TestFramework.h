#pragma once
/**
 * @file TestFramework.h
 * @brief 자동 등록 단위 테스트 프레임워크
 */
#include "Engine/EngineMinimal.h"

#include "TestFramework/TestContext.h"
#include "TestFramework/TestEnvironment.h"
#include "TestFramework/TestFilter.h"
#include "TestFramework/TestFixture.h"

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

    // ------------------------------------------------------------------------------
    // 2) 레지스트리 — 등록·필터·실행
    // ------------------------------------------------------------------------------
    class TestRegistry
    {
    public:
        /** @brief 프로세스 전역 레지스트리를 반환합니다. */
        static TestRegistry& getInstance();

        /** @brief 스위트·이름·함수로 테스트를 등록합니다. */
        void registerTest( const sw::string& suiteName, const sw::string& testName, sw::Delegate<void()> func );

        /** @brief 테스트 전용 인자를 파싱하고 나머지 인자를 반환합니다. */
        sw::vector<utf8*> configureFromArgs( int32 argc, utf8* argv[] );

        /** @brief glob 필터를 설정합니다. "Suite.*", 쉼표 include, "-RHI*" exclude. */
        void setFilter( const sw::string& filter );

        /** @brief 필터에 맞는 테스트를 모두 실행하고 실패 개수를 반환합니다. */
        int32 runAllTests();
        /** @brief 등록된 테스트 이름을 나열합니다. */
        void listTests() const;

        /** @brief 현재 테스트의 실패를 기록합니다. */
        void addFailure( const sw::string& condition, const sw::string& file, int32 line, const sw::string& message = "" );
        /** @brief 현재 테스트를 실패 없이 건너뜁니다. */
        void skipCurrentTest( const sw::string& reason, const sw::string& file, int32 line );

        /** @brief 현재 테스트가 실패했는지 반환합니다. */
        bool isCurrentTestHasFailed() const { return _currentContext.hasFailed(); }
        /** @brief 현재 테스트가 스킵되었는지 반환합니다. */
        bool isCurrentTestSkipped() const { return _currentContext.isSkipped(); }
        /** @brief 현재 테스트의 상세 실행 컨텍스트를 반환합니다. */
        TestContext*       getCurrentContext() { return &_currentContext; }
        const TestContext* getCurrentContext() const { return &_currentContext; }

    private:
        sw::vector<TestCaseInfo> _listTest;
        TestFilter               _filter;
        TestContext              _currentContext;
        TestEnvironment          _environment;
        bool                     _listOnly{ false };
    };

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

        const sw::string& getLogFolderPath() override
        {
            static const sw::string s_emptyPath{};
            return _pWrappedSink != nullptr ? _pWrappedSink->getLogFolderPath() : s_emptyPath;
        }

    private:
        sw::ILogSink* _pWrappedSink{ nullptr };
        sw::string    _reason;
    };

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
            {
                _pOldSink->writeLog( sw::LogLevel::Info, "Test", nullptr, "<<< [Defensive Test] Expected Error/Warning validation scope ended <<<", __FILE__, __LINE__ );
            }
        }

        ScopedDefensiveTestLog( const ScopedDefensiveTestLog& )            = delete;
        ScopedDefensiveTestLog& operator=( const ScopedDefensiveTestLog& ) = delete;

    private:
        sw::ILogSink*        _pOldSink{ nullptr };
        DefensiveTestLogSink _defensiveSink;
    };

    /** @brief 정적 초기화로 테스트를 레지스트리에 붙입니다. */
    class TestRegistrar
    {
    public:
        /** @brief 정적 초기화 시점에 테스트를 레지스트리에 등록합니다. */
        TestRegistrar( const sw::string& suiteName, const sw::string& testName, sw::Delegate<void()> func )
        {
            TestRegistry::getInstance().registerTest( suiteName, testName, func );
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

/** @brief 현재 테스트를 사유와 함께 건너뜁니다(스위트 실패로 치지 않음). */
#define SW_TEST_SKIP( reason )                                                               \
    do                                                                                       \
    {                                                                                        \
        test::TestRegistry::getInstance().skipCurrentTest( ( reason ), __FILE__, __LINE__ ); \
        return;                                                                              \
    } while ( 0 )

/** @brief 현재 테스트 종료 후 역순으로 실행할 정리 함수를 등록합니다. */
#define SW_TEST_DEFER_CLEANUP( cleanup ) test::TestRegistry::getInstance().getCurrentContext()->deferCleanup( cleanup )

/** @brief 현재 테스트 컨텍스트에 연결된 엔진 fixture를 생성합니다. */
#define SW_TEST_FIXTURE( name ) \
    test::TestFixture name { *test::TestRegistry::getInstance().getCurrentContext() }

/** @brief 약한 기대 — 실패를 기록하고 계속합니다. */
#define SW_EXPECT_TRUE( cond )                                                         \
    do                                                                                 \
    {                                                                                  \
        if ( !( cond ) )                                                               \
        {                                                                              \
            test::TestRegistry::getInstance().addFailure( #cond, __FILE__, __LINE__ ); \
        }                                                                              \
    } while ( 0 )

/** @brief 약한 기대 — 실패 시 메시지를 함께 기록합니다. */
#define SW_EXPECT_TRUE_MSG( cond, msg )                                                     \
    do                                                                                      \
    {                                                                                       \
        if ( !( cond ) )                                                                    \
        {                                                                                   \
            test::TestRegistry::getInstance().addFailure( #cond, __FILE__, __LINE__, msg ); \
        }                                                                                   \
    } while ( 0 )

/** @brief 조건이 거짓이어야 합니다. */
#define SW_EXPECT_FALSE( cond )                                                                 \
    do                                                                                          \
    {                                                                                           \
        if ( ( cond ) )                                                                         \
        {                                                                                       \
            test::TestRegistry::getInstance().addFailure( "!(" #cond ")", __FILE__, __LINE__ ); \
        }                                                                                       \
    } while ( 0 )

/** @brief 두 값이 같아야 합니다. */
#define SW_EXPECT_EQUAL( expected, actual )                                                                                  \
    do                                                                                                                       \
    {                                                                                                                        \
        const auto& _swExpected = ( expected );                                                                              \
        const auto& _swActual   = ( actual );                                                                                \
        if ( !( _swExpected == _swActual ) )                                                                                 \
        {                                                                                                                    \
            std::ostringstream oss;                                                                                          \
            oss << "Expected [" << _swExpected << "], Actual [" << _swActual << "]";                                         \
            test::TestRegistry::getInstance().addFailure( #actual " == " #expected, __FILE__, __LINE__, oss.str().c_str() ); \
        }                                                                                                                    \
    } while ( 0 )

/** @brief 두 값이 달라야 합니다. */
#define SW_EXPECT_NOT_EQUAL( expected, actual )                                                                              \
    do                                                                                                                       \
    {                                                                                                                        \
        const auto& _swExpected = ( expected );                                                                              \
        const auto& _swActual   = ( actual );                                                                                \
        if ( _swExpected == _swActual )                                                                                      \
        {                                                                                                                    \
            std::ostringstream oss;                                                                                          \
            oss << "Expected not equal to [" << _swExpected << "]";                                                          \
            test::TestRegistry::getInstance().addFailure( #actual " != " #expected, __FILE__, __LINE__, oss.str().c_str() ); \
        }                                                                                                                    \
    } while ( 0 )

/** @brief 허용 오차 안에서 두 값이 가까워야 합니다. */
#define SW_EXPECT_NEAR_EQUAL( expected, actual, tolerance )                                                                                        \
    do                                                                                                                                             \
    {                                                                                                                                              \
        const auto& _swExpected   = ( expected );                                                                                                  \
        const auto& _swActual     = ( actual );                                                                                                    \
        const auto& _swTolerance  = ( tolerance );                                                                                                 \
        const auto  _swDifference = sw::MathUtil::abs( _swExpected - _swActual );                                                                  \
        if ( _swDifference > _swTolerance )                                                                                                        \
        {                                                                                                                                          \
            std::ostringstream oss;                                                                                                                \
            oss << "Diff [" << _swDifference << "] exceeds tolerance [" << _swTolerance << "]";                                                    \
            test::TestRegistry::getInstance().addFailure( "|" #actual " - " #expected "| <= " #tolerance, __FILE__, __LINE__, oss.str().c_str() ); \
        }                                                                                                                                          \
    } while ( 0 )

/** @brief 포인터가 null 이 아니어야 합니다. */
#define SW_EXPECT_NOT_NULL( ptr )                                                                   \
    do                                                                                              \
    {                                                                                               \
        if ( ( ptr ) == nullptr )                                                                   \
        {                                                                                           \
            test::TestRegistry::getInstance().addFailure( #ptr " != nullptr", __FILE__, __LINE__ ); \
        }                                                                                           \
    } while ( 0 )

/** @brief 포인터가 null 이어야 합니다. */
#define SW_EXPECT_NULL( ptr )                                                                       \
    do                                                                                              \
    {                                                                                               \
        if ( ( ptr ) != nullptr )                                                                   \
        {                                                                                           \
            test::TestRegistry::getInstance().addFailure( #ptr " == nullptr", __FILE__, __LINE__ ); \
        }                                                                                           \
    } while ( 0 )

/** @brief null 종료/문자열 유사 값을 sw::string 으로 비교합니다. */
#define SW_EXPECT_STREQ( expected, actual )                                                                                  \
    do                                                                                                                       \
    {                                                                                                                        \
        const sw::string _sw_expect_streq_e( expected );                                                                     \
        const sw::string _sw_expect_streq_a( actual );                                                                       \
        if ( _sw_expect_streq_e != _sw_expect_streq_a )                                                                      \
        {                                                                                                                    \
            std::ostringstream oss;                                                                                          \
            oss << "Expected [" << _sw_expect_streq_e << "], Actual [" << _sw_expect_streq_a << "]";                         \
            test::TestRegistry::getInstance().addFailure( #actual " == " #expected, __FILE__, __LINE__, oss.str().c_str() ); \
        }                                                                                                                    \
    } while ( 0 )

/** @brief 컨테이너/문자열이 비어 있어야 합니다. */
#define SW_EXPECT_EMPTY( value )                                                                   \
    do                                                                                             \
    {                                                                                              \
        if ( !( ( value ).empty() ) )                                                              \
        {                                                                                          \
            test::TestRegistry::getInstance().addFailure( #value ".empty()", __FILE__, __LINE__ ); \
        }                                                                                          \
    } while ( 0 )

/** @brief 강한 어서션 — 실패를 기록하고 현재 테스트를 중단합니다. */
#define SW_ASSERT_TRUE( cond )                                                         \
    do                                                                                 \
    {                                                                                  \
        if ( !( cond ) )                                                               \
        {                                                                              \
            test::TestRegistry::getInstance().addFailure( #cond, __FILE__, __LINE__ ); \
            return;                                                                    \
        }                                                                              \
    } while ( 0 )

/** @brief 두 값이 같아야 하며, 아니면 테스트를 중단합니다. */
#define SW_ASSERT_EQUAL( expected, actual )                                                                                  \
    do                                                                                                                       \
    {                                                                                                                        \
        const auto& _swExpected = ( expected );                                                                              \
        const auto& _swActual   = ( actual );                                                                                \
        if ( !( _swExpected == _swActual ) )                                                                                 \
        {                                                                                                                    \
            std::ostringstream oss;                                                                                          \
            oss << "Expected [" << _swExpected << "], Actual [" << _swActual << "]";                                         \
            test::TestRegistry::getInstance().addFailure( #actual " == " #expected, __FILE__, __LINE__, oss.str().c_str() ); \
            return;                                                                                                          \
        }                                                                                                                    \
    } while ( 0 )

/** @brief 포인터가 null 이 아니어야 하며, 아니면 테스트를 중단합니다. */
#define SW_ASSERT_NOT_NULL( ptr )                                                                   \
    do                                                                                              \
    {                                                                                               \
        if ( ( ptr ) == nullptr )                                                                   \
        {                                                                                           \
            test::TestRegistry::getInstance().addFailure( #ptr " != nullptr", __FILE__, __LINE__ ); \
            return;                                                                                 \
        }                                                                                           \
    } while ( 0 )
