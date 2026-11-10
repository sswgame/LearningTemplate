#include "pch.h"

#include "Engine/HeadlessSubsystem.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Container/unordered_set.h"
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Process/StandardOutput.h"
#include "Core/String/string_splitter.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Testing/TestRunner.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Animation/Facial/LipSync.h"
#include "Engine/Animation/Facial/LipSyncImport.h"
#include "Engine/Animation/Retarget/PoseRetargeter.h"
#include "Engine/Animation/Scene/AnimationCrowd.h"
#include "Engine/Animation/Scene/VertexAnimationCookList.h"
#include "Engine/Asset/Cook/CookStore.h"
#include "Engine/Asset/Cook/PackCooker.h"
#include "Engine/Asset/EngineDefaultAssetsSubsystem.h"
#include "Engine/Automation/AutomationRuntime.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/Pack/PackConfig.h"
#include "Engine/Content/Pack/PackPatchBuilder.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Graphics/2D/Render2DSettingsSubsystem.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIBackendInfo.h"
#include "Engine/Graphics/RHI/RHIInitResult.h"
#include "Engine/Graphics/Shader/Compile/IShaderCookService.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Shader/Compile/ShaderCookStamp.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"
#include "Engine/Input/Map/InputMapBundle.h"
#include "Engine/Localization/LocalizationDocuments.h"
#include "Engine/Module/ModuleLoadStageSubsystem.h"
#include "Engine/Navigation/SceneNavigationCooker.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionDocWriter.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Scene/SceneCooker.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Text/FontService.h"
#include "Engine/UserSettings/UserSettingsManager.h"
#include "Engine/Window/SplashSubsystem.h"

namespace sw
{
    namespace
    {
        struct HeadlessSubsystemInternal
        {
            /**
             * @brief 씬 · 프리팹 · VAT · 내비메시를 @p cookedDir 에 베이크합니다. 하나라도 실패하면 false 입니다. 모두 성공하면 이번 쿠킹이 요청하지 않은 옛 출력(사라진 소스의 것)을 스테이징에서 지웁니다.
             * @details 엔티티 상태를 바이너리로 구우려면 리플렉션이 필요하고, 그것은 엔진 안에만 있다. 형식과 규칙을 쓰는 곳이 엔진 하나다.
             *          @p bCrowdSettingsLoaded 가 거짓이면 코드 기본 프레임율로 베이크하지 않고 VAT 베이크를 실패로 센다(군중 표는 쿠킹 세션 전에 읽는다 — 작업 키의 설정이다).
             */
            [[nodiscard]] static bool cookSceneStage( const string& cookedDir, const CookStore& cookStore, const AnimationCrowdSettings& crowdSettings, bool bCrowdSettingsLoaded )
            {
                const string& resourceRoot     = engine::getSubsystem<AssetPaths>().getRootFolderPath();
                uint32        sceneFailedCount = 0;
                // 쿠킹용 씬도 런타임 씬과 같은 엔진 서비스 묶음(씬 매니저의 것 — 의존 칸에 있다)으로 짓는다.
                const SceneEngineServices& sceneServices = engine::getSubsystem<SceneManager>().getEngineServices();
                // 쿠커별 시간(`Headless.CookStageTimes`) — 씬 쿠킹 병렬화가 빌드 시간을 줄일 수 있는지의 근거다.
                Stopwatch    stageStopwatch;
                const uint32 sceneCount        = SceneCooker::cookAllScenes( sceneServices, resourceRoot, cookedDir, cookStore, sceneFailedCount );
                const int64  sceneMs           = stageStopwatch.getElapsedMilliseconds();
                uint32       prefabFailedCount = 0;
                stageStopwatch.restart();
                [[maybe_unused]] const uint32 prefabCount                = PrefabCache::cookAllPrefabs( resourceRoot, cookedDir, cookStore, prefabFailedCount );
                const int64                   prefabMs                   = stageStopwatch.getElapsedMilliseconds();
                uint32                        vertexAnimationFailedCount = bCrowdSettingsLoaded ? 0u : 1u;
                stageStopwatch.restart();
                [[maybe_unused]] const uint32 vertexAnimationCount =
                    bCrowdSettingsLoaded
                        ? VertexAnimationCookList::cookAll( resourceRoot, cookedDir, crowdSettings._vertexAnimationFramesPerSecond, cookStore, vertexAnimationFailedCount )
                        : 0u;
                const int64 vertexAnimationMs = stageStopwatch.getElapsedMilliseconds();
                // 내비메시 — 표면이 놓인 씬마다 런타임 베이크와 같은 함수로 `<씬>.navmesh` 를 쓴다.
                uint32 navMeshFailedCount = 0;
                stageStopwatch.restart();
                [[maybe_unused]] const uint32 navMeshCount = SceneNavigationCooker::cookAll( sceneServices, resourceRoot, cookedDir, cookStore, navMeshFailedCount );
                SW_LOG_INFO_ID( "Headless.CookStageTimes", "Cook stage times: scenes %# ms, prefabs %# ms, vertex animations %# ms, navmeshes %# ms", sceneMs, prefabMs,
                                vertexAnimationMs, stageStopwatch.getElapsedMilliseconds() );
                SW_LOG_INFO_ID( "Headless.CookedScenesFailuresPrefabs", "Cooked %# scenes (%# failures), %# prefabs (%# failures), %# vertex animations (%# failures), "
                                                                        "%# navmeshes (%# failures).",
                                sceneCount, sceneFailedCount, prefabCount, prefabFailedCount, vertexAnimationCount,
                                vertexAnimationFailedCount, navMeshCount, navMeshFailedCount );
                const bool bFailed = sceneCount == 0 || sceneFailedCount > 0 || prefabFailedCount > 0 || vertexAnimationFailedCount > 0 ||
                                     navMeshFailedCount > 0;
                if ( bFailed )
                    return false;
                // 사라진 소스의 옛 쿠킹본이 스테이징에 남으면 팩에 실린다 — 네 쿠커가 모두 성공한 뒤에만(돌지 않은 쿠커의 출력을 지우지 않게) 요청에 없는 출력을 치운다.
                uint32                        removeFailedCount = 0;
                [[maybe_unused]] const uint32 removedCount      = cookStore.removeUnrequestedOutputs( cookedDir, removeFailedCount );
                return removeFailedCount == 0;
            }

            /** @brief 팩에 담을 셰이더 바이너리 포맷입니다 — 명령줄 백엔드(`-dx12` …), 없으면 엔진 설정의 `_defaultRHI` 입니다. */
            static ShaderTargetFormat resolveShaderTarget( const CommandLineManager& commandLine )
            {
                RHIBackend backend = engine::getSubsystem<EngineConfig>()._window._defaultRHI;
                RHIBackend commandLineBackend{};
                if ( RHI::findCommandLineBackend( commandLine, commandLineBackend ) )
                    backend = commandLineBackend;
                return RHI::getShaderTargetFormat( backend );
            }

            /** @brief 셰이더 쿠킹 창구입니다(렌더러). 렌더러를 짓지 않는 구성(전용 서버)에서 셰이더 작업을 청했으면 오류를 남기고 nullptr 입니다. */
            static IShaderCookService* requireShaderCookService( const utf8* pTask )
            {
                IShaderCookService* const pService = IShaderCookService::find();
                if ( pService == nullptr )
                    SW_LOG_ERROR_ID( "Headless.ShaderCookerNotBuilt", "%#: this build has no shader cooker (the renderer is not built - dedicated server target)", pTask );
                return pService;
            }

            /**
             * @brief 팩에 담을 셰이더를 쿠킹 스테이징에 굽고(바뀐 것만 — 내용 해시) 소스와 맞는지 판정합니다. 맞지 않으면 false 입니다.
             * @details 쿠킹본은 소스 트리가 아니라 스테이징에만 있으므로 팩을 구울 때 늘 굽는다 — 굽지 않은 채 팩을 만들면 셰이더 없는 팩이 된다.
             *          `CookSession` 을 열기 **전에** 부른다 — 셰이더 쿠커는 엔진 기본 에셋 같은 리소스를 `CookJob` 밖에서 읽는데, 세션 안에서는 그 읽기가 실패한다.
             *          쿠킹 요청이 만드는 바이너리 목록도 같은 까닭으로 여기서 모아 @p outSetRequestedBinary 로 돌려준다(팩 단계는 받기만 한다).
             */
            [[nodiscard]] static bool cookShadersForPack( const CommandLineManager& commandLine, string_view cookedDir, unordered_set<string>& outSetRequestedBinary )
            {
                // 셰이더 쿠커는 렌더러에 있다 — 렌더러를 짓지 않는 구성(전용 서버)의 팩에는 셰이더 바이너리가 없어 할 일이 없다.
                IShaderCookService* const pService = IShaderCookService::find();
                if ( pService == nullptr )
                    return true;
                const ShaderTargetFormat shaderTarget = resolveShaderTarget( commandLine );
                if ( pService->cookAllShaders( cookedDir, shaderTarget ) == false || pService->verifyCookedShaders( cookedDir, shaderTarget ) == false )
                {
                    SW_LOG_ERROR_ID( "Headless.CookedShadersDoNot", "Cooked shaders do not match their sources - a Shipping build has no runtime compiler, so packing stops" );
                    return false;
                }
                // 쿠킹 요청이 만드는 바이너리 — 팩이 그 가운데 원본이 닿지 않아 버리는 것을 경고로 알린다(배포 굽기에서도 보이게).
                vector<string> listRequestedBinary;
                if ( pService->collectRequestedBinaries( shaderTarget, listRequestedBinary ) == false )
                {
                    SW_LOG_ERROR_ID( "Headless.PackCookShaderRequests", "Pack cook: the shader cook requests could not be collected - cannot tell which dropped shader binaries the runtime needs" );
                    return false;
                }
                for ( string& binary : listRequestedBinary )
                    outSetRequestedBinary.insert( std::move( binary ) );
                return true;
            }

            /**
             * @brief 팩을 베이크합니다. 셰이더 굽기 · 판정은 세션 앞의 `cookShadersForPack` 이 끝냈다.
             * @details 빌드 타깃은 쿠킹하는 호스트다(`Engine::initialize` 의 `setHostTarget` 과 같은 규칙) — 전용 서버(`Server`)가 굽은 팩은 서버가 읽지 않는 종류를 뺀다.
             *          `-game=` 을 주면 그 게임 팩만 베이크한다(배포 패키지).
             */
            [[nodiscard]] static bool cookPackStage( const CommandLineManager& commandLine, bool bDedicatedServer, const string& cookedDir, const CookStore& cookStore,
                                                     unordered_set<string>&& setRequestedShaderBinary )
            {
                const string shaderFolder = string( ShaderCooker::getSubfolderForFormat( resolveShaderTarget( commandLine ) ) );

                PackConfig   packConfig;
                const string packConfigPath = FileUtil::joinPath( ProcessRoots::get()._project.str(), PackConfig::kConfigPath );
                if ( ConfigManager::readConfigFile( packConfig, packConfigPath ) != ConfigReadResult::Loaded )
                {
                    SW_LOG_ERROR_ID( "Headless.PackConfigMissingInvalid", "Pack config '%#' is missing or invalid", packConfigPath );
                    return false;
                }

                PackCookParams params;
                params._projectRoot  = ProcessRoots::get()._project.str(); // 참조 칸 표(`Config/Dev/AssetReferenceFields*.json`) — 쓰는 것만 담는 고르기가 읽는다
                params._resourceRoot = engine::getSubsystem<AssetPaths>().getRootFolderPath();
                params._cookedDir    = cookedDir;
                (void)commandLine.getArgument( CommandLineArgument::PACK_DIR, params._packOutputDir ); // 비면 cookAllPacks 가 오류로 멈춘다
                params._shaderFolder = shaderFolder;
                params._buildTarget  = bDedicatedServer ? "Server" : "Client";
                // 세션 앞의 `cookShadersForPack` 이 모은 요청 바이너리. 셰이더 쿠커가 없는 구성(전용 서버)은 빈 목록이다.
                params._setRequestedShaderBinary = std::move( setRequestedShaderBinary );
                // 실행기가 없는 구성(Shipping · 전용 서버)은 실행기가 스스로 등록하지 않는다 — 그 구성이 굽는 팩에는 시나리오를 담지 않는다.
                // 배포 대상의 팩(`-pack-for-shipping`)도 담지 않는다 — 교차 빌드(Android · 웹)는 호스트 개발 App(실행기가 있다)이 굽는다.
                const bool bShippingPack  = commandLine.isArgumentProvided( CommandLineArgument::PACK_FOR_SHIPPING );
                const bool bHasAutomation = engine::findSubsystem<IAutomationRuntime>() != nullptr;
                params._automationContent = bHasAutomation && bShippingPack == false ? PackAutomationContent::Included : PackAutomationContent::Excluded;
                // 출시 문화권(`stagedCultures`) 밖의 번역 테이블은 팩에 넣지 않는다 — 규칙은 로컬라이제이션 층이 갖고 쿠커는 목록만 받는다.
                if ( const expected<void> staged = LocalizationProject::collectUnstagedTranslationTables( params._resourceRoot, params._listExcludedSourceFile ); staged.has_value() == false )
                {
                    SW_LOG_ERROR_ID( "Headless.PackCookLocalizationProject", "Pack cook: a localization project could not be read for culture staging - %# (%#)", staged.error().message(),
                                     staged.error().detail() );
                    return false;
                }
                if ( commandLine.isArgumentProvided( CommandLineArgument::GAME ) )
                {
                    // 팩 폴더 이름은 게임 이름과 다를 수 있어(ThemeParkTycoon → themepark) 게임 설정의 팩 루트(`game/<폴더>`)에서 읽는다.
                    const string packRoot  = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( engine::getSubsystem<GameConfig>()._packRoot.get() ) );
                    params._gamePackFolder = FileUtil::getFileNamePart( packRoot );
                }
                uint32 packCount = 0;
                if ( PackCooker::cookAllPacks( packConfig, params, cookStore, packCount ) == false )
                    return false;
                // 설치된 빌드의 출처 — 시작 마운트가 패치를 적용할 때 이 빌드부터 이어지는 사슬을 찾는다. 빌드 번호를 안 주면 패치를 쓰지 않는 빌드이며, 기록을 남기지 않는 대신
                // `patching="none"` 을 명시한 기록을 남긴다(기록이 없는 폴더와 구별되고, 그 폴더에 패치 목록이 있으면 시작 마운트가 오류로 멈춘다).
                string build;
                (void)commandLine.getArgument( CommandLineArgument::PACK_BUILD, build );
                if ( build.empty() )
                    return PackPatchBuilder::writeNoPatchingRecord( FileUtil::joinPath( params._packOutputDir, PackBuildRecord::kFileName ) );
                return PackPatchBuilder::writeBuildRecord( params._packOutputDir, build, FileUtil::joinPath( params._packOutputDir, PackBuildRecord::kFileName ) );
            }

            /**
             * @brief 새 빌드의 팩(`-pack-dir`)과 이전 빌드의 내용 목록(`-patch-base`)으로 패치를 `-patch-dir` 에 만듭니다. 패치 팩의 압축은 팩 쿠킹 설정과 같습니다.
             */
            [[nodiscard]] static bool makePatchStage( const CommandLineManager& commandLine )
            {
                PackPatchBuildParams params;
                (void)commandLine.getArgument( CommandLineArgument::PACK_DIR, params._packsDirectory ); // 비면 makePatch 가 오류로 멈춘다
                (void)commandLine.getArgument( CommandLineArgument::PATCH_BASE, params._previousRecordPath );
                (void)commandLine.getArgument( CommandLineArgument::PACK_BUILD, params._newBuild );
                (void)commandLine.getArgument( CommandLineArgument::PATCH_DIR, params._outputDirectory );
                // 서명 — 개인 키는 저장소 밖 파일(`-patch-sign-key`)에서만 읽는다. 키를 줬는데 읽지 못하거나 제공자가 없으면 서명 없는 패치로 대신 만들지 않고 멈춘다.
                // 제공자는 GameFramework 가 프로세스 범위에 공개한 모듈 서브시스템의 멤버다(`OpenSSLNetSecuritySubsystem` 이 든 `OpenSSLNetSecurityProvider`).
                string signKeyPath;
                (void)commandLine.getArgument( CommandLineArgument::PATCH_SIGN_KEY, signKeyPath );
                if ( signKeyPath.empty() == false )
                {
                    params._signer._pProvider = engine::findSubsystem<INetSecurityProvider>();
                    if ( params._signer._pProvider == nullptr )
                    {
                        SW_LOG_ERROR_ID( "Headless.PatchSignKeyGiven", "-patch-sign-key was given but no INetSecurityProvider is published (GameFramework is not loaded) - no patch is written" );
                        return false;
                    }
                    if ( FileUtil::readTextFile( signKeyPath, params._signer._privateKeyPem ) == false || params._signer._privateKeyPem.empty() )
                    {
                        SW_LOG_ERROR_ID( "Headless.PatchSignKeyFile", "-patch-sign-key file '%#' could not be read or is empty - no patch is written", signKeyPath );
                        return false;
                    }
                }
                PackConfig   packConfig;
                const string packConfigPath = FileUtil::joinPath( ProcessRoots::get()._project.str(), PackConfig::kConfigPath );
                if ( ConfigManager::readConfigFile( packConfig, packConfigPath ) != ConfigReadResult::Loaded )
                {
                    SW_LOG_ERROR_ID( "Headless.PackConfigMissingInvalid", "Pack config '%#' is missing or invalid", packConfigPath );
                    return false;
                }
                params._writerDesc._compression      = packConfig._compression._codec;
                params._writerDesc._compressionLevel = packConfig._compression._level;
                return PackPatchBuilder::makePatch( params );
            }

            /**
             * @brief 전용 서버가 하는 헤드리스 작업은 씬 · 팩 쿠킹(서버 팩)까지다. 셰이더 쿠킹(DXC) · 판정 · 원본 임포트(에디터 모듈) · 립싱크 임포트(음성 디코드 — 서버는 오디오를 짓지 않는다)는
             *        App 의 일이다 — 조용히 넘기지 않고 실패한다.
             * @return 서버에 주어진 클라이언트 전용 작업이 있어 실패로 막았으면 true 입니다.
             */
            [[nodiscard]] static bool rejectClientOnlyTask( CommandLineManager& commandLine, bool bDedicatedServer, HeadlessTaskState& inoutState )
            {
                if ( bDedicatedServer == false )
                    return false;
                static constexpr CommandLineArgument kArrClientOnlyTask[] = {
                    CommandLineArgument::COOK_SHADERS,
                    CommandLineArgument::VERIFY_SHADERS,
                    CommandLineArgument::IMPORT_TEXTURES,
                    CommandLineArgument::CHECK_TEXTURES,
                    CommandLineArgument::COOK_TEXTURE_VARIANTS,
                    CommandLineArgument::CHECK_TEXTURE_VARIANTS,
                    CommandLineArgument::IMPORT_MODELS,
                    CommandLineArgument::CHECK_MODELS,
                    CommandLineArgument::IMPORT_HEIGHTFIELDS,
                    CommandLineArgument::CHECK_HEIGHTFIELDS,
                    CommandLineArgument::IMPORT_LIPSYNC,
                };
                for ( const CommandLineArgument argument : kArrClientOnlyTask )
                {
                    bool bGiven = false;
                    if ( commandLine.getArgument( argument, bGiven ) && bGiven )
                    {
                        SW_LOG_ERROR_ID( "Headless.HeadlessTaskRunsClient", "This headless task runs in the client build (App), not in the dedicated server" );
                        inoutState.enterHeadless();
                        inoutState._bFailed = SW_TRUE;
                        return true;
                    }
                }
                return false;
            }

            /** @brief `-help` — 받는 인자 목록을 표준 출력에 찍고 끝낸다(창 · 렌더러를 세우지 않는다). 작업을 돌렸으면 true 입니다. */
            [[nodiscard]] static bool runHelpTask( CommandLineManager& commandLine, HeadlessTaskState& inoutState )
            {
                bool bHelp = false;
                if ( commandLine.getArgument( CommandLineArgument::HELP, bHelp ) == false || bHelp == false )
                    return false;
                inoutState.enterHeadless();
                // 로거를 거치지 않는다 — 도움말은 로그 줄(시각 · 범주 머리)이 아니라 그대로 읽을 글이고, 로그 수준을 낮춘 실행에서도 나와야 한다.
                if ( StandardOutput::write( commandLine.makeUsageText() ) == false )
                {
                    SW_LOG_ERROR_ID( "Headless.CouldNotWriteHelp", "Could not write the -help text to standard output" );
                    inoutState._bFailed = SW_TRUE;
                }
                return true;
            }

            /**
             * @brief `--test` — 엔진 안 단위 테스트(`<기능>/Tests/` 의 `SW_TEST_CASE`)를 돌린다. 작업을 돌렸으면 true 입니다.
             * @details 모든 모듈이 올라온 뒤(ModuleLoadStage)라 엔진 · GameFramework · 키트 · 게임의 케이스가 다 등록돼 있고, 엔진 서비스가 서 있다.
             *          창 · RHI 는 세우지 않는다 — 그림이 필요한 확인은 시나리오다.
             */
            [[nodiscard]] static bool runTestTask( CommandLineManager& commandLine, HeadlessTaskState& inoutState )
            {
#if !defined( SW_SHIPPING )
                bool bTest = false;
                if ( commandLine.getArgument( CommandLineArgument::TEST, bTest ) == false || bTest == false )
                    return false;
                inoutState.enterHeadless();
                test::TestRunOptions options;
                int32                seed        = 0;
                int32                repeatCount = 1;
                (void)commandLine.getArgument( CommandLineArgument::TEST_FILTER, options._filter );
                (void)commandLine.getArgument( CommandLineArgument::TEST_SOURCE, options._sourceFolder );
                (void)commandLine.getArgument( CommandLineArgument::TEST_SEED, seed );
                (void)commandLine.getArgument( CommandLineArgument::TEST_REPEAT, repeatCount );
                (void)commandLine.getArgument( CommandLineArgument::TEST_REPORT, options._reportPath );
                (void)commandLine.getArgument( CommandLineArgument::UPDATE_SNAPSHOTS, options._bUpdateSnapshots );
                (void)commandLine.getArgument( CommandLineArgument::TEST_RUN_DISABLED, options._bRunDisabled );
                (void)commandLine.getArgument( CommandLineArgument::TEST_CHILD_ROLE, options._childRole );
                (void)commandLine.getArgument( CommandLineArgument::TEST_CHILD_ARG, options._childArgument );
                (void)commandLine.getArgument( CommandLineArgument::TEST_ISOLATED_CASE, options._isolatedCase );
                string shard{ "0/1" }; // 주지 않으면 몫 하나(전부)
                (void)commandLine.getArgument( CommandLineArgument::TEST_SHARD, shard );
                if ( test::TestRunner::parseShard( shard, options._shardIndex, options._shardCount ) == false )
                {
                    SW_LOG_ERROR_ID( "Headless.TestShardNotI", "--test-shard=%# is not i/n (0 <= i < n)", shard.c_str() );
                    inoutState._bFailed = SW_TRUE;
                    return true;
                }
                options._seed          = static_cast<uint32>( seed );
                options._repeatCount   = repeatCount > 0 ? static_cast<uint32>( repeatCount ) : 1u;
                options._pCrashHandler = &engine::getSubsystem<CrashHandler>();
                inoutState._bFailed    = test::TestRunner::run( options ) == false;
                // 이 실행의 "전부 통과" 를 에디터까지 통과한 것으로 읽지 않게 한다 - 에디터 모듈은 이 단계에 아직 올라오지 않았다.
                SW_LOG_INFO_ID( "Headless.EditorCasesRunElsewhere", "Editor cases are not in this run - they run inside the editor (self test editor.unitTests, ctest Scenario.editor)" );
                return true;
#else
                (void)commandLine;
                (void)inoutState;
                return false;
#endif
            }

            /** @brief 셰이더 쿠킹은 렌더러의 일이다(`IShaderCookService`) — 렌더러를 짓지 않는 구성은 쿠커가 없다는 오류로 멈춘다. 작업을 돌렸으면 true 입니다. */
            [[nodiscard]] static bool runCookShadersTask( CommandLineManager& commandLine, HeadlessTaskState& inoutState )
            {
                bool bCookShaders = false;
                if ( commandLine.getArgument( CommandLineArgument::COOK_SHADERS, bCookShaders ) == false || bCookShaders == false )
                    return false;
                inoutState.enterHeadless();
                string cookedDir;
                commandLine.getArgument( CommandLineArgument::COOKED_DIR, cookedDir );
                SW_LOG_INFO_ID( "Headless.StartingHeadlessCookShaders", "Starting Headless (CookShaders) -> '%#'...", cookedDir );
                // 명령줄 백엔드(`-vulkan` …)를 주면 그 형식만, 주지 않으면 네 형식을 모두 굽는다(`ShaderTargetFormat::Count`).
                ShaderTargetFormat shaderTarget = ShaderTargetFormat::Count;
                RHIBackend         commandLineBackend{};
                if ( RHI::findCommandLineBackend( commandLine, commandLineBackend ) )
                    shaderTarget = RHI::getShaderTargetFormat( commandLineBackend );
                IShaderCookService* const pService = requireShaderCookService( "--cook-shaders" );
                inoutState._bFailed                = pService == nullptr || pService->cookAllShaders( cookedDir, shaderTarget ) == false; // 비면 쿠커가 오류로 멈춘다
                return true;
            }

            /** @brief 리플렉션 문서도 같은 자리다 — 모든 타입 공급자가 등록을 끝낸 뒤(ModuleLoadStage)라 게임 · 킷의 타입까지 든다. */
            [[nodiscard]] static bool runReflectionDocsTask( CommandLineManager& commandLine, HeadlessTaskState& inoutState )
            {
                string reflectionDocsDir;
                if ( commandLine.getArgument( CommandLineArgument::WRITE_REFLECTION_DOCS, reflectionDocsDir ) == false || reflectionDocsDir.empty() )
                    return false;
                inoutState.enterHeadless();
                inoutState._bFailed = ReflectionDocWriter::writeMarkdown( engine::getSubsystem<TypeRegistry>(), reflectionDocsDir ) == 0;
                return true;
            }

            /** @brief 리타깃 베이크(`--bake-retarget=<프로필>,<원본 클립>,<출력 클립>`) — 클립 · 스켈레톤 · 코덱이 모두 엔진 것이라 여기서 한다. */
            [[nodiscard]] static bool runBakeRetargetTask( CommandLineManager& commandLine, HeadlessTaskState& inoutState )
            {
                string bakeRetarget;
                if ( commandLine.getArgument( CommandLineArgument::BAKE_RETARGET, bakeRetarget ) == false || bakeRetarget.empty() )
                    return false;
                inoutState.enterHeadless();
                const string_splitter      parts( bakeRetarget, { "," } );
                const vector<string_view>& listArgument = parts.getSplitList();
                const bool                 bWellFormed  = listArgument.size() == 3;
                if ( bWellFormed == false )
                    SW_LOG_ERROR_ID( "Headless.BakeRetargetExpectsProfile", "--bake-retarget expects <profile>,<source clip>,<output clip>" );
                inoutState._bFailed = bWellFormed == false ||
                                      PoseRetargeter::bakeClipFile( AssetID{ StringUtil::trim( listArgument[0] ) }, AssetID{ StringUtil::trim( listArgument[1] ) }, StringUtil::trim( listArgument[2] ) ) == false;
                return true;
            }

            /** @brief 쿠킹 인자(씬 · 팩 · 전부) 중 하나라도 주어졌는지입니다. */
            static bool isAnyCookRequested( CommandLineManager& commandLine )
            {
                bool bCookScenes = false;
                bool bCookPacks  = false;
                bool bCookAll    = false;
                commandLine.getArgument( CommandLineArgument::COOK_SCENES, bCookScenes );
                commandLine.getArgument( CommandLineArgument::COOK_PACKS, bCookPacks );
                commandLine.getArgument( CommandLineArgument::COOK_ALL, bCookAll );
                return bCookScenes || bCookPacks || bCookAll;
            }

            /** @brief 셰이더 판정만 — 쿠킹 스테이징(`-cooked-dir`)의 쿠킹 표 모든 백엔드를 본다. 고치지 않는다. 쿠킹과 함께 주어지면 쿠킹이 먼저다. */
            [[nodiscard]] static bool runVerifyShadersTask( CommandLineManager& commandLine, HeadlessTaskState& inoutState )
            {
                bool bVerifyShaders = false;
                commandLine.getArgument( CommandLineArgument::VERIFY_SHADERS, bVerifyShaders );
                if ( bVerifyShaders == false || isAnyCookRequested( commandLine ) )
                    return false;
                inoutState.enterHeadless();
                string cookedDir;
                commandLine.getArgument( CommandLineArgument::COOKED_DIR, cookedDir );
                // 굽기와 같은 규칙 — 명령줄 백엔드(`-dx12` …)를 주면 그 형식만, 주지 않으면 네 형식 모두를 본다(그 형식만 구운 폴더를 다른 형식으로 판정하지 않는다).
                RHIBackend                commandLineBackend{};
                const bool                bOneBackend = RHI::findCommandLineBackend( commandLine, commandLineBackend );
                IShaderCookService* const pService    = requireShaderCookService( "--verify-shaders" );
                bool                      bAllCurrent = pService != nullptr;
                for ( const RHIBackendInfo& info : RHIBackendInfoTable::kArrInfo )
                {
                    if ( bOneBackend && info._shaderTarget != RHI::getShaderTargetFormat( commandLineBackend ) )
                        continue;
                    bAllCurrent = pService != nullptr && pService->verifyCookedShaders( cookedDir, info._shaderTarget ) && bAllCurrent;
                }
                if ( bAllCurrent )
                    SW_LOG_INFO_ID( "Headless.CookedShaderBinariesMatch", "Cooked shader binaries in '%#' match their sources for %#", cookedDir, bOneBackend ? "the command-line backend" : "every backend" );
                inoutState._bFailed = bAllCurrent == false;
                return true;
            }

            /** @brief 패치 만들기(`-make-patch`) — 베이크한 팩끼리만 보므로 씬 쿠킹도 에셋 로드도 필요 없다. 로그와 명령줄이 있는 이 단계에서 한다. */
            [[nodiscard]] static bool runMakePatchTask( CommandLineManager& commandLine, HeadlessTaskState& inoutState )
            {
                bool bMakePatch = false;
                if ( commandLine.getArgument( CommandLineArgument::MAKE_PATCH, bMakePatch ) == false || bMakePatch == false )
                    return false;
                inoutState.enterHeadless();
                SW_LOG_INFO_ID( "Headless.StartingHeadlessMakePatch", "Starting Headless (MakePatch)..." );
                inoutState._bFailed = makePatchStage( commandLine ) == false;
                return true;
            }

            /** @brief 씬 · 팩 쿠킹도 같은 자리다. 엔티티 상태를 바이너리로 구우려면 리플렉션이 필요하고 그것은 엔진 안에만 있다. 팩은 그 산출물을 함께 담는다. */
            [[nodiscard]] static bool runCookTask( CommandLineManager& commandLine, bool bDedicatedServer, HeadlessTaskState& inoutState )
            {
                bool bCookScenes = false;
                bool bCookPacks  = false;
                bool bCookAll    = false;
                commandLine.getArgument( CommandLineArgument::COOK_SCENES, bCookScenes );
                commandLine.getArgument( CommandLineArgument::COOK_PACKS, bCookPacks );
                commandLine.getArgument( CommandLineArgument::COOK_ALL, bCookAll );
                const bool bAnyCook = bCookScenes || bCookPacks || bCookAll;
                if ( bAnyCook == false )
                    return false;
                inoutState.enterHeadless();
                string cookedDir;
                commandLine.getArgument( CommandLineArgument::COOKED_DIR, cookedDir );
                if ( cookedDir.empty() )
                {
                    SW_LOG_ERROR_ID( "Headless.CookingNeedsCookedDir", "Cooking needs -cooked-dir=<folder> (the staging folder of cooked scenes and prefabs)" );
                    inoutState._bFailed = SW_TRUE;
                    return true;
                }
                SW_LOG_INFO_ID( "Headless.StartingHeadlessCook", "Starting Headless (Cook%#) -> '%#'...", bCookAll ? "All" : ( bCookPacks ? "Packs" : "Scenes" ), cookedDir );
                AnimationCrowdSettings crowdSettings{};
                const bool             bCrowdSettingsLoaded = crowdSettings.loadFromAsset( AssetID{ engine::getSubsystem<EngineDefaultAssets>()._animationCrowdSettings.get() } );
                const CookStore        cookStore            = CookStore::makeLocal();
                // 입력 맵 묶음은 게임 시작의 검사와 같은 길로 먼저 본다 — 겹친 묶음은 배포본이 뜨지 않으므로 구울 까닭이 없다. 쿠킹 세션 밖이다(출력의 입력이 아니다).
                if ( InputMapBundle::checkPack( engine::getSubsystem<GameConfig>()._packRoot.get() ) == false )
                {
                    SW_LOG_ERROR_ID( "Headless.InputMapBundleRejected", "Cooking stopped: the input map bundle of the active game pack was rejected (listed above)" );
                    inoutState._bFailed = SW_TRUE;
                    return true;
                }
                // 셰이더 굽기 · 판정은 세션 밖에서(`cookShadersForPack` 설명). 씬 단계보다 먼저 하므로 깨진 셰이더로 씬을 굽는 시간을 쓰지 않는다.
                unordered_set<string> setRequestedShaderBinary;
                if ( bCookPacks || bCookAll )
                {
                    if ( cookShadersForPack( commandLine, cookedDir, setRequestedShaderBinary ) == false )
                    {
                        inoutState._bFailed = SW_TRUE;
                        return true;
                    }
                }
                // 세션 안에서는 작업(`CookJob`) 밖의 리소스 읽기가 실패한다 — 쿠커가 읽은 것이 모두 그 출력의 입력으로 기록된다.
                const CookSession cookSession{ engine::getSubsystem<AssetPaths>() };
                bool              bSucceeded = true;
                if ( bCookScenes || bCookAll )
                    bSucceeded = cookSceneStage( cookedDir, cookStore, crowdSettings, bCrowdSettingsLoaded );
                // 씬 단계가 실패하면 팩을 베이크하지 않는다 — 빠진 산출물로 베이크한 팩은 배포본에서야 실패가 드러난다.
                if ( bSucceeded && ( bCookPacks || bCookAll ) )
                    bSucceeded = cookPackStage( commandLine, bDedicatedServer, cookedDir, cookStore, std::move( setRequestedShaderBinary ) );
                inoutState._bFailed = bSucceeded == false;
                return true;
            }

            /** @brief 립싱크 분석 — 음성마다 비즘 트랙을 곁에 쓴다. 분석 표가 엔진 데이터라 엔진이 한다. */
            [[nodiscard]] static bool runImportLipSyncTask( CommandLineManager& commandLine, HeadlessTaskState& inoutState )
            {
                bool bImportLipSync = false;
                if ( commandLine.getArgument( CommandLineArgument::IMPORT_LIPSYNC, bImportLipSync ) == false || bImportLipSync == false )
                    return false;
                inoutState.enterHeadless();
                SW_LOG_INFO_ID( "Headless.StartingHeadlessImportLipSync", "Starting Headless (ImportLipSync)..." );
                // 분석 표는 런타임 얼굴과 같은 공유 캐시의 한 벌이다.
                const shared_ptr<const LipSyncSettings> pLipSyncSettings   = SharedAssetCache<LipSyncSettings>::acquire( engine::getSubsystem<EngineDefaultAssets>()._lipSyncSettings.get() );
                uint32                                  lipSyncFailedCount = 0;
                [[maybe_unused]] uint32                 lipSyncCount       = 0; // 로그로만 쓴다(Shipping 은 빠진다)
                if ( pLipSyncSettings != nullptr )
                    lipSyncCount = LipSyncImport::importAll( engine::getSubsystem<AssetPaths>().getRootFolderPath(), *pLipSyncSettings, lipSyncFailedCount );
                else
                    ++lipSyncFailedCount;
                SW_LOG_INFO_ID( "Headless.ImportedVisemeTracksFailures", "Imported %# viseme tracks (%# failures).", lipSyncCount, lipSyncFailedCount );
                inoutState._bFailed = lipSyncFailedCount > 0;
                return true;
            }

            /**
             * @brief 로컬라이제이션 도구(글 수집 · PO 교환)와 원본 임포트 · 대조(텍스처 · 모델 · 높이장)는 에디터 모듈이 한다(엔진은 에디터를 모른다).
             *        여기서는 창 · RHI 없이 세우기만 하고, 모듈을 올려 부르는 것은 App 이다(`EditorModuleManager::runLocalizationWithEditorModule` ·
             *        `importAssetsWithEditorModule`). 이 단계는 타입 공급자 모듈 뒤라 글 수집이 리플렉션 프로퍼티(`Meta = "Localizable"`)를 본다.
             * @return 그런 인자가 주어져 헤드리스로 세웠으면 true 입니다.
             */
            [[nodiscard]] static bool enterEditorToolTask( CommandLineManager& commandLine, HeadlessTaskState& inoutState )
            {
                bool bAnyEditorTool = false;
                for ( const CommandLineArgument argument : kArrEditorToolArgument )
                {
                    bAnyEditorTool = bAnyEditorTool || commandLine.isArgumentProvided( argument );
                }
                if ( bAnyEditorTool == false )
                    return false;
                inoutState.enterHeadless();
                SW_LOG_INFO_ID( "Headless.StartingHeadlessEditorTools", "Starting Headless (editor tools: localization or asset import/check)..." );
                return true;
            }
        };
    } // namespace

    HeadlessTaskState::HeadlessTaskState()
        : _bFailed{ SW_FALSE }
        , _failureExitCode{ -1 }
        , _bHeadless{ SW_FALSE }
    {
    }

} // namespace sw

namespace sw
{
    HeadlessSubsystem::HeadlessSubsystem( bool bDedicatedServer, HeadlessTaskState& outState )
        : _state{ outState }
        , _bDedicatedServer{ bDedicatedServer }
    {
    }

    SubsystemInitResult HeadlessSubsystem::initializeSubsystem()
    {
        using Internal                  = HeadlessSubsystemInternal;
        CommandLineManager& commandLine = engine::getSubsystem<CommandLineManager>();
        // 헤드리스 작업은 `enterHeadless` 로 들어가며 그것이 작업 직전에 누수 기준선을 잡는다(`HeadlessTaskState`). 작업 하나가 돌았으면 뒤 작업과 이 단계의 나머지는 건너뛴다.
        // 작업 순서가 곧 우선순위다: 도움말 → 단위 테스트 → 백엔드마다 시나리오 실행 → 서버의 클라이언트 전용 작업 거절 → 셰이더 쿠킹 → 리플렉션 문서 → 리타깃 베이크 → 셰이더 판정 → 패치 → 쿠킹 → 립싱크 → 에디터 도구.
        const bool bTaskRan = Internal::runHelpTask( commandLine, _state ) || Internal::runTestTask( commandLine, _state ) ||
                              runScenarioPerBackendTask( commandLine, _state ) ||
                              Internal::rejectClientOnlyTask( commandLine, _bDedicatedServer, _state ) ||
                              Internal::runCookShadersTask( commandLine, _state ) ||
                              Internal::runReflectionDocsTask( commandLine, _state ) || Internal::runBakeRetargetTask( commandLine, _state ) ||
                              Internal::runVerifyShadersTask( commandLine, _state ) || Internal::runMakePatchTask( commandLine, _state ) ||
                              Internal::runCookTask( commandLine, _bDedicatedServer, _state ) || Internal::runImportLipSyncTask( commandLine, _state ) ||
                              Internal::enterEditorToolTask( commandLine, _state );
        return bTaskRan ? SubsystemInitResult::SkipDependents : SubsystemInitResult::Succeeded;
    }

    bool HeadlessSubsystem::runScenarioPerBackendTask( CommandLineManager& commandLine, HeadlessTaskState& inoutState )
    {
        // `-scenario-backends` 가 없으면 보통 시나리오 실행(이 프로세스가 직접 돈다)이다.
        string backends;
        if ( commandLine.getArgument( CommandLineArgument::SCENARIO_BACKENDS, backends ) == false || backends.empty() )
            return false;
        IAutomationRuntime* const pAutomation = engine::findSubsystem<IAutomationRuntime>();
        if ( pAutomation == nullptr )
        {
            // 백엔드마다 실행을 시킨 실행은 실행기 없이 결과를 낼 수 없다 — 이유를 남기고 실패로 끝낸다(폴백 없음).
            inoutState.enterHeadless();
            SW_LOG_ERROR_ID( "Headless.ScenarioBackendsNeedsBuild", "-scenario-backends needs a development build (Debug or Release) - Shipping has no automation runner" );
            inoutState._bFailed = SW_TRUE;
            return true;
        }
        pAutomation->runPerBackend( backends, commandLine, inoutState );
        return true;
    }

    span<const utf8* const> HeadlessSubsystem::getSubsystemDependencies() const
    {
        // 2D 렌더 설정은 씬 쿠킹의 컴포넌트 검증(정렬 레이어 이름)이 읽는다.
        static constexpr const utf8* kArrDependency[] = { EngineDefaultAssetsSubsystem::kSubsystemName, ModuleLoadStageSubsystem::kSubsystemName, Render2DSettingsSubsystem::kSubsystemName,
                                                          SceneManager::kSubsystemName, ShaderCache::kSubsystemName };
        return kArrDependency;
    }

    span<const utf8* const> HeadlessSubsystem::getSubsystemDependents() const
    {
        // 스플래시도 막는다 — 창을 띄우지 않는 작업(단위 테스트 · 쿠킹 · 백엔드마다 시나리오를 자식으로 돌리는 조정 프로세스)에 스플래시가 서면 작업 내내 갱신되지 않은 채 떠 있었다.
        static constexpr const utf8* kArrDependent[] = { FontService::kSubsystemName, kRHISubsystemName, UserSettingsManager::kSubsystemName, SplashSubsystem::kSubsystemName };
        return kArrDependent;
    }
} // namespace sw

namespace sw
{
    // 헤드리스 작업(셰이더 · 씬 쿠킹, 임포트)이면 그에 기대는 단계(창 · RHI · 렌더러)를 건너뛴다 — 결과는 호스트가 준 상태에 적는다.
    SW_ENGINE_SUBSYSTEM( HeadlessSubsystem, Startup, (), []( engine::EngineSubsystemFactoryScope& scope, engine::EngineSubsystemBuild& build )
    {
        const engine::StartupSetup& setup = *build._pStartupSetup;
        (void)scope.emplaceSubsystem<HeadlessSubsystem>( setup._bDedicatedServer, setup._headlessState );
    } );
} // namespace sw
