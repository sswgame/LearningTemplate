/**
 * @file CrashHandler.h
 * @brief 처리되지 않은 예외와 치명적인 시그널을 잡아 심볼 변환한 콜 스택 · 미니덤프 · 컨텍스트를 남깁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    /**
     * @brief 개발 중 크래시 리포트 경로를 일부러 태워 보는 방법입니다(`CrashHandler::crashForTest`).
     * @details 언리얼의 `debug crash` · `debug stackoverflow` · `debug gpf` 콘솔 명령과 같은 역할입니다. 리포트는 크래시가 나야만
     *          만들어지므로 죽는 방식마다 한 번씩 태워 보지 않으면 배포 뒤에야 "그 방식으로 죽으면 아무것도 안 남는다" 를 압니다.
     */
    enum class CrashTestKind : uint8
    {
        None = 0,
        AccessViolation,     ///< 널 포인터 쓰기
        StackOverflow,       ///< 이 스레드에서 끝없는 재귀
        WorkerStackOverflow, ///< 새 스레드에서 끝없는 재귀(스레드별 준비 `initializeCurrentThread` 를 태운다)
        Abort,               ///< std::abort() — 엔진의 "로그 + abort" 치명 경로
        PureVirtualCall,     ///< 생성 중인 객체의 순수 가상 함수 호출
        StderrHeld,          ///< 다른 스레드가 stderr 를 쥔 채 놓지 않는 동안 접근 위반 — 보고가 끝날 수 없다(시한이 끝내야 한다)
        AssertFailure,       ///< `SW_ASSERT` 실패(Debug 에서만 멈춘다 — 그 밖의 빌드는 아무것도 하지 않고 돌아온다)
        Count
    };

    /**
     * @class CrashHandler
     * @brief 프로세스 전역 크래시 핸들러입니다.
     * @details 설치한 뒤 접근 위반이나 시그널이 발생하면 예외 코드 · 주소와 폴트 스레드의 콜 스택을 남기고, 원래 동작(프로세스
     *          종료)으로 넘깁니다. 로그가 없으면 원인을 전혀 알 수 없으므로 부팅 초기에 설치합니다.
     *
     *          배포본에서는 텍스트 콜 스택만으로는 부족합니다. 최적화된 빌드는 인라인과 꼬리 호출로 프레임이 합쳐지고 지역
     *          변수도 없습니다. 그래서 **미니덤프**를 함께 씁니다. 언리얼이 CrashReportClient 로 덤프 · 로그 · 컨텍스트를 묶어
     *          올리는 것과 같은 역할입니다(여기서는 별도 프로세스 없이 같은 프로세스에서 씁니다. 재진입 가드와 할당 없는 경로로
     *          버팁니다).
     */
    class SW_API CrashHandler
    {
    public:
        /** @brief 크래시 리포트에 함께 적을 키-값의 최대 개수입니다. */
        static constexpr uint32 kMaxContextEntry = 24;

        /**
         * @brief 핸들러를 설치합니다. 두 번 불러도 한 번만 설치됩니다. 부른 스레드는 `initializeCurrentThread` 도 거칩니다.
         * @details Windows 는 처리되지 않은 SEH 예외 필터와 함께 SIGABRT · 순수 가상 호출 · 잘못된 CRT 인자 훅을 겁니다. 셋은 CRT 가
         *          `__fastfail` 로 **예외 필터를 건너뛰고** 프로세스를 끝내는 길이라, 예전에는 abort 로 끝나면 덤프도 스택도 남지 않았습니다.
         */
        static void initialize();
        /** @brief 핸들러를 제거하고 이전 핸들러를 되돌립니다. */
        static void shutdown();

        /**
         * @brief **이 스레드**에서 스택 오버플로가 나도 리포트를 쓸 수 있게 준비합니다. 엔진이 만드는 스레드는 시작할 때 부릅니다.
         * @details 스택 오버플로는 스택이 바닥난 채로 핸들러에 들어옵니다. 준비가 없으면 핸들러가 첫 호출에서 다시 넘쳐 **아무것도 남기지
         *          못합니다**(실측: 덤프 파일 0 바이트). Windows 는 `SetThreadStackGuarantee` 로 넘친 뒤에 쓸 자리를 남겨 두고, POSIX 는
         *          이 스레드 전용 대체 시그널 스택을 깝니다 — `sigaltstack` 은 스레드마다라, 예전에는 `initialize` 를 부른 스레드만 덮였습니다.
         *          핸들러 설치 전에 불러도 됩니다(로거 작업 스레드가 그렇다). 두 번 불러도 됩니다.
         */
        static void initializeCurrentThread();

        /**
         * @brief 일부러 죽습니다(`CrashTestKind` 설명). 돌아오지 않습니다. `None` · 모르는 값이면 아무것도 하지 않습니다.
         */
        static void crashForTest( CrashTestKind kind );

        /**
         * @brief 크래시 리포트에 함께 적을 키-값을 등록합니다.
         * @details 엔진 · 게임이 아는 정보를 크래시 시점에 알 수 있도록 미리 올려 둡니다. 백엔드, GPU 어댑터와 드라이버 버전,
         *          빌드 식별자, 활성 씬 같은 것들입니다. **크래시 시점에는 할당을 하지 않습니다**(힙이 이미 깨져 있을 수 있습니다).
         *          그래서 여기서 미리 고정 버퍼에 복사해 둡니다. 같은 키를 다시 주면 덮어씁니다.
         *
         *          이 저장소는 RHI 백엔드가 넷이라 "어느 백엔드에서 났는가" 가 특히 중요합니다. 범위를 좁히는 첫 질문이 늘 그것이었습니다.
         * @param key   짧은 식별자(예: "RHI", "GPU", "Build")
         * @param value 값. 길면 잘립니다.
         */
        static void setContextValue( string_view key, string_view value );

        /**
         * @brief 이 실행을 식별하는 세션 ID 입니다(프로세스마다 하나이고, 부팅 때 정해집니다).
         * @details 로그 파일 이름과 크래시 리포트에 같은 값이 들어가 둘을 짝지을 수 있습니다. 사용자가 보낸 덤프와 로그가 같은
         *          실행의 것인지 확인하는 유일한 방법입니다.
         */
        static const utf8* getSessionId();

        /**
         * @brief 크래시 보고에 줄 시한(초)입니다. 넘기면 보고를 버리고 곧장 끝냅니다. 0 이면 기본값(20 초).
         * @details 보고는 죽어 가는 프로세스 안에서 돈다. 크래시가 남긴 락(힙 손상으로 abort 한 malloc 의 락 · 막힌 stdio · 로더)을
         *          보고가 기다리면 영영 끝나지 않고, 크래시 난 게임이 창을 띄운 채 서 있다. 덤프 · 컨텍스트 · 스택 파일을 먼저 쓰므로
         *          시한에 걸려도 대개 남는다. Windows 는 폴트 스레드가 보고 스레드를 이만큼만 기다리고, POSIX 는 `alarm` 이 끝낸다.
         */
        static void setReportDeadline( uint32 seconds );
    };
} // namespace sw
