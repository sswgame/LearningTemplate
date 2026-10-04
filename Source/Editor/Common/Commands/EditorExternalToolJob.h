/**
 * @file EditorExternalToolJob.h
 * @brief 에디터가 외부 도구(파이썬 검증 스크립트 · git LFS)를 띄우고 출력 줄을 모으는 잡입니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/Commands/EditorBackgroundTask.h"

namespace sw
{
    class Process;
} // namespace sw

namespace sw::editor
{
    /** @brief 띄울 명령 한 줄과 작업 폴더입니다. */
    struct EditorExternalToolInput
    {
        string _command;
        string _workingDirectory;
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 외부 도구 한 번의 결과입니다(표준 에러는 표준 출력에 합쳐져 들어옵니다). */
    struct EditorExternalToolResult
    {
        vector<string> _listLine;
        int32          _exitCode{ -1 };
        bool           _bLaunched{ false };
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
     */
    class EditorExternalToolJob final : public EditorBackgroundTask<EditorExternalToolInput, EditorExternalToolResult>
    {
    public:
        EditorExternalToolJob();
        ~EditorExternalToolJob();

        EditorExternalToolJob( const EditorExternalToolJob& )            = delete;
        EditorExternalToolJob& operator=( const EditorExternalToolJob& ) = delete;

        /** @brief 명령을 띄웁니다. 앞 실행이 아직 돌면 아무것도 하지 않고 false 입니다. */
        [[nodiscard]] bool request( string_view command, string_view workingDirectory );

        /** @brief 도는 자식이 있으면 끝내고 스레드를 기다립니다. */
        void cancelAndWait();

    private:
        static void runJob( shared_ptr<State> pState, shared_ptr<Process> pProcess, uint32 generation );

        std::thread         _thread;
        shared_ptr<Process> _pProcess;
    };
} // namespace sw::editor
