/**
 * @file Process.h
 * @brief 외부 프로세스를 만들고, 출력을 스트리밍하고, 제어 · 종료하는 크로스 플랫폼 유틸리티입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

namespace sw
{
    /**
     * @struct ProcessCommand
     * @brief 띄울 실행 파일과 인자 목록입니다. `Process` 는 명령을 글 하나로 받지 않습니다 — 인자를 인용하는 일은 `Process` 안 한 곳만 합니다.
     * @details **셸을 거치지 않습니다.** POSIX 는 `execv` 로 인자 배열을 그대로 넘기고, Windows 는 `toCommandLine`(CommandLineToArgvW 규칙)으로
     *          인용한 줄을 `CreateProcessW` 에 줍니다. 그래서 인자 안의 공백 · 따옴표 · 역슬래시 · `;` `&` `$` 는 그 인자의 글자일 뿐입니다.
     *          예전에는 부르는 쪽이 따옴표를 손으로 붙였고(이스케이프가 빠진 자리가 여럿이었다), POSIX 는 그 글을 `sh -c` 에 넘겼다.
     *          셸 문법(파이프 · 리디렉션 · `&&`)이 필요하면 셸을 실행 파일로 적는다(`/bin/sh` + `-c`, `cmd.exe` + `/c`) — 그때 셸 문법은 부르는 쪽 몫이다.
     *          `_executable` 은 경로이거나 PATH 에서 찾을 이름(`cmake` · `git`)이다. Core 라 Engine 의 `DataPath` 를 쓸 수 없고, PATH 이름은 경로가 아니다.
     */
    struct ProcessCommand
    {
        ProcessCommand() = default;
        /** @brief 인자 목록을 반드시 함께 받는다 — 글 하나에서 바뀌는 생성자를 두지 않아 `launch( "a b c" )` 가 컴파일되지 않게. */
        ProcessCommand( string executable, vector<string> listArgument )
            : _executable{ std::move( executable ) }
            , _listArgument{ std::move( listArgument ) }
        {
        }

        string         _executable{};
        vector<string> _listArgument{};

        /** @brief 실행 파일을 포함해 CommandLineToArgvW 규칙으로 인용한 명령줄입니다. Windows 가 `CreateProcessW` 에 넘기는 글이고, 양쪽 모두 로그에 씁니다. */
        [[nodiscard]] string toCommandLine() const;

        /**
         * @brief 사람이 적은 인자 줄(설정의 추가 인자 · 명령 템플릿)을 인자 목록으로 나눕니다.
         * @details 공백이 인자를 가르고, 큰따옴표 사이는 공백이 있어도 한 인자다(따옴표 자체는 뺀다 — `-a="x y"` 는 `-a=x y`). 역슬래시 이스케이프는 없다.
         *          셸이 아니다 — `$` `;` `&` 는 글자 그대로다. 프로그램이 만든 값은 이 함수를 거치지 말고 `_listArgument` 에 바로 넣는다.
         */
        [[nodiscard]] static vector<string> splitArgumentLine( string_view line );
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 프로세스 출력 한 줄을 받는 콜백입니다.
     * @details **표준 에러는 표준 출력에 합쳐져 들어옵니다.** 두 구현 모두 일부러 그렇게 합니다(Windows 는
     *          `si.hStdError = hStdOutWrite`, POSIX 는 자식 프로세스에서 `dup2` 로 둘을 같은 파이프에 겁니다). 빌드 로그처럼 두
     *          스트림이 **원래 순서대로** 섞여야 읽을 수 있는 출력을 다루는 것이 이 클래스의 용도이기 때문입니다. 그래서 어느 쪽에서
     *          온 줄인지 가려낼 방법은 없습니다.
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
        /** @brief 자식의 표준 입력을 파이프로 연결합니다 — `writeInput` · `closeInput` 으로 씁니다. 끄면 자식의 표준 입력은 비어 있다(NUL · /dev/null). */
        bool _bPipeStandardInput{ false };
        /**
         * @brief Windows 에서 자식을 새 프로세스 그룹으로 띄웁니다(`CREATE_NEW_PROCESS_GROUP`) — `requestStop` 이 그 그룹에 Ctrl+Break 를 보낼 수 있게.
         * @details 모든 자식에 걸면 터미널의 Ctrl+C 가 자식(빌드 중인 컴파일러)에 닿지 않으므로 옵션이다. POSIX 는 늘 자기 그룹이라 무시합니다.
         */
        bool _bNewProcessGroup{ false };
    };
} // namespace sw

namespace sw
{
    /**
     * @class Process
     * @brief 외부 OS 프로세스를 만들고, 표준 출력을 파이프로 받아 오고, 종료를 기다리는 클래스입니다.
     * @details **두 구현이 같은 일을 합니다.** Windows 는 `CreateProcess`, POSIX 는 `fork` + `exec` 으로 자식을 만들고, 양쪽 모두
     *          **자식을 직접 들고 있습니다.** 그래서 pid 조회 · 강제 종료 · 실행 중인지 확인이 모두 됩니다. 달라지는 곳은 두
     *          군데뿐이고 각 함수 주석에 적어 두었습니다. 종료 코드를 정해 줄 수 있는지(`terminate`), 그리고 시그널로 죽은
     *          자식의 종료 코드를 무엇으로 볼지(`waitForExit`)입니다.
     */
    class SW_API Process
    {
    public:
        /** @brief 빈 프로세스 핸들로 만듭니다. */
        Process();
        /**
         * @brief 프로세스 핸들을 닫고 자원을 정리합니다.
         * @details 양쪽 모두 실행 중인 자식을 **분리**하고 기다리지 않습니다. POSIX 는 이미 끝난 자식을 여기서 회수하고(좀비를 남기지
         *          않습니다), 아직 도는 자식은 이 프로세스가 끝난 뒤 init 이 거둡니다. 기다리면 **소멸자가 자식이 끝날 때까지 막힙니다.**
         */
        ~Process();

        Process( const Process& )            = delete;
        Process& operator=( const Process& ) = delete;

        Process( Process&& other ) noexcept;
        Process& operator=( Process&& other ) noexcept;

        /**
         * @brief 자식 프로세스를 비동기로 실행하고 입출력 파이프를 연결합니다.
         * @param command 실행 파일과 인자(셸을 거치지 않는다 — `ProcessCommand`)
         * @param options 작업 디렉터리 등 프로세스 옵션
         * @return 프로세스를 만들었으면 true. 실행 파일이 비었거나 찾지 못하면 false 입니다(POSIX 도 셸의 127 이 아니라 여기서 진다).
         */
        bool launch( const ProcessCommand& command, const ProcessOptions& options = {} );

        /**
         * @brief 파이프에서 다음 출력 한 줄을 읽습니다.
         * @param outLine 읽은 줄을 담을 버퍼
         * @return 읽었으면 true, EOF 이거나 파이프가 닫혔으면 false
         */
        [[nodiscard]] bool readOutputLine( string& outLine );

        /**
         * @brief 프로세스가 끝나기를 기다리고 종료 코드를 반환합니다.
         * @return 프로세스 종료 코드(-1 = 대기 실패). **POSIX 에서 시그널로 죽은 자식은 `128 + 시그널 번호`** 입니다(셸 규약.
         *         예: SIGKILL 이면 137). 시그널로 죽은 프로세스에는 종료 코드가 없기 때문입니다.
         * @details POSIX 는 여기서 자식을 회수하고 **pid 를 놓습니다.** 그 번호는 OS 가 곧 재사용하므로, 계속 보관하면 나중에 다른
         *          프로세스의 자식을 회수하게 됩니다. 그래서 `waitForExit` 뒤의 `getProcessID` 는 0 이고, 두 번째 `waitForExit` 은
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
         *          자식이 띄운 손자 프로세스(빌드라면 cmake 가 띄운 컴파일러)까지 멈춰야 취소가 끝나기 때문입니다.
         */
        bool terminate( int32 exitCode = 1 );

        /** @brief 자식의 표준 입력에 씁니다(`_bPipeStandardInput` 으로 띄웠을 때만). 다 쓰면 true 입니다. */
        [[nodiscard]] bool writeInput( string_view text );
        /** @brief 자식의 표준 입력을 닫습니다(자식은 EOF 를 받는다). 두 번 불러도 됩니다. */
        void closeInput();
        /**
         * @brief 자식에게 **정상 종료**를 요청합니다(강제 종료인 `terminate` 와 다르다). 보냈으면 true 입니다.
         * @details POSIX 는 자식의 프로세스 그룹에 SIGTERM(`launch` 가 자식을 제 그룹의 우두머리로 만든다 — 자식이 띄운 손자까지 닿는다).
         *          Windows 는 `CTRL_BREAK_EVENT` 를 자식의 프로세스 그룹에 보낸다(`_bNewProcessGroup` 으로 띄운 자식만). 이 프로세스가 콘솔에
         *          붙어 있지 않으면 보낼 수 없어 false 입니다(서비스 · 콘솔 없는 CI) — 부르는 쪽은 표준 입력 명령 같은 다른 길을 씁니다.
         */
        [[nodiscard]] bool requestStop();

        /**
         * @brief 프로세스가 아직 실행 중인지 반환합니다.
         * @details 양쪽 모두 **OS 에 직접 묻습니다.** POSIX 는 `waitid(WNOHANG | WNOWAIT)` 라서 묻기만 하고 회수하지는 않습니다.
         *          회수하면 이어서 부를 `waitForExit` 이 종료 코드를 잃기 때문입니다. 자기가 보관한 플래그만 보면 자식이 스스로
         *          끝나도 true 로 남습니다.
         */
        bool isRunning() const;

        /**
         * @brief 프로세스 ID 를 반환합니다. 시작하기 전에는 0 입니다.
         * @details **POSIX 는 `waitForExit` 이 거둔 뒤에도 0 입니다.** 그 시점에 pid 를 놓기 때문입니다. Windows 는 핸들을 닫을
         *          때까지 값이 남습니다.
         */
        int32 getProcessID() const { return _processID.load(); }
        /** @brief 네이티브 프로세스 핸들입니다(Windows: HANDLE, POSIX: pid 를 그대로 담은 값). */
        void* getNativeHandle() const { return _pNativeHandle; }

        /**
         * @brief 동기 실행 도우미입니다. 프로세스를 실행하고, 출력을 델리게이트로 바로바로 넘기며, 끝날 때까지 기다립니다.
         * @param command 실행 파일과 인자
         * @param options 작업 디렉터리 등 프로세스 옵션
         * @param onOutput 출력 줄을 받을 델리게이트(표준 에러가 합쳐져 들어옵니다)
         * @return 프로세스 종료 코드(-1 = 실행 실패)
         */
        static int32 execute( const ProcessCommand& command, const ProcessOptions& options = {}, const ProcessOutputDelegate& onOutput = {} );

        /**
         * @brief 출력을 받지 않고 **기다리지도 않고** 프로세스를 띄웁니다. 띄웠으면 true 입니다(종료 코드는 볼 수 없습니다).
         * @details 뷰어 · 새 창 게임처럼 **사용자가 닫을 때까지 사는** 프로그램을 띄울 때 씁니다. `execute` 는 출력 파이프가 닫힐
         *          때까지 기다리므로, 그런 프로그램(또는 그것이 띄운 손자)이 파이프를 물고 있으면 부른 스레드가 그동안 멈춥니다.
         *          언리얼의 `CreateProc( bLaunchDetached )` 에 해당합니다.
         *          자식은 이 프로세스의 핸들 · 서술자를 하나도 물려받지 않습니다(POSIX 는 표준 입출력을 /dev/null 로, 두 번 fork 해
         *          좀비를 남기지 않습니다).
         */
        static bool launchDetached( const ProcessCommand& command, const ProcessOptions& options = {} );

        /**
         * @brief 기본 브라우저(주소의 기본 처리기)로 @p url 을 엽니다. 기다리지 않습니다. 띄웠으면 true 입니다.
         * @details Windows 는 `ShellExecuteW( "open" )`, POSIX 는 `xdg-open <주소>`(인자 하나)입니다. 주소가 http(s) 인지는 부르는 쪽이 가린다 —
         *          이 함수는 무엇이든 그 형식의 기본 처리기에 넘긴다. 언리얼 `FPlatformProcess::LaunchURL` 에 해당합니다.
         *          예전에는 `rundll32 url.dll,FileProtocolHandler` 를 띄웠는데, rundll32 는 뒤 줄을 인자로 나누지 않아 인용 규칙 밖의 예외가 필요했다.
         */
        static bool launchURL( string_view url );

        /**
         * @brief 파일 관리자에서 @p filePath 를 고른 채 엽니다. 기다리지 않습니다. 열었으면 true 입니다.
         * @details Windows 는 `SHOpenFolderAndSelectItems`(탐색기 창을 쓰거나 새로 연다), POSIX 는 선택한 채로 열기 표준이 없어 그 폴더를 `xdg-open` 합니다.
         *          예전에는 `explorer.exe /select,"<경로>"` 를 띄웠는데, explorer 의 인자 규칙이 CommandLineToArgvW 와 달라 인용 규칙 밖의 예외가 필요했다.
         */
        static bool showInFileManager( string_view filePath );

        /** @brief 이 프로세스의 ID 입니다. 같은 폴더를 나눠 쓰는 프로세스끼리 파일 이름이 겹치지 않게 가를 때 씁니다. */
        static int32 getCurrentProcessID();

        /**
         * @brief 이 기계의 호스트 이름입니다. 얻지 못하면 빈 글입니다.
         * @details 프로세스마다 남긴 파일(트레이스 등)이 같은 기계에서 나왔는지 가를 때 씁니다 — 같은 기계면 단조 시계를 나눠 쓴다.
         */
        static string getHostName();

        /**
         * @brief ID 가 @p processID 인 프로세스가 지금 살아 있는지 OS 에 묻습니다. 0 이하는 false 입니다.
         * @details 있지만 권한이 없어 열 수 없는 프로세스는 살아 있다고 봅니다(지우면 안 되는 쪽으로 틀린다). 끝났지만 아직 회수하지 않은
         *          POSIX 좀비도 살아 있다고 나옵니다. OS 가 ID 를 다시 쓰면 다른 프로세스를 보고 true 일 수 있으므로, 이 답은 "죽었으니
         *          치워도 된다" 를 가르는 데만 씁니다.
         */
        static bool isProcessAlive( int32 processID );

        /**
         * @brief 이 프로세스에 디버거가 붙어 있는지 OS 에 묻습니다.
         * @details Windows 는 `IsDebuggerPresent`(PEB 를 읽어 싸다), Linux 는 `/proc/self/status` 의 `TracerPid`(파일을 읽는다 — 자주 부르는
         *          쪽은 답을 잠시 들고 있는다). 중단점에 멈춘 동안 시간 초과를 늦추는 데 쓴다(`NetHost` 디버거 대기).
         */
        static bool isDebuggerPresent();

    private:
        void shutdown();

    private:
        void*  _pNativeHandle;
        void*  _pStdOutRead;
        void*  _pNativeThread;
        void*  _pStdInWrite; ///< 표준 입력 파이프의 쓰는 끝(Windows HANDLE, POSIX 는 서술자 + 1 — 0 은 없음)
        string _bufferedOutput;
        /**
         * @brief 자식의 pid(없으면 0). **원자다** — `terminate` 는 다른 스레드가 `readOutputLine` · `waitForExit` 을 도는 중에 불리는 것이
         *        계약이고(`ModuleCompiler::cancel` · 테스트의 자식 시한), `waitForExit` 은 거둔 뒤 이것을 0 으로 쓴다(보통 정수면 ThreadSanitizer 가
         *        두 스레드의 쓰기를 짚는다).
         */
        atomic<int32> _processID;
        bool          _bNewProcessGroup; ///< `requestStop` 이 보낼 수 있는 자식인가(Windows)
    };
} // namespace sw
