#include "pch.h"

#include "Engine/HeadlessSubsystem.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/String/string_splitter.h"

#include "Engine/Animation/Facial/LipSync.h"
#include "Engine/Animation/Retarget/PoseRetargeter.h"
#include "Engine/Audio/LipSyncImport.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Localization/EngineDefaultAssetsSubsystem.h"
#include "Engine/Module/ModuleTypesSubsystem.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Object/SceneAnimation/AnimationCrowd.h"
#include "Engine/Object/SceneAnimation/VertexAnimationCooker.h"
#include "Engine/Reflection/ReflectionDocWriter.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Renderer/Cook/ShaderCookDriver.h"
#include "Engine/Resource/AssetDatabase.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Scene/SceneCooker.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneNavigationCooker.h"
#include "Engine/Text/FontService.h"
#include "Engine/UserSettings/UserSettingsManager.h"

namespace sw
{
    SW_LOG_CALLER( "Headless" );

    HeadlessSubsystem::HeadlessSubsystem( bool bDedicatedServer, HeadlessTaskState& outState )
        : _state{ outState }
        , _bDedicatedServer{ bDedicatedServer }
    {
    }

    SubsystemInitResult HeadlessSubsystem::initializeSubsystem()
    {
        CommandLineManager& commandLine = engine::getCommandLineManager();
        // 전용 서버가 하는 헤드리스 작업은 씬 쿠킹(서버 팩)까지다. 셰이더 쿠킹(DXC) · 원본 임포트(에디터 모듈)는 App 의 일이다 — 조용히 넘기지 않고 실패한다.
        if ( _bDedicatedServer )
        {
            static constexpr CommandLineArgument kArrClientOnlyTask[] = {
                CommandLineArgument::COOK_SHADERS,
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
            _state._bFailed   = ReflectionDocWriter::writeMarkdown( engine::getTypeRegistry(), reflectionDocsDir ) == 0;
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

        // 씬 쿠킹도 같은 자리다. 엔티티 상태를 바이너리로 구우려면 리플렉션이 필요하고,
        // 그것은 엔진 안에만 있어서 `CookAssets.py` 가 이쪽으로 넘겨준다.
        bool bCookScenes = false;
        if ( commandLine.getArgument( CommandLineArgument::COOK_SCENES, bCookScenes ) && bCookScenes )
        {
            MemoryProfiler::captureMemoryLeakBaseline();
            _state._bHeadless = true;
            string cookedDir;
            commandLine.getArgument( CommandLineArgument::COOKED_DIR, cookedDir );
            SW_LOG_INFO( "Starting Headless (CookScenes) -> '%#'...", cookedDir );
            const string& resourceRoot     = ResourcePaths::getRootFolderPath();
            uint32        sceneFailedCount = 0;
            const uint32  sceneCount       = SceneCooker::cookAllScenes( resourceRoot, cookedDir, sceneFailedCount );
            // 프리팹 · GUID 레지스트리도 여기서 만든다 — 형식과 규칙을 쓰는 곳이 엔진 하나여야 한다.
            uint32                        prefabFailedCount   = 0;
            [[maybe_unused]] const uint32 prefabCount         = PrefabCache::cookAllPrefabs( resourceRoot, cookedDir, prefabFailedCount );
            uint32                        registryFailedCount = 0;
            [[maybe_unused]] const uint32 registryCount       = AssetDatabase::writeRegistryFiles( resourceRoot, cookedDir, registryFailedCount );
            // 정점 애니메이션(VAT) — 데이터가 고른 (메시, 클립)을 굽는다. 프레임율은 군중 표의 것이라 런타임 굽기와 같다.
            AnimationCrowdSettings crowdSettings{};
            if ( ResourceIO::hasResource( AnimationCrowdSettings::kResourcePath ) )
                (void)crowdSettings.loadFromResource( AnimationCrowdSettings::kResourcePath ); // 실패는 loadFromResource 가 오류로 남기고 기본 프레임율로 굽는다
            uint32                        vertexAnimationFailedCount = 0;
            [[maybe_unused]] const uint32 vertexAnimationCount =
                VertexAnimationCooker::cookAll( resourceRoot, cookedDir, crowdSettings._vertexAnimationFramesPerSecond, vertexAnimationFailedCount );
            // 내비메시 — 표면이 놓인 씬마다 런타임 베이크와 같은 함수로 `<씬>.navmesh` 를 쓴다.
            uint32                        navMeshFailedCount = 0;
            [[maybe_unused]] const uint32 navMeshCount       = SceneNavigationCooker::cookAll( resourceRoot, cookedDir, navMeshFailedCount );
            SW_LOG_INFO( "Cooked %# scenes (%# failures), %# prefabs (%# failures), %# asset registries (%# failures), %# vertex animations (%# failures), "
                         "%# navmeshes (%# failures).",
                         sceneCount, sceneFailedCount, prefabCount, prefabFailedCount, registryCount, registryFailedCount, vertexAnimationCount,
                         vertexAnimationFailedCount, navMeshCount, navMeshFailedCount );
            _state._bFailed = sceneCount == 0 || sceneFailedCount > 0 || prefabFailedCount > 0 || registryFailedCount > 0 ||
                              vertexAnimationFailedCount > 0 || navMeshFailedCount > 0;
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
        static constexpr const utf8* kArrDependent[] = { FontService::kSubsystemName, UserSettingsManager::kSubsystemName };
        return kArrDependent;
    }
} // namespace sw
