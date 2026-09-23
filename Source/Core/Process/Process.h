/**
 * @file Process.h
 * @brief 외부 프로세스를 만들고, 출력을 스트리밍하고, 제어 · 종료하는 크로스 플랫폼 유틸리티입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"

namespace sw
{
    /**
     * @brief 프로세스 출력 한 줄을 받는 콜백입니다.
     * @details **표준 에러는 표준 출력에 합쳐져 들어옵니다.** 두 구현 모두 일부러 그렇게 합니다(Windows 는
     *          `si.hStdError = hStdOutWrite`, POSIX 는 자식 프로세스에서 `dup2` 로 둘을 같은 파이프에 겁니다. 예전 `popen` 구현은
     *          명령 끝에 `2>&1` 을 붙였습니다). 빌드 로그처럼 두 스트림이 **원래 순서대로** 섞여야 읽을 수 있는 출력을 다루는 것이
     *          이 클래스의 용도이기 때문입니다. 그래서 어느 쪽에서 온 줄인지 가려낼 방법은 없습니다. 예전에는 `bIsStdErr` 인자가
     *          있었지만 호출하는 곳 모두가 무시했고, 넘어오는 값도 항상 false 였습니다.
     */
    using ProcessOutputDelegate = Delegate<void( string_view line )>;

    /**
     * @struct ProcessOptions
     * @brief 프로세스 생성 옵션입니다.
     */
    struct ProcessOptions
    {
        string _workingDirectory{};
        bool   _bCreateWindow{ false };
    };

    /**
     * @class Process
     * @brief 외부 OS 프로세스를 만들고, 표준 출력을 파이프로 받아 오고, 종료를 기다리는 클래스입니다.
     * @details **두 구현이 같은 일을 합니다.** Windows 는 `CreateProcess`, POSIX 는 `fork` + `exec` 으로 자식을 만들고, 양쪽 모두
     *          **자식을 직접 들고 있습니다.** 그래서 pid 조회 · 강제 종료 · 실행 중인지 확인이 모두 됩니다. 달라지는 곳은 두
     *          군데뿐이고 각 함수 주석에 적어 두었습니다. 종료 코드를 정해 줄 수 있는지(`terminate`), 그리고 시그널로 죽은
     *          자식의 종료 코드를 무엇으로 볼지(`waitForExit`)입니다. 예전 POSIX 구현은 `popen` 위에 있어서 pid 가 없었고, 그래서
     *          죽이지도 상태를 묻지도 못했습니다.
     */
    class SW_API Process
    {
    public:
        /** @brief 빈 프로세스 핸들로 만듭니다. */
        Process();
        /**
         * @brief 프로세스 핸들을 닫고 자원을 정리합니다.
         * @details 양쪽 모두 실행 중인 자식을 **분리**하고 기다리지 않습니다. POSIX 는 이미 끝난 자식을 여기서 거두고(좀비를 남기지
         *          않습니다), 아직 도는 자식은 이 프로세스가 끝난 뒤 init 이 거둡니다. 예전에는 `pclose` 여서 **소멸자가 자식이 끝날
         *          때까지 막혔습니다.** 얼마나 걸릴지를 자식이 정한 셈입니다.
         */
        ~Process();

        Process( const Process& )            = delete;
        Process& operator=( const Process& ) = delete;

        Process( Process&& other ) noexcept;
        Process& operator=( Process&& other ) noexcept;

        /**
         * @brief 자식 프로세스를 비동기로 실행하고 입출력 파이프를 연결합니다.
         * @param command 실행할 명령(인자 포함)
         * @param options 작업 디렉터리 등 프로세스 옵션
         * @return 프로세스를 만들었으면 true
         */
        bool launch( string_view command, const ProcessOptions& options = {} );

        /**
         * @brief 파이프에서 다음 출력 한 줄을 읽습니다.
         * @param outLine 읽은 줄을 담을 버퍼
         * @return 읽었으면 true, EOF 이거나 파이프가 닫혔으면 false
         */
        bool readOutputLine( string& outLine );

        /**
         * @brief 프로세스가 끝나기를 기다리고 종료 코드를 반환합니다.
         * @return 프로세스 종료 코드(-1 = 대기 실패). **POSIX 에서 시그널로 죽은 자식은 `128 + 시그널 번호`** 입니다(셸 규약.
         *         예: SIGKILL 이면 137). 시그널로 죽은 프로세스에는 종료 코드가 없기 때문입니다.
         * @details POSIX 는 여기서 자식을 거두고 **pid 를 놓습니다.** 그 번호는 OS 가 곧 재사용하므로, 계속 들고 있으면 나중에 다른
         *          프로세스의 자식을 거두게 됩니다. 그래서 `waitForExit` 뒤의 `getProcessId` 는 0 이고, 두 번째 `waitForExit` 은
         *          -1 입니다.
         */
        int32 waitForExit();

        /**
         * @brief 프로세스에 강제 종료를 **요청**합니다.
         * @param exitCode 자식에게 줄 종료 코드. **POSIX 에서는 이 값을 쓸 수 없습니다.** 시그널로 죽이므로 종료 코드를 정해 줄
         *                 방법이 없고, `waitForExit` 이 `128 + SIGKILL` 을 반환합니다.
         * @return 요청이 받아들여지면 true
         * @details 반환됐다고 해서 자식이 이미 죽은 것은 아닙니다. 죽었는지 확인해야 한다면 `waitForExit` 으로 기다리십시오.
         *          그 전까지는 `isRunning` 도 true 일 수 있습니다. 다른 스레드가 `readOutputLine` 을 돌고 있어도 안전합니다. 자식이
         *          죽으면 파이프의 쓰기 쪽이 닫혀 그 스레드의 읽기가 false 를 반환합니다. POSIX 는 **프로세스 그룹째** 죽입니다.
         *          `sh` 가 띄운 손자 프로세스(빌드라면 실제 컴파일러)까지 멈춰야 취소가 끝나기 때문입니다.
         */
        bool terminate( int32 exitCode = 1 );

        /**
         * @brief 프로세스가 아직 실행 중인지 반환합니다.
         * @details 양쪽 모두 **OS 에 직접 묻습니다.** POSIX 는 `waitid(WNOHANG | WNOWAIT)` 라서 묻기만 하고 거두지는 않습니다.
         *          거두면 이어서 부를 `waitForExit` 이 종료 코드를 잃기 때문입니다. 예전에는 자기가 들고 있는 플래그만 봐서,
         *          자식이 스스로 끝나도 true 로 남았습니다.
         */
        bool isRunning() const;

        /**
         * @brief 프로세스 ID 를 반환합니다. 시작하기 전에는 0 입니다.
         * @details **POSIX 는 `waitForExit` 이 거둔 뒤에도 0 입니다.** 그 시점에 pid 를 놓기 때문입니다. Windows 는 핸들을 닫을
         *          때까지 값이 남습니다.
         */
        int32 getProcessId() const { return _processId; }
        /** @brief 네이티브 프로세스 핸들입니다(Windows: HANDLE, POSIX: pid 를 그대로 담은 값). */
        void* getNativeHandle() const { return _pNativeHandle; }

        /**
         * @brief 동기 실행 도우미입니다. 프로세스를 실행하고, 출력을 델리게이트로 바로바로 넘기며, 끝날 때까지 기다립니다.
         * @param command 실행할 명령
         * @param options 작업 디렉터리 등 프로세스 옵션
         * @param onOutput 출력 줄을 받을 델리게이트(표준 에러가 합쳐져 들어옵니다)
         * @return 프로세스 종료 코드(-1 = 실행 실패)
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
