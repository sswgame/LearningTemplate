#include "pch.h"

#include "Editor/Panels/Console/Model/ConsoleViewModel.h"

#include "Editor/Common/Commands/Asset/EditorAssetFileCommands.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

namespace sw::editor
{
    bool ConsoleViewModel::isPlayStopped()
    {
        return EditorPlaySession::isStopped();
    }

    bool ConsoleViewModel::isPlayRunning()
    {
        return EditorPlaySession::isPlaying() && EditorPlaySession::isPaused() == false;
    }

    void ConsoleViewModel::pausePlay()
    {
        EditorPlaySession::pause();
    }

    bool ConsoleViewModel::showInFileExplorer( string_view absolutePath )
    {
        return EditorAssetFileCommands::showInFileExplorer( absolutePath );
    }
} // namespace sw::editor
