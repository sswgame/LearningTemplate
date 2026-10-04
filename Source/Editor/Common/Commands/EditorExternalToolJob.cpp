#include "pch.h"

#include "Editor/Common/Commands/EditorExternalToolJob.h"

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

    bool EditorExternalToolJob::request( string_view command, string_view workingDirectory )
    {
        if ( isPending() )
            return false;
        if ( _thread.joinable() )
            _thread.join();

        EditorExternalToolInput input;
        input._command          = string{ command };
        input._workingDirectory = string{ workingDirectory };
        const uint32 generation = beginRequest( std::move( input ) );
        _pProcess               = sw::make_shared<Process>();
        _thread                 = std::thread( &EditorExternalToolJob::runJob, _pState, _pProcess, generation );
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
                    line.pop_back();
                result._listLine.push_back( line );
            }
            result._exitCode = pProcess->waitForExit();
        }
        publish( pState, generation, std::move( result ) );
    }
} // namespace sw::editor
