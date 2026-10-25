#include "pch.h"

#include "Engine/EngineSubsystemSet.h"

#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Event/EventDispatcher.h"
#include "Core/File/AsyncFileIO.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Bootstrap/CommandLineSubsystem.h"
#include "Engine/Bootstrap/ContentRootSubsystem.h"
#include "Engine/Bootstrap/CrashHandlerSubsystem.h"
#include "Engine/Bootstrap/DiagnosticsSubsystem.h"
#include "Engine/Bootstrap/LoggerSubsystem.h"
#include "Engine/Bootstrap/MemoryBudgetSubsystem.h"
#include "Engine/Bootstrap/NamePoolSubsystem.h"
#include "Engine/Bootstrap/StaticStorageSubsystem.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Graphics/2D/Render2DSettings.h"
#include "Engine/Graphics/2D/Render2DSettingsSubsystem.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Graphics/Debug/RenderTargetRegistry.h"
#include "Engine/Graphics/Debug/VisualLogger.h"
#include "Engine/Graphics/RHI/RHIBackendRegistry.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Module/ModuleImagesSubsystem.h"
#include "Engine/Module/ModuleTypesSubsystem.h"
#include "Engine/Net/GameNetSession.h"
#include "Engine/Object/Component/ComponentDefaults.h"
#include "Engine/Physics/PhysicsSystem.h"
#include "Engine/Profiling/DebugOverlayState.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Profiling/ProfilerBackendSubsystem.h"
#include "Engine/Reflection/TypeRegistry.h"

// 렌더러 헤더는 클라이언트 코드에서만 읽는다 — 렌더러 폴더를 짓지 않는 Server 타깃에는 그 정의가 없다(Engine.cpp 와 같은 까닭).
#if defined( SW_WITH_CLIENT_CODE )
    #include "Engine/Renderer/Frame/FrameRenderer.h"
    #include "Engine/Renderer/Frame/FrameRendererSubsystem.h"
    #include "Engine/Renderer/LiveShaderSubsystem.h"
    #include "Engine/Renderer/Module/ModuleRenderPassRegistry.h"
    #include "Engine/Renderer/RHISubsystem.h"
    #include "Engine/Renderer/RenderThreadSubsystem.h"
    #include "Engine/Renderer/SceneRHISubsystem.h"
    #include "Engine/Renderer/ScreenshotMailbox.h"
    #include "Engine/Window/MainWindowSubsystem.h"
#endif
#include "Engine/Resource/AssetLoadProfiler.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/AssetStreamingQueue.h"
#include "Engine/Resource/PackEngineSettingsSubsystem.h"
#include "Engine/Resource/ResourceDataKind.h"
#include "Engine/Scene/AuthorityGhost.h" // 헤더가 SW_DEV_COMMANDS_ENABLED 로 스스로 감싼다(여기서 #if 로 감싸면 그 매크로를 아직 모를 수 있다)
#include "Engine/Scene/CommandStack.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneStateHash.h"
#include "Engine/Telemetry/CrashReportService.h"
#include "Engine/Telemetry/TelemetryService.h"
#include "Engine/Text/FontService.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UserSettings/UserSettingsManager.h"
#include "Engine/Utility/EditSessionState.h"

namespace sw::engine
{
    BootstrapParts addBootstrapSubsystems( SubsystemScope& bootstrap, const BootstrapSetup& setup )
    {
        BootstrapParts parts;
        parts._pDiagnostics = &bootstrap.addSubsystem( sw::make_unique<DiagnosticsSubsystem>( setup._bDiagnostics ) );
        bootstrap.addSubsystem( sw::make_unique<NamePoolSubsystem>() );
        bootstrap.addSubsystem( sw::make_unique<LoggerSubsystem>() );
        parts._pCommandLine  = &bootstrap.addSubsystem( sw::make_unique<CommandLineSubsystem>( setup._argc, setup._ppArgv ) );
        parts._pCrashHandler = &bootstrap.addSubsystem( sw::make_unique<CrashHandlerSubsystem>( parts._pCommandLine ) );
        bootstrap.addSubsystem( sw::make_unique<ContentRootSubsystem>( *parts._pCommandLine, *parts._pCrashHandler ) );
        bootstrap.addSubsystem( sw::make_unique<StaticStorageSubsystem>() );
        if ( setup._bMemoryBudget )
            parts._pMemoryBudget = &bootstrap.addSubsystem( sw::make_unique<MemoryBudgetSubsystem>( *parts._pCrashHandler ) );
        // 서브시스템 안의 객체도 서비스 표의 행이다 — 그 타입으로 건다(새 범위라 겹치지 않는다).
        (void)bootstrap.bind( parts._pCommandLine->findCommandLine() );
        (void)bootstrap.bind( parts._pCommandLine->findGlobalVariables() );
        return parts;
    }

    bool initializeBootstrapSubsystems( SubsystemScope& bootstrap, const BootstrapParts& parts )
    {
        if ( bootstrap.getCollection().initializeAll( SubsystemHostMask::All ) == false )
            return false;
        (void)bootstrap.bind( parts._pDiagnostics->findMemoryProfiler() ); // 진단 도구를 껐으면(배포본) 없어(nullptr) 걸지 않는다 — 선택 행이다
        return true;
    }

    ServiceParts addServiceSubsystems( SubsystemScope& services, const ServiceSetup& setup )
    {
        // 초기화 · 종료할 것이 없고 수명만 있는 공용 서비스다 — 엔진 범위의 어느 것보다 먼저 서고 나중에 사라진다(누가 소멸자에서 부르든 살아 있다).
        SW_MEMORY_SCOPE( EngineMisc );
        ServiceParts parts;
        parts._pEventDispatcher = &services.addSubsystem( sw::make_unique<EventDispatcher>() );
        services.addSubsystem( sw::make_unique<LocalizationManager>() );
        if ( setup._bEditorServices )
        {
            // 에디터 Undo/Redo 전용이다(목록의 선택 행). 씬이 바뀌면 씬 매니저가 비운다.
            services.addSubsystem( sw::make_unique<CommandStack>() );
            // 에디터 인스턴스를 다시 만들 때(백엔드 교체 · 에디터 모듈 리로드) 선택 · 미저장 표시를 넘긴다.
            services.addSubsystem( sw::make_unique<EditSessionState>() );
        }
        services.addSubsystem( sw::make_unique<DebugOverlayState>() );
        services.addSubsystem( sw::make_unique<ComponentDefaults>() );
        FrameProfiler& frameProfiler = services.addSubsystem( sw::make_unique<FrameProfiler>() );
        // 외부 프로파일러(`-gv_tracy`) — 엔진 범위보다 먼저 켜져 기동 단계부터 타임라인에 남는다.
        services.addSubsystem( sw::make_unique<ProfilerBackendSubsystem>( frameProfiler ) );
        services.addSubsystem( sw::make_unique<CrashReportService>() );
        services.addSubsystem( sw::make_unique<AssetLoadProfiler>() );
        // 데이터 파일 종류 — 정적 등록자는 링크 때 목록에만 달리고, 모듈을 올리는 길(`registerModuleTypes`)이 걷어 이 레지스트리에 올린다.
        services.addSubsystem( sw::make_unique<ResourceDataKindRegistry>() );
        services.addSubsystem( sw::make_unique<StateHashHistory>() );
#if SW_DEV_COMMANDS_ENABLED
        if ( setup._bAuthorityGhost )
            services.addSubsystem( sw::make_unique<AuthorityGhost>() ); // 서버 포즈 고스트(개발 도구) — Shipping 에는 없다
#endif
        {
            SW_MEMORY_SCOPE( RenderCPU );
            services.addSubsystem( sw::make_unique<DebugDrawQueue>() );
            services.addSubsystem( sw::make_unique<VisualLogger>() );
            services.addSubsystem( sw::make_unique<RenderTargetRegistry>() );
            services.addSubsystem( sw::make_unique<RHIBackendRegistry>() );
#if defined( SW_WITH_CLIENT_CODE )
            services.addSubsystem( sw::make_unique<ModuleRenderPassRegistry>() ); // 렌더러 폴더와 같이 클라이언트에만 있다
            services.addSubsystem( sw::make_unique<ScreenshotMailbox>() );        // 같은 까닭 — 렌더 스레드의 스크린샷 요청이 쓰는 우편함
#endif
        }
        return parts;
    }

    StartupParts addStartupSubsystems( SubsystemScope& startup, StartupSetup&& setup )
    {
        // 단계 객체 · 본문 델리게이트의 할당은 엔진 기타 줄로 센다(만드는 객체는 제 용도 태그를 다시 건다).
        SW_MEMORY_SCOPE( EngineMisc );
        StartupParts parts;
        parts._pCodecRegistry = &startup.addSubsystem( sw::make_unique<CompressionCodecRegistry>() );
        {
            SW_MEMORY_SCOPE( Reflection );
            if ( setup._reflection == nullptr )
                setup._reflection = sw::make_unique<ReflectionSubsystem>( sw::make_unique<TypeRegistry>() );
            const ReflectionSubsystem& reflection = startup.addSubsystem( std::move( setup._reflection ) );
            (void)startup.bind( &reflection.getTypeRegistry() ); // 새 범위 — 겹치지 않는다
        }
        TaskManager* pTaskManager = nullptr;
        {
            SW_MEMORY_SCOPE( Task );
            pTaskManager = &startup.addSubsystem( sw::make_unique<TaskManager>() );
        }
        {
            // 완료 콜백(팩 해제 · CRC · 스트리밍 완료 기록)은 태스크 워커에서 돈다 — IO 스레드는 다음 읽기를 거는 일만 한다.
            SW_MEMORY_SCOPE( Asset );
            AsyncFileIOConfig fileIOSettings{};
            fileIOSettings._pTaskManager = pTaskManager;
            startup.addSubsystem( sw::make_unique<AsyncFileIO>() ).setSubsystemSettings( fileIOSettings );
        }
        // 모듈 이미지 창구는 호스트 범위에 있다 — 두 단계가 자기 범위에서 부모로 올라가며 찾는다(호스트 범위가 없으면 올리고 내릴 모듈이 없다).
        startup.addSubsystem( sw::make_unique<ModuleImagesSubsystem>( &startup ) );
        startup.addSubsystem( sw::make_unique<ModuleTypesSubsystem>( &startup ) );
#if defined( SW_WITH_CLIENT_CODE )
        // 창 · 디바이스 · 렌더러 · 렌더 스레드 · 라이브 셰이더 · 씬의 디바이스 — 클라이언트 호스트의 것이다(전용 서버는 거른다). 객체를 서로 참조로 넘긴다.
        // Server 타깃은 렌더러 폴더(`Engine/Renderer/`)를 짓지 않는다(cmake/Engine/BuildLayout.cmake `SW_UNBUILT_ENGINE_FOLDERS`) — 단계를 넣지도 않는다.
        if ( setup._bRenderer )
        {
            // 주 창의 소유자 — RHI 가 기본 창을 만들면 여기에 넘기고, App 이 직접 만든 창도 여기로 간다. RHI 보다 먼저 서고 나중에 내려간다(RHI 가 의존한다).
            (void)startup.addSubsystem( sw::make_unique<MainWindowSubsystem>() );
            RHISubsystem& rhiSubsystem = startup.addSubsystem( sw::make_unique<RHISubsystem>() );
            parts._pRHI                = &rhiSubsystem;
            unique_ptr<FrameRenderer> frameRenderer;
            {
                SW_MEMORY_SCOPE( RenderCPU );
                frameRenderer = make_unique<FrameRenderer>();
            }
            FrameRendererSubsystem& frameRendererSubsystem = startup.addSubsystem( sw::make_unique<FrameRendererSubsystem>( std::move( frameRenderer ), rhiSubsystem ) );
            parts._pFrameRendererSubsystem                 = &frameRendererSubsystem;
            (void)startup.bind( &frameRendererSubsystem.getFrameRenderer() ); // 새 범위 — 겹치지 않는다
            parts._pRenderThread = &startup.addSubsystem( sw::make_unique<RenderThreadSubsystem>( rhiSubsystem, frameRendererSubsystem ) );
            parts._pLiveShader   = &startup.addSubsystem( sw::make_unique<LiveShaderSubsystem>( frameRendererSubsystem, *parts._pRenderThread ) );
            startup.addSubsystem( sw::make_unique<SceneRHISubsystem>( rhiSubsystem ) );
        }
#endif
        parts._pInputManager = &startup.addSubsystem( sw::make_unique<InputManager>() );
        // 설정 표는 Config 서브시스템이다 — 엔진 · 게임 설정을 읽어 자기 자리에 둔다(Resource 가 그 뒤에 서서 팩 루트를 안다).
        {
            SW_MEMORY_SCOPE( EngineMisc );
            parts._pConfigManager = &startup.addSubsystem( sw::make_unique<ConfigManager>() );
        }
        // 호스트 설정 둘은 Config 가 소유한다 - 읽는 것은 Config 의 초기화이고, 서비스 표(`engine::getSubsystem<EngineConfig>()`)는 이 자리의 주소를 묶는다.
        (void)startup.bind( &parts._pConfigManager->getHostEngineConfig() ); // 새 범위 - 겹치지 않는다
        (void)startup.bind( &parts._pConfigManager->getHostGameConfig() );   // 새 범위 - 겹치지 않는다
        {
            SW_MEMORY_SCOPE( Asset );
            startup.addSubsystem( sw::make_unique<AssetManager>() );
        }
        if ( setup._defaultAssets == nullptr )
            setup._defaultAssets = sw::make_unique<EngineDefaultAssetsSubsystem>();
        const EngineDefaultAssetsSubsystem& defaultAssets = startup.addSubsystem( std::move( setup._defaultAssets ) );
        (void)startup.bind( &defaultAssets.getDefaultAssets() ); // 새 범위 — 겹치지 않는다
        // 활성 팩의 `<Engine>` 요소(창 제목 · 스키마 · UI 규칙) — 팩을 마운트한 뒤에 읽는다. 창 · 사용자 설정 · UI 가 이 뒤에 선다.
        const PackEngineSettingsSubsystem& packSettings = startup.addSubsystem( sw::make_unique<PackEngineSettingsSubsystem>() );
        (void)startup.bind( &packSettings.getSettings() ); // 새 범위 — 겹치지 않는다
        // 2D 렌더 설정(정렬 레이어 · 투명 정렬 축)은 엔진 파일이다 - 마운트한 뒤에 읽는다. 2D 컴포넌트를 읽는 단계(헤드리스 쿠킹 · 렌더러)가 이 뒤에 선다.
        const Render2DSettingsSubsystem& render2DSettings = startup.addSubsystem( sw::make_unique<Render2DSettingsSubsystem>() );
        (void)startup.bind( &render2DSettings.getSettings() ); // 새 범위 — 겹치지 않는다
        {
            SW_MEMORY_SCOPE( Audio );
            // 전용 서버는 장치를 열지 않는다 — 씬 컴포넌트가 드는 오디오 서비스는 그대로 있고 소리 요청을 받아 버린다.
            if ( setup._audio == nullptr )
                setup._audio = setup._bDedicatedServer ? IAudioService::createNull() : IAudioService::create();
            parts._pAudio = &startup.addSubsystem( std::move( setup._audio ) );
        }
        // 스트리밍 큐의 소멸자는 태스크를 기다리므로 Task 보다 먼저 놓는다.
        {
            SW_MEMORY_SCOPE( Asset );
            startup.addSubsystem( sw::make_unique<AssetStreamingQueue>() );
        }
        // 씬 매니저가 서브시스템이다 — 씬이 바뀌면 되돌리기 기록을 비우는 구독도 그것이 건다.
        {
            SW_MEMORY_SCOPE( Scene );
            parts._pSceneManager = &startup.addSubsystem( sw::make_unique<SceneManager>() );
        }
        // 헤드리스 작업(셰이더 · 씬 쿠킹, 임포트)이면 그에 기대는 단계(창 · RHI · 렌더러)를 건너뛴다 — 결과는 호스트가 준 상태에 적는다.
        startup.addSubsystem( sw::make_unique<HeadlessSubsystem>( setup._bDedicatedServer, setup._headlessState ) );
        parts._pUserSettings = &startup.addSubsystem( sw::make_unique<UserSettingsManager>() );
        {
            SW_MEMORY_SCOPE( Shader );
            startup.addSubsystem( sw::make_unique<ShaderCache>() );
        }
        startup.addSubsystem( sw::make_unique<PhysicsSystem>() );
        startup.addSubsystem( sw::make_unique<GameNetSession>() ); // 게임 네트워크 세션 — 게임 모듈 리로드를 넘어 연결을 지킨다
        parts._pFontService = &startup.addSubsystem( sw::make_unique<FontService>() );
        parts._pTelemetry   = &startup.addSubsystem( sw::make_unique<TelemetryService>() );
        parts._pUISystem    = &startup.addSubsystem( sw::make_unique<UISystem>() );
        return parts;
    }
} // namespace sw::engine
