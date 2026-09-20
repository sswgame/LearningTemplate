/**
 * @file Process.h
 * @brief 크로스 플랫폼 외부 프로세스 생성, 출력 스트리밍, 제어 및 종료 유틸리티
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"

namespace sw
{
    /**
     * @brief 프로세스 출력 한 줄 콜백.
     * @details **표준 에러는 표준 출력에 합쳐져 들어온다** — 두 구현 모두 일부러 그렇게 한다
     *          (Windows 는 `si.hStdError = hStdOutWrite`, POSIX 는 자식에서 `dup2` 로 둘을 같은
     *          파이프에 건다 — 예전 `popen` 구현은 명령 끝에 `2>&1` 을 붙였다). 빌드 로그처럼
     *          두 스트림이 **원래 순서대로** 섞여야 읽히는 것이 이 클래스의 용도이기 때문이다.
     *          그래서 어느 쪽에서 온 줄인지 가려낼 방법이 없다 — 예전에는 `bIsStdErr` 인자가 있었지만
     *          호출부 전부가 무시했고 넘어오는 값도 언제나 false 였다.
     */
    using ProcessOutputDelegate = Delegate<void( string_view line )>;

    /**
     * @struct ProcessOptions
     * @brief 프로세스 생성 옵션
     */
    struct ProcessOptions
    {
        string _workingDirectory{};
        bool   _bCreateWindow{ false };
    };

    /**
     * @class Process
     * @brief 외부 OS 프로세스를 생성하고 표준 출력을 파이프로 스트리밍하며 종료를 대기하는 클래스
     * @details **두 구현이 같은 일을 한다.** Windows 는 `CreateProcess`, POSIX 는 `fork`+`exec` 으로
     *          자식을 만들고 양쪽 다 **자식을 직접 들고 있다** — pid·강제 종료·살아 있는지 묻기가
     *          모두 된다. 갈리는 곳은 두 군데뿐이고 각 함수 주석에 적어 두었다: 종료 코드를 정해 줄 수
     *          있는지(`terminate`), 그리고 신호로 죽은 자식의 종료 코드를 무엇으로 볼지(`waitForExit`).
     *          예전 POSIX 구현은 `popen` 위에 서 있어 pid 가 없었고, 그래서 죽이지도 묻지도 못했다.
     */
    class SW_API Process
    {
    public:
        /** @brief 비어 있는 프로세스 핸들로 생성합니다. */
        Process();
        /**
         * @brief 프로세스 핸들을 닫고 리소스를 정리합니다.
         * @details 양쪽 다 실행 중인 자식을 **분리**한다 — 기다리지 않는다. POSIX 는 이미 끝난 자식을
         *          여기서 거두고(좀비를 남기지 않는다), 아직 도는 자식은 우리가 죽은 뒤 init 이 거둔다.
         *          예전에는 `pclose` 라 **소멸자가 자식이 끝날 때까지 막혔다** — 얼마나 걸릴지 자식이 정했다.
         */
        ~Process();

        Process( const Process& )            = delete;
        Process& operator=( const Process& ) = delete;

        Process( Process&& other ) noexcept;
        Process& operator=( Process&& other ) noexcept;

        /**
         * @brief 자식 프로세스를 비동기로 실행하고 입출력 파이프를 연결합니다.
         * @param command 실행할 명령어 (인자 포함)
         * @param options 작업 디렉터리 등 프로세스 옵션
         * @return 프로세스 생성 성공 시 true
         */
        bool launch( string_view command, const ProcessOptions& options = {} );

        /**
         * @brief 파이프로부터 다음 출력 한 줄을 읽습니다.
         * @param outLine 출력 문자열이 담길 버퍼
         * @return 읽기 성공 시 true, EOF 또는 파이프 종료 시 false
         */
        bool readOutputLine( string& outLine );

        /**
         * @brief 프로세스 종료를 대기하고 종료 코드를 반환합니다.
         * @return 프로세스 종료 코드 (-1 = 대기 실패). **POSIX 에서 신호로 죽은 자식은 `128 + 신호번호`**
         *         다(셸 규약. 예: SIGKILL 이면 137) — 신호에 죽은 프로세스에는 종료 코드가 없다.
         * @details POSIX 는 여기서 자식을 거두고 **pid 를 놓는다.** 그 번호는 OS 가 곧 재사용하므로,
         *          들고 있으면 나중에 남의 자식을 거두게 된다. 그래서 `waitForExit` 뒤의 `getProcessId`
         *          는 0 이고 두 번째 `waitForExit` 은 -1 이다.
         */
        int32 waitForExit();

        /**
         * @brief 프로세스에 강제 종료를 **요청**합니다.
         * @param exitCode 자식에게 물릴 종료 코드. **POSIX 는 이 값을 쓸 수 없다** — 신호로 죽이므로
         *                 종료 코드를 정해 줄 방법이 없고, `waitForExit` 이 `128 + SIGKILL` 을 준다.
         * @return 요청이 접수되면 true.
         * @details 돌아왔다고 해서 자식이 이미 죽은 것은 아니다. 죽었는지 확인해야 한다면
         *          `waitForExit` 로 기다린다 — `isRunning` 도 그 전까지는 true 일 수 있다.
         *          다른 스레드가 `readOutputLine` 을 돌고 있어도 안전하다. 자식이 죽으면
         *          파이프 쓰기 끝이 닫혀 그 스레드의 읽기가 false 로 돌아온다.
         *          POSIX 는 **프로세스 그룹째** 죽인다 — `sh` 가 낳은 손자(빌드라면 진짜 컴파일러)까지
         *          멈춰야 취소가 끝나기 때문이다.
         */
        bool terminate( int32 exitCode = 1 );

        /**
         * @brief 프로세스가 아직 실행 중인지 여부를 반환합니다.
         * @details 양쪽 다 **OS 에 직접 묻는다.** POSIX 는 `waitid(WNOHANG | WNOWAIT)` 이라 묻기만 하고
         *          거두지는 않는다 — 거두면 뒤이어 부를 `waitForExit` 이 종료 코드를 잃는다.
         *          예전에는 자기가 들고 있는 깃발을 볼 뿐이라 자식이 스스로 끝나도 true 로 남았다.
         */
        bool isRunning() const;

        /**
         * @brief 프로세스 ID를 반환합니다. 시작하기 전에는 0 입니다.
         * @details **POSIX 는 `waitForExit` 이 거둔 뒤에도 0 이다** — 그 자리에서 pid 를 놓기 때문이다.
         *          Windows 는 핸들을 닫을 때까지 값이 남는다.
         */
        int32 getProcessId() const { return _processId; }
        /** @brief 네이티브 프로세스 핸들입니다 (Windows: HANDLE, POSIX: pid 를 그대로 담은 값). */
        void* getNativeHandle() const { return _pNativeHandle; }

        /**
         * @brief 동기 실행 헬퍼: 프로세스를 실행하고 출력을 실시간 델리게이트로 전달하며 종료까지 대기합니다.
         * @param command 실행할 명령어
         * @param options 작업 디렉터리 등 프로세스 옵션
         * @param onOutput 출력 라인 수신 델리게이트 (표준 에러가 합쳐져 들어온다)
         * @return 프로세스 종료 코드 (-1 = 실행 실패)
         */
        static int32 execute( string_view command, const ProcessOptions& options = {}, const ProcessOutputDelegate& onOutput = {} );

    private:
        void shutdown();

    private:
        void*  _pNativeHandle;
        void*  _pStdOutRead;
        void*  _pNativeThread;
        string _bufferedOutput;
        int32  _processId;
        bool   _bRunning;
    };
} // namespace sw
