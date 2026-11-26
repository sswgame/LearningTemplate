#include "pch.h"

#include "DevTools/Host/HostClientDevTasks.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Log/Logger.h"

#include "DevTools/Host/Client/EditorGameModuleManager.h"

#include "Engine/Engine.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Renderer/Capture/PortraitStudio.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Window/SplashSubsystem.h"
#include "Engine/Window/SplashWindow.h"

namespace sw
{
    namespace HostClientDevTasksInternal
    {
        /** @brief 초상화 한 변의 기본 픽셀 수입니다(`--portrait-size` 가 없거나 0 이하일 때). */
        static constexpr int32 kDefaultPortraitSize = 256;
    } // namespace HostClientDevTasksInternal

    HostTaskResult HostClientDevTasks::runPortraitBake( Engine& engine, const CommandLineManager& commandLine )
    {
        using namespace HostClientDevTasksInternal;
        string portraitList;
        if ( commandLine.getArgument( CommandLineArgument::RENDER_PORTRAITS, portraitList ) == false || portraitList.empty() )
            return HostTaskResult::NotRequested;
        int32        portraitSize = kDefaultPortraitSize;
        AbsolutePath portraitDir;
        commandLine.getArgument( CommandLineArgument::PORTRAIT_SIZE, portraitSize );
        commandLine.getArgument( CommandLineArgument::PORTRAIT_DIR, portraitDir );
        // 굽는 동안 스플래시를 남기지 않는다. 렌더러 없는 구성은 스플래시 단계가 없다.
        if ( SplashSubsystem* const pSplashHost = engine::findSubsystem<SplashSubsystem>() )
            pSplashHost->getSplash().dismiss();
        // 스튜디오 씬 · 패킷은 이 스레드가, 그리기 · 읽기는 렌더 스레드가 명령으로 한다(`PortraitStudio`). 하나라도 실패하면 실패다.
        RenderThread* const pRenderThread = engine.getRenderThread();
        if ( pRenderThread == nullptr || pRenderThread->getDevice() == nullptr )
        {
            SW_LOG_ERROR_ID( "HostDevLayer.PortraitBakeNeedsRender", "Portrait bake needs a render thread with a device (--render-portraits)" );
            return HostTaskResult::Failed;
        }
        const bool bBaked = PortraitStudio::renderPrefabList( *pRenderThread, engine::getSubsystem<SceneManager>().getSceneEngineSubsystems(), portraitList, portraitDir,
                                                              static_cast<uint32>( portraitSize > 0 ? portraitSize : kDefaultPortraitSize ) );
        return bBaked ? HostTaskResult::Finished : HostTaskResult::Failed;
    }

    unique_ptr<GameModuleManager> HostClientDevTasks::makeEditorModuleManager( Engine& engine )
    {
        return make_unique<EditorGameModuleManager>( engine );
    }
} // namespace sw
