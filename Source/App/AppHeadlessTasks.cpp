#include "pch.h"

#include "App/AppHeadlessTasks.h"

#include "App/EditorModuleManager.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/Log/Logger.h"

#include "Engine/Engine.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Renderer/Capture/PortraitBatchUtil.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Telemetry/CrashReportService.h"
#include "Engine/Telemetry/CrashReportUploader.h"
#include "Engine/Window/SplashSubsystem.h"
#include "Engine/Window/SplashWindow.h"

namespace sw
{
    namespace
    {
        /** @brief 이 TU 의 작업 함수와 작업 표입니다. 위에서부터 차례로 실행하며, 인자가 없는 작업은 건너뜁니다. */
        struct AppHeadlessTasksInternal
        {
            using TaskResult = AppHeadlessTasks::TaskResult;

            /**
             * @brief 크래시 보고 프로세스(`-crash-reporter=<폴더>`)입니다. 엔진은 명령줄 처리까지만 시작한 상태이며(서비스와 게임 모듈이 없습니다) 번들만 보내고 끝냅니다.
             * @note 이 저장소는 네트워크 API 를 포함하지 않습니다. 게임이 IHTTPClient 를 구현하면 여기서 HTTPCrashReportUploader 를 씁니다.
             */
            static TaskResult runCrashReporter( const CommandLineManager& commandLine )
            {
                string reporterFolder;
                if ( commandLine.getArgument( CommandLineArgument::CRASH_REPORTER, reporterFolder ) == false || reporterFolder.empty() )
                    return TaskResult::NotRequested;
                NullCrashReportUploader       uploader;
                [[maybe_unused]] const uint32 sentCount = CrashReportService::runReporter( reporterFolder, uploader );
                SW_LOG_INFO_ID( "App.ReportSent", "[CrashReporter] %# report(s) sent from '%#'", sentCount, reporterFolder.c_str() );
                return TaskResult::Finished;
            }

            /**
             * @brief 원본 임포트(`--import-<종류>`) 또는 대조만 하는 실행(`--check-<종류>`)입니다. 에디터 모듈의 일이라 App 이 모듈을 로드해 부릅니다.
             * @tparam kKind 임포트 종류. @tparam kImportArgument 임포트 인자. @tparam kCheckArgument 대조만 하는 인자.
             */
            template <EditorImportKind kKind, CommandLineArgument kImportArgument, CommandLineArgument kCheckArgument>
            static TaskResult runEditorImport( const CommandLineManager& commandLine )
            {
                bool bImport = false;
                bool bCheck  = false;
                commandLine.getArgument( kImportArgument, bImport );
                commandLine.getArgument( kCheckArgument, bCheck );
                if ( bImport == false && bCheck == false )
                    return TaskResult::NotRequested;
                if constexpr ( build::kShipping ) // 에디터 모듈의 일이다 — 배포에는 에디터 모듈도 그 경로(`EditorModuleManager.cpp` 의 개발 전용 구역)도 없다
                {
                    SW_LOG_ERROR_ID( "App.AssetImportingNeedsEditor", "Asset importing needs the editor module, which a Shipping build does not have - run it from a Dev build." );
                    return TaskResult::Failed;
                }
                else
                {
                    return EditorModuleManager::importAssetsWithEditorModule( kKind, bCheck ) ? TaskResult::Succeeded : TaskResult::Failed;
                }
            }

            /**
             * @brief 로컬라이제이션 도구(`--gather-text`, `--check-text`, `--import-po=<파일>`, `--export-po`, 프로젝트는 `-loc-project`)입니다.
             * @details 에디터 모듈의 일이라 임포트와 같은 경로로 부릅니다. 번역을 수집한 직후 교환할 수 있도록, 한 번의 실행에서 수집, 가져오기, 내보내기 순서로 처리합니다.
             */
            static TaskResult runLocalizationTools( const CommandLineManager& commandLine )
            {
                bool   bGatherText = false;
                bool   bCheckText  = false;
                bool   bExportPo   = false;
                string importPoPath;
                string projectPath;
                commandLine.getArgument( CommandLineArgument::GATHER_TEXT, bGatherText );
                commandLine.getArgument( CommandLineArgument::CHECK_TEXT, bCheckText );
                commandLine.getArgument( CommandLineArgument::EXPORT_PO, bExportPo );
                commandLine.getArgument( CommandLineArgument::IMPORT_PO, importPoPath );
                commandLine.getArgument( CommandLineArgument::LOC_PROJECT, projectPath );
                const bool bNoLocalizationTask = bGatherText == false && bCheckText == false && bExportPo == false && importPoPath.empty();
                if ( bNoLocalizationTask )
                    return TaskResult::NotRequested;

                if constexpr ( build::kShipping ) // 에디터 모듈의 일이다 — 배포에는 에디터 모듈도 그 경로(`EditorModuleManager.cpp` 의 개발 전용 구역)도 없다
                {
                    SW_LOG_ERROR_ID( "App.LocalizationToolsNeedEditor", "Localization tools need the editor module, which a Shipping build does not have - run them from a Dev build." );
                    return TaskResult::Failed;
                }
                else
                {
                    bool bSucceeded = true;
                    if ( bGatherText || bCheckText )
                    {
                        const EditorLocalizationTask gatherTask = bCheckText ? EditorLocalizationTask::CheckText : EditorLocalizationTask::GatherText;
                        bSucceeded                              = EditorModuleManager::runLocalizationWithEditorModule( gatherTask, {}, projectPath ) && bSucceeded;
                    }
                    if ( importPoPath.empty() == false )
                        bSucceeded = EditorModuleManager::runLocalizationWithEditorModule( EditorLocalizationTask::ImportPo, importPoPath, projectPath ) && bSucceeded;
                    if ( bExportPo )
                        bSucceeded = EditorModuleManager::runLocalizationWithEditorModule( EditorLocalizationTask::ExportPo, {}, projectPath ) && bSucceeded;
                    return bSucceeded ? TaskResult::Succeeded : TaskResult::Failed;
                }
            }

            /**
             * @brief 에셋 참조 검사(`--check-asset-refs`)입니다. 글 에셋 · 테스트 CMake 의 경로 칸과 리플렉션 `AssetPath` 프로퍼티를 칸 목록(`Config/Dev/AssetReferenceFields.json`)으로 본다.
             * @details 에디터 모듈의 일이라 임포트와 같은 경로로 부릅니다(배포 엔진에는 검증 코드가 없다).
             */
            static TaskResult runAssetReferenceCheck( const CommandLineManager& commandLine )
            {
                bool bCheck = false;
                commandLine.getArgument( CommandLineArgument::CHECK_ASSET_REFS, bCheck );
                if ( bCheck == false )
                    return TaskResult::NotRequested;
                if constexpr ( build::kShipping ) // 에디터 모듈의 일이다 — 배포에는 에디터 모듈도 그 경로(`EditorModuleManager.cpp` 의 개발 전용 구역)도 없다
                {
                    SW_LOG_ERROR_ID( "App.AssetReferenceCheckNeedsEditor", "The asset reference check needs the editor module, which a Shipping build does not have - run it from a Dev build." );
                    return TaskResult::Failed;
                }
                else
                {
                    return EditorModuleManager::checkAssetReferencesWithEditorModule() ? TaskResult::Succeeded : TaskResult::Failed;
                }
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
                int32  portraitSize = 256;
                string portraitDir;
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
                const bool bBaked = PortraitBatchUtil::renderPrefabList( *pRenderThread, engine::getSubsystem<SceneManager>().getEngineServices(), portraitList, portraitDir,
                                                                         static_cast<uint32>( portraitSize > 0 ? portraitSize : 256 ) );
                return bBaked ? TaskResult::Finished : TaskResult::Failed;
            }

            /** @brief 헤드리스 작업 표입니다. 새 작업은 인자를 `ArgumentList.xxx` 에 추가하고 여기에 항목 하나를 더합니다. 에디터 모듈의 작업이면 인자를 `kArrEditorToolArgument` 에도 더한다(엔진이 헤드리스로 서는 판정). */
            static constexpr AppHeadlessTasks::TaskFunction kArrHeadlessTask[] = {
                &runCrashReporter,
                &runLocalizationTools,
                &runAssetReferenceCheck,
                &runEditorImport<EditorImportKind::Texture, CommandLineArgument::IMPORT_TEXTURES, CommandLineArgument::CHECK_TEXTURES>,
                &runEditorImport<EditorImportKind::Model, CommandLineArgument::IMPORT_MODELS, CommandLineArgument::CHECK_MODELS>,
                &runEditorImport<EditorImportKind::Heightfield, CommandLineArgument::IMPORT_HEIGHTFIELDS, CommandLineArgument::CHECK_HEIGHTFIELDS>,
            };

            /** @brief 호스트 서비스 뒤 작업 표입니다(`AppHeadlessTasks::runAfterHostServices`). 새 작업은 항목 하나를 더합니다. */
            static constexpr AppHeadlessTasks::AfterHostServicesTask kArrAfterHostServicesTask[] = {
                &runPortraitBake,
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool AppHeadlessTasks::runAll( const CommandLineManager* pCommandLine )
    {
        if ( pCommandLine == nullptr )
            return true;
        return runTable( AppHeadlessTasksInternal::kArrHeadlessTask, static_cast<uint32>( std::size( AppHeadlessTasksInternal::kArrHeadlessTask ) ), *pCommandLine );
    }

    AppHeadlessTasks::TaskResult AppHeadlessTasks::runAfterHostServices( const CommandLineManager& commandLine, Engine& hostEngine )
    {
        for ( const AfterHostServicesTask pTask : AppHeadlessTasksInternal::kArrAfterHostServicesTask )
        {
            const TaskResult result = pTask( commandLine, hostEngine );
            if ( result != TaskResult::NotRequested )
                return result;
        }
        return TaskResult::NotRequested;
    }
} // namespace sw
