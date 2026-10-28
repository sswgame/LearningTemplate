#include "pch.h"

#include "Engine/Automation/Runner/AutomationHangWatch.h"

#include "Core/Concurrency/ThreadName.h"
#include "Core/Container/StringUtil.h"
#include "Core/Diagnostics/CrashContext.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Automation/Runner/AutomationExitCode.h"
#include "Engine/Automation/Runner/AutomationOutcome.h"
#include "Engine/Serialization/JSON/JSONWriter.h"

#include <algorithm>
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

            /** @brief `reason` 글의 버퍼 크기(바이트)입니다. 고정 문구(200 자 안팎)와 숫자 둘에 마지막 단계(`kLastStepBytes`)를 더해도 넉넉하다. */
            static constexpr uint32 kReasonCapacity = AutomationHangPayload::kLastStepBytes + 512;

            static string_view toView( const utf8* pText ) { return string_view{ pText }; }

            /** @brief 문자열 멤버 하나를 씁니다. */
            static void writeStringField( JSONWriter& writer, string_view key, string_view value )
            {
                writer.key( key );
                writer.valueString( value );
            }

            /** @brief 부호 없는 정수 멤버 하나를 씁니다. */
            static void writeNumberField( JSONWriter& writer, string_view key, uint64 value )
            {
                writer.key( key );
                writer.valueUint( value );
            }
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
        // 게임 스레드가 멈춘 채 힙 잠금을 쥐고 있을 수 있다 — 고정 버퍼 쓰기기와 스택 버퍼만 쓰고 할당하지 않는다.
        JSONWriter        writer{ pOutText, capacity, 2 };
        const string_view lastStep = Internal::toView( payload._lastStep );
        utf8              arrReason[Internal::kReasonCapacity];
        const int32       reasonLength = std::snprintf( arrReason, Internal::kReasonCapacity,
                                                        "hang watchdog: the scenario was not ended by the runner within its wall-clock limit of %u ms plus grace "
                                                              "(elapsed %u ms) - the game thread stopped running frames; last known position: %s",
                                                        payload._wallLimitMilliseconds, elapsedMilliseconds, payload._lastStep );
        // 넘쳐 잘렸으면 snprintf 가 돌려준 길이가 버퍼보다 길다 — 버퍼 안으로 줄인다(글자 중간에서 잘린 바이트는 쓰기기가 U+FFFD 로 바꾼다).
        const size_t reasonSize = reasonLength < 0 ? 0 : std::min( static_cast<size_t>( reasonLength ), static_cast<size_t>( Internal::kReasonCapacity - 1 ) );
        writer.beginObject();
        Internal::writeStringField( writer, "name", Internal::toView( payload._scenario ) );
        Internal::writeStringField( writer, "result", getAutomationOutcomeName( AutomationOutcome::TimedOut ) );
        Internal::writeNumberField( writer, "exitCode", static_cast<uint64>( getAutomationExitCode( AutomationOutcome::TimedOut ) ) );
        Internal::writeNumberField( writer, "frames", payload._frame );
        Internal::writeStringField( writer, "reason", string_view{ arrReason, reasonSize } );
        Internal::writeStringField( writer, "backend", Internal::toView( payload._backend ) );
        Internal::writeStringField( writer, "game", Internal::toView( payload._game ) );
        Internal::writeStringField( writer, "startedAt", Internal::toView( payload._startedAt ) );
        Internal::writeNumberField( writer, "startedAtUnixMs", payload._startedUnixMs > 0 ? static_cast<uint64>( payload._startedUnixMs ) : 0u );
        Internal::writeNumberField( writer, "wallMilliseconds", elapsedMilliseconds );
        Internal::writeNumberField( writer, "wallLimitMilliseconds", payload._wallLimitMilliseconds );
        Internal::writeStringField( writer, "lastStep", lastStep );
        Internal::writeStringField( writer, "reproduce", Internal::toView( payload._reproduce ) );
        writer.key( "hang" );
        writer.valueBool( true );
        for ( const utf8* pListName : { "failures", "unconfirmed", "unhandledLogs", "allowedLogs", "metrics", "records" } )
        {
            writer.key( pListName );
            writer.beginArray();
            writer.endArray();
        }
        writer.endObject();
        // 실행기 보고(`AutomationReportWriter`)처럼 줄바꿈으로 끝낸다. 넘쳐 잘렸으면 덧붙이지 않는다.
        uint32 length = static_cast<uint32>( writer.getWrittenSize() );
        if ( writer.isComplete() && length + 1 < capacity )
        {
            pOutText[length++] = '\n';
            pOutText[length]   = '\0';
        }
        return length;
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
