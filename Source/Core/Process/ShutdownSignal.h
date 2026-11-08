/**
 * @file ShutdownSignal.h
 * @brief 프로세스 밖에서 온 정상 종료 요청(신호 · 콘솔 이벤트 · 서비스 정지)을 깃발 하나에 모읍니다. 전용 서버의 메인 루프가 틱마다 묻습니다.
 * @details 상용 서버의 대응: 언리얼 리눅스 서버의 SIGTERM · SIGINT 처리기(→ 엔진 종료 요청), Windows `SetConsoleCtrlHandler`, 서비스 제어 처리기.
 *          - POSIX: SIGINT · SIGTERM · SIGHUP 은 종료 요청입니다. 처리기는 원자 비교-교환 하나만 합니다(async-signal-safe). 두 번째 SIGINT 는
 *            `_exit(130)` 입니다(멈춘 종료를 사람이 끊는 경로). SIGPIPE 는 무시합니다 — 끊긴 TCP 에 쓸 때 프로세스가 죽는 기본 동작을 끄고, 소켓 코드는 오류 코드로 받는다.
 *            처리기는 `SA_RESTART` 라 잠자기 · 읽기를 깨우지 않습니다 — 메인 루프는 다음 틱(최대 한 틱 주기)에 봅니다.
 *          - Windows: Ctrl+C · Ctrl+Break 는 요청만 하고 돌아갑니다. 창 닫기 · 로그오프 · 시스템 종료는 처리기가 돌아가는 순간 OS 가 프로세스를
 *            끝내므로, 요청한 뒤 `notifyShutdownComplete` 를 상한 시간까지 기다립니다. 서비스로 돌 때(`setServiceMode`)는 로그오프를 무시합니다.
 *          - 요청은 처음 것만 남습니다. 메인 루프는 `getRequestedCause` 로 묻고, 정리를 끝내면 `notifyShutdownComplete` 를 부릅니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 종료를 요청한 까닭입니다. 처음 요청만 남습니다. */
    enum class ShutdownCause : uint8
    {
        None,           ///< 요청 없음
        Interrupt,      ///< SIGINT · Ctrl+C · Ctrl+Break
        Terminate,      ///< SIGTERM(systemd · docker stop · kill)
        Hangup,         ///< SIGHUP(터미널이 닫힘)
        ConsoleClose,   ///< Windows 콘솔 창 닫기
        SystemShutdown, ///< Windows 로그오프 · 시스템 종료
        ServiceStop,    ///< Windows 서비스 정지 · 사전 종료
        ConsoleCommand, ///< 서버 콘솔의 quit · stop
        TickLimit,      ///< 테스트용 틱 상한(`-gv_serverExitAfterTicks`)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ShutdownSignalAction
     * @brief OS 종료 신호 하나에 처리기가 할 일입니다. 처리기(OS 스레드 · 신호 처리기)는 이 결정대로만 움직인다.
     * @details 결정(`ShutdownSignal::decideConsoleControl` · `decideSignal`)은 전역 상태를 읽지 않는 순수 함수라 처리기를 띄우지 않고 검사한다.
     *          예전에는 처리기 본문을 테스트가 직접 부르는 함수와 전역 상태를 되돌리는 함수를 이 클래스에 두었다 — 테스트 때문에 배포 코드에 생긴 창구였다.
     */
    struct ShutdownSignalAction
    {
        ShutdownCause _cause{ ShutdownCause::None }; ///< 세울 까닭. None 이면 세우지 않는다
        bool          _bPassToDefault{ false };      ///< NOLINT(sw-bool-flag): 집합체다. 다음 처리기(기본 = 즉시 종료)로 넘긴다 — 두 번째 인터럽트 · 서비스의 로그오프
        bool          _bWaitForCompletion{ false };  ///< NOLINT(sw-bool-flag): 집합체다. 정리 완료 알림(`notifyShutdownComplete`)을 기다린 뒤 돌아간다 — 돌아가는 순간 OS 가 프로세스를 끝낸다
    };

    /**
     * @class ShutdownSignal
     * @brief 프로세스에 하나인 종료 요청 깃발과 그것을 세우는 OS 처리기입니다. 모두 정적입니다.
     */
    class SW_API ShutdownSignal
    {
    public:
        /** @brief OS 처리기를 설치합니다(두 번 불러도 한 번). 실패하면 false — 처리기가 없어도 콘솔 명령 · 틱 상한 종료는 됩니다. */
        [[nodiscard]] static bool install();
        /** @brief 설치한 처리기를 해제하고 이전 처리기를 되돌립니다. */
        static void uninstall();
        /** @brief 종료를 요청합니다. 아무 스레드 · 신호 처리기에서 불러도 됩니다. 이미 요청이 있으면 까닭을 바꾸지 않습니다. */
        static void request( ShutdownCause cause );
        /** @brief 요청된 까닭입니다. 없으면 `None` 입니다. */
        static ShutdownCause getRequestedCause();
        /** @brief 요청이 있으면 true 입니다. */
        static bool isRequested() { return getRequestedCause() != ShutdownCause::None; }
        /** @brief 정리를 다 마쳤다고 알립니다 — Windows 창 닫기 · 시스템 종료 처리기의 기다림을 풉니다(POSIX 는 아무것도 하지 않는다). */
        static void notifyShutdownComplete();
        /** @brief 서비스로 돈다고 알립니다(Windows) — 로그오프 이벤트를 무시합니다. POSIX 는 아무것도 하지 않습니다. */
        static void setServiceMode( bool bServiceMode );
        /** @brief 까닭의 로그 이름입니다(플랫폼과 무관한 이름 — `"Terminate"`). */
        static const utf8* getCauseName( ShutdownCause cause );

#if defined( SW_PLATFORM_WINDOWS )
        /**
         * @brief 콘솔 제어 사건(`CTRL_*_EVENT`) 하나에 할 일입니다. @p earlierInterruptCount 는 이 사건 앞에 받은 Ctrl+C · Ctrl+Break 수입니다.
         * @details 두 번째 인터럽트는 기본 처리기(즉시 종료)로 넘긴다 — 멈춘 종료를 사람이 끊는 길(POSIX `_exit(130)` 자리). 서비스는 로그오프에 돌기를 멈추지 않는다.
         */
        static ShutdownSignalAction decideConsoleControl( uint32 controlType, uint32 earlierInterruptCount, bool bServiceMode );
#elif defined( SW_PLATFORM_POSIX )
        /** @brief 신호(`SIGINT` · `SIGTERM` · `SIGHUP`) 하나에 할 일입니다. 두 번째 `SIGINT` 는 기다리지 않고 끝낸다(`_bPassToDefault` — 처리기가 `_exit(130)`). */
        static ShutdownSignalAction decideSignal( int32 signalNumber, uint32 earlierInterruptCount );
#else
    #error "Unsupported platform"
#endif

    private:
        static atomic<uint8> _s_requestedCause; ///< `ShutdownCause` — 신호 처리기가 쓰므로 잠금 없는 원자(상수 초기화라 첫 사용에 가드가 없다)
    };
} // namespace sw
