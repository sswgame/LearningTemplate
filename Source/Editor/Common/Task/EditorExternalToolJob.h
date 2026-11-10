/**
 * @file EditorExternalToolJob.h
 * @brief 에디터가 외부 도구(파이썬 검증 스크립트 · git LFS)를 띄우고 출력 줄을 모으는 잡입니다.
 */
#pragma once
#include "Core/Concurrency/thread.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Process/Process.h"

#include "Editor/Common/Task/EditorBackgroundTask.h"

namespace sw::editor
{
    /** @brief 띄울 명령(실행 파일과 인자)과 작업 폴더입니다. */
    struct EditorExternalToolInput
    {
        ProcessCommand _command;
        string         _workingDirectory;
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 외부 도구 한 번의 결과입니다(표준 에러는 표준 출력에 합쳐져 들어옵니다). */
    struct EditorExternalToolResult
    {
        vector<string>  _listLine;
        optional<int32> _exitCode{};         ///< 띄우고 거뒀을 때만 종료 코드다. 띄우지 못했으면(`_bLaunched`) · 기다리지 못했으면(까닭은 `_listLine` 끝 줄) 없다
        bool            _bLaunched{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorExternalToolJob
     * @brief 외부 도구를 **전용 스레드**에서 띄워 끝날 때까지 기다리고, 게임 스레드가 `take()` 로 결과를 가져갑니다.
     * @details 작업 관리자의 워커를 쓰지 않는다 — 네트워크를 타는 git 명령은 몇 초를 기다릴 수 있고, 그동안 워커 하나가 프레임 잡을 못 받는다.
     *          한 번에 하나만 돈다(`request` 가 false 면 앞 실행이 아직 돈다). 잡이 사라질 때(에디터 종료 · 모듈 언로드) 도는 자식을 끝내고
     *          스레드를 조인한다 — 언로드된 DLL 의 코드를 도는 스레드를 남기지 않는다.
     *          **에디터의 태스크 단계(`EditorBackgroundJob::_stage`)에 넣지 않는다.** 단계 대기(`waitStage`)는 잡이 끝나기를 기다리는데, 이 잡은 느린 git 자식을 기다리게 되어
     *          종료가 시간 제한까지 늘어진다. 소멸자가 자식을 끝내고 조인하므로 주인(서브시스템 · 패널)이 범위와 함께 사라질 때 합류가 끝난다.
     *          스레드 본문은 에디터 범위를 찾지 않고 값으로 받은 상태(`shared_ptr<State>`)만 쓴다.
     */
    class EditorExternalToolJob final : public EditorBackgroundTask<EditorExternalToolInput, EditorExternalToolResult>
    {
    public:
        EditorExternalToolJob();
        ~EditorExternalToolJob();

        EditorExternalToolJob( const EditorExternalToolJob& )            = delete;
        EditorExternalToolJob& operator=( const EditorExternalToolJob& ) = delete;

        /** @brief 명령을 띄웁니다. 앞 실행이 아직 돌면 아무것도 하지 않고 false 입니다. */
        [[nodiscard]] bool request( ProcessCommand command, string_view workingDirectory );

        /** @brief 도는 자식이 있으면 끝내고 스레드를 기다립니다. */
        void cancelAndWait();

        /**
         * @brief 저장소 파이썬 스크립트 @p scriptPath 를 띄우는 명령입니다. 스크립트 인자는 돌려받은 `_listArgument` 뒤에 덧붙인다(예전 EditorUtil).
         * @details 실행기는 저장소 스크립트가 쓰는 것과 같다(Windows 는 런처 `py -3`, 그 밖은 `python3`).
         */
        static ProcessCommand makePythonScriptCommand( string scriptPath )
        {
#if defined( SW_PLATFORM_WINDOWS )
            return ProcessCommand::fromSearchPath( "py", { "-3", std::move( scriptPath ) } );
#else
            return ProcessCommand::fromSearchPath( "python3", { std::move( scriptPath ) } );
#endif
        }

    private:
        static void runJob( shared_ptr<State> pState, shared_ptr<Process> pProcess, uint32 generation );

        sw::thread          _thread;
        shared_ptr<Process> _pProcess;
    };
} // namespace sw::editor
