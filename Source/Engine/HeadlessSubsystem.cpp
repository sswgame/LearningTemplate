#include "pch.h"

#include "Engine/HeadlessSubsystem.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/File/FileUtil.h"
#include "Core/String/string_splitter.h"

#include "Engine/Animation/Facial/LipSync.h"
#include "Engine/Animation/Retarget/PoseRetargeter.h"
#include "Engine/Audio/LipSyncImport.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIBackendInfo.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"
#include "Engine/Localization/EngineDefaultAssetsSubsystem.h"
#include "Engine/Module/ModuleTypesSubsystem.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Object/SceneAnimation/AnimationCrowd.h"
#include "Engine/Object/SceneAnimation/VertexAnimationCooker.h"
#include "Engine/Reflection/ReflectionDocWriter.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Renderer/Cook/ShaderCookDriver.h"
#include "Engine/Renderer/RHISubsystem.h"
#include "Engine/Resource/AssetDatabase.h"
#include "Engine/Resource/Cook/CookStore.h"
#include "Engine/Resource/Cook/PackConfig.h"
#include "Engine/Resource/Cook/PackCooker.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Scene/SceneCooker.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneNavigationCooker.h"
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
             * @brief 씬 · 프리팹 · GUID 레지스트리 · VAT · 내비메시를 @p cookedDir 에 굽습니다. 하나라도 실패하면 false 입니다.
             * @details 엔티티 상태를 바이너리로 구우려면 리플렉션이 필요하고, 그것은 엔진 안에만 있다. 형식과 규칙을 쓰는 곳이 엔진 하나다.
             *          @p bCrowdSettingsLoaded 가 거짓이면 코드 기본 프레임율로 굽지 않고 VAT 굽기를 실패로 센다(군중 표는 쿠킹 세션 전에 읽는다 — 작업 키의 설정이다).
             */
            [[nodiscard]] static bool cookSceneStage( const string& cookedDir, const CookStore& cookStore, const AnimationCrowdSettings& crowdSettings, bool bCrowdSettingsLoaded )
            {
                const string& resourceRoot     = ResourcePaths::getRootFolderPath();
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
                return bFailed == false;
            }

            /** @brief 팩에 담을 셰이더 바이너리 폴더입니다 — 명령줄 백엔드(`-dx12` …), 없으면 엔진 설정의 `_defaultRHI` 입니다. */
            static string resolveShaderFolder( const CommandLineManager& commandLine )
            {
                RHIBackend backend = EngineConfig::getActive()._window._defaultRHI;
                RHIBackend commandLineBackend{};
                if ( RHIBackendUtil::findCommandLineBackend( commandLine, commandLineBackend ) )
                    backend = commandLineBackend;
                return string( ShaderCooker::getSubfolderForFormat( RHI::getShaderTargetFormat( backend ) ) );
            }

            /** @brief @p shaderFolder 의 커밋된 셰이더 바이너리를 판정하고 문제를 오류로 남깁니다. 문제가 없으면 true 입니다. */
            static bool verifyShaderFolder( string_view shaderFolder )
            {
                vector<string> listProblem;
                ShaderCooker::verifyCookedShaders( ResourcePaths::getRootFolderPath(), shaderFolder, listProblem );
                for ( const string& problem : listProblem )
                {
                    SW_LOG_ERROR( "[%#] %#", shaderFolder, problem );
                }
                return listProblem.empty();
            }

            /**
             * @brief 팩을 굽습니다. 셰이더 판정을 켰으면(배포 빌드) 먼저 그 백엔드를 보고, 낡았으면 다시 쿠킹한 뒤 다시 본다 — 그래도 맞지 않으면 굽지 않는다.
             * @details 빌드 타깃은 쿠킹하는 호스트다(`Engine::initialize` 의 `setHostTarget` 과 같은 규칙) — 전용 서버(`Server`)가 굽은 팩은 서버가 읽지 않는 종류를 뺀다.
             *          `-game=` 을 주면 그 게임 팩만 굽는다(배포 패키지).
             */
            [[nodiscard]] static bool cookPackStage( const CommandLineManager& commandLine, bool bDedicatedServer, const string& cookedDir, const CookStore& cookStore )
            {
                const string shaderFolder   = resolveShaderFolder( commandLine );
                bool         bVerifyShaders = false;
                (void)commandLine.getArgument( CommandLineArgument::VERIFY_SHADERS, bVerifyShaders ); // 없으면 false 그대로
                // 전용 서버는 이 인자를 받지 않는다(`kArrClientOnlyTask`) — 서버 팩에는 셰이더 바이너리가 없다.
                const bool bStaleShaders = bVerifyShaders && verifyShaderFolder( shaderFolder ) == false;
                if ( bStaleShaders )
                {
                    // 낡았을 뿐이라면 쿠커가 있는 자리에서 스스로 고친다 — 셰이더 한 줄 고칠 때마다 손으로 다시 쿠킹하라고 요구할 이유는 없다.
                    SW_LOG_INFO( "Cooked shaders are stale - cooking them again before packing" );
                    const ShaderCookSummary summary = ShaderCookDriver::cookAllShaders();
                    ShaderCooker::invalidateSharedHeaderCache();
                    if ( summary.isClean() == false || verifyShaderFolder( shaderFolder ) == false )
                    {
                        SW_LOG_ERROR( "Cooked shaders do not match their sources - a Shipping build has no runtime compiler, so packing stops (App --cook-shaders)" );
                        return false;
                    }
                }

                PackConfig   packConfig;
                const string packConfigPath = FileUtil::joinPath( ResourcePaths::getProjectFolderPath(), PackConfig::kConfigPath );
                if ( ConfigManager::readConfigFile( packConfig, packConfigPath ) != ConfigReadResult::Loaded )
                {
                    SW_LOG_ERROR( "Pack config '%#' is missing or invalid", packConfigPath );
                    return false;
                }

                PackCookParams params;
                params._resourceRoot = ResourcePaths::getRootFolderPath();
                params._cookedDir    = cookedDir;
                (void)commandLine.getArgument( CommandLineArgument::PACK_DIR, params._packOutputDir ); // 비면 cookAllPacks 가 오류로 멈춘다
                params._shaderFolder = shaderFolder;
                params._buildTarget  = bDedicatedServer ? "Server" : "Client";
                if ( commandLine.isArgumentProvided( CommandLineArgument::GAME ) )
                {
                    // 팩 폴더 이름은 게임 이름과 다를 수 있어(ThemeParkTycoon → themepark) 게임 설정의 팩 루트(`game/<폴더>`)에서 읽는다.
                    const string packRoot  = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( GameConfig::getActive()._packRoot.get() ) );
                    params._gamePackFolder = FileUtil::getFileNamePart( packRoot );
                }
                uint32 packCount = 0;
                return PackCooker::cookAllPacks( packConfig, params, cookStore, packCount );
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
        CommandLineManager& commandLine = engine::getSubsystem<CommandLineManager>();
        // 전용 서버가 하는 헤드리스 작업은 씬 · 팩 쿠킹(서버 팩)까지다. 셰이더 쿠킹(DXC) · 판정 · 원본 임포트(에디터 모듈)는 App 의 일이다 — 조용히 넘기지 않고 실패한다.
        if ( _bDedicatedServer )
        {
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
                    _state._bHeadless = true;
                    _state._bFailed   = true;
                    return SubsystemInitResult::SkipDependents;
                }
            }
        }

        // 헤드리스 작업은 보통 실행이 누수 기준선을 잡는 자리(`initialize` 끝)에 닿지 않는다. 기준선이 없으면 종료 때 살아 있는 블록을 모두
        // 누수로 찍으므로, 작업 직전에 같은 기준선을 잡는다 — 작업이 만든 것을 종료까지 놓지 않을 때만 누수로 보인다.
        bool bCookShaders = false;
        if ( commandLine.getArgument( CommandLineArgument::COOK_SHADERS, bCookShaders ) && bCookShaders )
        {
            MemoryProfiler::captureMemoryLeakBaseline();
            _state._bHeadless = true;
            SW_LOG_INFO( "Starting Headless (CookShaders)..." );
            const ShaderCookSummary summary = ShaderCookDriver::cookAllShaders();
            _state._bFailed                 = summary.isClean() == false;
            return SubsystemInitResult::SkipDependents;
        }

        // 리플렉션 문서도 같은 자리다 — 모든 타입 공급자가 등록을 끝낸 뒤(ModuleTypes)라 게임 · 킷의 타입까지 든다.
        string reflectionDocsDir;
        if ( commandLine.getArgument( CommandLineArgument::WRITE_REFLECTION_DOCS, reflectionDocsDir ) && reflectionDocsDir.empty() == false )
        {
            _state._bHeadless = true;
            _state._bFailed   = ReflectionDocWriter::writeMarkdown( engine::getSubsystem<TypeRegistry>(), reflectionDocsDir ) == 0;
            return SubsystemInitResult::SkipDependents;
        }

        // 리타깃 굽기(`--bake-retarget=<프로필>,<원본 클립>,<출력 클립>`) — 클립 · 스켈레톤 · 코덱이 모두 엔진 것이라 여기서 한다.
        string bakeRetarget;
        if ( commandLine.getArgument( CommandLineArgument::BAKE_RETARGET, bakeRetarget ) && bakeRetarget.empty() == false )
        {
            MemoryProfiler::captureMemoryLeakBaseline();
            _state._bHeadless = true;
            const string_splitter      parts( bakeRetarget, { "," } );
            const vector<string_view>& listArgument = parts.getSplitList();
            const bool                 bWellFormed  = listArgument.size() == 3;
            if ( bWellFormed == false )
                SW_LOG_ERROR( "--bake-retarget expects <profile>,<source clip>,<output clip>" );
            _state._bFailed = bWellFormed == false ||
                              RetargetBakeUtil::bakeClipFile( StringUtil::trim( listArgument[0] ), StringUtil::trim( listArgument[1] ), StringUtil::trim( listArgument[2] ) ) == false;
            return SubsystemInitResult::SkipDependents;
        }

        // 셰이더 판정만(커밋 훅) — 쿠킹 표의 모든 백엔드를 본다. 고치지 않는다.
        bool bVerifyShaders = false;
        bool bCookScenes    = false;
        bool bCookPacks     = false;
        bool bCookAll       = false;
        commandLine.getArgument( CommandLineArgument::VERIFY_SHADERS, bVerifyShaders );
        commandLine.getArgument( CommandLineArgument::COOK_SCENES, bCookScenes );
        commandLine.getArgument( CommandLineArgument::COOK_PACKS, bCookPacks );
        commandLine.getArgument( CommandLineArgument::COOK_ALL, bCookAll );
        const bool bAnyCook = bCookScenes || bCookPacks || bCookAll;
        if ( bVerifyShaders && bAnyCook == false )
        {
            _state._bHeadless = true;
            bool bAllCurrent  = true;
            for ( const RHIBackendInfo& info : RHIBackendInfoTable::kArrInfo )
            {
                bAllCurrent = HeadlessSubsystemInternal::verifyShaderFolder( ShaderCooker::getSubfolderForFormat( info._shaderTarget ) ) && bAllCurrent;
            }
            if ( bAllCurrent )
                SW_LOG_INFO( "Committed shader binaries match their sources for every backend" );
            _state._bFailed = bAllCurrent == false;
            return SubsystemInitResult::SkipDependents;
        }

        // 씬 · 팩 쿠킹도 같은 자리다. 엔티티 상태를 바이너리로 구우려면 리플렉션이 필요하고 그것은 엔진 안에만 있다. 팩은 그 산출물을 함께 담는다.
        if ( bAnyCook )
        {
            MemoryProfiler::captureMemoryLeakBaseline();
            _state._bHeadless = true;
            string cookedDir;
            commandLine.getArgument( CommandLineArgument::COOKED_DIR, cookedDir );
            if ( cookedDir.empty() )
            {
                SW_LOG_ERROR( "Cooking needs -cooked-dir=<folder> (the staging folder of cooked scenes and prefabs)" );
                _state._bFailed = true;
                return SubsystemInitResult::SkipDependents;
            }
            SW_LOG_INFO( "Starting Headless (Cook%#) -> '%#'...", bCookAll ? "All" : ( bCookPacks ? "Packs" : "Scenes" ), cookedDir );
            AnimationCrowdSettings crowdSettings{};
            const bool             bCrowdSettingsLoaded = crowdSettings.loadFromResource( AnimationCrowdSettings::kResourcePath );
            const CookStore        cookStore            = CookStore::makeLocal();
            // 세션 안에서는 작업(`CookJob`) 밖의 리소스 읽기가 실패한다 — 쿠커가 읽은 것이 모두 그 출력의 입력으로 기록된다.
            const CookSession cookSession;
            bool              bSucceeded = true;
            if ( bCookScenes || bCookAll )
                bSucceeded = HeadlessSubsystemInternal::cookSceneStage( cookedDir, cookStore, crowdSettings, bCrowdSettingsLoaded );
            // 씬 단계가 실패하면 팩을 굽지 않는다 — 빠진 산출물로 구운 팩은 배포본에서야 실패가 드러난다.
            if ( bSucceeded && ( bCookPacks || bCookAll ) )
                bSucceeded = HeadlessSubsystemInternal::cookPackStage( commandLine, _bDedicatedServer, cookedDir, cookStore );
            _state._bFailed = bSucceeded == false;
            return SubsystemInitResult::SkipDependents;
        }

        // 립싱크 분석 — 음성마다 비즘 트랙을 곁에 쓴다. 분석 표가 엔진 데이터라 엔진이 한다.
        bool bImportLipSync = false;
        if ( commandLine.getArgument( CommandLineArgument::IMPORT_LIPSYNC, bImportLipSync ) && bImportLipSync )
        {
            MemoryProfiler::captureMemoryLeakBaseline();
            _state._bHeadless = true;
            SW_LOG_INFO( "Starting Headless (ImportLipSync)..." );
            LipSyncSettings         lipSyncSettings{};
            uint32                  lipSyncFailedCount = 0;
            [[maybe_unused]] uint32 lipSyncCount       = 0; // 로그로만 쓴다(Shipping 은 빠진다)
            if ( lipSyncSettings.loadFromResource( LipSyncSettings::kResourcePath ) )
                lipSyncCount = LipSyncImport::importAll( ResourcePaths::getRootFolderPath(), lipSyncSettings, lipSyncFailedCount );
            else
                ++lipSyncFailedCount;
            SW_LOG_INFO( "Imported %# viseme tracks (%# failures).", lipSyncCount, lipSyncFailedCount );
            _state._bFailed = lipSyncFailedCount > 0;
            return SubsystemInitResult::SkipDependents;
        }

        // 로컬라이제이션 도구(글 수집 · PO 교환)와 원본 임포트 · 대조(텍스처 · 모델 · 높이장)는 에디터 모듈이 한다(엔진은 에디터를 모른다).
        // 여기서는 창 · RHI 없이 세우기만 하고, 모듈을 올려 부르는 것은 App 이다(`EditorModuleHost::runLocalizationWithEditorModule` ·
        // `importAssetsWithEditorModule`). 이 단계는 타입 공급자 모듈 뒤라 글 수집이 리플렉션 프로퍼티(`Meta = "Localizable"`)를 본다.
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
        if ( bAnyLocalization || bAnyImport )
        {
            MemoryProfiler::captureMemoryLeakBaseline();
            _state._bHeadless = true;
            SW_LOG_INFO( "Starting Headless (%#)...", bAnyLocalization ? "localization tools" : "asset import/check" );
            return SubsystemInitResult::SkipDependents;
        }
        return SubsystemInitResult::Succeeded;
    }

    span<const utf8* const> HeadlessSubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { EngineDefaultAssetsSubsystem::kSubsystemName, ModuleTypesSubsystem::kSubsystemName, SceneManager::kSubsystemName,
                                                          ShaderCache::kSubsystemName };
        return kArrDependency;
    }

    span<const utf8* const> HeadlessSubsystem::getSubsystemDependents() const
    {
        static constexpr const utf8* kArrDependent[] = { FontService::kSubsystemName, RHISubsystem::kSubsystemName, UserSettingsManager::kSubsystemName };
        return kArrDependent;
    }
} // namespace sw
