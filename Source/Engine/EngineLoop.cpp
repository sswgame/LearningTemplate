#include "pch.h"

#include "Engine/EngineLoop.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Common/BuildInfo.h"
#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Concurrency/ThreadName.h"
#include "Core/Container/StringUtil.h"
#include "Core/Diagnostics/CrashContext.h"
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/Event/EventDispatcher.h"
#include "Core/File/AsyncFileIo.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Module/ModuleBuildId.h"
#include "Core/String/hashed_string.h"
#include "Core/String/string_splitter.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Animation/Facial/LipSync.h"
#include "Engine/Animation/Retarget/PoseRetargeter.h"
#include "Engine/Audio/IAudioSystem.h"
#include "Engine/Audio/LipSyncImport.h"
#include "Engine/Automation/AutomationRunner.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Compression/EngineCompressionCodecUtil.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Graphics/2D/Render2DSettings.h"
#include "Engine/Graphics/Canvas/CanvasTestPattern.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Graphics/Debug/PhysicsDebugDrawAdapter.h"
#include "Engine/Graphics/Debug/RenderTargetRegistry.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIBackendRegistry.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/RHI/Support/RHIMemoryLedger.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Shader/Compile/ShaderRecompiler.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Graphics/Upload/GPUUploadQueue.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/StringTable.h"
#include "Engine/Module/ModuleTypeRegistry.h"
#include "Engine/Object/Animation/AnimationCrowd.h"
#include "Engine/Object/Animation/VertexAnimationCooker.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/ComponentDefaults.h"
#include "Engine/Object/GameObject/CameraRegistry.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Physics/PhysicsSystem.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Profiling/ProfilerBackend.h"
#include "Engine/Reflection/ReflectionDocWriter.h"
#include "Engine/Renderer/Capture/PortraitRenderer.h"
#include "Engine/Renderer/Capture/RenderDocCapture.h"
#include "Engine/Renderer/Cook/ShaderCookDriver.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/RenderFramePacket.h"
#include "Engine/Renderer/Frame/RenderViewCollector.h"
#include "Engine/Renderer/Frame/RenderViewScheduler.h"
#include "Engine/Renderer/Light/GPULightBuffer.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Renderer/Scene/GPUSceneBuilder.h"
#include "Engine/Resource/AssetDatabase.h"
#include "Engine/Resource/AssetLoadProfiler.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/AssetStreamingQueue.h"
#include "Engine/Resource/Image/ImageFileWriter.h"
#include "Engine/Resource/Pack/ResourcePackManager.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Scene/SceneCooker.h"
#include "Engine/Scene/SceneNavigationCooker.h"
#include "Engine/Telemetry/CrashReportService.h"
#include "Engine/Telemetry/TelemetryService.h"
#include "Engine/Text/FontSystem.h"
#include "Engine/Text/GlyphCache.h"
#include "Engine/UI/UiSystem.h"
#include "Engine/UserSettings/HardwareProbe.h"
#include "Engine/UserSettings/UserSettingsManager.h"
#include "Engine/UserSettings/UserSettingsVariables.h"
#include "Engine/Utility/CommandStack.h"
#include "Engine/Utility/DebugOverlayState.h"
#include "Engine/Utility/GameTimeScale.h"
#include "Engine/Window/IWindow.h"

#include "sw/config/ConfigConstants.h"
#include "sw/config/ShippingHostDefaults.h"

namespace sw
{
    /**
     * @brief `-gv_crashTest=N`: RHI 초기화 직후 일부러 크래시를 냅니다(리포트 경로 검증용). N 은 `CrashTestKind` 입니다.
     * @details 크래시 리포트는 크래시가 나야만 만들어집니다. 그래서 "덤프가 제대로 써지는가" 는 일부러
     *          죽여 보는 것 말고는 확인할 방법이 없습니다. 배포하고 나서 안 된다는 것을 알면 늦습니다. **죽는 방식마다** 따로 태워 봐야
     *          합니다 — 접근 위반으로 남는 리포트가 스택 오버플로 · abort 에서도 남는다는 보장은 없습니다.
     */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_crashTest, 0, "일부러 크래시를 내 리포트 경로를 검증합니다 (1=널 쓰기 2=스택 오버플로 3=작업 스레드 스택 오버플로 4=abort 5=순수 가상 호출)" );
    /**
     * @brief `-gv_rhiSwapAtFrame=N -gv_rhiSwapTo=<backend>`: N 번째 프레임에 백엔드 교체를 요청합니다.
     * @details 에디터 메뉴 없이(헤드리스로) 교체를 재현 · 검증하는 창구입니다. 요청 방식은 에디터
     *          패널과 같습니다(`GlobalVariableInfo::setValueAsInt` → 변경 콜백 → RHIBackendSwitcher 가 다음 프레임에
     *          적용). C++ 대입(`gv_rhiBackend = x`)은 콜백을 부르지 않아 아무 일도 일어나지 않습니다. 0 이면 꺼져 있습니다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_rhiSwapAtFrame, 0, "이 프레임에 백엔드 교체를 요청한다 (0=사용 안 함)" );
    SW_TEST_GLOBAL_VARIABLE( RHIBackend, gv_rhiSwapTo, RHIBackend::DirectX12, "gv_rhiSwapAtFrame 에 바꿀 백엔드" );
    /**
     * @brief `-gv_canvasTestPattern=1`: 주 출력에 캔버스 시험 그림(둥근 · 테두리 · 반투명 겹침 · 가위 · 둥근 자르기 · 그림자 · 글자)을 그립니다.
     * @details 위젯(UI 단계)이 생기기 전에 Canvas 패스 · canvas.hlsl · 글리프 아틀라스를 네 백엔드에서 눈으로 보는 길이다(스크린샷에도 들어간다).
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_canvasTestPattern, false, "주 출력에 캔버스(화면 2D) 시험 그림을 그린다 — 사각형 · 자르기 · 그림자 · 글자" );
    /**
     * @brief `-gv_dumpReflection=CameraComponent,CameraRole`: 첫 프레임에 그 타입 · enum 의 등록 내용을 로그로 남깁니다(`TypeRegistry::describeType`).
     * @details 첫 프레임이라 게임 · 에디터 모듈의 타입까지 등록된 뒤다. 한 번 찍고 비운다.
     */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_dumpReflection, "", "첫 프레임에 이 이름들(쉼표로 여럿)의 리플렉션 등록 내용을 로그로 남긴다 — 타입 · enum (비우면 사용 안 함)" );
    /** @brief 활성 씬의 강체 물리(바디 셰이프 · 캐릭터 캡슐)를 디버그 선으로 그립니다(`ScenePhysics::drawDebug` → `DebugDrawQueue`). */
    SW_GLOBAL_VARIABLE( bool, gv_physicsDebugDraw, false, "강체 물리 바디 · 캐릭터를 디버그 선으로 그린다" );
    SW_GLOBAL_VARIABLE( int32, gv_navDebugDraw, 0, "내비메시 디버그 — 선(편집기 뷰포트): 1 폴리곤 테두리 · 2 에이전트 경로 · 4 에이전트 속도 · 8 장애물, 16 걷는 면 · 경로를 게임 화면의 메시로 (31 = 모두, 0 = 끔)" );
    /** @brief `-gv_telemetryFolder=<경로>`: 텔레메트리 스풀 폴더입니다(자동화 — 사용자 폴더를 건드리지 않는다). 비면 사용자 설정 파일 옆의 `telemetry/`. */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( sw::string, gv_telemetryFolder, "", "텔레메트리 스풀 폴더 (비면 사용자 폴더의 telemetry/)" );

} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "EngineLoop" );

    // ------------------------------------------------------------------------------
    // 기동 단계 본문 — 표(`EngineInitStepList.xxx`)의 줄 순서대로 적는다. 단계마다 무엇을 세우고(initialize) · 내리고(shutdown) ·
    // 해제하는지(destroy)가 한 자리에 있다. 순서는 표가 정한다: 초기화는 위에서 아래로, 종료와 해제는 아래에서 위로.
    // 종료는 초기화한 단계에만 불린다. 해제는 모든 종료 뒤에 **표의 모든 단계**에 불리므로(기동이 어디서 멈췄든) null 안전해야 한다.
    // ------------------------------------------------------------------------------

    struct EngineLoop::CompressionStartupStep : EngineInitStepDefaults<EngineLoop>
    {
        static EngineInitResult initialize( EngineLoop& loop )
        {
            // 압축 레지스트리는 여기가 소유하고, Core 의 CompressionStream 이 볼 수 있도록 슬롯에 연결한다.
            // 스트림이 Core 에 있어서 엔진 서비스 테이블에는 닿지 못한다(Logger::setGlobalSink 와 같은 모양).
            loop._owned._pCompressionCodecRegistry->initialize();
            CompressionCodecRegistry::setActive( loop._owned._pCompressionCodecRegistry.get() );
            // 외부 라이브러리 코덱은 **목록이 있는 자리**에서 붙인다(EngineCompressionCodecUtil).
            EngineCompressionCodecUtil::registerAll( *loop._owned._pCompressionCodecRegistry );
            return EngineInitResult::Succeeded;
        }
        // 종료는 하지 않는다. 모듈이 등록한 코덱을 거두는 것은 등록한 모듈의 책임이다(registerCodec 주석 참고). 슬롯을 끊고 통째로 없앤다.
        // 맨 먼저 서므로 맨 나중에 해제된다 — 팩을 내리는 Resource 해제까지 코덱이 살아 있다.
        static void destroy( EngineLoop& loop ) { loop._owned.destroyCompressionCodecRegistry(); }
    };

    struct EngineLoop::ReflectionStartupStep : EngineInitStepDefaults<EngineLoop>
    {
        static EngineInitResult initialize( EngineLoop& )
        {
            // 여기서는 이미 이 프로세스에 올라온 등록기만 모은다. 배포 구성은 GameFramework · 킷 · 게임이 정적 링크라 "Engine" 이 다 모으고,
            // 개발 구성의 모듈 DLL 은 `ModuleTypes` 단계에서 호스트 로더가 올리며 제 이름으로 등록한다.
            engine::registerModuleTypes( "Engine" );
            // 명령줄의 enum 이름(`-gv_rhiBackend=Vulkan`)은 enum 표가 선 지금 적용한다. 모르는 이름이면 기본값으로 돌지 않고 멈춘다.
            if ( engine::bindGlobalVariableEnumNames() == false )
                return EngineInitResult::Failed;
            return EngineInitResult::Succeeded;
        }
        // 설정 · 에셋 · 씬 객체는 이 단계에 의존하므로 모두 먼저 해제된다.
        static void destroy( EngineLoop& loop )
        {
            engine::unbindGlobalVariableEnumNames();
            loop._owned._pTypeRegistry.reset();
        }
    };

    struct EngineLoop::ConfigStartupStep : EngineInitStepDefaults<EngineLoop>
    {
        static EngineInitResult initialize( EngineLoop& loop )
        {
            // 설정은 리소스 초기화보다 먼저 읽는다(Resource 의 의존 칸). `AssetManager::mountContent` 는 GameConfig 의 `_packRoot` 가
            // 정해져 있어야 팩을 제대로 마운트하고 게임 도메인의 `assetregistry.txt` 를 읽는다.
            // Config/ 는 프로젝트 루트에 있고 실행 파일은 build/<preset>/Bin 에서 돈다. 작업 디렉터리 기준으로만 찾으면
            // 모두 "없음" 이 되어 조용히 기본값으로 떨어지므로, Resource/ 를 찾을 때 알아낸 프로젝트 루트를 넘긴다.
            loop._configManager = make_unique<ConfigManager>();
            loop._configManager->setRootDirectory( ResourceUtil::getProjectFolderPath() );
            ConfigManager::setPrimary( loop._configManager.get() );
            loop._configManager->onConfigReloaded().add( SW_DELEGATE_METHOD( Delegate<void( const hashed_string& )>, &EngineLoop::onConfigReloaded, &loop ) );

            loop._pEngineConfig = loop._configManager->ensureConfig<EngineConfig>( config::kFileRuntimeEngineConfig, shipping_host::kEngineConfigJson );
            if ( loop._pEngineConfig == nullptr )
                return EngineInitResult::Failed;
            EngineConfig::setActive( *loop._pEngineConfig );

            // 틀린 설정 파일은 nullptr 이다(키 이름은 이미 오류로 남았다) — 기본값으로 뜨면 고친 값이 무시된 것을 아무도 모른다.
            const GameConfig* pGameConfig = loop._configManager->ensureConfig<GameConfig>( config::kFileRuntimeGameConfig, shipping_host::kGameConfigJson );
            if ( pGameConfig == nullptr )
                return EngineInitResult::Failed;
            GameConfig::setActive( *pGameConfig );

            // 메모리 태그 예산(데이터). 틀린 표는 오류를 남기고 예산 없이 간다 — 진단 설정 하나로 기동을 세우지 않는다.
            (void)loop._memoryBudgetMonitor.loadBudgetFile( FileUtil::joinPath( ResourceUtil::getProjectFolderPath(), MemoryBudgetMonitor::kBudgetFile ) );
            return EngineInitResult::Succeeded;
        }
        static void destroy( EngineLoop& loop )
        {
            // `_pEngineConfig` 는 설정 매니저가 든 객체를 가리킨다. 같이 놓는다.
            loop._pEngineConfig = nullptr;
            ConfigManager::setPrimary( nullptr );
            loop._configManager.reset();
        }
    };

    struct EngineLoop::ResourceStartupStep : EngineInitStepDefaults<EngineLoop>
    {
        static EngineInitResult initialize( EngineLoop& loop )
        {
            if ( loop._owned._pAssetManager->initialize() == false )
                return EngineInitResult::Failed;
            // GameConfig 가 활성화된 뒤라야 "game" 토큰이 팩 루트로 풀린다. 그 전제는 `mountContent` 의 인자에 드러나 있다.
            // 설정의 우선순위 목록이 비어 있으면 지금 것을 쓴다. 씬 쿠킹(Headless 단계)의 입력은 소스 트리다 — 팩은 그 산출물이다.
            bool bCookScenes = false;
            loop._owned._pCommandLineManager->getArgument( CommandLineArgument::COOK_SCENES, bCookScenes );
            const ContentSource contentSource = bCookScenes ? ContentSource::SourceTree : ContentSource::Cooked;
            loop._owned._pAssetManager->mountContent( loop._pEngineConfig->_listResourcePriority, contentSource );
            return EngineInitResult::Succeeded;
        }
        // 에셋 캐시를 비운다(`AssetManager::shutdown`). 에셋을 드는 단계(셰이더 캐시 · 오디오 · 입력 · 씬 · 렌더러)는 모두 이 단계 뒤에
        // 서므로, 역순 해제에서 그들의 소멸자가 에셋을 놓은 다음이다.
        static void destroy( EngineLoop& loop ) { loop._owned.destroyAssetManager(); }
    };

    struct EngineLoop::EngineDefaultAssetsStartupStep : EngineInitStepDefaults<EngineLoop>
    {
        static EngineInitResult initialize( EngineLoop& loop )
        {
            const bool bEngineDefaultAssetsLoaded = loop._owned._pEngineDefaultAssets->loadFromResource( path::kEngineDefaultAssets );
            if ( bEngineDefaultAssetsLoaded == false )
                SW_LOG_WARNING( "Engine data could not be read - using built-in defaults" );

            // 문화권 표 · 엔진 문자열 — 플레이어 설정(언어)이 이 뒤(`UserSettings`)에 적용되고, 게임은 자기 프로젝트를 위에 올린다.
            LocalizationManager&       localization  = *loop._owned._pLocalizationManager;
            const EngineDefaultAssets& defaultAssets = *loop._owned._pEngineDefaultAssets;
            if ( defaultAssets._cultureTable.empty() == false )
                (void)localization.loadCultureTable( defaultAssets._cultureTable ); // 실패는 loadCultureTable 이 오류로 남긴다
            if ( defaultAssets._localizationProject.empty() == false && localization.mountProject( defaultAssets._localizationProject, LocalizationScope::Engine ) == false )
                SW_LOG_ERROR( "Engine localization project '%#' is not (fully) loaded", defaultAssets._localizationProject.c_str() );
            string commandLineLanguage;
            if ( loop._owned._pCommandLineManager->getArgument( CommandLineArgument::LANGUAGE, commandLineLanguage ) && localization.hasLanguage( commandLineLanguage ) )
                localization.setCurrentLanguage( commandLineLanguage );
            return EngineInitResult::Succeeded;
        }
        static void destroy( EngineLoop& loop ) { loop._owned._pEngineDefaultAssets.reset(); }
    };

    struct EngineLoop::FontsStartupStep : EngineInitStepDefaults<EngineLoop>
    {
        static EngineInitResult initialize( EngineLoop& loop )
        {
            // 기본 가족(저장소 글꼴)을 못 열면 글자를 그릴 수 없다 — 기동 오류다. 시스템 대체 가족은 없어도 경고만 한다.
            const EngineDefaultAssets& defaultAssets = *loop._owned._pEngineDefaultAssets;
            return loop._owned._pFontSystem->initialize( defaultAssets._fontCatalog ) ? EngineInitResult::Succeeded : EngineInitResult::Failed;
        }
        static void shutdown( EngineLoop& loop ) { loop._owned._pFontSystem->shutdown(); }
        static void destroy( EngineLoop& loop ) { loop._owned._pFontSystem.reset(); }
    };

    struct EngineLoop::ShaderCacheStartupStep : EngineInitStepDefaults<EngineLoop>
    {
        static EngineInitResult initialize( EngineLoop& loop )
        {
            return loop._owned._pShaderCache->initialize() ? EngineInitResult::Succeeded : EngineInitResult::Failed;
        }
        static void shutdown( EngineLoop& loop ) { loop._owned._pShaderCache->shutdown(); }
        static void destroy( EngineLoop& loop ) { loop._owned._pShaderCache.reset(); }
    };

    struct EngineLoop::TaskStartupStep : EngineInitStepDefaults<EngineLoop>
    {
        static EngineInitResult initialize( EngineLoop& loop )
        {
            return loop._owned._pTaskManager->initialize() ? EngineInitResult::Succeeded : EngineInitResult::Failed;
        }
        static void shutdown( EngineLoop& loop ) { loop._owned._pTaskManager->shutdown(); }
        // 소멸자에서 태스크를 기다리는 객체(에셋 스트리밍 큐)는 이 단계에 의존하는 단계(Scene)가 먼저 해제한다.
        static void destroy( EngineLoop& loop ) { loop._owned._pTaskManager.reset(); }
    };

    struct EngineLoop::FileIoStartupStep : EngineInitStepDefaults<EngineLoop>
    {
        static EngineInitResult initialize( EngineLoop& loop )
        {
            // 완료 콜백(팩 해제 · CRC · 스트리밍 완료 기록)은 태스크 워커에서 돈다 — IO 스레드는 다음 읽기를 거는 일만 한다.
            AsyncFileIoSettings settings{};
            settings._pTaskManager = loop._owned._pTaskManager.get();
            return loop._owned._pAsyncFileIo->initialize( settings ) ? EngineInitResult::Succeeded : EngineInitResult::Failed;
        }
        // 큐를 비우고 걸린 읽기와 완료 콜백(태스크)을 다 기다린다. Task 보다 먼저, 모듈을 내리기 전에 내려간다.
        static void shutdown( EngineLoop& loop ) { loop._owned._pAsyncFileIo->shutdown(); }
    };

    struct EngineLoop::ModuleImagesStartupStep : EngineInitStepDefaults<EngineLoop>
    {
        // 모듈은 이 뒤(`ModuleTypes` 단계의 호스트 로더 · App 의 ModuleHost)에 오른다. 이 단계는 종료 쪽 자리(모듈을 내리는 구간)를 순서에 박아 둔다.
        static void shutdown( EngineLoop& loop )
        {
            // 씬은 방금 사라졌고 서비스는 아직 살아 있다. 모듈 DLL 을 내리기에 알맞은 유일한 자리다.
            // Engine 은 거기서 무슨 일이 일어나는지 모른다(App 이 핫 리로드를 건다).
            if ( loop._onScenesReleased.isBound() )
                loop._onScenesReleased();
        }
    };

    struct EngineLoop::AudioStartupStep : EngineInitStepDefaults<EngineLoop>
    {
        static EngineInitResult initialize( EngineLoop& loop )
        {
            return loop._audioSystem->initialize() ? EngineInitResult::Succeeded : EngineInitResult::Failed;
        }
        static void shutdown( EngineLoop& loop ) { loop._audioSystem->shutdown(); }
        static void destroy( EngineLoop& loop ) { loop._audioSystem.reset(); }
    };

    struct EngineLoop::InputStartupStep : EngineInitStepDefaults<EngineLoop>
    {
        static EngineInitResult initialize( EngineLoop& loop )
        {
            return loop._owned._pInputManager->initialize() ? EngineInitResult::Succeeded : EngineInitResult::Failed;
        }
        static void shutdown( EngineLoop& loop ) { loop._owned._pInputManager->shutdown(); }
        static void destroy( EngineLoop& loop )
        {
            // 셸 디버그 액션 맵은 입력 매니저를 가리킨다. 먼저 놓는다(`updateShellActions` 가 처음 쓸 때 만든다).
            loop._mapDebugAction.reset();
            loop._bShellActionsBound = false;
            loop._owned._pInputManager.reset();
        }
    };

    struct EngineLoop::ModuleTypesStartupStep : EngineInitStepDefaults<EngineLoop>
    {
        static EngineInitResult initialize( EngineLoop& loop )
        {
            // 호스트가 동적으로 올리는 타입 공급자(개발 구성의 GameFramework · 킷 · 게임 DLL)를 여기서 올린다. 로더가 없으면 올릴 것이 없다
            // (배포 구성은 정적 링크라 리플렉션 단계가 이미 다 모았다). 이 뒤로 씬을 읽는다 — 헤드리스 쿠킹이 이 단계에 의존한다.
            if ( loop._moduleTypeLoader.isBound() && loop._moduleTypeLoader() == false )
            {
                SW_LOG_ERROR( "The host could not load its type-providing modules - scenes are not read" );
                return EngineInitResult::Failed;
            }
            loop._owned._pTypeRegistry->markAllModuleTypesRegistered();
            return EngineInitResult::Succeeded;
        }
    };

    struct EngineLoop::PhysicsStartupStep : EngineInitStepDefaults<EngineLoop>
    {
        static EngineInitResult initialize( EngineLoop& loop )
        {
            return loop._owned._pPhysicsSystem->initialize() ? EngineInitResult::Succeeded : EngineInitResult::Failed;
        }
        // 씬(과 그 물리 씬)은 Scene 단계 종료에서 이미 사라졌다 — 백엔드를 내린다.
        static void shutdown( EngineLoop& loop ) { loop._owned._pPhysicsSystem->shutdown(); }
    };

    struct EngineLoop::SceneStartupStep : EngineInitStepDefaults<EngineLoop>
    {
        static EngineInitResult initialize( EngineLoop& loop )
        {
            return loop._owned._pSceneManager->initialize() ? EngineInitResult::Succeeded : EngineInitResult::Failed;
        }
        static void shutdown( EngineLoop& loop ) { loop._owned._pSceneManager->shutdown(); }
        static void destroy( EngineLoop& loop )
        {
            loop._owned._pSceneManager.reset();
            // 씬 · 컴포넌트가 에셋을 요청하는 큐다. 소멸자가 태스크를 기다리므로(`waitAll`) Task 해제보다 먼저여야 한다 — 이 단계가 Task 에 의존한다.
            loop._owned._pAssetStreamingQueue.reset();
            // 에디터 Undo 기록은 씬 객체를 가리킨다(`SceneManager::shutdown` 이 이미 비웠다).
            loop._commandStack.reset();
        }
    };

    struct EngineLoop::HeadlessStartupStep : EngineInitStepDefaults<EngineLoop>
    {
        static EngineInitResult initialize( EngineLoop& loop )
        {
            // 전용 서버가 하는 헤드리스 작업은 씬 쿠킹(서버 팩)까지다. 셰이더 쿠킹(DXC) · 원본 임포트(에디터 모듈)는 App 의 일이다 — 조용히 넘기지 않고 실패한다.
            if ( loop._hostRole == EngineHostRole::DedicatedServer )
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
                    if ( loop._owned._pCommandLineManager->getArgument( argument, bGiven ) && bGiven )
                    {
                        SW_LOG_ERROR( "This headless task runs in the client build (App), not in the dedicated server" );
                        loop._bHeadless           = true;
                        loop._bHeadlessTaskFailed = true;
                        return EngineInitResult::SkipDependents;
                    }
                }
            }

            // 헤드리스 작업은 보통 실행이 누수 기준선을 잡는 자리(`initialize` 끝)에 닿지 않는다. 기준선이 없으면 종료 때 살아 있는 블록을 모두
            // 누수로 찍으므로, 작업 직전에 같은 기준선을 잡는다 — 작업이 만든 것을 종료까지 놓지 않을 때만 누수로 보인다.
            bool bCookShaders = false;
            if ( loop._owned._pCommandLineManager->getArgument( CommandLineArgument::COOK_SHADERS, bCookShaders ) && bCookShaders )
            {
                MemoryProfiler::captureMemoryLeakBaseline();
                loop._bHeadless = true;
                SW_LOG_INFO( "Starting Headless (CookShaders)..." );
                const ShaderCookSummary summary = ShaderCookDriver::cookAllShaders();
                loop._bHeadlessTaskFailed       = summary.isClean() == false;
                return EngineInitResult::SkipDependents;
            }

            // 리플렉션 문서도 같은 자리다 — 모든 타입 공급자가 등록을 끝낸 뒤(ModuleTypes)라 게임 · 킷의 타입까지 든다.
            string reflectionDocsDir;
            if ( loop._owned._pCommandLineManager->getArgument( CommandLineArgument::WRITE_REFLECTION_DOCS, reflectionDocsDir ) && reflectionDocsDir.empty() == false )
            {
                loop._bHeadless           = true;
                loop._bHeadlessTaskFailed = ReflectionDocWriter::writeMarkdown( engine::getTypeRegistry(), reflectionDocsDir ) == 0;
                return EngineInitResult::SkipDependents;
            }

            // 리타깃 굽기(`--bake-retarget=<프로필>,<원본 클립>,<출력 클립>`) — 클립 · 스켈레톤 · 코덱이 모두 엔진 것이라 여기서 한다.
            string bakeRetarget;
            if ( loop._owned._pCommandLineManager->getArgument( CommandLineArgument::BAKE_RETARGET, bakeRetarget ) && bakeRetarget.empty() == false )
            {
                MemoryProfiler::captureMemoryLeakBaseline();
                loop._bHeadless = true;
                const string_splitter      parts( bakeRetarget, { "," } );
                const vector<string_view>& listArgument = parts.getSplitList();
                const bool                 bWellFormed  = listArgument.size() == 3;
                if ( bWellFormed == false )
                    SW_LOG_ERROR( "--bake-retarget expects <profile>,<source clip>,<output clip>" );
                loop._bHeadlessTaskFailed = bWellFormed == false ||
                                            RetargetBakeUtil::bakeClipFile( StringUtil::trim( listArgument[0] ), StringUtil::trim( listArgument[1] ), StringUtil::trim( listArgument[2] ) ) == false;
                return EngineInitResult::SkipDependents;
            }

            // 씬 쿠킹도 같은 자리다. 엔티티 상태를 바이너리로 구우려면 리플렉션이 필요하고,
            // 그것은 엔진 안에만 있어서 `CookAssets.py` 가 이쪽으로 넘겨준다.
            bool bCookScenes = false;
            if ( loop._owned._pCommandLineManager->getArgument( CommandLineArgument::COOK_SCENES, bCookScenes ) && bCookScenes )
            {
                MemoryProfiler::captureMemoryLeakBaseline();
                loop._bHeadless = true;
                string cookedDir;
                loop._owned._pCommandLineManager->getArgument( CommandLineArgument::COOKED_DIR, cookedDir );
                SW_LOG_INFO( "Starting Headless (CookScenes) -> '%#'...", cookedDir );
                const string& resourceRoot     = ResourceUtil::getRootFolderPath();
                uint32        sceneFailedCount = 0;
                const uint32  sceneCount       = SceneCooker::cookAllScenes( resourceRoot, cookedDir, sceneFailedCount );
                // 프리팹 · GUID 레지스트리도 여기서 만든다 — 형식과 규칙을 쓰는 곳이 엔진 하나여야 한다.
                uint32                        prefabFailedCount   = 0;
                [[maybe_unused]] const uint32 prefabCount         = PrefabCache::cookAllPrefabs( resourceRoot, cookedDir, prefabFailedCount );
                uint32                        registryFailedCount = 0;
                [[maybe_unused]] const uint32 registryCount       = AssetDatabase::writeRegistryFiles( resourceRoot, cookedDir, registryFailedCount );
                // 정점 애니메이션(VAT) — 데이터가 고른 (메시, 클립)을 굽는다. 프레임율은 군중 표의 것이라 런타임 굽기와 같다.
                AnimationCrowdSettings crowdSettings{};
                if ( ResourceUtil::hasResource( AnimationCrowdSettings::kResourcePath ) )
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
                loop._bHeadlessTaskFailed = sceneCount == 0 || sceneFailedCount > 0 || prefabFailedCount > 0 || registryFailedCount > 0 ||
                                            vertexAnimationFailedCount > 0 || navMeshFailedCount > 0;
                return EngineInitResult::SkipDependents;
            }

            // 립싱크 분석 — 음성마다 비즘 트랙을 곁에 쓴다. 분석 표가 엔진 데이터라 엔진이 한다.
            bool bImportLipSync = false;
            if ( loop._owned._pCommandLineManager->getArgument( CommandLineArgument::IMPORT_LIPSYNC, bImportLipSync ) && bImportLipSync )
            {
                MemoryProfiler::captureMemoryLeakBaseline();
                loop._bHeadless = true;
                SW_LOG_INFO( "Starting Headless (ImportLipSync)..." );
                LipSyncSettings         lipSyncSettings{};
                uint32                  lipSyncFailedCount = 0;
                [[maybe_unused]] uint32 lipSyncCount       = 0; // 로그로만 쓴다(Shipping 은 빠진다)
                if ( lipSyncSettings.loadFromResource( LipSyncSettings::kResourcePath ) )
                    lipSyncCount = LipSyncImport::importAll( ResourceUtil::getRootFolderPath(), lipSyncSettings, lipSyncFailedCount );
                else
                    ++lipSyncFailedCount;
                SW_LOG_INFO( "Imported %# viseme tracks (%# failures).", lipSyncCount, lipSyncFailedCount );
                loop._bHeadlessTaskFailed = lipSyncFailedCount > 0;
                return EngineInitResult::SkipDependents;
            }

            // 로컬라이제이션 도구(글 수집 · PO 교환)와 원본 임포트 · 대조(텍스처 · 모델 · 높이장)는 에디터 모듈이 한다(엔진은 에디터를 모른다).
            // 여기서는 창 · RHI 없이 세우기만 하고, 모듈을 올려 부르는 것은 App 이다(`EditorModuleHost::runLocalizationWithEditorModule` ·
            // `importAssetsWithEditorModule`). 이 단계는 타입 공급자 모듈 뒤라 글 수집이 리플렉션 프로퍼티(`Meta = "Localizable"`)를 본다.
            bool   bGatherText = false;
            bool   bCheckText  = false;
            bool   bExportPo   = false;
            string importPoPath;
            loop._owned._pCommandLineManager->getArgument( CommandLineArgument::GATHER_TEXT, bGatherText );
            loop._owned._pCommandLineManager->getArgument( CommandLineArgument::CHECK_TEXT, bCheckText );
            loop._owned._pCommandLineManager->getArgument( CommandLineArgument::EXPORT_PO, bExportPo );
            loop._owned._pCommandLineManager->getArgument( CommandLineArgument::IMPORT_PO, importPoPath );
            const bool bAnyLocalization = bGatherText || bCheckText || bExportPo || importPoPath.empty() == false;

            bool bImportTextures     = false;
            bool bCheckTextures      = false;
            bool bImportModels       = false;
            bool bCheckModels        = false;
            bool bImportHeightfields = false;
            bool bCheckHeightfields  = false;
            loop._owned._pCommandLineManager->getArgument( CommandLineArgument::IMPORT_TEXTURES, bImportTextures );
            loop._owned._pCommandLineManager->getArgument( CommandLineArgument::CHECK_TEXTURES, bCheckTextures );
            loop._owned._pCommandLineManager->getArgument( CommandLineArgument::IMPORT_MODELS, bImportModels );
            loop._owned._pCommandLineManager->getArgument( CommandLineArgument::CHECK_MODELS, bCheckModels );
            loop._owned._pCommandLineManager->getArgument( CommandLineArgument::IMPORT_HEIGHTFIELDS, bImportHeightfields );
            loop._owned._pCommandLineManager->getArgument( CommandLineArgument::CHECK_HEIGHTFIELDS, bCheckHeightfields );
            const bool bAnyImport = bImportTextures || bCheckTextures || bImportModels || bCheckModels || bImportHeightfields || bCheckHeightfields;
            if ( bAnyLocalization || bAnyImport )
            {
                MemoryProfiler::captureMemoryLeakBaseline();
                loop._bHeadless = true;
                SW_LOG_INFO( "Starting Headless (%#)...", bAnyLocalization ? "localization tools" : "asset import/check" );
                return EngineInitResult::SkipDependents;
            }
            return EngineInitResult::Succeeded;
        }
    };

    struct EngineLoop::UserSettingsStartupStep : EngineInitStepDefaults<EngineLoop>
    {
        static EngineInitResult initialize( EngineLoop& loop )
        {
            UserSettingsManager& settings = *loop._owned._pUserSettingsManager;
            UserSettingsTargets  targets;
            targets._pGlobalVariableManager = loop._owned._pGlobalVariableManager.get();
            targets._pCommandLineManager    = loop._owned._pCommandLineManager.get();
            targets._pInputMap              = &loop._owned._pInputManager->getInputMap();
            targets._pAudioSystem           = loop._audioSystem.get();
            targets._pLocalizationManager   = loop._owned._pLocalizationManager.get();
            settings.initialize( targets );

            // 스키마 · 사용자 파일이 틀려도 기동은 멈추지 않는다(오류로 알리고 옵션 메뉴가 비거나 기본값이다). 화면을 못 띄우는 설정 파일 하나로
            // 게임이 안 뜨면 플레이어가 고칠 길이 없다.
            const GameConfig& gameConfig = GameConfig::getActive();
            if ( settings.loadSchema( engine::getEngineDefaultAssets()._userSettingsSchema ) && gameConfig._userSettingsSchema.empty() == false )
            {
                const string gameSchema = FileUtil::joinPath( FileUtil::trimTrailingSlashes( gameConfig._packRoot ), gameConfig._userSettingsSchema );
                if ( settings.loadSchema( gameSchema ) == false )
                    SW_LOG_ERROR( "Game user settings schema '%#' is not loaded", gameSchema.c_str() );
            }
            for ( const auto& [settingId, value] : gameConfig._mapUserSettingDefault )
            {
                (void)settings.setGameDefault( hashed_string( settingId ), value );
            }

            // 사용자 폴더의 파일이다(세이브 게임과 별개). 자동화는 `-gv_userSettingsFile` 로 사용자 폴더를 건드리지 않는다.
            const string gameName = FileUtil::getFileNamePart( FileUtil::trimTrailingSlashes( gameConfig._packRoot ) );
            settings.setUserFilePath( gv_userSettingsFile.empty() ? UserSettingsManager::makeDefaultUserFilePath( gameName ) : gv_userSettingsFile );
            if ( settings.loadUserFile( settings.getUserFilePath() ) == false )
                (void)settings.applyAutoDetectedPreset( HardwareProbe::probe() ); // 맞는 프리셋이 없으면 기본값 그대로다
            settings.reapplyAll();
            return EngineInitResult::Succeeded;
        }
        static void shutdown( EngineLoop& loop ) { loop._owned._pUserSettingsManager->shutdown(); }
        static void destroy( EngineLoop& loop ) { loop._owned._pUserSettingsManager.reset(); }
    };

    struct EngineLoop::RHIStartupStep : EngineInitStepDefaults<EngineLoop>
    {
        static EngineInitResult initialize( EngineLoop& loop )
        {
            // RenderDoc 은 그래픽 API 를 만들 때 끼어든다 — 디바이스보다 먼저 찾고(`-renderdoc` 면 올리고) 백엔드 교체 뒤에도 그대로 쓴다.
            RenderDocCapture::initialize( loop._owned._pCommandLineManager->isArgumentProvided( CommandLineArgument::RENDERDOC ) );

            // 커맨드라인이 백엔드를 명시하지 않았을 때만 설정 기본값이 이긴다.
            RHIBackend commandLineBackend{};
            if ( RHIBackendUtil::findCommandLineBackend( *loop._owned._pCommandLineManager, commandLineBackend ) == false )
                gv_rhiBackend = loop._pEngineConfig->_window._defaultRHI;

            // 화면 값의 순서: 엔진 설정(EngineConfig) → 플레이어가 고른 사용자 설정(기본값이 아닌 것) → 명령줄.
            const DisplaySettingsRequest& display = loop._owned._pUserSettingsManager->getDisplayRequest();
            if ( IWindow::getActiveWindow() == nullptr )
            {
                uint32 windowWidth  = display._bHasResolution ? display._width : loop._pEngineConfig->_window._width;
                uint32 windowHeight = display._bHasResolution ? display._height : loop._pEngineConfig->_window._height;
                // 인자를 주지 않으면 getArgument 가 false 를 돌려주므로 설정값이 그대로 남는다.
                loop._owned._pCommandLineManager->getArgument( CommandLineArgument::WIDTH, windowWidth );
                loop._owned._pCommandLineManager->getArgument( CommandLineArgument::HEIGHT, windowHeight );

                unique_ptr<IWindow> defaultWindow = IWindow::createPlatformWindow();
                if ( defaultWindow != nullptr && defaultWindow->initializeWindow( GameConfig::getActive()._windowTitle.c_str(), windowWidth, windowHeight ) )
                {
                    // 전체 화면은 스왑체인을 만들기 전에 고른다 — 스왑체인이 처음부터 모니터 크기다.
                    if ( display._bHasMode && display._mode != WindowDisplayMode::Windowed )
                        (void)defaultWindow->setDisplayMode( display._mode, windowWidth, windowHeight );
                    // 소유권은 App::initialize 가 IWindow::getActiveWindow() 로 넘겨받는다.
                    // (App 이 없는 임베드 시나리오라면 부르는 쪽이 getActiveWindow() 를 직접 소유해야 한다.)
                    IWindow::setActiveWindow( defaultWindow.release() );
                }
            }

            loop._rhi = make_unique<RHI>();
            loop._rhi->setPreferredVSync( display._bHasVSync ? display._bVSync : loop._pEngineConfig->_window._bVSync );
            // RHI 는 창 시스템을 모른다. 표면(IRenderSurface)만 넘긴다. 창은 위에서 만들었거나 호스트가 들고 있다.
            const bool bRhiReady = loop._rhi->initialize( IWindow::getActiveWindow() );
            // 단계가 내려가면(destroy 는 모든 단계를 돈다) _rhi 가 사라진다 — 실패의 이유를 그 전에 남긴다. App 이 이것으로 종료 코드를 고른다.
            loop._rhiInitResult = loop._rhi->getInitResult();
            if ( bRhiReady == false )
                return EngineInitResult::Failed;
            // 백엔드가 정해졌으니 크래시 리포트에 남긴다. 이 저장소는 백엔드가 넷이라 "어느
            // 백엔드에서 났는가" 가 범위를 좁히는 첫 질문이다.
            CrashHandler::setContextValue( "RHI", loop._rhi->getDevice().getBackendName() );

            // `-gv_crashTest=1`: 크래시 리포트 경로를 실제로 확인하는 유일한 방법이다. 리포트는
            // 크래시가 나야만 만들어지므로, 일부러 한 번 죽여 보지 않으면 배포 뒤에야 안 되는 것을 안다.
            if ( gv_crashTest != 0 )
                CrashHandler::crashForTest( static_cast<CrashTestKind>( gv_crashTest ) );
            return EngineInitResult::Succeeded;
        }
        static void shutdown( EngineLoop& loop )
        {
            // 디바이스 생성이 실패하면 RHI 객체는 있어도 **디바이스가 없다.** getDevice() 는
            // 널 참조를 역참조하므로 hasDevice() 로 먼저 막는다.
            if ( loop._rhi->hasDevice() )
                loop._rhi->getDevice().waitIdle();
            // GPU 자원을 든 객체들은 여기서 손으로 훑지 않는다. IRHIDevice::shutdown 이 내려가기 직전에
            // 등록부 전체에 releaseRhi 를 부른다(RHIRenderResource). 백엔드 모듈 DLL 도 여기서 내린다.
            loop._rhi->shutdown();
        }
        // 종료가 디바이스와 백엔드 모듈을 이미 내렸다. 남은 것은 디바이스가 없는 팩토리 객체다.
        static void destroy( EngineLoop& loop ) { loop._rhi.reset(); }
    };

    struct EngineLoop::FrameRendererStartupStep : EngineInitStepDefaults<EngineLoop>
    {
        static EngineInitResult initialize( EngineLoop& loop )
        {
            // 씬 스냅샷 · 패킷 · 업로드 큐는 그리는 쪽의 것이다. 렌더러를 세우지 않는 헤드리스 작업에는 없다.
            // 백엔드 교체로 다시 설 때(`restartStoppedSteps`)는 있는 것을 그대로 쓰고, 디바이스에 매인 설정만 새로 건다.
            if ( loop._gpuSceneBuilder == nullptr )
                loop._gpuSceneBuilder = make_unique<GPUSceneBuilder>();
            if ( loop._packetScratch == nullptr )
                loop._packetScratch = make_unique<RenderFramePacket>();
            if ( loop._gpuUploadQueue == nullptr )
                loop._gpuUploadQueue = make_unique<GPUUploadQueue>();
            if ( loop._renderViewScheduler == nullptr )
                loop._renderViewScheduler = make_unique<RenderViewScheduler>();
            // GT 쪽 GPUScene 이 배치를 만든다. 텍스처를 인덱스로 고를 수 있는 백엔드면 머티리얼이 달라도
            // 셰이더 타입 단위로 합친다(언리얼 GPUScene).
            loop._gpuSceneBuilder->setMergeBatchesAcrossMaterials( loop._rhi->getDevice().supportsNativeBindlessSampling() );
            loop._gpuUploadQueue->bindDevice( &loop._rhi->getDevice(), loop._owned._pTaskManager.get() );

            if ( loop._frameRenderer->initialize( &loop._rhi->getDevice(), loop._owned._pTaskManager.get() ) == false )
            {
                SW_LOG_ERROR( "Failed to initialize FrameRenderer!" );
                return EngineInitResult::Failed;
            }
            return EngineInitResult::Succeeded;
        }
        static void shutdown( EngineLoop& loop )
        {
            // GT 쪽 GPUScene 도 스냅샷의 소유(머티리얼 · 인스턴스)를 들고 있다. 렌더러와 같은 시점에, **디바이스가 살아 있을 때** 놓는다.
            // 해제(destroy)까지 미루면 디바이스가 사라진 뒤에 놓게 된다.
            loop._gpuSceneBuilder->clear();
            loop._frameRenderer->shutdown();
        }
        static void destroy( EngineLoop& loop )
        {
            loop._gpuUploadQueue.reset();
            loop._renderViewScheduler.reset();
            loop._packetScratch.reset();
            loop._gpuSceneBuilder.reset();
            loop._frameRenderer.reset();
        }
    };

    struct EngineLoop::RenderThreadStartupStep : EngineInitStepDefaults<EngineLoop>
    {
        static EngineInitResult initialize( EngineLoop& loop )
        {
            // 다시 설 때는 같은 객체에 붙인다 — 호스트가 건 프레젠트 훅과 렌더 스레드 포인터(ModuleHost)가 그대로 남는다.
            if ( loop._renderThread == nullptr )
                loop._renderThread = make_unique<RenderThread>();
            if ( loop._renderThread->attach( &loop._rhi->getDevice(), loop._frameRenderer.get() ) == false )
            {
                SW_LOG_ERROR( "Failed to attach RenderThread!" );
                return EngineInitResult::Failed;
            }
            return EngineInitResult::Succeeded;
        }
        static void shutdown( EngineLoop& loop )
        {
            loop._renderThread->waitIdle();
            loop._renderThread->stop();
        }
        static void destroy( EngineLoop& loop ) { loop._renderThread.reset(); }
    };

    struct EngineLoop::LiveShaderStartupStep : EngineInitStepDefaults<EngineLoop>
    {
        static EngineInitResult initialize( EngineLoop& loop )
        {
#if defined( SW_DEBUG )
            // 셰이더 라이브 리로드는 개발 도구다. Debug 에서만 만든다(Shipping 에는 코드 자체가 없다).
            if ( loop._shaderRecompiler == nullptr )
                loop._shaderRecompiler = make_unique<ShaderRecompiler>();
            if ( loop._shaderRecompiler->initialize( "Shaders" ) == false )
            {
                SW_LOG_ERROR( "Failed to initialize ShaderRecompiler!" );
                loop._shaderRecompiler.reset();
            }
#endif
            ShaderRecompiler* pShaderRecompiler = loop.getShaderRecompiler();
            if ( pShaderRecompiler == nullptr )
                return EngineInitResult::Succeeded;
            // onShaderRecompiled 는 셰이더 바인딩 레이아웃 캐시 항목을 **파괴**하는데,
            // FrameRenderer::_mapPsoLayout 과 패스 컨텍스트의 1-entry 캐시가 그 실체를 가리키는
            // 생포인터를 들고 있다. 이 콜백은 게임 스레드(tick 의 핫 리로드 블록)에서 불리고
            // 렌더 스레드는 직전 패킷을 그리는 중이라, 렌더 스레드를 세운 뒤에 반영한다.
            // 재컴파일은 개발 중 가끔 일어나는 일이라 이때의 스톨은 문제가 되지 않는다.
            EngineLoop* pLoop        = &loop;
            auto        onRecompiled = [pLoop]( string_view shaderPath, const ShaderCompileResult& result )
            {
                if ( pLoop->_renderThread != nullptr )
                    pLoop->_renderThread->waitIdle();
                pLoop->_frameRenderer->onShaderRecompiled( shaderPath, result );
            };
            pShaderRecompiler->setOnAnyShaderRecompiled( SW_DELEGATE_LAMBDA( ShaderRecompiledDelegate, onRecompiled ) );
            return EngineInitResult::Succeeded;
        }
        static void shutdown( [[maybe_unused]] EngineLoop& loop )
        {
#if !defined( SW_SHIPPING )
            if ( loop._shaderRecompiler != nullptr )
                loop._shaderRecompiler->shutdown();
#endif
        }
        static void destroy( [[maybe_unused]] EngineLoop& loop )
        {
#if !defined( SW_SHIPPING )
            loop._shaderRecompiler.reset();
#endif
        }
    };

    struct EngineLoop::SceneRhiStartupStep : EngineInitStepDefaults<EngineLoop>
    {
        static EngineInitResult initialize( EngineLoop& loop )
        {
            loop._owned._pSceneManager->setRhiDevice( &loop._rhi->getDevice() );
            return EngineInitResult::Succeeded;
        }
        // 종료는 씬에서 디바이스를 떼는 것이 처음이다(표의 마지막 줄).
        static void shutdown( EngineLoop& loop ) { loop._owned._pSceneManager->setRhiDevice( nullptr ); }
    };

    struct EngineLoop::TelemetryStartupStep : EngineInitStepDefaults<EngineLoop>
    {
        static EngineInitResult initialize( EngineLoop& loop )
        {
            // 문맥은 파일마다 첫 줄 — 크래시 보고 · 로그와 같은 세션 id, 심볼과 짝지을 빌드 id.
            const GameConfig& gameConfig = GameConfig::getActive();
            TelemetryContext  context;
            context._sessionId   = CrashHandler::getSessionId();
            context._buildConfig = build::kConfigName;
            context._platform    = build::kPlatformName;
            context._buildId     = ModuleBuildId::find( nullptr )._id;
            context._game        = FileUtil::getFileNamePart( FileUtil::trimTrailingSlashes( gameConfig._packRoot ) );
            const string folder  = gv_telemetryFolder.empty()
                                     ? FileUtil::joinPath( FileUtil::getDirectoryPart( loop._owned._pUserSettingsManager->getUserFilePath() ), "telemetry" )
                                     : string( gv_telemetryFolder );

            TelemetryService& telemetry = *loop._owned._pTelemetryService;
            // 스키마가 틀려도 기동은 멈추지 않는다 — 스키마에 없는 사건은 기록되지 않을 뿐이다.
            if ( telemetry.loadSchema( engine::getEngineDefaultAssets()._telemetrySchema ) == false )
                SW_LOG_ERROR( "Engine telemetry schema '%#' is not loaded", engine::getEngineDefaultAssets()._telemetrySchema.c_str() );
            if ( gameConfig._telemetrySchema.empty() == false )
            {
                const string gameSchema = FileUtil::joinPath( FileUtil::trimTrailingSlashes( gameConfig._packRoot ), gameConfig._telemetrySchema );
                if ( telemetry.loadSchema( gameSchema ) == false )
                    SW_LOG_ERROR( "Game telemetry schema '%#' is not loaded", gameSchema.c_str() );
            }
            telemetry.initialize( folder, context );
            // 동의는 플레이어 옵션(기본 꺼짐)이다. 꺼져 있으면 지난 실행이 남긴 스풀까지 지운다.
            telemetry.bindConsentSetting( *loop._owned._pUserSettingsManager );

            // 지난 실행의 크래시를 묶는다(덤프 · 로그 폴더 옆의 Saved/CrashReports). 기본 동의(local)는 묶기만 하고, 보낼 것이 있을 때만 보고 프로세스를 띄운다.
            const string crashFolder = getCrashReportFolder();
            if ( crashFolder.empty() == false )
            {
                CrashReportService& crashReports = *loop._owned._pCrashReportService;
                const string        reportsFolder =
                    FileUtil::joinPath( FileUtil::getDirectoryPart( FileUtil::trimTrailingSlashes( crashFolder ) ), CrashReportService::kReportsFolderName );
                crashReports.initialize( crashFolder, reportsFolder, CrashHandler::getSessionId() );
                crashReports.bindConsentSetting( *loop._owned._pUserSettingsManager );
                crashReports.setReporterExecutable( FileUtil::getExecutablePath() ); // App 이 -crash-reporter 를 알아듣는다(App::initialize 헤드리스 분기)
                (void)crashReports.collectNewCrashes();
                (void)crashReports.launchReporterProcess();
            }
            return EngineInitResult::Succeeded;
        }
        static void shutdown( EngineLoop& loop )
        {
            loop._owned._pCrashReportService->shutdown();
            loop._owned._pTelemetryService->shutdown();
        }
    };

    struct EngineLoop::UiStartupStep : EngineInitStepDefaults<EngineLoop>
    {
        static EngineInitResult initialize( EngineLoop& loop )
        {
            // UI 배율 규칙 — 게임 프리셋이 팩 상대 경로로 덮어쓰면 그것, 아니면 엔진 기본(engine/ui/uiscale.xml). 모르는 키는 기동 오류다.
            const EngineDefaultAssets& defaultAssets = *loop._owned._pEngineDefaultAssets;
            const GameConfig&          gameConfig    = GameConfig::getActive();
            const string               scalePath     = gameConfig._uiScaleSettings.empty()
                                                         ? string( defaultAssets._uiScaleSettings )
                                                         : FileUtil::joinPath( FileUtil::trimTrailingSlashes( gameConfig._packRoot ), gameConfig._uiScaleSettings );
            UiScaleSettings            scaleSettings{};
            if ( scaleSettings.loadFromResource( scalePath ) == false )
                return EngineInitResult::Failed;
            UiSystem& ui = *loop._owned._pUiSystem;
            ui.setScaleSettings( scaleSettings );
            // UI 행동 맵(탐색 · 확인 · 뒤로)을 못 읽으면 메뉴를 패드로 다룰 수 없다 — 데이터 오류라 기동 실패다.
            if ( ui.initialize( *loop._owned._pInputManager, loop._owned._pFontSystem.get(), defaultAssets._uiInputMap ) == false )
                return EngineInitResult::Failed;
            // UI 문서 · 스타일 시트 캐시를 에셋 캐시 등록부에 올린다 — 핫 리로드 · 종료 · 진단이 다른 에셋과 같은 길로 간다.
            loop._owned._pAssetManager->registerAssetCache( &ui.getDocumentCache() );
            loop._owned._pAssetManager->registerAssetCache( &ui.getStyleSheetCache() );
            // 테마 목록 — 게임 프리셋이 덮어쓰면 그것, 아니면 엔진 기본. 모르는 키는 기동 오류다(배율 규칙과 같다).
            const string   themePath = gameConfig._uiThemes.empty() ? string( defaultAssets._uiThemes )
                                                                    : FileUtil::joinPath( FileUtil::trimTrailingSlashes( gameConfig._packRoot ), gameConfig._uiThemes );
            UiThemeCatalog themes{};
            if ( themes.loadFromResource( themePath ) == false )
                return EngineInitResult::Failed;
            ui.setThemeCatalog( themes );
            // 엔진 기본 메뉴(옵션 · 일시정지) — 게임 프리셋이 팩 상대 경로로 문서를 덮어쓴다. 일시정지 메뉴는 게임이 켤 때만.
            const string packRoot = FileUtil::trimTrailingSlashes( gameConfig._packRoot );
            ui.setOptionsMenuDocument( gameConfig._uiOptionsMenu.empty() ? string( defaultAssets._uiOptionsMenu )
                                                                         : FileUtil::joinPath( packRoot, gameConfig._uiOptionsMenu ) );
            if ( gameConfig._bUiPauseMenu )
                ui.setPauseMenuDocument( gameConfig._uiPauseMenu.empty() ? string( defaultAssets._uiPauseMenu )
                                                                         : FileUtil::joinPath( packRoot, gameConfig._uiPauseMenu ) );
            return EngineInitResult::Succeeded;
        }
        static void shutdown( EngineLoop& loop )
        {
            UiSystem& ui = *loop._owned._pUiSystem;
            loop._owned._pAssetManager->unregisterAssetCache( &ui.getStyleSheetCache() );
            loop._owned._pAssetManager->unregisterAssetCache( &ui.getDocumentCache() );
            ui.shutdown();
        }
        static void destroy( EngineLoop& loop ) { loop._owned._pUiSystem.reset(); }
    };

    EngineLoop::EngineLoop()
        : _bootstrap{}
        , _configManager{ nullptr }
        , _rhi{ nullptr }
        , _mapDebugAction{ nullptr }
        , _audioSystem{ nullptr }
        , _frameRenderer{ nullptr }
        , _renderThread{ nullptr }
        , _gpuSceneBuilder{ nullptr }
        , _packetScratch{ nullptr }
        , _commandStack{ nullptr }
        , _gpuUploadQueue{ nullptr }
        , _pAutomationRunner{ nullptr }
        , _renderViewScheduler{ nullptr }
        , _renderViewClock{ 0.0 }
        , _listAnimationLodView{}
        , _hostRole{ EngineHostRole::Client }
        , _bShellActionsBound{ false }
        , _bHeadless{ false }
        , _bHeadlessTaskFailed{ false }
        , _bQuitRequested{ false }
        , _rhiInitResult{ RHIInitResult::NotStarted }
        , _sceneDeltaSeconds{ 0.0f }
        , _exitCode{ 0 }
        , _profileSession{}
        , _startup{}
        , _pEngineConfig{ nullptr }
    {
    }

    EngineLoop::~EngineLoop() = default;

    bool EngineLoop::initialize( int32 argc, utf8* pArgv[], EngineHostRole role )
    {
        // 이름 풀 · 로거 · 크래시 핸들러 · 리소스 루트 · 진단 도구(Debug) · 명령줄 · 전역 변수 — 시험 하네스와 같은 부트스트랩이다.
#if defined( SW_DEBUG )
        constexpr bool kDiagnostics = true;
#else
        constexpr bool kDiagnostics = false;
#endif
        if ( _bootstrap.initialize( _owned, kDiagnostics, argc, pArgv ) == false )
            return false;
        // 크래시 보고 프로세스(`-crash-reporter=<폴더>`)는 헤드리스다 — 서비스 · 기동 단계를 세우지 않고 App 이 묶음을 보내고 끝낸다.
        // 부트스트랩이 이 모드에서는 크래시 핸들러(죽으면 보고 프로세스를 또 띄운다) · 리소스 루트를 세우지 않았다.
        if ( _bootstrap.isCrashReporterRun() )
        {
            // 다른 헤드리스 작업처럼 작업 직전에 누수 기준선을 잡는다 — 없으면 종료 때 살아 있는 부트스트랩 블록을 모두 누수로 찍는다.
            MemoryProfiler::captureMemoryLeakBaseline();
            _hostRole  = role;
            _bHeadless = true;
            return true;
        }
        // 역할은 서비스를 만들기 전에 정한다 — 오디오 장치 · 기동 표 대상 · 읽지 않는 에셋 종류가 이것을 본다. 빌드에 없는 역할로는 서지 않는다.
        _hostRole                   = role;
        const bool bDedicatedServer = role == EngineHostRole::DedicatedServer;
        if ( ( bDedicatedServer && build::kWithServerCode == false ) || ( bDedicatedServer == false && build::kWithClientCode == false ) )
        {
            SW_LOG_ERROR( "The %# build cannot host the %# role", build::kTargetName, bDedicatedServer ? "dedicated server" : "client" );
            return false;
        }
        // 서버 패키지에 없는 에셋 종류(텍스처 · 셰이더 바이너리 · 오디오 — 쿠킹 표의 target_excluded_asset_kinds)는 서버가 읽지 않는다.
        ResourceUtil::setHostTarget( bDedicatedServer ? "Server" : "Client" );
        // `-gv_memoryTracking=1` 은 기동의 할당부터 센다(Release 는 추적이 꺼진 채 선다).
        _memoryBudgetMonitor.applyTrackingSetting();

        BLOCK( "Core Services 생성 및 바인딩" )
        {
            // 목록(`EngineServiceList.xxx`)의 `EngineCreated` 은 여기서 **한 줄로** 만들어진다. 생성자는 서로를 보지 않으므로 순서가 없다.
            _owned.createAll();

            // 만드는 방법이 특별한 것만 손으로 남는다(목록의 HostCreated 셋).
            {
                SW_MEMORY_SCOPE( Audio );
                // 전용 서버는 장치를 열지 않는다 — 씬 컴포넌트가 드는 오디오 서비스는 그대로 있고 소리 요청을 받아 버린다.
                _audioSystem = bDedicatedServer ? IAudioSystem::createNull() : IAudioSystem::create();
            }
#if !defined( SW_SHIPPING )
            {
                SW_MEMORY_SCOPE( EngineMisc );
                _commandStack = make_unique<CommandStack>();
            }
#endif
            // 렌더러는 서비스 표에 실리므로(아래) 여기서 만든다. 초기화와 해제는 FrameRenderer 단계가 한다.
            {
                SW_MEMORY_SCOPE( RenderCpu );
                _frameRenderer = make_unique<FrameRenderer>();
            }

            EngineServices services{};
            _bootstrap.fillServices( services );
            // HostCreated 인 자리만 손으로 연결한다.
            services._pAudioSystem  = _audioSystem.get();
            services._pCommandStack = _commandStack.get();
            // 렌더러는 씬이 아니라 **호스트**가 내준다. 에디터가 뷰 모드를 바꾸려고 찾는 창구다.
            services._pFrameRenderer = _frameRenderer.get();

            engine::bindEngineServices( services );
        }

        // 외부 프로파일러(`-gv_tracy`)는 서비스 표가 선 직후에 켠다 — 기동 단계부터 타임라인에 남고, 켤 때 엔진 프로파일러도 같이 켠다.
        ThreadName::setCurrentThreadName( "GameThread" );
        ProfilerBackend::initialize();

        // 초기화(`initialize()`)의 순서는 손으로 적지 않는다. 단계마다 먼저 서야 하는 단계를 `EngineInitStepList.xxx` 에 적고,
        // 여기서는 위상 순서로 단계 구조체(`<단계>StartupStep`)의 본문을 부른다. 종료와 해제는 그 역순이다(`shutdown`).
        const bool bStarted = _startup.initializeAll( *this, bDedicatedServer ? EngineInitTarget::Server : EngineInitTarget::Client );
        if ( bStarted == false )
            return false;
        if ( bDedicatedServer )
        {
            // 실제 바이너리에서 단계가 빠졌는지 시험(ServerBootTest)이 이 줄로 본다.
            string skipped;
            for ( uint32 stepIndex = 0; stepIndex < static_cast<uint32>( EngineInitStep::Count ); ++stepIndex )
            {
                const EngineInitStep step = static_cast<EngineInitStep>( stepIndex );
                if ( ( static_cast<uint8>( EngineInitSequence::getStepTarget( step ) ) & static_cast<uint8>( EngineInitTarget::Server ) ) != 0 )
                    continue;
                if ( skipped.empty() == false )
                    skipped += " ";
                skipped += EngineInitSequence::getStepName( step );
            }
            SW_LOG_INFO( "Dedicated server: startup steps not run - %#", skipped.c_str() );
        }
        // 헤드리스 작업(셰이더 · 씬 쿠킹, 텍스처 임포트)은 RHI 이후 단계를 건너뛰고 여기서 끝난다. 누수 기준선은 그 작업 직전에 잡았다.
        if ( _bHeadless )
            return true;

        // 자동화 시나리오(`-scenario=<파일>`) — 시나리오는 늘 고정 프레임 시간이다(App 의 `gv_fixedFrameDelta` 를 시나리오 값으로).
        string scenarioPath;
        if ( _owned._pCommandLineManager->getArgument( CommandLineArgument::SCENARIO, scenarioPath ) && scenarioPath.empty() == false )
        {
            string reportPath;
            (void)_owned._pCommandLineManager->getArgument( CommandLineArgument::SCENARIO_REPORT, reportPath );
            _pAutomationRunner = make_unique<AutomationRunner>();
            _pAutomationRunner->startFromPath( scenarioPath, reportPath );
            if ( _renderThread != nullptr )
                _renderThread->setScenarioCaptureEnabled( true );
            if ( _pAutomationRunner->isActive() )
            {
                const string fixedDelta = to_string( _pAutomationRunner->getScenario().getFixedDelta() );
                if ( engine::getGlobalVariableManager().setValueFromString( "gv_fixedFrameDelta", fixedDelta ) == false )
                    SW_LOG_WARNING( "[Scenario] this host has no gv_fixedFrameDelta - the scenario runs on the wall clock" );
            }
        }

        _profileSession.begin();

        MemoryProfiler::captureMemoryLeakBaseline();

        return true;
    }

    void EngineLoop::shutdown()
    {
        // 시나리오 실행기는 로그 리스너 · 입력을 빌려 쓴다 — 서비스보다 먼저 내린다.
        _pAutomationRunner.reset();
        AssetLoadProfiler::get().reportIfRequested( "session" );
        // 단계 본문을 초기화한 것만 역순으로 내린다: 씬의 디바이스 → 렌더 스레드 → 렌더러 → RHI → 씬 → 입력 · 오디오 → 모듈 이미지 → 태스크 → 셰이더 캐시.
        _startup.shutdownAll();
        // 단계가 소유한 객체를 표의 역순으로 해제한다(`<단계>StartupStep::destroy`): 렌더러 쪽 → RHI → 씬 → 입력 · 오디오 → 태스크 → 셰이더 캐시 →
        // 엔진 데이터 → 리소스 → 설정 → 리플렉션 → 압축. 기동이 어디서 멈췄든 모든 단계를 해제한다.
        _startup.destroyAll();
        // 할당 관찰을 떼고 출력을 비운다. 출력 객체(Tracy)는 프로세스 수명이라 남아 있는 구간을 닫을 수 있다.
        ProfilerBackend::shutdown();
        // 표 밖 부트스트랩을 세운 역순으로 내린다(시험 하네스와 같은 끝 정리).
        _bootstrap.shutdown();

        MemoryProfiler::reportMemoryLeaks( "EngineLoop::shutdown" );
    }

    void EngineLoop::onConfigReloaded( const hashed_string& configTypeName )
    {
        if ( configTypeName == GameConfig::StaticType()->_fullyQualifiedName )
        {
            const GameConfig* pGameConfig = _configManager != nullptr ? _configManager->getConfig<GameConfig>() : nullptr;
            if ( pGameConfig != nullptr )
                GameConfig::setActive( *pGameConfig );
            return;
        }
        if ( configTypeName != EngineConfig::StaticType()->_fullyQualifiedName || _pEngineConfig == nullptr )
            return;
        EngineConfig::setActive( *_pEngineConfig );
        // 수직 동기화는 다음에 스왑체인을 만들 때(창 크기 변경 · 백엔드 교체) 적용된다.
        if ( _rhi != nullptr )
            _rhi->setPreferredVSync( _pEngineConfig->_window._bVSync );
    }

    bool EngineLoop::renderPortraits( string_view prefabList, string_view outputDirectory, uint32 size )
    {
        if ( _rhi == nullptr || _rhi->hasDevice() == false || prefabList.empty() )
            return false;
        // 렌더 스레드가 받은 일을 모두 끝내게 하고, 컨텍스트가 스레드에 묶이는 백엔드(GL · DX11)는 이 스레드가 잡는다.
        if ( _renderThread != nullptr )
            _renderThread->waitIdle();
        IRHIDevice& device     = _rhi->getDevice();
        const bool  bExclusive = device.requiresExclusiveContextThread();
        if ( bExclusive && device.bindGraphicsContext() == false )
        {
            SW_LOG_ERROR( "Portrait: could not bind the graphics context" );
            return false;
        }
        const string directory = outputDirectory.empty() ? string( "Saved/Portraits" ) : string( outputDirectory );
        (void)FileUtil::ensureDirectoryExists( directory );

        PortraitRenderer      portrait;
        bool                  bAllSucceeded = portrait.initialize( &device );
        const string_splitter parts( prefabList, { "," } );
        for ( const string_view part : parts.getSplitList() )
        {
            const string_view path = StringUtil::trim( part );
            if ( path.empty() || bAllSucceeded == false )
                continue;
            PortraitRequest request;
            request._prefabPath = string( path );
            request._width      = MathUtil::clamp( size, 16u, 4096u );
            request._height     = request._width;
            vector<uint8> rgbaBytes;
            if ( portrait.renderPrefab( request, rgbaBytes ) == false )
            {
                bAllSucceeded = false;
                continue;
            }
            // 이름은 경로의 마지막 조각에서 첫 점 앞까지다(`hero.prefab.xml` → `hero`).
            string_view stem  = path.substr( path.find_last_of( '/' ) == string_view::npos ? 0 : path.find_last_of( '/' ) + 1 );
            stem              = stem.substr( 0, stem.find( '.' ) );
            const string base = directory + "/" + string( stem ) + ".portrait";
            const bool   bDDS = ImageFileWriter::writeDDSRgba8( base + ".dds", rgbaBytes, request._width, request._height );
            const bool   bPng = ImageFileWriter::writePngRgba8( base + ".png", rgbaBytes, request._width, request._height );
            bAllSucceeded     = bAllSucceeded && bDDS && bPng;
            SW_LOG_INFO( "Portrait '%#' -> %#.dds / .png (%#x%#)", string( path ).c_str(), base.c_str(), request._width, request._height );
        }
        portrait.shutdown();
        if ( bExclusive )
            device.unbindGraphicsContext();
        return bAllSucceeded;
    }

    void EngineLoop::requestQuit( int32 exitCode )
    {
        if ( _bQuitRequested )
            return;
        _bQuitRequested = true;
        _exitCode       = exitCode;
        SW_LOG_INFO( "Quit requested (exit code %#)", exitCode );
    }

    void EngineLoop::onWindowClosed()
    {
        if ( _pAutomationRunner == nullptr )
            return;
        const AutomationResult result = _pAutomationRunner->onWindowClosed( _owned._pInputManager.get() );
        _bQuitRequested               = true;
        _exitCode                     = static_cast<int32>( result );
    }

    void EngineLoop::beginFrame( float32 deltaSeconds )
    {
        // 시나리오의 시작 조건 · 가상 입력 붙이기 · 행동 층 단계는 이 프레임의 입력 재생 전이다.
        if ( _pAutomationRunner != nullptr && _owned._pInputManager != nullptr )
            _pAutomationRunner->onFrameBegin( *_owned._pInputManager );
        if ( _owned._pInputManager != nullptr )
            _owned._pInputManager->beginFrame( deltaSeconds );
        // UI 는 게임 틱보다 먼저 입력을 받는다 — UI 가 먹은 행동 · 마우스 버튼은 이번 프레임 폰의 의도에 들지 않는다(플레이어 조종자가 본다).
        if ( _owned._pUiSystem != nullptr && _owned._pUiSystem->isInitialized() )
            _owned._pUiSystem->processInput( GameTimeScale::getUnscaledDeltaTime( deltaSeconds ) ); // 정지 메뉴의 탐색 반복은 실제 시간

        if ( gv_dumpReflection.empty() == false )
        {
            const TypeRegistry&   registry = engine::getTypeRegistry();
            const string_splitter parts( string_view{ gv_dumpReflection.c_str(), gv_dumpReflection.size() }, { "," } );
            for ( const string_view part : parts.getSplitList() )
            {
                const string_view name = StringUtil::trim( part );
                if ( name.empty() )
                    continue;
                const hashed_string key( name );
                const string        text = ( registry.findType( key ) == nullptr && registry.findEnum( key ) != nullptr ) ? registry.describeEnum( key )
                                                                                                                          : registry.describeType( key );
                SW_LOG_INFO( "[gv_dumpReflection]\n%#", text.c_str() );
            }
            gv_dumpReflection = string{}; // 한 번만
        }
    }

    void EngineLoop::tick( float32 deltaTime, const HostViewTargets& views, const ViewCameraProviderDelegate& sceneViewCameraProvider, bool bTickScene )
    {
        // 주 출력 — 게임 뷰가 있으면 그것(게임 카메라 · 화면 UI · 화면 사각형 뷰), 씬 뷰만 있으면 씬 뷰, 둘 다 없으면 백버퍼다.
        const HostViewTarget& mainOutput = views.getMainOutput();
        const uint32          vpWidth    = mainOutput._width;
        const uint32          vpHeight   = mainOutput._height;
        const bool            bSceneMain = views.isSceneViewMain();
        // 진단: 지정한 프레임에 백엔드 교체를 요청한다(에디터 패널과 같은 경로. setValueAsInt 가 변경 콜백을 부른다).
        // 테스트용 스위치라 Shipping 에는 없다(그 빌드에서 gv_rhiSwapAtFrame 은 등록되지 않아 늘 0 이다).
#if !defined( SW_SHIPPING )
        if ( gv_rhiSwapAtFrame > 0 && engine::getFrameProfiler().getFrameCount() == static_cast<uint64>( gv_rhiSwapAtFrame ) &&
             gv_rhiBackend != gv_rhiSwapTo )
        {
            SW_LOG_INFO( "[SwapProbe] frame %# — requesting backend %#", gv_rhiSwapAtFrame, RHI::getBackendTypeName( gv_rhiSwapTo ) );
            // C++ 대입은 변경 콜백을 부르지 않는다. 메뉴 · 콘솔이 쓰는 setValueAsInt 로 가야 RHIBackendSwitcher 가 받는다.
            if ( GlobalVariableInfo* pVar = engine::getGlobalVariableManager().findVariable( "gv_rhiBackend" ) )
                pVar->setValueAsInt( static_cast<int32>( gv_rhiSwapTo ) );
            gv_rhiSwapAtFrame = 0; // 한 번만. 프로파일러가 프레임 수를 되돌리면(워밍업 뒤) 같은 번호가 다시 온다
        }
#endif

        if ( _bHeadless )
            return;

        FrameProfiler& profiler = engine::getFrameProfiler();
        profiler.beginFrame();

        // 게임 스레드 전체를 한 구간으로 잡는다. 보고서가 RT 구간만 보여 주면 "프레임의 몇 % 를 쟀나" 에
        // 답할 수 없다. 그 답이 없으면 다음 최적화 대상을 고르는 근거도 없다.
        SW_PROFILE_SCOPE( "GT.Frame" );

        BLOCK( "핫 리로드 / 씬 트랜지션 / 이벤트" )
        {
#if !defined( SW_SHIPPING )
    #if defined( SW_DEBUG )
            if ( _rhi != nullptr )
            {
                if ( ShaderRecompiler* pShaderRecompiler = getShaderRecompiler() )
                    pShaderRecompiler->update();
            }
    #endif
            pollShaderReloadHotkey();
#endif
            // 파일 대화 상자 결과를 **여기서** 메인 스레드로 넘긴다. 대화 상자는 별도 스레드가 띄운다.
            FileUtil::pumpFileDialogResults();
            engine::getAssetStreamingQueue().update();

            if ( _owned._pSceneManager != nullptr )
                _owned._pSceneManager->tickTransitions();
            if ( _owned._pEventDispatcher != nullptr )
                _owned._pEventDispatcher->processEvents();
            if ( _audioSystem != nullptr )
                _audioSystem->update( deltaTime );
        }

        _sceneDeltaSeconds = bTickScene ? deltaTime : 0.0f;
        BLOCK( "Scene update" )
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick" );
            // 게임 카메라가 그리는 뷰포트 크기를 틱 전에 적는다 — 픽셀 퍼펙트 카메라가 배율을 고른다(`CameraRegistry::getViewportWidth`).
            Scene* pTickScene = ( _owned._pSceneManager != nullptr ) ? _owned._pSceneManager->getActiveScene() : nullptr;
            if ( pTickScene != nullptr && pTickScene->getObjectManager() != nullptr )
                pTickScene->getObjectManager()->getCameraRegistry().setViewportSize( vpWidth, vpHeight );
            if ( _owned._pSceneManager != nullptr && bTickScene )
                _owned._pSceneManager->tick( deltaTime );
        }

        if ( gv_physicsDebugDraw && _owned._pSceneManager != nullptr )
        {
            const Scene* pDebugScene = _owned._pSceneManager->getActiveScene();
            if ( pDebugScene != nullptr && pDebugScene->getObjectManager() != nullptr )
            {
                PhysicsDebugDrawAdapter adapter{ engine::getDebugDrawQueue() };
                pDebugScene->getObjectManager()->getScenePhysics().drawDebug( adapter );
            }
        }

        // 내비메시 · 에이전트 경로 · 속도 · 장애물 — 선은 물리 디버그와 같은 출구로(편집기 뷰포트가 그린다), 비트 16 은 게임 화면에 보이는 메시로.
        if ( _owned._pSceneManager != nullptr )
        {
            Scene* pDebugScene = _owned._pSceneManager->getActiveScene();
            if ( pDebugScene != nullptr && pDebugScene->getObjectManager() != nullptr )
            {
                SceneNavigation& navigation = pDebugScene->getObjectManager()->getSceneNavigation();
                navigation.updateDebugView( static_cast<uint32>( gv_navDebugDraw ) );
                if ( gv_navDebugDraw != 0 )
                {
                    PhysicsDebugDrawAdapter adapter{ engine::getDebugDrawQueue() };
                    navigation.drawDebug( adapter, static_cast<uint32>( gv_navDebugDraw ) );
                }
            }
        }

        // 텔레메트리 — 장면별 프레임 시간을 모으고 flush 시간이 되면 쓴다. 동의가 없으면 둘 다 아무 일도 하지 않는다.
        if ( _owned._pTelemetryService != nullptr )
        {
            const Scene* pTelemetryScene = _owned._pSceneManager != nullptr ? _owned._pSceneManager->getActiveScene() : nullptr;
            string_view  sceneId{};
            if ( pTelemetryScene != nullptr )
                sceneId = pTelemetryScene->getSourcePath().empty() ? pTelemetryScene->getName() : pTelemetryScene->getSourcePath();
            _owned._pTelemetryService->recordFrame( sceneId, deltaTime );
            _owned._pTelemetryService->update( deltaTime );
        }

        // 런타임 UI — 이번 프레임의 게임 상태로 애니메이션 · 스타일 · 레이아웃을 돌린다(게임 틱 뒤 · 렌더 패킷 앞). 뷰포트는 게임이 그려지는 화면이고,
        // UI 단위 크기 · 배율 · 안전 영역은 해상도 규칙과 사용자 배율(gv_uiScale)로 정한다.
        if ( _owned._pUiSystem != nullptr && _owned._pUiSystem->isInitialized() )
        {
            // 뷰포트 0 은 "백버퍼 전체"(게임 창 — 패킷 · 캔버스 시험 그림과 같은 규칙)다.
            const IWindow* const pWindow      = IWindow::getActiveWindow();
            const float32        contentScale = pWindow != nullptr ? pWindow->getContentScale() : 1.0f;
            const bool           bHasDevice   = _rhi != nullptr && _rhi->hasDevice();
            uint32               uiWidth      = vpWidth;
            uint32               uiHeight     = vpHeight;
            if ( uiWidth == 0 || uiHeight == 0 )
            {
                uiWidth  = bHasDevice ? _rhi->getDevice().getBackBufferWidth() : ( pWindow != nullptr ? pWindow->getWidth() : 0 );
                uiHeight = bHasDevice ? _rhi->getDevice().getBackBufferHeight() : ( pWindow != nullptr ? pWindow->getHeight() : 0 );
            }
            const UiViewport viewport =
                _owned._pUiSystem->computeViewport( float2{ static_cast<float32>( uiWidth ), static_cast<float32>( uiHeight ) }, contentScale );
            _owned._pUiSystem->update( GameTimeScale::getUnscaledDeltaTime( deltaTime ), viewport ); // 애니메이션 · 자막 · 알림은 정지 메뉴 아래서도 흐른다
        }

        // 이번 틱에 경로로 잡힌 머티리얼(메시의 저장된 참조)을 패킷을 내기 **전에** 올린다. 컴포넌트는 디바이스를 모른다(`MaterialCache::requestInitialize`).
        // 올리기는 bindless 표에 등록하므로 렌더 스레드가 지난 프레임을 병렬로 기록하는 동안 하면 안 된다 — 올릴 것이 있는 프레임(씬 로드 ·
        // 처음 쓰는 머티리얼의 스폰)만 렌더 스레드를 기다린다.
        if ( _rhi != nullptr && _rhi->hasDevice() )
        {
            MaterialCache& materials = _owned._pAssetManager->getMaterialManager();
            if ( materials.hasPendingInitialize() && _renderThread != nullptr )
                _renderThread->waitIdle();
            materials.initializePending( &_rhi->getDevice() );
        }

        // 패킷 · 씬 스냅샷은 FrameRenderer 단계가 만든다. 그 단계가 서지 않았으면(헤드리스 작업) 낼 패킷이 없다.
        if ( _packetScratch == nullptr || _gpuSceneBuilder == nullptr )
            return;

        Scene* pActiveScene = _owned._pSceneManager != nullptr ? _owned._pSceneManager->getActiveScene() : nullptr;

        BLOCK( "RenderFramePacket 제출" )
        {
            SW_MEMORY_SCOPE( RenderCpu );
            RenderFramePacket& packet = *_packetScratch;
            packet.resetForFrame();
            packet._bValid = 1;
            // 시나리오 `<Screenshot>` 은 다음에 그리는 이 패킷에 싣는다.
            if ( _pAutomationRunner != nullptr )
                (void)_pAutomationRunner->takePendingScreenshotPath( packet._screenshotPath ); // 없으면 빈 채로 둔다

            packet._outputRenderTarget = mainOutput._renderTarget;
            packet._viewportWidth      = vpWidth;
            packet._viewportHeight     = vpHeight;
            packet._cameraPos          = float3{ 0.0f, 1.2f, 3.2f };

            if ( pActiveScene != nullptr )
            {
                // 주광은 씬이 갖고, 렌더 스레드는 패킷으로만 받는다. executePacket 은 _pScene 을
                // null 로 두므로 렌더 스레드에서 씬을 조회할 수 없다.
                // 그림자 행렬은 그림자 맵을 가져가는 빛(`findShadowCastingDirectionalLight`)에서 — 라이트 목록의 그림자 플래그와 같은 빛이다.
                DirectionalLightComponent* pLight       = pActiveScene->findActiveDirectionalLight();
                DirectionalLightComponent* pShadowLight = pActiveScene->findShadowCastingDirectionalLight();
                if ( pLight != nullptr )
                {
                    const float3 dir          = pLight->getLightDirection();
                    const float3 color        = pLight->getColor();
                    packet._lightDirIntensity = float4{ dir._x, dir._y, dir._z, pLight->getIntensity() };
                    packet._lightColorAmbient = float4{ color._x, color._y, color._z, pLight->getAmbient() };
                    packet._bHasLight         = SW_TRUE;
                }

                // 씬의 **모든** 라이트다. 방향광 · 점광 · 스폿이 한 목록으로 간다. 위의 키라이트는 그림자
                // 행렬과 앰비언트의 출처이자, 라이트 목록이 비었을 때의 폴백이다.
                collectSceneLights( pActiveScene, packet._listLight );

                pActiveScene->ensureDefaultCameras();
                // 위의 핫 리로드 · 씬 전환 · 씬 틱이 GameObject 를 파괴했을 수 있으므로 여기서 조회한다. 씬 뷰 카메라는 씬 뷰가 있을 때만 묻는다.
                CameraComponent* pSceneViewCamera = ( views._scene.isValid() && sceneViewCameraProvider.isBound() ) ? sceneViewCameraProvider() : nullptr;
                if ( pSceneViewCamera != nullptr && pSceneViewCamera->isActive() == false )
                    pSceneViewCamera = nullptr;
                CameraComponent* pCam = bSceneMain ? pSceneViewCamera : nullptr;
                if ( pCam == nullptr || pCam->isActive() == false )
                    pCam = pActiveScene->getActiveGameCamera();
                // 주 출력의 크기 — 게임 뷰 RT 면 그 크기, 백버퍼 경로면 스왑체인 크기다(화면 사각형 뷰 · 주 시점 사각형의 비율이 이것을 본다).
                const uint32 outputWidth  = packet._viewportWidth > 0 ? packet._viewportWidth : _rhi->getDevice().getBackBufferWidth();
                const uint32 outputHeight = packet._viewportHeight > 0 ? packet._viewportHeight : _rhi->getDevice().getBackBufferHeight();
                if ( pCam != nullptr )
                {
                    // 주 시점의 출력 설정(사각형 · 배율 · 끌 기능)과 컷 표시. 비율은 사각형의 것이다(분할 화면의 한 칸).
                    packet._mainView     = RenderViewCollector::makeMainSettings( pCam );
                    packet._cameraPos    = pCam->getCameraPosition();
                    packet._viewProj     = pCam->getViewProjectionMatrix( RenderViewCollector::computeAspect( packet._mainView, outputWidth, outputHeight ) );
                    packet._bHasViewProj = SW_TRUE;
                    // 투명 정렬의 깊이 — 직교 카메라는 시선 축(같은 Z 의 스프라이트가 카메라를 따라 앞뒤가 바뀌지 않게), 원근은 거리(render2d.xml).
                    _gpuSceneBuilder->setTransparentSortAxis(
                        Render2DSettings::getActive().computeTransparentSortAxis( pCam->isOrthographic(), pCam->getCameraForward() ) );
                }
                // 그림자 행렬과 그 바이어스는 한 볼륨에서 같이, 카메라가 정해진 **뒤에** 만든다 — `_shadowViewDistance` 를 준 빛은 볼륨을
                // 카메라가 보는 곳에 맞춘다. 텍셀 스냅 · 바이어스는 렌더 스레드가 만들 그림자 맵과 같은 해상도로 잰다.
                if ( pLight != nullptr && pShadowLight != nullptr )
                {
                    const uint32                      shadowResolution = FrameRenderer::getShadowMapResolution();
                    const DirectionalShadowProjection shadow           = packet._bHasViewProj == SW_TRUE
                                                                           ? pShadowLight->buildShadowProjectionForView( packet._viewProj, shadowResolution )
                                                                           : pShadowLight->buildShadowProjection( shadowResolution );
                    packet._lightViewProj                              = shadow._viewProj;
                    packet._shadowParams                               = shadow.computeShaderParams();
                }
                // 추가 뷰(캡처 카메라 · 화면 사각형) — 갱신 주기 · 보이는가 · 예산으로 이번 프레임에 그릴 것을 고른다. 쉬는 뷰도 실린다.
                _renderViewClock += static_cast<float64>( MathUtil::max( 0.0f, deltaTime ) );
                if ( _renderViewScheduler != nullptr && pActiveScene->getObjectManager() != nullptr )
                {
                    RenderViewCollector::collectExtraViews( *pActiveScene->getObjectManager(), pCam, packet._viewProj, outputWidth, outputHeight,
                                                            _renderViewClock, RenderViewCollector::getDefaultBudget(), *_renderViewScheduler, packet._listView );
                }
                // 씬 뷰 — 주 출력이면 게임 화면의 일부(화면 사각형 뷰)를 빼고, 게임 뷰와 함께면 자기 RT 에 그리는 추가 뷰로 싣는다.
                if ( bSceneMain )
                    RenderViewCollector::removeScreenRectViews( packet._listView );
                else if ( pSceneViewCamera != nullptr && pSceneViewCamera != pCam )
                    RenderViewCollector::appendHostView( *pSceneViewCamera, views._scene, packet._listView );
                // 애니메이션 LOD 의 뷰 — 주 시점과 이번 프레임에 그리는 추가 뷰(CCTV · 분할 화면). 다음 프레임 평가가 이것으로 가시성 · 화면 크기를 본다.
                // 갱신 주기로 쉬는 추가 뷰는 넣지 않는다 — 그 뷰에만 보이는 캐릭터는 그 뷰가 그리는 프레임에만 포즈를 만든다.
                if ( pActiveScene->getObjectManager() != nullptr )
                {
                    _listAnimationLodView.clear();
                    if ( packet._bHasViewProj == SW_TRUE )
                        _listAnimationLodView.push_back( AnimationLodView::make( packet._viewProj, packet._cameraPos ) );
                    for ( const RenderViewRequest& view : packet._listView )
                    {
                        if ( view._bRender == SW_TRUE )
                            _listAnimationLodView.push_back( AnimationLodView::make( view._viewProj, view._position ) );
                    }
                    pActiveScene->getObjectManager()->getAnimationSystem().setLodViews( _listAnimationLodView );
                }
                _gpuSceneBuilder->buildFromScene( pActiveScene, packet._cameraPos );
                // 추가 뷰(CCTV · PiP)는 자기 눈으로 투명을 정렬한다 — 주 카메라 순서로 그리면 반대편을 보는 뷰에서 앞뒤가 뒤집힌다.
                _gpuSceneBuilder->buildViewTransparentOrders( packet._listView );

                // 그릴 것이 정해졌으니 **스냅샷을 내보내기 전에** GPU 쪽을 만들어 둔다. 렌더 스레드는 그리기만
                // 하면 된다(새 메시가 등장한 프레임에 RT 가 정점 버퍼 생성을 떠안지 않게).
                if ( _gpuUploadQueue != nullptr )
                {
                    _gpuSceneBuilder->requestGPUUploads( *_gpuUploadQueue );
                    _gpuUploadQueue->flush();
                }

                {
                    SW_PROFILE_SCOPE( "GT.Packet.export" );
                    _gpuSceneBuilder->exportCpuSnapshot( packet._gpuScene );
                }
            }

            // 캔버스(화면 2D) — 런타임 UI 가 칠한 목록(내용 번호가 같으면 렌더러가 사각형을 다시 올리지 않는다), 개발 시험 그림은 그 위에.
            // 글리프 아틀라스의 새 구간은 칠한 것이 없어도 늘 넘긴다(렌더러의 거울이 게임 스레드의 페이지와 어긋나지 않게).
            // 씬 뷰가 주 출력인 프레임은 화면 UI 를 싣지 않는다 — UI 는 게임 화면(게임 뷰 · 백버퍼)에만 있다.
            if ( bSceneMain == false && _owned._pUiSystem != nullptr && _owned._pUiSystem->isInitialized() && _owned._pUiSystem->getCanvas().isEmpty() == false )
            {
                packet._canvas._mainOutput      = _owned._pUiSystem->getCanvas();
                packet._canvas._contentRevision = _owned._pUiSystem->getCanvasRevision();
            }
            if ( _owned._pUiSystem != nullptr && _owned._pUiSystem->isInitialized() )
                _owned._pUiSystem->collectWorldCanvases( packet._canvas._listTarget ); // 월드 공간 UI 의 렌더 텍스처
            if ( bSceneMain == false && gv_canvasTestPattern && _rhi != nullptr && _rhi->hasDevice() )
            {
                packet._canvas._contentRevision = 0; // 시험 그림은 내용 번호가 없다 — 늘 올린다
                const uint32 canvasWidth        = packet._viewportWidth > 0 ? packet._viewportWidth : _rhi->getDevice().getBackBufferWidth();
                const uint32 canvasHeight       = packet._viewportHeight > 0 ? packet._viewportHeight : _rhi->getDevice().getBackBufferHeight();
                CanvasTestPattern::paint( packet._canvas._mainOutput, float2{ static_cast<float32>( canvasWidth ), static_cast<float32>( canvasHeight ) },
                                          _owned._pFontSystem.get(), engine::getFrameProfiler().getFrameCount() );
            }
            if ( _owned._pFontSystem != nullptr && _owned._pFontSystem->isInitialized() )
                _owned._pFontSystem->getGlyphCache().getAtlas().takeUploads( packet._canvas._listAtlasUpload );
            // 색각 보정 — UI 는 톤맵 뒤라 캔버스 셰이더가 직접 건다(사용자 설정 accessibility.colorVision, 범위 밖은 끔).
            packet._canvas._colorVisionMode = ( 0 <= gv_colorVisionMode && gv_colorVisionMode <= 3 ) ? static_cast<uint32>( gv_colorVisionMode ) : 0u;

            if ( _renderThread != nullptr )
            {
                // GT 가 여기서 기다린다면 그것은 렌더 스레드가 밀린 것이다. 링이 차면 submit 이 막는다.
                SW_PROFILE_SCOPE( "GT.Packet.submit" );
                _renderThread->submit( packet );
            }
        }
    }

    void EngineLoop::endFrame()
    {
        engine::getFrameProfiler().endFrame();
        // 외부 프로파일러(Tracy)의 주 프레임 경계 — 게임 스레드 프레임이다.
        IProfilerBackend* pProfilerBackend = ProfilerBackend::getActiveBackend();
        if ( pProfilerBackend != nullptr )
            pProfilerBackend->markFrame( nullptr );
        const bool bReportedBefore = _profileSession.hasReported();
        _profileSession.onFrameEnd();
        // CPU 메모리 태그 표 옆에 GPU 메모리 표를 둔다. 보고 세션은 Utility 층이라 Graphics 의 장부를 볼 수 없어 여기서 잇는다.
        const bool bReportedNow = bReportedBefore == false && _profileSession.hasReported();
        if ( bReportedNow && _rhi != nullptr && _rhi->hasDevice() )
            _rhi->getDevice().getMemoryLedger().report( _rhi->getDevice().getBackendName() );

        _memoryBudgetMonitor.onFrameEnd();

        // 시나리오의 단언 · 환경 단계는 씬 틱 · 렌더 제출 뒤, 입력 프레임을 닫기 전이다(그 프레임의 눌림 엣지가 아직 보인다).
        // `isActive` 가 아니라 `hasEnded` — 시작 프레임에서 진 시나리오(읽기 오류 · 시작 시한)도 여기서 끝맺고 그 코드로 끝난다.
        if ( _pAutomationRunner != nullptr && _pAutomationRunner->hasEnded() == false && _owned._pInputManager != nullptr )
        {
            _pAutomationRunner->setCompletedScreenshotCount( _renderThread != nullptr ? _renderThread->getCompletedScenarioScreenshotCount() : 0u );
            const AutomationResult result = _pAutomationRunner->onFrameEnd( *_owned._pInputManager );
            if ( result != AutomationResult::Running )
                requestQuit( static_cast<int32>( result ) );
        }
        if ( _owned._pInputManager != nullptr )
            _owned._pInputManager->endFrame();
        // 이번 프레임에 넣은 디버그 도형을 보이는 목록으로 확정하고, 씬이 흘린 시간만큼 지속 시간을 줄인다(일시정지면 그대로 남는다).
        engine::getDebugDrawQueue().endFrame( _sceneDeltaSeconds );
    }

    bool EngineLoop::applyPendingBackendChange()
    {
        if ( _rhi == nullptr )
            return false;

        const RHIBackend requested = _rhi->consumePendingBackendChange();
        const RHIBackend previous  = _rhi->getCommittedBackend();
        if ( requested == previous )
            return true;

        SW_LOG_INFO( "Soft-recreating RHI: %# → %#",
                     RHI::getBackendTypeName( previous ),
                     RHI::getBackendTypeName( requested ) );

        BLOCK( "디바이스에 매인 단계 내리기" )
        {
            // RHI 에 (간접으로라도) 의존하는 단계(씬의 디바이스 · 라이브 셰이더 · 렌더 스레드 · 렌더러)를 기동 표의 역순으로, 기동과 같은
            // 본문으로 내린다. 씬 스냅샷 빌더가 든 머티리얼 · 인스턴스도 FrameRenderer 단계의 종료가 옛 디바이스가 살아 있을 때 놓는다.
            _startup.shutdownDependentsOf( EngineInitStep::RHI );

            // 옛 디바이스의 GPU 자원은 recreateDevice 안의 shutdown 이 등록부에 통보하며 거둔다.
            _rhi->getDevice().waitIdle();
            if ( _owned._pShaderCache != nullptr )
                _owned._pShaderCache->clearCache();
        }

        if ( _rhi->recreateDevice( requested ) == false )
        {
            SW_LOG_ERROR( "recreateDevice failed; restoring previous backend %#",
                          RHI::getBackendTypeName( previous ) );
            gv_rhiBackend = previous;
            if ( _rhi->recreateDevice( previous ) == false )
            {
                SW_LOG_ERROR( "Failed to restore previous RHI backend — device is gone." );
                return false;
            }
            (void)rebindSceneAfterDeviceRecreate();
            return false;
        }

        if ( rebindSceneAfterDeviceRecreate() == false )
            return false;
        // 교체 뒤 디바이스에 매인 설정이 새 디바이스를 따르는지 한 줄로 남긴다(AppSmokeTest.BackendSwapFollowsTheNewDevice 가 읽는다).
        SW_LOG_INFO( "Active backend is now %# (merge batches across materials %#, native bindless sampling %#)", RHI::getBackendTypeName( _rhi->getCommittedBackend() ),
                     ( _gpuSceneBuilder != nullptr && _gpuSceneBuilder->isMergeBatchesAcrossMaterials() ) ? 1 : 0,
                     _rhi->getDevice().supportsNativeBindlessSampling() ? 1 : 0 );
        return true;
    }

    bool EngineLoop::rebindSceneAfterDeviceRecreate()
    {
        if ( _rhi == nullptr || _rhi->hasDevice() == false )
            return false;

        // 새 디바이스가 섰다. 등록부 전체에 "다시 올려라" 를 알린다. 어떤 캐시를 빠뜨렸는지
        // 기억할 필요가 없는 것이 이 구조의 요점이다(언리얼 FRenderResource::InitRHI 와 같은 자리).
        RHIRenderResource::initAllFor( &_rhi->getDevice() );

        // 내렸던 단계를 기동과 같은 본문으로 다시 세운다: 렌더러(업로드 큐 · 배치 합치기 설정을 새 디바이스로) → 렌더 스레드 → 라이브 셰이더 →
        // 씬의 디바이스. 본문은 이미 있는 객체를 다시 쓰므로 렌더 스레드에 걸린 훅과 그것을 가리키는 포인터가 그대로다.
        const bool bRestarted = _startup.restartStoppedSteps();

        Scene* pScene = _owned._pSceneManager != nullptr ? _owned._pSceneManager->getActiveScene() : nullptr;
        if ( pScene != nullptr )
            pScene->ensureDefaultCameras();
        return bRestarted;
    }

    ShaderRecompiler* EngineLoop::getShaderRecompiler() const
    {
#if defined( SW_SHIPPING )
        return nullptr;
#else
        return _shaderRecompiler.get();
#endif
    }

    void EngineLoop::setOnScenesReleased( Delegate<void()> onScenesReleased )
    {
        _onScenesReleased = std::move( onScenesReleased );
    }

    void EngineLoop::setModuleTypeLoader( ModuleTypeLoaderDelegate moduleTypeLoader )
    {
        _moduleTypeLoader = std::move( moduleTypeLoader );
    }

    void EngineLoop::setPresentHook( PresentHookDelegate presentHook )
    {
        if ( _renderThread != nullptr )
            _renderThread->setPresentHook( std::move( presentHook ) );
    }

    void EngineLoop::setPostPresentHook( PresentHookDelegate postPresentHook )
    {
        if ( _renderThread != nullptr )
            _renderThread->setPostPresentHook( std::move( postPresentHook ) );
    }

    void EngineLoop::updateShellActions( float32 deltaTime )
    {
        if ( _owned._pInputManager == nullptr )
            return;

        if ( _bShellActionsBound == false )
        {
            _mapDebugAction     = createShellInputMap( engine::getEngineDefaultAssets()._shellInputMap );
            _bShellActionsBound = true;
        }

        // _mapDebugAction 은 위에서 처음 한 번 만든 뒤로 계속 있으므로 nullptr 일 수 없다.

        if ( _mapDebugAction->hasLayer( InputMapDefaults::kTitleLayerName ) )
            _mapDebugAction->setLayerEnabled( InputMapDefaults::kTitleLayerName, false );
        _mapDebugAction->setInputManager( _owned._pInputManager.get() );
        _mapDebugAction->update( deltaTime );
    }

    unique_ptr<InputMap> EngineLoop::createShellInputMap( string_view inputMapPath )
    {
        SW_MEMORY_SCOPE( EngineMisc );
        unique_ptr<InputMap> map = make_unique<InputMap>();
        // 셸 단축키와 개발 콘솔은 콘솔이 키보드를 쥔 동안에도 키를 받아야 한다(닫는 키 · 편집 키).
        map->setKeyboardFocusIgnored( true );
        if ( inputMapPath.empty() || map->loadFromResource( inputMapPath ) == false )
        {
            map->clear();
            SW_LOG_ERROR( "Shell input map '%#' could not be loaded - shell debug actions stay unbound", inputMapPath );
        }
        return map;
    }

    void EngineLoop::pollShaderReloadHotkey()
    {
#if defined( SW_DEBUG )
        // 셰이더 리로드는 **Engine 자신의** 개발 도구다(ShaderRecompiler 를 여기서 소유한다). 그래서
        // 바깥에 콜백을 달라고 하지 않고 여기서 끝낸다. 모듈을 다시 올리는 일은 App 의 것이라 App 이 묻는다.
        if ( _mapDebugAction == nullptr || _rhi == nullptr )
            return;
        if ( _mapDebugAction->wasActionTriggered( InputMapDefaults::kReloadShadersAction ) == false )
            return;
        if ( ShaderRecompiler* pShaderRecompiler = getShaderRecompiler() )
        {
            pShaderRecompiler->triggerReloadAll();
            SW_LOG_INFO( "%#: force shader reload", InputMapDefaults::kReloadShadersAction );
        }
#endif
    }

    bool EngineLoop::wasDebugActionTriggered( string_view actionName ) const
    {
        if ( _mapDebugAction == nullptr )
            return false;
        return _mapDebugAction->wasActionTriggered( hashed_string( actionName ) );
    }
} // namespace sw
