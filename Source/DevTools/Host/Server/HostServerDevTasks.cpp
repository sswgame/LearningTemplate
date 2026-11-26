#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Log/Logger.h"

#include "DevTools/Host/HostClientDevTasks.h"

#include "ModuleManager/GameModuleManager.h"

namespace sw
{
    HostTaskResult HostClientDevTasks::runPortraitBake( Engine&, const CommandLineManager& commandLine )
    {
        string portraitList;
        if ( commandLine.getArgument( CommandLineArgument::RENDER_PORTRAITS, portraitList ) == false || portraitList.empty() )
            return HostTaskResult::NotRequested;
        SW_LOG_ERROR_ID( "HostDevLayer.PortraitBakeNeedsClient", "--render-portraits needs the client build (a dedicated server has no renderer)" );
        return HostTaskResult::Failed;
    }

    unique_ptr<GameModuleManager> HostClientDevTasks::makeEditorModuleManager( Engine& )
    {
        SW_LOG_ERROR_ID( "HostDevLayer.EditorNeedsClient", "-EnableEditor needs the client build (a dedicated server has no editor)" );
        return nullptr;
    }
} // namespace sw
