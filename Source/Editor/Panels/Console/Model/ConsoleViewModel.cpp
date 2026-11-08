#include "pch.h"

#include "Editor/Panels/Console/Model/ConsoleViewModel.h"

#include "Core/Log/LogTypes.h"

#include "Editor/Common/Commands/Asset/EditorAssetFileCommands.h"
#include "Editor/Common/Commands/Diagnostics/EditorSourceLocation.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

namespace sw::editor
{
    ConsolePlayState ConsoleViewModel::gatherPlayState()
    {
        ConsolePlayState state;
        state._bStopped = EditorPlaySession::isStopped();
        state._bRunning = EditorPlaySession::isPlaying() && EditorPlaySession::isPaused() == false;
        return state;
    }

    void ConsoleViewModel::pausePlay()
    {
        EditorPlaySession::pause();
    }

    void ConsoleViewModel::openEntryInIde( const LogEntry& entry )
    {
        EditorSourceLocation location;
        if ( EditorSourceLocation::findLogEntryLocation( entry._message, entry._file, entry._line, location ) )
            (void)EditorSourceLocation::openInIde( location ); // 실패는 openInIde 가 경고로 남긴다
    }

    void ConsoleViewModel::showInFileExplorer( const string& path )
    {
        (void)EditorAssetFileCommands::showInFileExplorer( path );
    }
} // namespace sw::editor
