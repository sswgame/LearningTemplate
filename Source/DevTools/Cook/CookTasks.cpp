#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Log/Logger.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Time/MonotonicClock.h"

#include "DevTools/Cook/CookReflectionFingerprint.h"
#include "DevTools/Cook/CookStore.h"
#include "DevTools/Cook/IShaderCookService.h"
#include "DevTools/Cook/LocalizationCooker.h"
#include "DevTools/Cook/PackCooker.h"
#include "DevTools/Cook/PackPatchBuilder.h"
#include "DevTools/Cook/PrefabCooker.h"
#include "DevTools/Cook/SceneCooker.h"
#include "DevTools/Cook/SceneNavigationCooker.h"
#include "DevTools/Cook/VertexAnimationCooker.h"

#include "Engine/Animation/Scene/AnimationCrowd.h"
#include "Engine/Asset/AssetDatabase.h"
#include "Engine/Automation/AutomationRuntime.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Config/PackStorageSettings.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/ContentHash.h"
#include "Engine/Content/Pack/PackConfig.h"
#include "Engine/Content/Pack/PackPatch.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/HeadlessTask.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIBackendInfo.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"
#include "Engine/Graphics/Shader/Compile/ShaderRequestLog.h"
#include "Engine/Input/Map/InputMapBundle.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Inherit/ResourceTags.h"

namespace sw
{
    namespace
    {
        /** @brief 쿠킹 명령줄(`--cook-*` · `--verify-shaders` · `-make-patch`)의 헤드리스 작업입니다. 배포 구성은 DevTools 를 짓지 않아 굽지 않는다 — 배포 팩은 호스트 개발 빌드가 굽는다(cmake/Modules/Build/AssetAndToolTargets.cmake). */
        struct CookTasksInternal
        {
            /** @brief bool 인자 @p argument 가 켜져 주어졌으면 true 입니다. */
            static bool isFlagGiven( const CommandLineManager& commandLine, CommandLineArgument argument )
            {
                bool bGiven = false;
                return commandLine.getArgument( argument, bGiven ) && bGiven;
            }

            /**
             * @brief 씬 · 프리팹 · VAT · 내비메시 · 이진 번역 표를 @p cookedDir 에 베이크합니다. 하나라도 실패하면 false 입니다. 모두 성공하면 이번 쿠킹이 요청하지 않은 옛 출력(사라진 소스의 것)을 스테이징에서 지웁니다.
             * @details 엔티티 상태를 바이너리로 구우려면 리플렉션이 필요하고, 그것은 엔진 안에만 있다. 형식과 규칙을 쓰는 곳이 엔진 하나다.
             *          @p bCrowdSettingsLoaded 가 거짓이면 코드 기본 프레임율로 베이크하지 않고 VAT 베이크를 실패로 센다(군중 표는 쿠킹 세션 전에 읽는다 — 작업 키의 설정이다).
             */
            [[nodiscard]] static bool cookSceneStage( const AbsolutePath& cookedDir, const CookStore& cookStore, const uint64 reflectionFingerprint, const AnimationCrowdSettings& crowdSettings,
                                                      bool bCrowdSettingsLoaded )
            {
                const AbsolutePath resourceRoot     = engine::getSubsystem<AssetPaths>().getRootFolderPath();
                uint32             sceneFailedCount = 0;
                // 쿠킹용 씬도 런타임 씬과 같은 엔진 서비스 묶음(씬 매니저의 것 — 의존 칸에 있다)으로 짓는다.
                const SceneEngineServices& sceneServices = engine::getSubsystem<SceneManager>().getEngineServices();
                // 쿠커별 시간(`Headless.CookStageTimes`) — 씬 쿠킹 병렬화가 빌드 시간을 줄일 수 있는지의 근거다.
                Stopwatch    stageStopwatch;
                const uint32 sceneCount        = SceneCooker::cookAllScenes( sceneServices, resourceRoot, cookedDir, cookStore, reflectionFingerprint, sceneFailedCount );
                const int64  sceneMs           = stageStopwatch.getElapsedMilliseconds();
                uint32       prefabFailedCount = 0;
                stageStopwatch.restart();
                [[maybe_unused]] const uint32 prefabCount                = PrefabCooker::cookAll( resourceRoot, cookedDir, cookStore, reflectionFingerprint, prefabFailedCount );
                const int64                   prefabMs                   = stageStopwatch.getElapsedMilliseconds();
                uint32                        vertexAnimationFailedCount = bCrowdSettingsLoaded ? 0u : 1u;
                stageStopwatch.restart();
                [[maybe_unused]] const uint32 vertexAnimationCount =
                    bCrowdSettingsLoaded
                        ? VertexAnimationCooker::cookAll( resourceRoot, cookedDir, crowdSettings._vertexAnimationFramesPerSecond, cookStore, vertexAnimationFailedCount )
                        : 0u;
                const int64 vertexAnimationMs = stageStopwatch.getElapsedMilliseconds();
                // 내비메시 — 표면이 놓인 씬마다 런타임 베이크와 같은 함수로 `<씬>.navmesh` 를 쓴다.
                uint32 navMeshFailedCount = 0;
                stageStopwatch.restart();
                [[maybe_unused]] const uint32 navMeshCount = SceneNavigationCooker::cookAll( sceneServices, resourceRoot, cookedDir, cookStore, reflectionFingerprint, navMeshFailedCount );
                const int64                   navMeshMs    = stageStopwatch.getElapsedMilliseconds();
                // 이진 번역 표 — 배포본은 저작 XML 대신 조회 사슬의 표만 읽는다(감사 A-16).
                uint32 localizationFailedCount = 0;
                stageStopwatch.restart();
                [[maybe_unused]] const uint32 localizationCount = LocalizationCooker::cookAll( resourceRoot, cookedDir, cookStore, localizationFailedCount );
                SW_LOG_INFO_ID( "Headless.CookStageTimes", "Cook stage times: scenes %# ms, prefabs %# ms, vertex animations %# ms, navmeshes %# ms, localization tables %# ms", sceneMs,
                                prefabMs, vertexAnimationMs, navMeshMs, stageStopwatch.getElapsedMilliseconds() );
                SW_LOG_INFO_ID( "Headless.CookedScenesFailuresPrefabs", "Cooked %# scenes (%# failures), %# prefabs (%# failures), %# vertex animations (%# failures), "
                                                                        "%# navmeshes (%# failures), %# localization tables (%# failures).",
                                sceneCount, sceneFailedCount, prefabCount, prefabFailedCount, vertexAnimationCount,
                                vertexAnimationFailedCount, navMeshCount, navMeshFailedCount, localizationCount, localizationFailedCount );
                const bool bFailed = sceneCount == 0 || sceneFailedCount > 0 || prefabFailedCount > 0 || vertexAnimationFailedCount > 0 ||
                                     navMeshFailedCount > 0 || localizationFailedCount > 0;
                if ( bFailed )
                    return false;
                // 사라진 소스의 옛 쿠킹본이 스테이징에 남으면 팩에 실린다 — 다섯 쿠커가 모두 성공한 뒤에만(돌지 않은 쿠커의 출력을 지우지 않게) 요청에 없는 출력을 치운다.
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
             *          쿠킹 요청이 만드는 바이너리와 그 출처도 같은 까닭으로 여기서 모아 @p outMapRequestedBinary 로 돌려준다(팩 단계는 받기만 한다).
             */
            [[nodiscard]] static bool cookShadersForPack( const CommandLineManager& commandLine, const AbsolutePath& cookedDir, unordered_map<string, vector<vector<string>>>& outMapRequestedBinary )
            {
                // 셰이더 쿠커는 렌더러에 있다 — 렌더러를 짓지 않는 구성(전용 서버)의 팩에는 셰이더 바이너리가 없어 할 일이 없다.
                IShaderCookService* const pService = IShaderCookService::find();
                if ( pService == nullptr )
                    return true;
                const ShaderTargetFormat shaderTarget = resolveShaderTarget( commandLine );
                if ( pService->cookAllShaders( cookedDir, shaderTarget, ShaderCookScope::All ) == false || pService->verifyCookedShaders( cookedDir, shaderTarget ) == false )
                {
                    SW_LOG_ERROR_ID( "Headless.CookedShadersDoNot", "Cooked shaders do not match their sources - a Shipping build has no runtime compiler, so packing stops" );
                    return false;
                }
                // 쿠킹 요청이 만드는 바이너리 — 팩이 그 가운데 원본이 닿지 않아 버리는 것을 경고로 알린다(배포 굽기에서도 보이게).
                if ( pService->collectRequestedBinaries( shaderTarget, outMapRequestedBinary ) == false )
                {
                    SW_LOG_ERROR_ID( "Headless.PackCookShaderRequests", "Pack cook: the shader cook requests could not be collected - cannot tell which dropped shader binaries the runtime needs" );
                    return false;
                }
                return true;
            }

            /**
             * @brief 팩을 베이크합니다. 셰이더 굽기 · 판정은 세션 앞의 `cookShadersForPack` 이 끝냈다.
             * @details 빌드 타깃은 쿠킹하는 호스트다(`Engine::initialize` 의 `setHostTarget` 과 같은 규칙) — 전용 서버(`Server`)가 굽은 팩은 서버가 읽지 않는 종류를 뺀다.
             *          `-game=` 을 주면 그 게임 팩만 베이크한다(배포 패키지).
             */
            [[nodiscard]] static bool cookPackStage( const CommandLineManager& commandLine, bool bDedicatedServer, const AbsolutePath& cookedDir, const CookStore& cookStore,
                                                     unordered_map<string, vector<vector<string>>>&& mapRequestedShaderBinary, vector<string>&& listStoredExtension )
            {
                const string shaderFolder = string( ShaderCooker::getSubfolderForFormat( resolveShaderTarget( commandLine ) ) );

                PackConfig         packConfig;
                const AbsolutePath packConfigPath = ProcessRoots::get()._project.join( PackConfig::kConfigPath );
                if ( ConfigManager::readConfigFile( packConfig, packConfigPath ) != ConfigReadResult::Loaded )
                {
                    SW_LOG_ERROR_ID( "Headless.PackConfigMissingInvalid", "Pack config '%#' is missing or invalid", packConfigPath.str() );
                    return false;
                }

                PackCookParams params;
                params._listStoredExtension = std::move( listStoredExtension );
                params._projectRoot         = ProcessRoots::get()._project; // 참조 칸 표(`Config/Dev/AssetReferenceFields*.json`) — 쓰는 것만 담는 고르기가 읽는다
                params._resourceRoot        = engine::getSubsystem<AssetPaths>().getRootFolderPath();
                params._cookedDir           = cookedDir;
                (void)commandLine.getArgument( CommandLineArgument::PACK_DIR, params._packOutputDir ); // 비면 cookAllPacks 가 오류로 멈춘다
                params._shaderFolder = shaderFolder;
                params._buildTarget  = bDedicatedServer ? "Server" : "Client";
                // 세션 앞의 `cookShadersForPack` 이 모은 요청 바이너리. 셰이더 쿠커가 없는 구성(전용 서버)은 빈 목록이다.
                params._mapRequestedShaderBinary = std::move( mapRequestedShaderBinary );
                // 실행기가 없는 구성(Shipping · 전용 서버)은 실행기가 스스로 등록하지 않는다 — 그 구성이 굽는 팩에는 시나리오를 담지 않는다.
                // 배포 대상의 팩(`-pack-for-shipping`)도 담지 않는다 — 교차 빌드(Android · 웹)는 호스트 개발 App(실행기가 있다)이 굽는다.
                const bool bShippingPack  = commandLine.isArgumentProvided( CommandLineArgument::PACK_FOR_SHIPPING );
                const bool bHasAutomation = engine::findSubsystem<IAutomationRuntime>() != nullptr;
                params._automationContent = bHasAutomation && bShippingPack == false ? PackAutomationContent::Included : PackAutomationContent::Excluded;
                if ( commandLine.isArgumentProvided( CommandLineArgument::GAME ) )
                {
                    // 팩 폴더 이름은 게임 이름과 다를 수 있어 게임 설정의 팩 루트(`game/<폴더>`)에서 읽는다.
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
                    return PackPatchBuilder::writeNoPatchingRecord( params._packOutputDir.join( PackBuildRecord::kFileName ) );
                return PackPatchBuilder::writeBuildRecord( params._packOutputDir, build, params._packOutputDir.join( PackBuildRecord::kFileName ) );
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
                AbsolutePath signKeyPath;
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
                        SW_LOG_ERROR_ID( "Headless.PatchSignKeyFile", "-patch-sign-key file '%#' could not be read or is empty - no patch is written", signKeyPath.str() );
                        return false;
                    }
                }
                PackConfig         packConfig;
                const AbsolutePath packConfigPath = ProcessRoots::get()._project.join( PackConfig::kConfigPath );
                if ( ConfigManager::readConfigFile( packConfig, packConfigPath ) != ConfigReadResult::Loaded )
                {
                    SW_LOG_ERROR_ID( "Headless.PackConfigMissingInvalid", "Pack config '%#' is missing or invalid", packConfigPath.str() );
                    return false;
                }
                params._writerDesc._compression      = packConfig._compression._codec;
                params._writerDesc._compressionLevel = packConfig._compression._level;
                params._writerDesc._blockSize        = packConfig._compression._blockSize;
                params._writerDesc._writeWindowBytes = packConfig._compression._writeWindowBytes;
                return PackPatchBuilder::makePatch( params );
            }

            static bool isCookShadersRequested( const CommandLineManager& commandLine ) { return isFlagGiven( commandLine, CommandLineArgument::COOK_SHADERS ); }

            /** @brief 셰이더 쿠킹은 렌더러의 일이다(`IShaderCookService`) — 렌더러를 짓지 않는 구성은 쿠커가 없다는 오류로 멈춘다. */
            static bool runCookShadersTask( HeadlessTaskContext& context )
            {
                const CommandLineManager& commandLine = context._commandLine;
                AbsolutePath              cookedDir;
                commandLine.getArgument( CommandLineArgument::COOKED_DIR, cookedDir );
                // `-shader-request-dir` 는 모든 퍼뮤테이션 대신 지난 실행의 요청 기록만 컴파일해 셰이더 DDC 를 채운다 — 쿠킹본(스테이징 · 매니페스트 · 스탬프)은 쓰지 않는다.
                AbsolutePath requestDir;
                if ( commandLine.getArgument( CommandLineArgument::SHADER_REQUEST_DIR, requestDir ) )
                    return ShaderRequestLog::warmFromFolder( requestDir );
                SW_LOG_INFO_ID( "Headless.StartingHeadlessCookShaders", "Starting Headless (CookShaders) -> '%#'...", cookedDir.str() );
                // 명령줄 백엔드(`-vulkan` …)를 주면 그 형식만, 주지 않으면 네 형식을 모두 굽는다(`ShaderTargetFormat::Count`).
                ShaderTargetFormat shaderTarget = ShaderTargetFormat::Count;
                RHIBackend         commandLineBackend{};
                if ( RHI::findCommandLineBackend( commandLine, commandLineBackend ) )
                    shaderTarget = RHI::getShaderTargetFormat( commandLineBackend );
                // `-cook-shaders-sample` 은 표본 규칙(`Config/Dev/ShaderCookSample.json`)이 고른 요청만 빈 스테이징에 굽는다(빈 캐시에서 다시 굽는 길의 테스트).
                bool bSample = false;
                (void)commandLine.getArgument( CommandLineArgument::COOK_SHADERS_SAMPLE, bSample );
                const ShaderCookScope     scope    = bSample ? ShaderCookScope::Sample : ShaderCookScope::All;
                IShaderCookService* const pService = requireShaderCookService( "--cook-shaders" );
                return pService != nullptr && pService->cookAllShaders( cookedDir, shaderTarget, scope ); // 비면 쿠커가 오류로 멈춘다
            }

            /** @brief 쿠킹 인자(씬 · 팩 · 전부) 중 하나라도 주어졌는지입니다. */
            static bool isCookRequested( const CommandLineManager& commandLine )
            {
                return isFlagGiven( commandLine, CommandLineArgument::COOK_SCENES ) || isFlagGiven( commandLine, CommandLineArgument::COOK_PACKS ) ||
                       isFlagGiven( commandLine, CommandLineArgument::COOK_ALL );
            }

            /** @brief 셰이더 판정만이다 — 쿠킹과 함께 주어지면 쿠킹이 판정을 포함하므로 청하지 않은 것으로 본다. */
            static bool isVerifyShadersRequested( const CommandLineManager& commandLine )
            {
                return isFlagGiven( commandLine, CommandLineArgument::VERIFY_SHADERS ) && isCookRequested( commandLine ) == false;
            }

            /** @brief 셰이더 판정 — 쿠킹 스테이징(`-cooked-dir`)의 쿠킹 표 모든 백엔드를 본다. 고치지 않는다. */
            static bool runVerifyShadersTask( HeadlessTaskContext& context )
            {
                const CommandLineManager& commandLine = context._commandLine;
                AbsolutePath              cookedDir;
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
                    SW_LOG_INFO_ID( "Headless.CookedShaderBinariesMatch", "Cooked shader binaries in '%#' match their sources for %#", cookedDir.str(), bOneBackend ? "the command-line backend" : "every backend" );
                return bAllCurrent;
            }

            static bool isMakePatchRequested( const CommandLineManager& commandLine ) { return isFlagGiven( commandLine, CommandLineArgument::MAKE_PATCH ); }

            /** @brief 패치 만들기(`-make-patch`) — 베이크한 팩끼리만 보므로 씬 쿠킹도 에셋 로드도 필요 없다. 로그와 명령줄이 있는 이 단계에서 한다. */
            static bool runMakePatchTask( HeadlessTaskContext& context )
            {
                SW_LOG_INFO_ID( "Headless.StartingHeadlessMakePatch", "Starting Headless (MakePatch)..." );
                return makePatchStage( context._commandLine );
            }

            /** @brief 씬 · 팩 쿠킹도 같은 자리다. 엔티티 상태를 바이너리로 구우려면 리플렉션이 필요하고 그것은 엔진 안에만 있다. 팩은 그 산출물을 함께 담는다. */
            static bool runCookTask( HeadlessTaskContext& context )
            {
                const CommandLineManager& commandLine = context._commandLine;
                const bool                bCookPacks  = isFlagGiven( commandLine, CommandLineArgument::COOK_PACKS );
                const bool                bCookAll    = isFlagGiven( commandLine, CommandLineArgument::COOK_ALL );
                const bool                bCookScenes = isFlagGiven( commandLine, CommandLineArgument::COOK_SCENES );
                AbsolutePath              cookedDir;
                commandLine.getArgument( CommandLineArgument::COOKED_DIR, cookedDir );
                if ( cookedDir.empty() )
                {
                    SW_LOG_ERROR_ID( "Headless.CookingNeedsCookedDir", "Cooking needs -cooked-dir=<folder> (the staging folder of cooked scenes and prefabs)" );
                    return false;
                }
                // 교차 쿠킹(호스트 App 이 Android · 웹 팩을 굽는다)은 대상 플랫폼의 조건을 푼다 — 주지 않으면 이 기계의 플랫폼이다(`Engine` 기동). 쿠킹 입력을 읽기 전에 둔다.
                string cookPlatform;
                commandLine.getArgument( CommandLineArgument::COOK_PLATFORM, cookPlatform );
                if ( cookPlatform.empty() == false && engine::getSubsystem<ResourceTags>().setAxisValue( "platform", cookPlatform ) == false )
                    return false; // 모르는 플랫폼 — 이유는 setAxisValue 가 남겼다
                SW_LOG_INFO_ID( "Headless.StartingHeadlessCook", "Starting Headless (Cook%#) -> '%#'...", bCookAll ? "All" : ( bCookPacks ? "Packs" : "Scenes" ), cookedDir.str() );
                AnimationCrowdSettings crowdSettings{};
                const bool             bCrowdSettingsLoaded = crowdSettings.loadFromAsset( AssetID{ engine::getSubsystem<EngineDefaultAssets>()._animationCrowdSettings.get() } );
                expected<CookStore>    localStore           = CookStore::makeLocal();
                if ( localStore.has_value() == false )
                {
                    SW_LOG_ERROR_ID( "Headless.CookStoreUnavailable", "Cooking stopped: %# (%#)", localStore.error().message(), localStore.error().detail() );
                    return false;
                }
                const CookStore& cookStore = *localStore;
                // 쿠킹 시작에 한 번 — 언리얼 파일 DDC 가 시작할 때 오래 안 쓴 항목을 지우는 자리다. 지운 수는 DDC.EntriesEvicted 로그가 남긴다.
                DerivedDataCache::evictAllBuckets();
                // 앞 쿠킹이 남긴 내용 해시를 싣는다 — 도장이 같은 입력은 읽지 않는다. 캐시라 못 읽어도 빈 채로 계속한다(다음 저장이 새로 쓴다).
                const LocalContentHashCacheScope         hashCacheScope{ LocalContentHashCacheUse::LoadAndSave };
                [[maybe_unused]] const ContentHashCache& hashCache         = engine::getSubsystem<ContentHashCache>();
                [[maybe_unused]] const uint64            hashedBytesBefore = hashCache.getHashedByteCount(); // 로그로만 쓴다(Shipping 은 빠진다)
                // 입력 맵 묶음은 게임 시작의 검사와 같은 길로 먼저 본다 — 겹친 묶음은 배포본이 뜨지 않으므로 구울 까닭이 없다. 쿠킹 세션 밖이다(출력의 입력이 아니다).
                if ( InputMapBundle::validatePack( engine::getSubsystem<GameConfig>()._packRoot.get() ) == false )
                {
                    SW_LOG_ERROR_ID( "Headless.InputMapBundleRejected", "Cooking stopped: the input map bundle of the active game pack was rejected (listed above)" );
                    return false;
                }
                // 셰이더 굽기 · 판정은 세션 밖에서(`cookShadersForPack` 설명). 씬 단계보다 먼저 하므로 깨진 셰이더로 씬을 굽는 시간을 쓰지 않는다.
                unordered_map<string, vector<vector<string>>> mapRequestedShaderBinary;
                if ( ( bCookPacks || bCookAll ) && cookShadersForPack( commandLine, cookedDir, mapRequestedShaderBinary ) == false )
                    return false;
                // 팩 저장 방식은 세션 밖에서 읽는다 — 모드 팩 빌더와 같은 파일(배포본에는 `Config/` 가 없어 엔진 데이터에 둔다)이고, 값은 팩 작업 키의 설정 해시에 든다(`PackCooker` computeSettingsHash).
                vector<string> listStoredExtension;
                if ( bCookPacks || bCookAll )
                {
                    PackStorageSettings storageSettings;
                    if ( storageSettings.loadFromAsset( AssetID{ engine::getSubsystem<EngineDefaultAssets>()._packStorage.get() } ) == false )
                        return false;
                    listStoredExtension = std::move( storageSettings._listStoredExtension );
                }
                // 정체성 규칙(원본 폴더 표)도 세션 밖에서 처음 읽는다 — 프로세스에 한 번 읽어 두는 프로젝트 설정이라, 개발 빌드는 에디터 · 임포터가 먼저 읽지만
                // 배포 쿠킹은 팩 작업 키(`PackCooker` computeSettingsHash)가 세션 안에서 처음 닿아 읽기가 거절됐다.
                (void)AssetIdentityRules::getProjectRules();
                // 리플렉션 지문은 세션 밖에서 한 번 — 기본 인스턴스를 만들며 게임 설정의 `<Defaults>` 를 읽을 수 있다(그 값은 지문에 든다).
                const bool   bCooksScenes          = bCookScenes || bCookAll;
                const uint64 reflectionFingerprint = bCooksScenes ? CookReflectionFingerprint::compute( engine::getSubsystem<TypeRegistry>() ) : 0;
                // 세션 안에서는 작업(`CookJob`) 밖의 리소스 읽기가 실패한다 — 쿠커가 읽은 것이 모두 그 출력의 입력으로 기록된다.
                const CookSession cookSession{ engine::getSubsystem<AssetPaths>() };
                bool              bSucceeded = true;
                if ( bCooksScenes )
                    bSucceeded = cookSceneStage( cookedDir, cookStore, reflectionFingerprint, crowdSettings, bCrowdSettingsLoaded );
                // 씬 단계가 실패하면 팩을 베이크하지 않는다 — 빠진 산출물로 베이크한 팩은 배포본에서야 실패가 드러난다.
                if ( bSucceeded && ( bCookPacks || bCookAll ) )
                    bSucceeded = cookPackStage( commandLine, context._bDedicatedServer, cookedDir, cookStore, std::move( mapRequestedShaderBinary ), std::move( listStoredExtension ) );
                // 변경 없는 두 번째 쿠킹은 0 이어야 한다(도장만 본다 — `Pack.Scale` 측정의 입력 읽기 값).
                SW_LOG_INFO_ID( "Headless.CookHashedBytes", "Cook hashed %# input bytes", hashCache.getHashedByteCount() - hashedBytesBefore );
                return bSucceeded;
            }
        };
    } // namespace

    // 서버가 돌지 않는 작업(셰이더 쿠킹 · 판정)도 여기서 올린다 — 서버에 주면 조용히 넘어가지 않고 거절된다(`HeadlessTaskHost::Client`).
    SW_HEADLESS_TASK( cookShaders, "CookShaders", HeadlessTaskPriority::kCookShaders, Client, &CookTasksInternal::isCookShadersRequested, &CookTasksInternal::runCookShadersTask );
    SW_HEADLESS_TASK( verifyShaders, "VerifyShaders", HeadlessTaskPriority::kVerifyShaders, Client, &CookTasksInternal::isVerifyShadersRequested, &CookTasksInternal::runVerifyShadersTask );
    SW_HEADLESS_TASK( makePatch, "MakePatch", HeadlessTaskPriority::kMakePatch, Any, &CookTasksInternal::isMakePatchRequested, &CookTasksInternal::runMakePatchTask );
    SW_HEADLESS_TASK( cook, "Cook", HeadlessTaskPriority::kCook, Any, &CookTasksInternal::isCookRequested, &CookTasksInternal::runCookTask );
} // namespace sw
