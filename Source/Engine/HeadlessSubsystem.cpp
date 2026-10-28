#include "pch.h"

#include "Engine/HeadlessSubsystem.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Common/StdHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/Process/StandardOutput.h"
#include "Core/String/string_splitter.h"

#include "Engine/Animation/Facial/LipSync.h"
#include "Engine/Animation/Retarget/PoseRetargeter.h"
#include "Engine/Animation/Scene/AnimationCrowd.h"
#include "Engine/Animation/Scene/VertexAnimationCooker.h"
#include "Engine/Asset/AssetDatabase.h"
#include "Engine/Asset/AssetPaths.h"
#include "Engine/Asset/Cook/CookStore.h"
#include "Engine/Asset/Cook/PackConfig.h"
#include "Engine/Asset/Cook/PackCooker.h"
#include "Engine/Asset/Pack/PackPatchBuilder.h"
#include "Engine/Asset/Pack/PackPatchTrustSource.h"
#include "Engine/Audio/LipSyncImport.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Graphics/2D/Render2DSettingsSubsystem.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIBackendInfo.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"
#include "Engine/Localization/EngineDefaultAssetsSubsystem.h"
#include "Engine/Localization/LocalizationDocuments.h"
#include "Engine/Module/ModuleLoadStageSubsystem.h"
#include "Engine/Navigation/SceneNavigationCooker.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionDocWriter.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Renderer/Cook/ShaderCookDriver.h"
#include "Engine/Renderer/RHISubsystem.h"
#include "Engine/Scene/SceneCooker.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Text/FontService.h"
#include "Engine/UserSettings/UserSettingsManager.h"

namespace sw
{
    SW_LOG_CALLER( "Headless" );

    namespace
    {
        struct HeadlessSubsystemInternal
        {
            /**
             * @brief 씬 · 프리팹 · GUID 레지스트리 · VAT · 내비메시를 @p cookedDir 에 베이크합니다. 하나라도 실패하면 false 입니다. 모두 성공하면 이번 쿠킹이 요청하지 않은 옛 출력(사라진 소스의 것)을 스테이징에서 지웁니다.
             * @details 엔티티 상태를 바이너리로 구우려면 리플렉션이 필요하고, 그것은 엔진 안에만 있다. 형식과 규칙을 쓰는 곳이 엔진 하나다.
             *          @p bCrowdSettingsLoaded 가 거짓이면 코드 기본 프레임율로 베이크하지 않고 VAT 베이크를 실패로 센다(군중 표는 쿠킹 세션 전에 읽는다 — 작업 키의 설정이다).
             */
            [[nodiscard]] static bool cookSceneStage( const string& cookedDir, const CookStore& cookStore, const AnimationCrowdSettings& crowdSettings, bool bCrowdSettingsLoaded )
            {
                const string& resourceRoot     = AssetPaths::getRootFolderPath();
                uint32        sceneFailedCount = 0;
                // 쿠킹용 씬도 런타임 씬과 같은 엔진 서비스 묶음(씬 매니저의 것 — 의존 칸에 있다)으로 짓는다.
                const SceneEngineServices&    sceneServices              = engine::getSubsystem<SceneManager>().getEngineServices();
                const uint32                  sceneCount                 = SceneCooker::cookAllScenes( sceneServices, resourceRoot, cookedDir, cookStore, sceneFailedCount );
                uint32                        prefabFailedCount          = 0;
                [[maybe_unused]] const uint32 prefabCount                = PrefabCache::cookAllPrefabs( resourceRoot, cookedDir, cookStore, prefabFailedCount );
                uint32                        registryFailedCount        = 0;
                [[maybe_unused]] const uint32 registryCount              = AssetDatabase::writeRegistryFiles( resourceRoot, cookedDir, cookStore, registryFailedCount );
                uint32                        vertexAnimationFailedCount = bCrowdSettingsLoaded ? 0u : 1u;
                [[maybe_unused]] const uint32 vertexAnimationCount =
                    bCrowdSettingsLoaded
                        ? VertexAnimationCooker::cookAll( resourceRoot, cookedDir, crowdSettings._vertexAnimationFramesPerSecond, cookStore, vertexAnimationFailedCount )
                        : 0u;
                // 내비메시 — 표면이 놓인 씬마다 런타임 베이크와 같은 함수로 `<씬>.navmesh` 를 쓴다.
                uint32                        navMeshFailedCount = 0;
                [[maybe_unused]] const uint32 navMeshCount       = SceneNavigationCooker::cookAll( sceneServices, resourceRoot, cookedDir, cookStore, navMeshFailedCount );
                SW_LOG_INFO( "Cooked %# scenes (%# failures), %# prefabs (%# failures), %# asset registries (%# failures), %# vertex animations (%# failures), "
                             "%# navmeshes (%# failures).",
                             sceneCount, sceneFailedCount, prefabCount, prefabFailedCount, registryCount, registryFailedCount, vertexAnimationCount,
                             vertexAnimationFailedCount, navMeshCount, navMeshFailedCount );
                const bool bFailed = sceneCount == 0 || sceneFailedCount > 0 || prefabFailedCount > 0 || registryFailedCount > 0 || vertexAnimationFailedCount > 0 ||
                                     navMeshFailedCount > 0;
                if ( bFailed )
                    return false;
                // 사라진 소스의 옛 쿠킹본이 스테이징에 남으면 팩에 실린다 — 다섯 쿠커가 모두 성공한 뒤에만(돌지 않은 쿠커의 출력을 지우지 않게) 요청에 없는 출력을 치운다.
                uint32                        removeFailedCount = 0;
                [[maybe_unused]] const uint32 removedCount      = cookStore.removeUnrequestedOutputs( cookedDir, removeFailedCount );
                return removeFailedCount == 0;
            }

            /** @brief 팩에 담을 셰이더 바이너리 폴더입니다 — 명령줄 백엔드(`-dx12` …), 없으면 엔진 설정의 `_defaultRHI` 입니다. */
            static string resolveShaderFolder( const CommandLineManager& commandLine )
            {
                RHIBackend backend = engine::getSubsystem<EngineConfig>()._window._defaultRHI;
                RHIBackend commandLineBackend{};
                if ( RHIBackendUtil::findCommandLineBackend( commandLine, commandLineBackend ) )
                    backend = commandLineBackend;
                return string( ShaderCooker::getSubfolderForFormat( RHI::getShaderTargetFormat( backend ) ) );
            }

            /** @brief @p shaderFolder 의 커밋된 셰이더 바이너리를 판정하고 문제를 오류로 남깁니다. 문제가 없으면 true 입니다. */
            static bool verifyShaderFolder( string_view shaderFolder )
            {
                vector<string> listProblem;
                ShaderCooker::verifyCookedShaders( AssetPaths::getRootFolderPath(), shaderFolder, listProblem );
                for ( const string& problem : listProblem )
                {
                    SW_LOG_ERROR( "[%#] %#", shaderFolder, problem );
                }
                return listProblem.empty();
            }

            /**
             * @brief 팩에 담을 셰이더 바이너리가 소스와 맞는지 판정합니다(배포 빌드의 `-verify-shaders`). 낡았으면 쿠커가 있는 자리에서 다시 쿠킹한 뒤 다시 보고, 그래도 맞지 않으면 false 입니다.
             * @details `CookSession` 을 열기 **전에** 부른다 — 셰이더 쿠커는 엔진 기본 에셋 같은 리소스를 `CookJob` 밖에서 읽는데, 세션 안에서는 그 읽기가 실패한다.
             *          셰이더 바이너리는 쿠킹 저장소가 아니라 `Content/engine/shaders/<백엔드>` 의 커밋 산출물이라 `CookJob` 입력 기록 대상도 아니다.
             */
            [[nodiscard]] static bool ensureShadersFresh( const CommandLineManager& commandLine )
            {
                const string shaderFolder   = resolveShaderFolder( commandLine );
                bool         bVerifyShaders = false;
                (void)commandLine.getArgument( CommandLineArgument::VERIFY_SHADERS, bVerifyShaders ); // 없으면 false 그대로
                // 전용 서버는 이 인자를 받지 않는다(`kArrClientOnlyTask`) — 서버 팩에는 셰이더 바이너리가 없다.
                if ( bVerifyShaders == false || verifyShaderFolder( shaderFolder ) )
                    return true;
#if defined( SW_WITH_CLIENT_CODE )
                // 낡았을 뿐이라면 쿠커가 있는 자리에서 스스로 고친다 — 셰이더 한 줄 고칠 때마다 손으로 다시 쿠킹하라고 요구할 이유는 없다.
                SW_LOG_INFO( "Cooked shaders are stale - cooking them again before packing" );
                const ShaderCookSummary summary = ShaderCookDriver::cookAllShaders();
                ShaderCooker::invalidateSharedHeaderCache();
                if ( summary.isClean() == false || verifyShaderFolder( shaderFolder ) == false )
                {
                    SW_LOG_ERROR( "Cooked shaders do not match their sources - a Shipping build has no runtime compiler, so packing stops (App --cook-shaders)" );
                    return false;
                }
                return true;
#else
                // 셰이더 쿠커는 렌더러(`Engine/Renderer/Cook`)에 있고 Server 타깃은 그 폴더를 짓지 않는다 — 고칠 수 없으니 베이크하지 않는다.
                SW_LOG_ERROR( "Cooked shaders are stale and this build has no shader cooker - cook them with App --cook-shaders" );
                return false;
#endif
            }

            /**
             * @brief 팩을 베이크합니다. 셰이더 판정은 세션 앞의 `ensureShadersFresh` 가 끝냈다.
             * @details 빌드 타깃은 쿠킹하는 호스트다(`Engine::initialize` 의 `setHostTarget` 과 같은 규칙) — 전용 서버(`Server`)가 굽은 팩은 서버가 읽지 않는 종류를 뺀다.
             *          `-game=` 을 주면 그 게임 팩만 베이크한다(배포 패키지).
             */
            [[nodiscard]] static bool cookPackStage( const CommandLineManager& commandLine, bool bDedicatedServer, const string& cookedDir, const CookStore& cookStore )
            {
                const string shaderFolder = resolveShaderFolder( commandLine );

                PackConfig   packConfig;
                const string packConfigPath = FileUtil::joinPath( AssetPaths::getProjectFolderPath(), PackConfig::kConfigPath );
                if ( ConfigManager::readConfigFile( packConfig, packConfigPath ) != ConfigReadResult::Loaded )
                {
                    SW_LOG_ERROR( "Pack config '%#' is missing or invalid", packConfigPath );
                    return false;
                }

                PackCookParams params;
                params._resourceRoot = AssetPaths::getRootFolderPath();
                params._cookedDir    = cookedDir;
                (void)commandLine.getArgument( CommandLineArgument::PACK_DIR, params._packOutputDir ); // 비면 cookAllPacks 가 오류로 멈춘다
                params._shaderFolder = shaderFolder;
                params._buildTarget  = bDedicatedServer ? "Server" : "Client";
                // 출시 문화권(`stagedCultures`) 밖의 번역 테이블은 팩에 넣지 않는다 — 규칙은 로컬라이제이션 층이 갖고 쿠커는 목록만 받는다.
                string localizationError;
                if ( LocalizationProject::collectUnstagedTranslationTables( params._resourceRoot, params._listExcludedSourceFile, localizationError ) == false )
                {
                    SW_LOG_ERROR( "Pack cook: a localization project could not be read for culture staging - %#", localizationError );
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
                    return PackPatchBuilder::writeNoPatchingRecord( FileUtil::joinPath( params._packOutputDir, PackPatchUtil::kBuildRecordFileName ) );
                return PackPatchBuilder::writeBuildRecord( params._packOutputDir, build, FileUtil::joinPath( params._packOutputDir, PackPatchUtil::kBuildRecordFileName ) );
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
                // 서명 — 개인 키는 저장소 밖 파일(`-patch-sign-key`)에서만 읽는다. 키를 줬는데 읽지 못하거나 제공자 칸이 비었으면 서명 없는 패치로 대신 만들지 않고 멈춘다.
                string signKeyPath;
                (void)commandLine.getArgument( CommandLineArgument::PATCH_SIGN_KEY, signKeyPath );
                if ( signKeyPath.empty() == false )
                {
                    params._signer._pProvider = PackPatchTrustSource::findProvider();
                    if ( params._signer._pProvider == nullptr )
                    {
                        SW_LOG_ERROR( "-patch-sign-key was given but the crypto provider slot is empty (GameFramework is not loaded) - no patch is written" );
                        return false;
                    }
                    if ( FileUtil::readTextFile( signKeyPath, params._signer._privateKeyPem ) == false || params._signer._privateKeyPem.empty() )
                    {
                        SW_LOG_ERROR( "-patch-sign-key file '%#' could not be read or is empty - no patch is written", signKeyPath );
                        return false;
                    }
                }
                PackConfig   packConfig;
                const string packConfigPath = FileUtil::joinPath( AssetPaths::getProjectFolderPath(), PackConfig::kConfigPath );
                if ( ConfigManager::readConfigFile( packConfig, packConfigPath ) != ConfigReadResult::Loaded )
                {
                    SW_LOG_ERROR( "Pack config '%#' is missing or invalid", packConfigPath );
                    return false;
                }
                params._writerDesc._compression      = packConfig._compression._codec;
                params._writerDesc._compressionLevel = packConfig._compression._level;
                return PackPatchBuilder::makePatch( params );
            }

            /**
             * @brief 전용 서버가 하는 헤드리스 작업은 씬 · 팩 쿠킹(서버 팩)까지다. 셰이더 쿠킹(DXC) · 판정 · 원본 임포트(에디터 모듈)는 App 의 일이다 — 조용히 넘기지 않고 실패한다.
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
                    CommandLineArgument::IMPORT_MODELS,
                    CommandLineArgument::CHECK_MODELS,
                    CommandLineArgument::IMPORT_HEIGHTFIELDS,
                    CommandLineArgument::CHECK_HEIGHTFIELDS,
                };
                for ( const CommandLineArgument argument : kArrClientOnlyTask )
                {
                    bool bGiven = false;
                    if ( commandLine.getArgument( argument, bGiven ) && bGiven )
                    {
                        SW_LOG_ERROR( "This headless task runs in the client build (App), not in the dedicated server" );
                        inoutState.enterHeadless();
                        inoutState._bFailed = true;
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
                    SW_LOG_ERROR( "Could not write the -help text to standard output" );
                    inoutState._bFailed = true;
                }
                return true;
            }

            /** @brief 셰이더 쿠킹은 렌더러(`Engine/Renderer/Cook`)의 일이다 — Server 타깃은 그 폴더를 짓지 않고, 전용 서버는 위에서 거절했다. 작업을 돌렸으면 true 입니다. */
            [[nodiscard]] static bool runCookShadersTask( CommandLineManager& commandLine, HeadlessTaskState& inoutState )
            {
#if defined( SW_WITH_CLIENT_CODE )
                bool bCookShaders = false;
                if ( commandLine.getArgument( CommandLineArgument::COOK_SHADERS, bCookShaders ) && bCookShaders )
                {
                    inoutState.enterHeadless();
                    SW_LOG_INFO( "Starting Headless (CookShaders)..." );
                    const ShaderCookSummary summary = ShaderCookDriver::cookAllShaders();
                    inoutState._bFailed             = summary.isClean() == false;
                    return true;
                }
#else
                (void)commandLine;
                (void)inoutState;
#endif
                return false;
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
                    SW_LOG_ERROR( "--bake-retarget expects <profile>,<source clip>,<output clip>" );
                inoutState._bFailed = bWellFormed == false ||
                                      RetargetBakeUtil::bakeClipFile( AssetID{ StringUtil::trim( listArgument[0] ) }, AssetID{ StringUtil::trim( listArgument[1] ) }, StringUtil::trim( listArgument[2] ) ) == false;
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

            /** @brief 셰이더 판정만(커밋 훅) — 쿠킹 표의 모든 백엔드를 본다. 고치지 않는다. 쿠킹과 함께 주어지면 쿠킹이 먼저다. */
            [[nodiscard]] static bool runVerifyShadersTask( CommandLineManager& commandLine, HeadlessTaskState& inoutState )
            {
                bool bVerifyShaders = false;
                commandLine.getArgument( CommandLineArgument::VERIFY_SHADERS, bVerifyShaders );
                if ( bVerifyShaders == false || isAnyCookRequested( commandLine ) )
                    return false;
                inoutState.enterHeadless();
                bool bAllCurrent = true;
                for ( const RHIBackendInfo& info : RHIBackendInfoTable::kArrInfo )
                {
                    bAllCurrent = verifyShaderFolder( ShaderCooker::getSubfolderForFormat( info._shaderTarget ) ) && bAllCurrent;
                }
                if ( bAllCurrent )
                    SW_LOG_INFO( "Committed shader binaries match their sources for every backend" );
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
                SW_LOG_INFO( "Starting Headless (MakePatch)..." );
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
                    SW_LOG_ERROR( "Cooking needs -cooked-dir=<folder> (the staging folder of cooked scenes and prefabs)" );
                    inoutState._bFailed = true;
                    return true;
                }
                SW_LOG_INFO( "Starting Headless (Cook%#) -> '%#'...", bCookAll ? "All" : ( bCookPacks ? "Packs" : "Scenes" ), cookedDir );
                AnimationCrowdSettings crowdSettings{};
                const bool             bCrowdSettingsLoaded = crowdSettings.loadFromAsset( AssetID{ engine::getSubsystem<EngineDefaultAssets>()._animationCrowdSettings.get() } );
                const CookStore        cookStore            = CookStore::makeLocal();
                // 셰이더 판정 · 재쿠킹은 세션 밖에서(`ensureShadersFresh` 설명). 씬 단계보다 먼저 보므로 낡은 셰이더로 씬을 굽는 시간을 쓰지 않는다.
                if ( bCookPacks || bCookAll )
                {
                    if ( ensureShadersFresh( commandLine ) == false )
                    {
                        inoutState._bFailed = true;
                        return true;
                    }
                }
                // 세션 안에서는 작업(`CookJob`) 밖의 리소스 읽기가 실패한다 — 쿠커가 읽은 것이 모두 그 출력의 입력으로 기록된다.
                const CookSession cookSession;
                bool              bSucceeded = true;
                if ( bCookScenes || bCookAll )
                    bSucceeded = cookSceneStage( cookedDir, cookStore, crowdSettings, bCrowdSettingsLoaded );
                // 씬 단계가 실패하면 팩을 베이크하지 않는다 — 빠진 산출물로 베이크한 팩은 배포본에서야 실패가 드러난다.
                if ( bSucceeded && ( bCookPacks || bCookAll ) )
                    bSucceeded = cookPackStage( commandLine, bDedicatedServer, cookedDir, cookStore );
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
                SW_LOG_INFO( "Starting Headless (ImportLipSync)..." );
                LipSyncSettings         lipSyncSettings{};
                uint32                  lipSyncFailedCount = 0;
                [[maybe_unused]] uint32 lipSyncCount       = 0; // 로그로만 쓴다(Shipping 은 빠진다)
                if ( lipSyncSettings.loadFromAsset( AssetID{ engine::getSubsystem<EngineDefaultAssets>()._lipSyncSettings.get() } ) )
                    lipSyncCount = LipSyncImport::importAll( AssetPaths::getRootFolderPath(), lipSyncSettings, lipSyncFailedCount );
                else
                    ++lipSyncFailedCount;
                SW_LOG_INFO( "Imported %# viseme tracks (%# failures).", lipSyncCount, lipSyncFailedCount );
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
                bool   bGatherText = false;
                bool   bCheckText  = false;
                bool   bExportPo   = false;
                string importPoPath;
                commandLine.getArgument( CommandLineArgument::GATHER_TEXT, bGatherText );
                commandLine.getArgument( CommandLineArgument::CHECK_TEXT, bCheckText );
                commandLine.getArgument( CommandLineArgument::EXPORT_PO, bExportPo );
                commandLine.getArgument( CommandLineArgument::IMPORT_PO, importPoPath );
                const bool bAnyLocalization = bGatherText || bCheckText || bExportPo || importPoPath.empty() == false;

                bool bImportTextures     = false;
                bool bCheckTextures      = false;
                bool bImportModels       = false;
                bool bCheckModels        = false;
                bool bImportHeightfields = false;
                bool bCheckHeightfields  = false;
                commandLine.getArgument( CommandLineArgument::IMPORT_TEXTURES, bImportTextures );
                commandLine.getArgument( CommandLineArgument::CHECK_TEXTURES, bCheckTextures );
                commandLine.getArgument( CommandLineArgument::IMPORT_MODELS, bImportModels );
                commandLine.getArgument( CommandLineArgument::CHECK_MODELS, bCheckModels );
                commandLine.getArgument( CommandLineArgument::IMPORT_HEIGHTFIELDS, bImportHeightfields );
                commandLine.getArgument( CommandLineArgument::CHECK_HEIGHTFIELDS, bCheckHeightfields );
                const bool bAnyImport = bImportTextures || bCheckTextures || bImportModels || bCheckModels || bImportHeightfields || bCheckHeightfields;
                if ( bAnyLocalization == false && bAnyImport == false )
                    return false;
                inoutState.enterHeadless();
                SW_LOG_INFO( "Starting Headless (%#)...", bAnyLocalization ? "localization tools" : "asset import/check" );
                return true;
            }
        };
    } // namespace
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
        // 작업 순서가 곧 우선순위다: 도움말 → 서버의 클라이언트 전용 작업 거절 → 셰이더 쿠킹 → 리플렉션 문서 → 리타깃 베이크 → 셰이더 판정 → 패치 → 쿠킹 → 립싱크 → 에디터 도구.
        const bool bTaskRan = Internal::runHelpTask( commandLine, _state ) || Internal::rejectClientOnlyTask( commandLine, _bDedicatedServer, _state ) ||
                              Internal::runCookShadersTask( commandLine, _state ) ||
                              Internal::runReflectionDocsTask( commandLine, _state ) || Internal::runBakeRetargetTask( commandLine, _state ) ||
                              Internal::runVerifyShadersTask( commandLine, _state ) || Internal::runMakePatchTask( commandLine, _state ) ||
                              Internal::runCookTask( commandLine, _bDedicatedServer, _state ) || Internal::runImportLipSyncTask( commandLine, _state ) ||
                              Internal::enterEditorToolTask( commandLine, _state );
        return bTaskRan ? SubsystemInitResult::SkipDependents : SubsystemInitResult::Succeeded;
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
        static constexpr const utf8* kArrDependent[] = { FontService::kSubsystemName, RHISubsystem::kSubsystemName, UserSettingsManager::kSubsystemName };
        return kArrDependent;
    }
} // namespace sw
