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
     *          (Windows 는 `si.hStdError = hStdOutWrite`, POSIX 는 명령 끝에 `2>&1`). 빌드 로그처럼
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
     * @details **플랫폼마다 할 수 있는 일이 다르다.** `launch` · `readOutputLine` · `waitForExit` 는
     *          양쪽이 같지만, 나머지는 POSIX 구현이 `popen` 위에 서 있어서 자식 pid 를 알 수 없다.
     *          각 함수의 주석에 어느 쪽이 무엇을 못 하는지 적어 두었다. POSIX 를 fork/exec 로 바꾸면
     *          전부 같아진다 — `docs/06_Backlog.md` 에 남겼다.
     */
    class SW_API Process
    {
    public:
        /** @brief 비어 있는 프로세스 핸들로 생성합니다. */
        Process();
        /**
         * @brief 프로세스 핸들을 닫고 리소스를 정리합니다.
         * @details Windows 는 실행 중인 자식을 **분리**한다(핸들만 닫는다). POSIX 는 `pclose` 라
         *          **자식이 끝날 때까지 막힌다** — 소멸자가 얼마나 걸릴지 자식이 정한다.
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
         * @return 프로세스 종료 코드 (-1 = 대기 실패)
         */
        int32 waitForExit();

        /**
         * @brief 프로세스에 강제 종료를 **요청**합니다.
         * @param exitCode 자식에게 물릴 종료 코드
         * @return 요청이 접수되면 true. **POSIX 는 언제나 false 다** — pid 가 없어 죽일 수 없다.
         * @details 돌아왔다고 해서 자식이 이미 죽은 것은 아니다. 죽었는지 확인해야 한다면
         *          `waitForExit` 로 기다린다 — `isRunning` 도 그 전까지는 true 일 수 있다.
         *          다른 스레드가 `readOutputLine` 을 돌고 있어도 안전하다(Windows). 자식이 죽으면
         *          파이프 쓰기 끝이 닫혀 그 스레드의 읽기가 false 로 돌아온다.
         */
        bool terminate( int32 exitCode = 1 );

        /**
         * @brief 프로세스가 아직 실행 중인지 여부를 반환합니다.
         * @details Windows 는 OS 에 직접 묻는다. **POSIX 는 자기가 들고 있는 깃발을 볼 뿐이라**
         *          자식이 스스로 끝나도 `waitForExit` 를 부르기 전까지 true 로 남는다.
         */
        bool isRunning() const;

        /** @brief 프로세스 ID를 반환합니다. **POSIX 는 언제나 0 이다** (`popen` 이 pid 를 주지 않는다). */
        int32 getProcessId() const { return _processId; }
        /** @brief 네이티브 프로세스 핸들입니다 (Windows: HANDLE). **POSIX 는 언제나 nullptr 이다.** */
        void* getNativeHandle() const { return _pNativeHandle; }

        /**
         * @brief 동기 실행 헬퍼: 프로세스를 실행하고 출력을 실시간 델리게이트로 전달하며 종료까지 대기합니다.
         * @param command 실행할 명령어
         * @param options 작업 디렉터리 등 프로세스 옵션
         * @param onOutput 출력 라인 수신 델리게이트 (표준 에러가 합쳐져 들어온다)
         * @return 프로세스 종료 코드 (-1 = 실행 실패)
         */
        static int32 execute( string_view command, const ProcessOptions& options = {}, ProcessOutputDelegate onOutput = {} );

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
