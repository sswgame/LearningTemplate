#include "pch.h"

#include "Editor/Panels/Console/Model/ConsoleViewModel.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "DevTools/Client/Automation/Runner/AutomationSession.h"

#include "Editor/Common/Commands/Asset/EditorAssetFileCommands.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorUICommandQueue.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "Engine/Console/DevCommandRegistry.h"

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

    namespace
    {
        struct ConsoleDevCommandsInternal
        {
            /** @brief `log.repeat` 한 번에 남길 수 있는 줄 수의 상한입니다(로그를 넘치게 하지 않는다). */
            static constexpr int32 kMaxRepeatCount = 1000;

            /** @brief `log.repeat <count> <warning|info> <text...>` — 같은 줄을 여러 번 남깁니다(Output Log 접기 테스트). */
            static bool runLogRepeat( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() < 3 )
                    return false;
                int32      count{ 0 };
                const bool bInvalidLogCount = StringUtil::parseInt( listArgument[0], count ) == false || count <= 0 || count > kMaxRepeatCount;
                if ( bInvalidLogCount )
                    return false;
                const bool bWarning = listArgument[1] == "warning";
                if ( bWarning == false && listArgument[1] != "info" )
                    return false;
                string text = listArgument[2];
                for ( size_t argumentIndex = 3; argumentIndex < listArgument.size(); ++argumentIndex )
                {
                    text += " ";
                    text += listArgument[argumentIndex];
                }
                for ( int32 lineIndex = 0; lineIndex < count; ++lineIndex )
                {
                    if ( bWarning )
                        SW_LOG_WARNING_ID( "Console.LogRepeat", "%#", text.c_str() );
                    else
                        SW_LOG_INFO_ID( "Console.LogRepeat", "%#", text.c_str() );
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "logged %# lines", count );
                outReply = string( replyBuilder.view() );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( LogRepeat, "log.repeat", "log.repeat <count> <warning|info> <text...>", "Write the same log line several times (Output Log Collapse checks)",
                    &ConsoleDevCommandsInternal::runLogRepeat );
} // namespace sw::editor
