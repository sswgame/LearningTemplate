/**
 * @file AutomationHangWatch.h
 * @brief 프레임이 멈춘 행을 잡는 감시 — 게임 스레드가 미리 채워 두는 고정 크기 스냅샷, 시한 판정과 보고 글 만들기(순수 함수), 감시 스레드입니다.
 * @details 실행기의 벽시계 감시(`AutomationRunner::finishIfWallClockExceeded`)는 프레임이 도는 동안만 본다. 게임 스레드가 교착으로 서면 `endRun` 이 불리지 않아 보고가 없고
 *          테스트의 프로세스 시한이 죽일 때까지 간다. 감시 스레드는 시한(상한 + 유예)이 지나도 실행기가 끝맺지 않으면 **게임 스레드의 도움 없이**(할당 · 잠금 없이)
 *          스냅샷으로 보고 JSON 을 쓰고 종료 코드 12 로 프로세스를 끝낸다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include <type_traits>

namespace sw
{
    /**
     * @struct AutomationHangPayload
     * @brief 행 보고를 쓰는 데 필요한 값을 담은 고정 크기 평범한 자료입니다(복사 가능, 할당 없음). 글은 끝에 0 이 붙는 고정 배열입니다.
     */
    struct SW_API AutomationHangPayload
    {
        static constexpr uint32 kScenarioBytes   = 96;
        static constexpr uint32 kBackendBytes    = 24;
        static constexpr uint32 kGameBytes       = 48;
        static constexpr uint32 kStartedAtBytes  = 24;
        static constexpr uint32 kLastStepBytes   = 160;
        static constexpr uint32 kReproduceBytes  = 1024;
        static constexpr uint32 kReportPathBytes = 520;

        int64  _startedUnixMs{ 0 };
        uint32 _frame{ 0 };
        uint32 _wallLimitMilliseconds{ 0 };
        utf8   _scenario[kScenarioBytes]{};
        utf8   _backend[kBackendBytes]{};
        utf8   _game[kGameBytes]{};
        utf8   _startedAt[kStartedAtBytes]{};
        utf8   _lastStep[kLastStepBytes]{};
        utf8   _reproduce[kReproduceBytes]{};
        utf8   _reportPath[kReportPathBytes]{};

        /** @brief @p text 를 @p capacity 바이트 배열 @p pDestination 에 복사합니다(넘으면 자르고, 늘 0 으로 끝낸다). */
        static void assign( utf8* pDestination, uint32 capacity, string_view text );
    };

    static_assert( sizeof( AutomationHangPayload ) % sizeof( uint64 ) == 0, "the payload is copied in 64-bit words" );
    static_assert( std::is_trivially_copyable_v<AutomationHangPayload>, "the payload is copied as raw words" );
} // namespace sw

namespace sw
{
    /**
     * @class AutomationHangSnapshot
     * @brief 게임 스레드(쓰는 쪽 하나)가 갱신하고 감시 스레드가 읽는 스냅샷입니다 — 버전 번호 seqlock 이라 서로를 기다리지 않습니다.
     * @details 값은 64 비트 낱말의 원자 칸에 relaxed 로 담는다(경쟁 검출기가 보는 평범한 메모리 경쟁이 없다). 읽는 쪽은 앞뒤 버전이 같고 짝수일 때만 받는다.
     *          게임 스레드가 갱신 도중 멈췄어도(버전이 홀수) 읽기는 몇 번 재시도하고 포기할 뿐 막히지 않는다.
     */
    class SW_API AutomationHangSnapshot
    {
    public:
        AutomationHangSnapshot();

        /** @brief 새 값으로 바꿉니다. 쓰는 스레드는 하나여야 합니다. */
        void publish( const AutomationHangPayload& payload );
        /** @brief 마지막으로 완성된 값을 읽습니다. 한 번도 쓰지 않았거나 재시도 끝까지 갱신과 겹쳤으면 false 입니다. */
        [[nodiscard]] bool tryRead( AutomationHangPayload& outPayload ) const;

    private:
        static constexpr uint32 kWordCount   = sizeof( AutomationHangPayload ) / sizeof( uint64 );
        static constexpr uint32 kMaxAttempts = 64;

        atomic<uint32> _version;
        atomic<uint64> _arrWord[kWordCount];
    };
} // namespace sw

namespace sw
{
    /** @struct AutomationHangReport @brief 행 보고 글을 만드는 순수 함수 묶음입니다(스레드 · 파일 없이 글자만 다룬다). */
    struct SW_API AutomationHangReport
    {
        /** @brief 시작부터 @p elapsedSeconds 가 지났을 때 행으로 볼지입니다 — 상한 @p limitSeconds 와 유예 @p graceSeconds 를 모두 넘겨야 합니다. */
        [[nodiscard]] static bool isDeadlineExceeded( float64 elapsedSeconds, float32 limitSeconds, float32 graceSeconds );
        /**
         * @brief 행 보고 JSON 을 @p pOutText(@p capacity 바이트)에 씁니다. 할당하지 않습니다. 쓴 길이를 돌려주고 늘 0 으로 끝납니다.
         * @details 필드는 실행기의 보고(`AutomationReportWriter`)와 같은 이름이고 `hang` 만 더 있다. `records` 는 끝에 둔다.
         * @param elapsedMilliseconds 시작부터 지금까지 벽시계로 지난 시간
         */
        [[nodiscard]] static uint32 format( const AutomationHangPayload& payload, uint32 elapsedMilliseconds, utf8* pOutText, uint32 capacity );
    };
} // namespace sw

namespace sw
{
    /**
     * @class AutomationHangWatchdog
     * @brief 시나리오가 시작되면 하나 띄우고 끝나면 내리는 감시 스레드입니다. 시한이 지나도 `stop` 이 불리지 않으면 행으로 보고 보고를 쓰고 프로세스를 끝냅니다.
     * @details 행 처리는 할당 · 잠금 없이 한다 — 스냅샷을 읽어 보고를 OS 원시 호출로 쓰고, 크래시 컨텍스트 · 빵부스러기 파일(`writeCrashContextFile`)을 남기고, stderr 에 한 줄 적은 뒤
     *          종료 함수(기본은 `std::_Exit`)를 부른다. 로거를 거치지 않는다 — 비동기 로거는 크래시 직전 줄을 잃고 락을 쥔 게임 스레드와 얽힐 수 있다.
     */
    class SW_API AutomationHangWatchdog
    {
    public:
        /** @brief 프로세스를 끝내는 함수 모양입니다. 기본은 `std::_Exit`(테스트가 기록만 하는 함수로 바꾼다). */
        using TerminateFunction = void ( * )( int32 exitCode );

        /** @struct Settings @brief 감시 한 번의 조건입니다. */
        struct Settings
        {
            float64           _startSeconds{ 0.0 };   ///< 단조 시계 초 — 실행기가 시작한 때
            float32           _limitSeconds{ 0.0f };  ///< 시나리오의 벽시계 상한
            float32           _graceSeconds{ 0.0f };  ///< 상한 뒤 더 기다리는 유예
            int32             _exitCode{ 0 };         ///< 행으로 끝낼 때의 종료 코드
            TerminateFunction _pTerminate{ nullptr }; ///< nullptr 이면 `std::_Exit`
        };

        AutomationHangWatchdog();
        ~AutomationHangWatchdog();
        AutomationHangWatchdog( const AutomationHangWatchdog& )            = delete;
        AutomationHangWatchdog& operator=( const AutomationHangWatchdog& ) = delete;

        /** @brief 스냅샷을 갱신합니다(게임 스레드). 스레드를 띄우기 전에도 부를 수 있다. */
        void publish( const AutomationHangPayload& payload ) { _snapshot.publish( payload ); }
        /** @brief 감시 스레드를 띄웁니다. 이미 돌고 있으면 아무것도 하지 않습니다. */
        void start( const Settings& settings );
        /**
         * @brief 감시를 내립니다(스레드 합류). 행 처리가 이미 시작됐으면 그것이 프로세스를 끝낼 때까지 기다립니다. 돌고 있지 않으면 아무것도 하지 않습니다.
         * @return 감시가 행으로 처리했으면 true(테스트의 가짜 종료 함수가 돌아온 경우에만 보인다)
         */
        bool stop();
        bool isRunning() const { return _thread.joinable(); }

    private:
        enum class State : uint32
        {
            Idle,
            Running,
            Stopped,
            Fired,
        };

        void run();
        /** @brief 행으로 처리합니다 — 스냅샷으로 보고를 쓰고 컨텍스트 파일을 남기고 stderr 에 적은 뒤 종료 함수를 부릅니다. */
        void handleHang( uint32 elapsedMilliseconds ) const;

        AutomationHangSnapshot _snapshot;
        Settings               _settings;
        thread                 _thread;
        atomic<uint32>         _state;
    };
} // namespace sw
