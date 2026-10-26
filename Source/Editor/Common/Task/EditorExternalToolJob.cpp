#include "pch.h"

#include "Editor/Common/Task/EditorExternalToolJob.h"

#include "Core/Concurrency/thread.h"
#include "Core/File/FileUtil.h"
#include "Core/Process/Process.h"

namespace sw::editor
{
    EditorExternalToolJob::EditorExternalToolJob()
        : EditorBackgroundTask{}
        , _thread{}
        , _pProcess{}
    {
    }

    EditorExternalToolJob::~EditorExternalToolJob()
    {
        cancelAndWait();
    }

    bool EditorExternalToolJob::request( ProcessCommand command, string_view workingDirectory )
    {
        if ( isPending() )
            return false;
        if ( _thread.joinable() )
            _thread.join();

        EditorExternalToolInput input;
        input._command          = std::move( command );
        input._workingDirectory = string{ workingDirectory };
        const uint32 generation = beginRequest( std::move( input ) );
        _pProcess               = sw::make_shared<Process>();
        _thread                 = sw::thread( &EditorExternalToolJob::runJob, _pState, _pProcess, generation );
        return true;
    }

    void EditorExternalToolJob::cancelAndWait()
    {
        if ( _pProcess != nullptr && _pProcess->isRunning() )
            (void)_pProcess->terminate();
        if ( _thread.joinable() )
            _thread.join();
        _pProcess.reset();
    }

    void EditorExternalToolJob::runJob( shared_ptr<State> pState, shared_ptr<Process> pProcess, uint32 generation )
    {
        EditorExternalToolInput input;
        if ( readInput( pState, generation, input ) == false )
            return;

        EditorExternalToolResult result;
        ProcessOptions           options;
        options._workingDirectory = input._workingDirectory;
        result._bLaunched         = pProcess->launch( input._command, options );
        if ( result._bLaunched )
        {
            string line;
            while ( pProcess->readOutputLine( line ) )
            {
                while ( line.empty() == false && ( line.back() == '\r' || line.back() == '\n' ) )
                {
                    line.pop_back();
                }
                result._listLine.push_back( line );
            }
            result._exitCode = pProcess->waitForExit();
            // 끝난 도구가 파일을 만들거나 지웠을 수 있다 — 이 프로세스의 경로 캐시가 옛 "없음" · "있음" 을 돌려주지 않게 알린다(`Process` 는 `File` 보다 아래 층이라 스스로 알리지 못한다).
            FileUtil::noteFileCreated();
            FileUtil::noteFileRemoved();
        }
        publish( pState, generation, std::move( result ) );
    }
} // namespace sw::editor
