#include "pch.h"

#include "Engine/Automation/AutomationHangWatch.h"

#include "Core/Concurrency/ThreadName.h"
#include "Core/Container/StringUtil.h"
#include "Core/Diagnostics/CrashContext.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Automation/AutomationExitCode.h"
#include "Engine/Automation/AutomationOutcome.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sw
{
    namespace
    {
        struct AutomationHangWatchInternal
        {
            /** @brief 감시 스레드가 시한을 다시 보는 간격(나노초)입니다. 합류가 이만큼 늦을 수 있다. */
            static constexpr int64 kPollNanoseconds = 10 * constant::kNanosecondsPerMillisecond;
            /** @brief 행 보고 글 버퍼의 크기(바이트)입니다. 재현 명령(1 KB)과 이름들을 이스케이프해도 넉넉하다. */
            static constexpr uint32 kReportCapacity = 8192;

            /** @brief 고정 버퍼에 글을 덧붙입니다. 넘치면 버리고, 늘 0 으로 끝낼 자리를 남긴다. 할당하지 않는다. */
            struct TextWriter
            {
                utf8*  _pBuffer{ nullptr };
                uint32 _capacity{ 0 };
                uint32 _length{ 0 };

                void appendText( string_view text )
                {
                    for ( const utf8 character : text )
                    {
                        if ( _length + 1 >= _capacity )
                            return;
                        _pBuffer[_length++] = character;
                    }
                }

                /** @brief 실행기의 보고와 같은 이스케이프입니다(`"` · `\` 앞에 `\`, 줄바꿈은 `\n`, 나머지 제어 문자는 버린다). */
                void appendEscaped( string_view text )
                {
                    for ( const utf8 character : text )
                    {
                        if ( character == '"' || character == '\\' )
                        {
                            appendText( "\\" );
                            appendText( string_view{ &character, 1 } );
                        }
                        else if ( character == '\n' )
                        {
                            appendText( "\\n" );
                        }
                        else if ( static_cast<uint8>( character ) >= 0x20 )
                        {
                            appendText( string_view{ &character, 1 } );
                        }
                    }
                }

                void appendNumber( uint64 value )
                {
                    utf8   arrDigit[24]{};
                    uint32 count = 0;
                    do
                    {
                        arrDigit[count++] = static_cast<utf8>( '0' + value % 10 );
                        value /= 10;
                    } while ( value != 0 );
                    while ( count > 0 )
                    {
                        appendText( string_view{ &arrDigit[--count], 1 } );
                    }
                }

                /** @brief `  "key": "escaped value",\n` 한 줄. */
                void appendStringField( string_view key, string_view value )
                {
                    appendText( "  \"" );
                    appendText( key );
                    appendText( "\": \"" );
                    appendEscaped( value );
                    appendText( "\",\n" );
                }

                void appendNumberField( string_view key, uint64 value )
                {
                    appendText( "  \"" );
                    appendText( key );
                    appendText( "\": " );
                    appendNumber( value );
                    appendText( ",\n" );
                }
            };

            static string_view toView( const utf8* pText ) { return string_view{ pText }; }
        };
    } // namespace

    void AutomationHangPayload::assign( utf8* pDestination, uint32 capacity, string_view text )
    {
        if ( pDestination == nullptr || capacity == 0 )
            return;
        // UTF-8 글자 중간에서 자르지 않는다 — 이어지는 바이트 위에서는 앞 글자 시작까지 물러난다.
        const size_t count = StringUtil::floorToUtf8Boundary( text, text.size() < capacity ? text.size() : static_cast<size_t>( capacity - 1 ) );
        std::memcpy( pDestination, text.data(), count );
        std::memset( pDestination + count, 0, capacity - count );
    }
} // namespace sw

namespace sw
{
    AutomationHangSnapshot::AutomationHangSnapshot()
        : _version{ 0 }
        , _arrWord{}
    {
    }

    void AutomationHangSnapshot::publish( const AutomationHangPayload& payload )
    {
        uint64 arrWord[kWordCount];
        std::memcpy( arrWord, &payload, sizeof( AutomationHangPayload ) );
        const uint32 version = _version.load( std::memory_order_relaxed );
        _version.store( version + 1, std::memory_order_relaxed ); // 홀수 = 쓰는 중
        std::atomic_thread_fence( std::memory_order_release );
        for ( uint32 wordIndex = 0; wordIndex < kWordCount; ++wordIndex )
        {
            _arrWord[wordIndex].store( arrWord[wordIndex], std::memory_order_relaxed );
        }
        _version.store( version + 2, std::memory_order_release );
    }

    bool AutomationHangSnapshot::tryRead( AutomationHangPayload& outPayload ) const
    {
        for ( uint32 attempt = 0; attempt < kMaxAttempts; ++attempt )
        {
            const uint32 before = _version.load( std::memory_order_acquire );
            const bool   bBusy  = ( before & 1u ) != 0;
            if ( before == 0 )
                return false; // 한 번도 쓰지 않았다
            if ( bBusy )
                continue;
            uint64 arrWord[kWordCount];
            for ( uint32 wordIndex = 0; wordIndex < kWordCount; ++wordIndex )
            {
                arrWord[wordIndex] = _arrWord[wordIndex].load( std::memory_order_relaxed );
            }
            std::atomic_thread_fence( std::memory_order_acquire );
            if ( _version.load( std::memory_order_relaxed ) != before )
                continue;
            std::memcpy( &outPayload, arrWord, sizeof( AutomationHangPayload ) );
            return true;
        }
        return false;
    }
} // namespace sw

namespace sw
{
    bool AutomationHangReport::isDeadlineExceeded( float64 elapsedSeconds, float32 limitSeconds, float32 graceSeconds )
    {
        return elapsedSeconds > static_cast<float64>( limitSeconds ) + static_cast<float64>( graceSeconds );
    }

    uint32 AutomationHangReport::format( const AutomationHangPayload& payload, uint32 elapsedMilliseconds, utf8* pOutText, uint32 capacity )
    {
        using Internal = AutomationHangWatchInternal;
        if ( pOutText == nullptr || capacity == 0 )
            return 0;
        Internal::TextWriter writer;
        writer._pBuffer            = pOutText;
        writer._capacity           = capacity;
        const string_view lastStep = Internal::toView( payload._lastStep );
        writer.appendText( "{\n" );
        writer.appendStringField( "name", Internal::toView( payload._scenario ) );
        writer.appendStringField( "result", getAutomationOutcomeName( AutomationOutcome::TimedOut ) );
        writer.appendNumberField( "exitCode", static_cast<uint64>( getAutomationExitCode( AutomationOutcome::TimedOut ) ) );
        writer.appendNumberField( "frames", payload._frame );
        writer.appendText( "  \"reason\": \"hang watchdog: the scenario was not ended by the runner within its wall-clock limit of " );
        writer.appendNumber( payload._wallLimitMilliseconds );
        writer.appendText( " ms plus grace (elapsed " );
        writer.appendNumber( elapsedMilliseconds );
        writer.appendText( " ms) - the game thread stopped running frames; last known position: " );
        writer.appendEscaped( lastStep );
        writer.appendText( "\",\n" );
        writer.appendStringField( "backend", Internal::toView( payload._backend ) );
        writer.appendStringField( "game", Internal::toView( payload._game ) );
        writer.appendStringField( "startedAt", Internal::toView( payload._startedAt ) );
        writer.appendNumberField( "startedAtUnixMs", payload._startedUnixMs > 0 ? static_cast<uint64>( payload._startedUnixMs ) : 0u );
        writer.appendNumberField( "wallMilliseconds", elapsedMilliseconds );
        writer.appendNumberField( "wallLimitMilliseconds", payload._wallLimitMilliseconds );
        writer.appendStringField( "lastStep", lastStep );
        writer.appendStringField( "reproduce", Internal::toView( payload._reproduce ) );
        writer.appendText( "  \"hang\": true,\n" );
        writer.appendText( "  \"failures\": [],\n  \"unconfirmed\": [],\n  \"unhandledLogs\": [],\n  \"allowedLogs\": [],\n  \"metrics\": [],\n  \"records\": []\n}\n" );
        writer._pBuffer[writer._length] = '\0';
        return writer._length;
    }
} // namespace sw

namespace sw
{
    AutomationHangWatchdog::AutomationHangWatchdog()
        : _snapshot{}
        , _settings{}
        , _thread{}
        , _state{ static_cast<uint32>( State::Idle ) }
    {
    }

    AutomationHangWatchdog::~AutomationHangWatchdog()
    {
        (void)stop();
    }

    void AutomationHangWatchdog::start( const Settings& settings )
    {
        if ( _thread.joinable() )
            return;
        _settings = settings;
        _state.store( static_cast<uint32>( State::Running ) );
        _thread = thread( [this]()
        { run(); } );
    }

    bool AutomationHangWatchdog::stop()
    {
        if ( _thread.joinable() == false )
            return false;
        uint32 expected = static_cast<uint32>( State::Running );
        (void)_state.compare_exchange_strong( expected, static_cast<uint32>( State::Stopped ) ); // 이미 Fired 면 그대로 — 아래 합류가 행 처리가 끝나기를 기다린다
        _thread.join();
        return _state.load() == static_cast<uint32>( State::Fired );
    }

    void AutomationHangWatchdog::run()
    {
        using Internal = AutomationHangWatchInternal;
        ThreadName::setCurrentThreadName( "AutomationHangWatch" );
        while ( _state.load() == static_cast<uint32>( State::Running ) )
        {
            const float64 elapsedSeconds = static_cast<float64>( MonotonicClock::nowMicroseconds() ) * 1.0e-6 - _settings._startSeconds;
            if ( AutomationHangReport::isDeadlineExceeded( elapsedSeconds, _settings._limitSeconds, _settings._graceSeconds ) )
            {
                uint32 expected = static_cast<uint32>( State::Running );
                if ( _state.compare_exchange_strong( expected, static_cast<uint32>( State::Fired ) ) )
                    handleHang( static_cast<uint32>( elapsedSeconds * 1000.0 ) );
                return;
            }
            MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + Internal::kPollNanoseconds );
        }
    }

    void AutomationHangWatchdog::handleHang( uint32 elapsedMilliseconds ) const
    {
        using Internal = AutomationHangWatchInternal;
        AutomationHangPayload payload;
        if ( _snapshot.tryRead( payload ) == false )
            payload = AutomationHangPayload{}; // 한 번도 쓰지 못했거나 갱신 도중에 멈췄다 — 빈 값으로 쓴다
        utf8         arrReport[Internal::kReportCapacity]{};
        const uint32 length = AutomationHangReport::format( payload, elapsedMilliseconds, arrReport, Internal::kReportCapacity );
        if ( payload._reportPath[0] != '\0' )
            writeFileWithoutAllocation( payload._reportPath, arrReport, length );
        // 크래시와 같은 할당 없는 기록 — 컨텍스트 파일(`Scenario` 키)과 빵부스러기 파일.
        CrashBreadcrumbStore::get().add( "[Scenario] hang watchdog fired" );
        writeCrashContextFile( "ScenarioHang", nullptr, 0, 0 );
        // 로거를 거치지 않는다(비동기 로거는 크래시 직전 줄을 잃는다) — stderr 에 직접 적고 비운다.
        std::fputs( "\n[Scenario] ERROR hang watchdog: the runner did not end the scenario in time - report written, exiting. Last position: ", stderr );
        std::fputs( payload._lastStep, stderr );
        std::fputs( "\n", stderr );
        std::fflush( stderr );
        const int32 exitCode = _settings._exitCode;
        if ( _settings._pTerminate != nullptr )
            _settings._pTerminate( exitCode );
        else
            std::_Exit( exitCode );
    }
} // namespace sw
