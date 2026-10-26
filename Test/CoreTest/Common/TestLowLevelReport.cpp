#include "pch.h"

#include "Core/Common/Macros.h"

#include "TestFramework/TestFramework.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include <io.h>
#else
    #include <unistd.h> // dup · dup2 · close
#endif

// 로그보다 아래 티어의 보고(`SW_LOW_LEVEL_*`, `Core/Common/Macros.h` 10 절). 테스트가 보고자로 함수를 등록하고 stderr 를 임시 파일로 돌려,
// 한 줄이 파일 · 줄 · 글을 정확히 받는지, Error 가 보고자보다 먼저 stderr 에 쓰이는지, 받는 쪽이 없을 때 stderr 로 가는지, 넘침이 표시되는지 본다.
namespace
{
    /** @brief 보고자가 마지막으로 받은 보고입니다(기본 멤버 초기화가 있어 감싼 클래스 밖에 둔다 — 안에 두면 정적 멤버 초기화가 완성 전 타입을 쓴다). */
    struct LowLevelReceived
    {
        sw::internal::LowLevelReportLevel _level{ sw::internal::LowLevelReportLevel::Warning };
        std::string                       _file;
        int32                             _line{ 0 };
        std::string                       _message;
        uint32                            _count{ 0 };
        bool                              _bStderrHadTextAtCall{ false };
    };

    struct LowLevelReportTestInternal
    {
        inline static LowLevelReceived _s_received{};
        inline static std::FILE*       _s_pCapture{ nullptr };
        inline static bool             _s_bHandled{ true };

        static bool reporter( sw::internal::LowLevelReportLevel level, const utf8* pFile, int32 line, const utf8* pMessage ) noexcept
        {
            _s_received._level                = level;
            _s_received._file                 = pFile;
            _s_received._line                 = line;
            _s_received._message              = pMessage;
            _s_received._bStderrHadTextAtCall = _s_pCapture != nullptr && std::ftell( _s_pCapture ) > 0;
            ++_s_received._count;
            return _s_bHandled;
        }

        static int32 duplicateDescriptor( int32 descriptor )
        {
#if defined( SW_PLATFORM_WINDOWS )
            return _dup( descriptor );
#else
            return dup( descriptor );
#endif
        }

        static void redirectDescriptor( int32 source, int32 target )
        {
#if defined( SW_PLATFORM_WINDOWS )
            _dup2( source, target );
#else
            dup2( source, target );
#endif
        }

        static void closeDescriptor( int32 descriptor )
        {
#if defined( SW_PLATFORM_WINDOWS )
            _close( descriptor );
#else
            close( descriptor );
#endif
        }

        static int32 descriptorOf( std::FILE* pFile )
        {
#if defined( SW_PLATFORM_WINDOWS )
            return _fileno( pFile );
#else
            return fileno( pFile );
#endif
        }

        static bool contains( const std::string& text, const utf8* pPart ) { return text.find( pPart ) != std::string::npos; }

        static bool endsWith( const std::string& text, const utf8* pSuffix )
        {
            const std::string_view suffix{ pSuffix };
            return text.size() >= suffix.size() && text.compare( text.size() - suffix.size(), suffix.size(), suffix ) == 0;
        }
    };

    /**
     * @brief 보고자를 등록하고 stderr 를 임시 파일로 돌립니다. 끝에 둘 다 복원합니다(로그가 등록한 보고자도 복원한다).
     * @details 보고자는 `nullptr` 도 등록할 수 있다 — 받는 쪽이 없는 상태를 만든다.
     */
    class ScopedLowLevelCapture
    {
    public:
        explicit ScopedLowLevelCapture( sw::internal::LowLevelReporterFunc pfnReporter, bool bHandled = true )
            : _savedStderr{ 0 }
            , _pPreviousReporter{ nullptr }
        {
            LowLevelReportTestInternal::_s_received = {};
            LowLevelReportTestInternal::_s_bHandled = bHandled;

            std::fflush( stderr );
            LowLevelReportTestInternal::_s_pCapture = std::tmpfile();
            _savedStderr                            = LowLevelReportTestInternal::duplicateDescriptor( LowLevelReportTestInternal::descriptorOf( stderr ) );
            LowLevelReportTestInternal::redirectDescriptor( LowLevelReportTestInternal::descriptorOf( LowLevelReportTestInternal::_s_pCapture ),
                                                            LowLevelReportTestInternal::descriptorOf( stderr ) );
            _pPreviousReporter = sw::internal::setLowLevelReporter( pfnReporter );
        }

        ~ScopedLowLevelCapture()
        {
            (void)sw::internal::setLowLevelReporter( _pPreviousReporter );
            std::fflush( stderr );
            LowLevelReportTestInternal::redirectDescriptor( _savedStderr, LowLevelReportTestInternal::descriptorOf( stderr ) );
            LowLevelReportTestInternal::closeDescriptor( _savedStderr );
            std::fclose( LowLevelReportTestInternal::_s_pCapture );
            LowLevelReportTestInternal::_s_pCapture = nullptr;
        }

        ScopedLowLevelCapture( const ScopedLowLevelCapture& )            = delete;
        ScopedLowLevelCapture& operator=( const ScopedLowLevelCapture& ) = delete;

        /** @brief 지금까지 stderr 로 간 글을 전부 읽습니다(테스트당 한 번 — 읽으면 쓰기 위치가 끝으로 옮겨진다). */
        std::string readStderr() const
        {
            std::fflush( stderr );
            std::fseek( LowLevelReportTestInternal::_s_pCapture, 0, SEEK_SET );
            std::string text;
            utf8        arrChunk[256];
            size_t      readCount = 0;
            while ( ( readCount = std::fread( arrChunk, 1, sizeof( arrChunk ), LowLevelReportTestInternal::_s_pCapture ) ) > 0 )
            {
                text.append( arrChunk, readCount );
            }
            return text;
        }

    private:
        int32                              _savedStderr;
        sw::internal::LowLevelReporterFunc _pPreviousReporter;
    };
} // namespace

/**
 * @brief [LowLevelReportTest] 경고는 보고자에게 파일 · 줄 · 글이 그대로 닿고, 보고자가 받았으면 stderr 에는 쓰지 않는다
 */
SW_TEST_CASE( LowLevelReportTest, WarningReachesReporterWithSiteAndText )
{
    const ScopedLowLevelCapture capture( &LowLevelReportTestInternal::reporter );
    const int32                 expectedLine = __LINE__ + 1;
    SW_LOW_LEVEL_WARNING( "value %u of %s", 7u, "units" );

    const LowLevelReceived& received = LowLevelReportTestInternal::_s_received;
    SW_EXPECT_EQUAL( 1u, received._count );
    SW_EXPECT_TRUE( received._level == sw::internal::LowLevelReportLevel::Warning );
    SW_EXPECT_EQUAL( expectedLine, received._line );
    SW_EXPECT_TRUE_MSG( LowLevelReportTestInternal::endsWith( received._file, "TestLowLevelReport.cpp" ), received._file.c_str() );
    SW_EXPECT_TRUE_MSG( received._message == "value 7 of units", received._message.c_str() );

    const std::string stderrText = capture.readStderr();
    SW_EXPECT_TRUE_MSG( stderrText.find( "value 7 of units" ) == std::string::npos, stderrText.c_str() );
}

/**
 * @brief [LowLevelReportTest] 오류는 보고자가 받아 가더라도 보고자를 부르기 전에 stderr 에 먼저 쓰인다 — 비동기 로거가 크래시 직전 메시지를 잃기 때문이다
 */
SW_TEST_CASE( LowLevelReportTest, ErrorIsWrittenToStderrBeforeReporter )
{
    const ScopedLowLevelCapture capture( &LowLevelReportTestInternal::reporter );
    const int32                 expectedLine = __LINE__ + 1;
    SW_LOW_LEVEL_ERROR( "failed to allocate %zu bytes", static_cast<size_t>( 4096 ) );

    const LowLevelReceived& received = LowLevelReportTestInternal::_s_received;
    SW_EXPECT_EQUAL( 1u, received._count );
    SW_EXPECT_TRUE( received._level == sw::internal::LowLevelReportLevel::Error );
    SW_EXPECT_EQUAL( expectedLine, received._line );
    SW_EXPECT_TRUE_MSG( received._message == "failed to allocate 4096 bytes", received._message.c_str() );
    SW_EXPECT_TRUE_MSG( received._bStderrHadTextAtCall, "stderr was still empty when the reporter ran" );

    const std::string stderrText = capture.readStderr();
    SW_EXPECT_TRUE_MSG( LowLevelReportTestInternal::contains( stderrText, "[Error] " ), stderrText.c_str() );
    SW_EXPECT_TRUE_MSG( LowLevelReportTestInternal::contains( stderrText, "TestLowLevelReport.cpp:" ), stderrText.c_str() );
    SW_EXPECT_TRUE_MSG( LowLevelReportTestInternal::contains( stderrText, "failed to allocate 4096 bytes" ), stderrText.c_str() );
}

/**
 * @brief [LowLevelReportTest] 치명 보고(`SW_FATAL` · `SW_LOW_LEVEL_FATAL` 이 멈추기 직전에 하는 일)는 오류처럼 stderr 에 먼저 쓰이고 보고자는 Fatal 로 받는다
 * @details 보고자(로그)는 Fatal 을 받으면 비동기 싱크를 비운다. 멈춤(`reportFatal` 의 abort)은 프로세스 안에서 볼 수 없어 그 앞 단계만 부른다.
 */
SW_TEST_CASE( LowLevelReportTest, FatalIsWrittenToStderrBeforeReporter )
{
    const ScopedLowLevelCapture capture( &LowLevelReportTestInternal::reporter );
    sw::internal::reportLowLevel( sw::internal::LowLevelReportLevel::Fatal, __FILE__, static_cast<int32>( __LINE__ ), "fatal probe" );

    const LowLevelReceived& received = LowLevelReportTestInternal::_s_received;
    SW_EXPECT_EQUAL( 1u, received._count );
    SW_EXPECT_TRUE( received._level == sw::internal::LowLevelReportLevel::Fatal );
    SW_EXPECT_TRUE_MSG( received._bStderrHadTextAtCall, "stderr was still empty when the reporter ran" );

    const std::string stderrText = capture.readStderr();
    SW_EXPECT_TRUE_MSG( LowLevelReportTestInternal::contains( stderrText, "[Error] " ), stderrText.c_str() );
    SW_EXPECT_TRUE_MSG( LowLevelReportTestInternal::contains( stderrText, "fatal probe" ), stderrText.c_str() );
}

/**
 * @brief [LowLevelReportTest] 단언 실패는 식 · 메시지 · 위치를 한 오류로 보고하고, stderr 에 먼저 쓴다
 */
SW_TEST_CASE( LowLevelReportTest, AssertReportsExpressionMessageAndSite )
{
    const ScopedLowLevelCapture capture( &LowLevelReportTestInternal::reporter );
    test::ScopedAssertCapture   assertCapture; // Debug 의 단언은 멈추므로 가로챈다.
    const int32                 value        = 2;
    const int32                 expectedLine = __LINE__ + 1;
    SW_LOW_LEVEL_ASSERT( value == 3, "value was %d, %s", value, "not three" );
    if constexpr ( test::ScopedAssertCapture::kAssertsAreActive )
        SW_EXPECT_EQUAL( 1u, assertCapture.getCount() );

    const LowLevelReceived& received = LowLevelReportTestInternal::_s_received;
    SW_EXPECT_EQUAL( 1u, received._count );
    SW_EXPECT_TRUE( received._level == sw::internal::LowLevelReportLevel::Error );
    SW_EXPECT_EQUAL( expectedLine, received._line );
    SW_EXPECT_TRUE_MSG( LowLevelReportTestInternal::endsWith( received._file, "TestLowLevelReport.cpp" ), received._file.c_str() );
    SW_EXPECT_TRUE_MSG( LowLevelReportTestInternal::contains( received._message, "Expression : value == 3" ), received._message.c_str() );
    SW_EXPECT_TRUE_MSG( LowLevelReportTestInternal::contains( received._message, "Message    : value was 2, not three" ), received._message.c_str() );
    SW_EXPECT_TRUE_MSG( received._bStderrHadTextAtCall, "stderr was still empty when the reporter ran" );
}

/**
 * @brief [LowLevelReportTest] 보고자가 없으면(로거 이전 · 로거 없는 도구) 경고도 stderr 에 파일 · 줄과 함께 남는다
 */
SW_TEST_CASE( LowLevelReportTest, WithoutReporterWarningGoesToStderr )
{
    const ScopedLowLevelCapture capture( nullptr );
    const int32                 expectedLine = __LINE__ + 1;
    SW_LOW_LEVEL_WARNING( "no reporter %d", 5 );

    const std::string stderrText = capture.readStderr();
    SW_EXPECT_TRUE_MSG( LowLevelReportTestInternal::contains( stderrText, "[Warning] " ), stderrText.c_str() );
    SW_EXPECT_TRUE_MSG( LowLevelReportTestInternal::contains( stderrText, ( std::string( "TestLowLevelReport.cpp:" ) + std::to_string( expectedLine ) ).c_str() ), stderrText.c_str() );
    SW_EXPECT_TRUE_MSG( LowLevelReportTestInternal::contains( stderrText, "no reporter 5" ), stderrText.c_str() );
}

/**
 * @brief [LowLevelReportTest] 보고자가 받지 못한 경고(싱크 없음)는 stderr 로 가고, 오류는 이미 쓴 것을 한 번 더 쓰지 않는다
 */
SW_TEST_CASE( LowLevelReportTest, UnhandledReportFallsBackToStderrOnce )
{
    const ScopedLowLevelCapture capture( &LowLevelReportTestInternal::reporter, false );
    SW_LOW_LEVEL_WARNING( "unhandled warning" );
    SW_LOW_LEVEL_ERROR( "unhandled error" );

    SW_EXPECT_EQUAL( 2u, LowLevelReportTestInternal::_s_received._count );

    const std::string stderrText = capture.readStderr();
    const auto        countOf    = [&stderrText]( const utf8* pPart )
    {
        uint32 count = 0;
        for ( size_t at = stderrText.find( pPart ); at != std::string::npos; at = stderrText.find( pPart, at + 1 ) )
        {
            ++count;
        }
        return count;
    };
    SW_EXPECT_EQUAL( 1u, countOf( "unhandled warning" ) );
    SW_EXPECT_EQUAL( 1u, countOf( "unhandled error" ) );
}

/**
 * @brief [LowLevelReportTest] 버퍼를 넘친 글은 끝이 잘리고 `...[truncated]` 로 잘렸음을 드러내며, 딱 들어맞는 글은 그대로 둔다
 */
SW_TEST_CASE( LowLevelReportTest, OverflowIsMarkedTruncated )
{
    const ScopedLowLevelCapture capture( &LowLevelReportTestInternal::reporter );
    const size_t                fittingLength = sw::internal::kLowLevelMessageCapacity - 1;

    const std::string fitting( fittingLength, 'x' );
    SW_LOW_LEVEL_WARNING( "%s", fitting.c_str() );
    SW_EXPECT_TRUE( LowLevelReportTestInternal::_s_received._message == fitting );

    const std::string overflowing( fittingLength + 1, 'x' );
    SW_LOW_LEVEL_WARNING( "%s", overflowing.c_str() );
    const std::string& message = LowLevelReportTestInternal::_s_received._message;
    SW_EXPECT_EQUAL( fittingLength, message.size() );
    SW_EXPECT_TRUE_MSG( LowLevelReportTestInternal::endsWith( message, "...[truncated]" ), message.c_str() );
    SW_EXPECT_TRUE( message.compare( 0, 8, "xxxxxxxx" ) == 0 );
}

/**
 * @brief [LowLevelReportTest] 필수 서비스 부재 보고는 조회 글을 담은 오류 하나를 stderr 에 먼저 쓰고 멈춘다(Debug 가로채기에서는 세고 반환한다)
 * @details 모든 구성에서 `abort` 하므로(`SW_ASSERT` 는 Release 에서 사라져 널 역참조가 됐다) 멈춤을 프로세스 안에서 볼 수 있는 구성은 가로채기가 있는 Debug 뿐이다.
 *          그래서 부르는 것은 그 구성에서만이다. 세 서비스 창구(`SubsystemScope::get` · `engine::getSubsystem` · `game::getSubsystem`)와 에디터 창구가 모두 이 함수 하나를 부른다.
 */
SW_TEST_CASE( LowLevelReportTest, MissingRequiredServiceReportsOneErrorBeforeStopping )
{
    const ScopedLowLevelCapture capture( &LowLevelReportTestInternal::reporter );
    test::ScopedAssertCapture   assertCapture;
    if constexpr ( test::ScopedAssertCapture::kAssertsAreActive )
    {
        SW_TEST_DEFENSIVE_SCOPE( "a required service is missing" );
        sw::internal::reportMissingRequiredService( "MissingProbeService" );
        SW_EXPECT_EQUAL( 1u, assertCapture.getCount() );

        const LowLevelReceived& received = LowLevelReportTestInternal::_s_received;
        SW_EXPECT_EQUAL( 1u, received._count );
        SW_EXPECT_TRUE( received._level == sw::internal::LowLevelReportLevel::Error );
        SW_EXPECT_TRUE_MSG( LowLevelReportTestInternal::contains( received._message, "MissingProbeService" ), received._message.c_str() );
        SW_EXPECT_TRUE_MSG( received._bStderrHadTextAtCall, "stderr was still empty when the reporter ran" );
    }
}
