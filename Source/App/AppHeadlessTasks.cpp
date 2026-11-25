#include "pch.h"

#include "App/AppHeadlessTasks.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Log/Logger.h"

#include "Engine/Engine.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Renderer/Capture/PortraitStudio.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Telemetry/CrashReportService.h"
#include "Engine/Telemetry/CrashReportUploader.h"
#include "Engine/Window/SplashSubsystem.h"
#include "Engine/Window/SplashWindow.h"

#include "ModuleManager/Host/HostDevLayer.h"

namespace sw
{
    namespace
    {
        /** @brief 이 TU 의 작업 함수입니다. */
        struct AppHeadlessTasksInternal
        {
            using TaskResult = AppHeadlessTasks::TaskResult;

            /**
             * @brief 크래시 보고 프로세스(`-crash-reporter=<폴더>`)입니다. 엔진은 명령줄 처리까지만 시작한 상태이며(서비스와 게임 모듈이 없습니다) 번들만 보내고 끝냅니다. 청해지지 않았으면 false 입니다.
             * @note App 은 GameFramework 의 HTTP 클라이언트를 링크하지 않아(층) 이 프로세스의 업로더는 보내지 않는 기본값이고 묶음을 건드리지 않습니다(`Kept`).
             *       끝점이 있는 게임은 다음 기동의 게임 프로세스가 같은 폴더를 올립니다(`TelemetryUploadLink`).
             */
            static bool runCrashReporter( const CommandLineManager& commandLine )
            {
                AbsolutePath reporterFolder;
                if ( commandLine.getArgument( CommandLineArgument::CRASH_REPORTER, reporterFolder ) == false || reporterFolder.empty() )
                    return false;
                NullCrashReportUploader       uploader;
                [[maybe_unused]] const uint32 sentCount = CrashReportService::runReporter( reporterFolder, uploader );
                SW_LOG_INFO_ID( "App.ReportSent", "[CrashReporter] %# report(s) sent from '%#'", sentCount, reporterFolder.c_str() );
                return true;
            }

            /**
             * @brief 초상화 굽기(`--render-portraits=a.prefab.xml,b.prefab.xml`)입니다. 게임 컴포넌트 타입이 로드된 뒤 그리고 끝낸다(창은 띄우지 않는다).
             * @details 스튜디오 씬 · 패킷은 이 스레드가, 그리기 · 읽기는 렌더 스레드가 명령으로 한다(`PortraitStudio`). 하나라도 실패하면 실패다.
             */
            static TaskResult runPortraitBake( const CommandLineManager& commandLine, Engine& hostEngine )
            {
                string portraitList;
                if ( commandLine.getArgument( CommandLineArgument::RENDER_PORTRAITS, portraitList ) == false || portraitList.empty() )
                    return TaskResult::NotRequested;
                int32        portraitSize = 256;
                AbsolutePath portraitDir;
                commandLine.getArgument( CommandLineArgument::PORTRAIT_SIZE, portraitSize );
                commandLine.getArgument( CommandLineArgument::PORTRAIT_DIR, portraitDir );
                // 굽는 동안 스플래시를 남기지 않는다. 렌더러 없는 구성은 스플래시 단계가 없다.
                if ( SplashSubsystem* const pSplashHost = engine::findSubsystem<SplashSubsystem>() )
                    pSplashHost->getSplash().dismiss();
                RenderThread* const pRenderThread = hostEngine.getRenderThread();
                if ( pRenderThread == nullptr || pRenderThread->getDevice() == nullptr )
                {
                    SW_LOG_ERROR_ID( "App.PortraitBakeNeedsRender", "Portrait bake needs a render thread with a device (--render-portraits)" );
                    return TaskResult::Failed;
                }
                const bool bBaked = PortraitStudio::renderPrefabList( *pRenderThread, engine::getSubsystem<SceneManager>().getEngineServices(), portraitList, portraitDir,
                                                                      static_cast<uint32>( portraitSize > 0 ? portraitSize : 256 ) );
                return bBaked ? TaskResult::Finished : TaskResult::Failed;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool AppHeadlessTasks::runAll( const CommandLineManager* pCommandLine, IHostDevLayer* pDevLayer )
    {
        if ( pCommandLine == nullptr )
            return true;
        if ( AppHeadlessTasksInternal::runCrashReporter( *pCommandLine ) )
            return true; // 이 작업만 하고 끝낸다
        // 에디터 모듈 작업은 개발 계층의 표가 든다 — 배포에는 개발 계층도 그 인자도 없다.
        return pDevLayer == nullptr || pDevLayer->runEditorModuleHeadlessTasks( *pCommandLine );
    }

    AppHeadlessTasks::TaskResult AppHeadlessTasks::runAfterHostServices( const CommandLineManager& commandLine, Engine& hostEngine )
    {
        return AppHeadlessTasksInternal::runPortraitBake( commandLine, hostEngine ); // 지금은 초상화 굽기 하나다
    }
} // namespace sw
