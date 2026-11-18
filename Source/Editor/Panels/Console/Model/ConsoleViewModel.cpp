#include "pch.h"

#include "Editor/Panels/Console/Model/ConsoleViewModel.h"

#include "DevTools/Client/Automation/Runner/AutomationSession.h"

#include "Editor/Common/Commands/Asset/EditorAssetFileCommands.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorUICommandQueue.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

namespace sw::editor
{
    bool ConsoleViewModel::isPlayStopped()
    {
        return EditorPlaySession::getUIState().isStopped();
    }

    bool ConsoleViewModel::isPlayRunning()
    {
        return EditorPlaySession::getUIState().isPlaying() && EditorPlaySession::getUIState().isPaused() == false;
    }

    void ConsoleViewModel::pausePlay()
    {
        EditorUICommandQueue::postToEditor( EditorUICommand::make( EditorUICommandKind::Pause ) ); // 이 UI 프레임 뒤 게임 틱 시작에 적용된다(UI 출력 계약)
    }

    bool ConsoleViewModel::showInFileExplorer( string_view absolutePath )
    {
        return EditorAssetFileCommands::showInFileExplorer( absolutePath );
    }

    shared_ptr<const AutomationLogPolicy> ConsoleViewModel::findScenarioLogPolicy()
    {
        const AutomationSession* const pSession = findSubsystem<AutomationSession>();
        return pSession != nullptr ? pSession->getLogPolicy() : nullptr;
    }
} // namespace sw::editor
