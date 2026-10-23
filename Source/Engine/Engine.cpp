#include "pch.h"

#include "Engine/Engine.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Common/BuildInfo.h"
#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Concurrency/ThreadName.h"
#include "Core/Console/DevCommandsEnabled.h"
#include "Core/Container/StringUtil.h"
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/Event/EventDispatcher.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MatrixMath.h"
#include "Core/String/hashed_string.h"
#include "Core/String/string_splitter.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Animation/Facial/LipSync.h"
#include "Engine/Animation/Retarget/PoseRetargeter.h"
#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/IAudioService.h"
#include "Engine/Audio/LipSyncImport.h"
#include "Engine/Automation/AutomationCaptureUtil.h"
#include "Engine/Bootstrap/CommandLineSubsystem.h"
#include "Engine/Bootstrap/CrashHandlerSubsystem.h"
#include "Engine/Bootstrap/DiagnosticsSubsystem.h"
#include "Engine/Bootstrap/MemoryBudgetSubsystem.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Common/EngineSubsystemHost.h"
#include "Engine/Compression/EngineCompressionCodecUtil.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Config/GameSelection.h"
#include "Engine/Config/PackEngineSettings.h"
#include "Engine/EngineSubsystemSet.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
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
#include "Engine/HeadlessSubsystem.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/StringTable.h"
#include "Engine/Module/ModuleTypeRegistry.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Object/GameObject/Service/SceneEngineServices.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Object/SceneAnimation/AnimationCrowd.h"
#include "Engine/Object/SceneAnimation/VertexAnimationCooker.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Profiling/FrameStall.h"
#include "Engine/Profiling/ProfilerBackend.h"
#include "Engine/Profiling/ProfilerTraceExport.h"
#include "Engine/Reflection/ReflectionDocWriter.h"
#include "Engine/Reflection/TypeRegistry.h"

// 렌더러 헤더는 클라이언트 코드에서만 읽는다 — 내보낸(SW_ENGINE_API) 클래스의 인라인 멤버는 include 한 번역 단위마다 만들어지고, 렌더러 폴더를
// 짓지 않는 Server 타깃에서는 그 멤버가 부르는 정의가 없어 링크가 진다(cmake/Engine/BuildLayout.cmake `SW_UNBUILT_ENGINE_FOLDERS`).
#if defined( SW_WITH_CLIENT_CODE )
    #include "Engine/Renderer/Capture/PortraitBatchUtil.h"
    #include "Engine/Renderer/Capture/RenderDocCapture.h"
    #include "Engine/Renderer/Cook/ShaderCookDriver.h"
    #include "Engine/Renderer/Frame/FrameRenderer.h"
    #include "Engine/Renderer/Frame/FrameRendererSubsystem.h"
    #include "Engine/Renderer/Frame/Packet/RenderFramePacket.h"
    #include "Engine/Renderer/LiveShaderSubsystem.h"
    #include "Engine/Renderer/RHISubsystem.h"
    #include "Engine/Renderer/RenderThread.h"
    #include "Engine/Renderer/RenderThreadSubsystem.h"
#endif
#include "Engine/Resource/AssetDatabase.h"
#include "Engine/Resource/AssetLoadProfiler.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/AssetStreamingQueue.h"
#include "Engine/Resource/Pack/ResourcePakManager.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneCooker.h"
#include "Engine/Scene/SceneDebugDraw.h"

#if SW_DEV_COMMANDS_ENABLED
    #include "Engine/Scene/AuthorityGhost.h"
#endif
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneNavigationCooker.h"
#include "Engine/Telemetry/TelemetryService.h"
#include "Engine/Text/FontService.h"
#include "Engine/UI/Debug/DebugHUD.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UserSettings/HardwareProbe.h"
#include "Engine/UserSettings/UserSettingsManager.h"
#include "Engine/UserSettings/UserSettingsVariables.h"
#include "Engine/Utility/EngineFrameObserver.h"
#include "Engine/Config/GameTimeScale.h"
#include "Engine/Window/IWindow.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
    /**
     * @brief `-gv_dumpReflection=CameraComponent,CameraRole`: 첫 프레임에 그 타입 · enum 의 등록 내용을 로그로 남깁니다(`TypeRegistry::describeType`).
     * @details 첫 프레임이라 게임 · 에디터 모듈의 타입까지 등록된 뒤다. 한 번 찍고 비운다.
     */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_dumpReflection, "", "첫 프레임에 이 이름들(쉼표로 여럿)의 리플렉션 등록 내용을 로그로 남긴다 — 타입 · enum (비우면 사용 안 함)" );

} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "Engine" );

    Engine::Engine()
        : _bootstrap{}
        , _services{}
        , _pCommandLineSubsystem{ nullptr }
        , _pEventDispatcher{ nullptr }
        , _pConfigManager{ nullptr }
        , _shellActions{}
        , _listFrameObserver{}
        , _hostRole{ EngineHostRole::Client }
        , _headlessState{}
        , _bQuitRequested{ false }
        , _rhiInitResult{ RHIInitResult::NotStarted }
        , _sceneDeltaSeconds{ 0.0f }
        , _frameDeltaSeconds{ 0.0f }
        , _exitCode{ 0 }
        , _profileSession{}
        , _pMemoryBudget{ nullptr }
        , _startup{}
        , _pRHISubsystem{ nullptr }
        , _pRHI{ nullptr }
        , _pFontService{ nullptr }
        , _pUISystem{ nullptr }
        , _pInputManager{ nullptr }
        , _pAudioService{ nullptr }
        , _pSceneManager{ nullptr }
        , _pFrameRenderer{ nullptr }
        , _pPacketBuilder{ nullptr }
        , _pRenderThread{ nullptr }
#if !defined( SW_SHIPPING )
        , _pShaderRecompiler{ nullptr }
#endif
    {
    }

    Engine::~Engine() = default;

    CommandLineManager* Engine::getCommandLineManager() const
    {
        return _pCommandLineSubsystem != nullptr ? _pCommandLineSubsystem->findCommandLine() : nullptr;
    }

    bool Engine::initialize( int32 argc, utf8* pArgv[], EngineHostRole role, const ISubsystemLocator* pHostScope )
    {
        // 범위는 호스트 아래 셋이다. 호스트(엔진을 띄운 쪽이 들고, 엔진보다 먼저 서서 나중에 사라진다 — 모듈 이미지 창구 · 자동화 구동기) 아래로
        // 부트스트랩(진단 도구 · 이름 풀 · 로거 · 명령줄 · 크래시 핸들러 · 정적 저장소) → 서비스(엔진 범위의 모든 것보다 오래 사는
        // 공용 서비스) → 엔진(기동 단계). 아래 범위가 모두 선 뒤에 위 범위를 세우고, 위 범위가 모두 사라진 뒤에 아래 범위를 내린다 — 범위 사이의
        // 순서는 의존으로 적지 않는다. 서비스 범위의 객체는 이름 풀 · 로거가 선 뒤에 만든다(생성자가 이름 · 블록을 잡는다). 테스트 하네스도 같은 세 범위다(호스트 범위는 없다).
        _bootstrap.setParentScope( pHostScope );
#if defined( SW_DEBUG )
        constexpr bool kDiagnostics = true;
#else
        constexpr bool kDiagnostics = false;
#endif
        // 서브시스템 목록은 `EngineSubsystemSet.cpp` 의 한 곳이다 — 테스트 하네스 · 기동 순서 테스트도 같은 함수로 채운다.
        engine::BootstrapSetup bootstrapSetup;
        bootstrapSetup._argc                        = argc;
        bootstrapSetup._ppArgv                      = pArgv;
        bootstrapSetup._bDiagnostics                = kDiagnostics;
        bootstrapSetup._bMemoryBudget               = true;
        const engine::BootstrapParts bootstrapParts = engine::addBootstrapSubsystems( _bootstrap, bootstrapSetup );
        CrashHandlerSubsystem*       pCrashHandler  = bootstrapParts._pCrashHandler;
        _pCommandLineSubsystem                      = bootstrapParts._pCommandLine;
        _pMemoryBudget                              = bootstrapParts._pMemoryBudget;
        if ( engine::initializeBootstrapSubsystems( _bootstrap, bootstrapParts ) == false )
        {
            // 정렬 오류(선언 오타 · 순환)는 로거가 서기 전이다 — 표준 오류로도 낸다.
            std::fprintf( stderr, "[Engine] bootstrap failed: %s\n", _bootstrap.getCollection().getError().c_str() );
            SW_LOG_ERROR( "Engine bootstrap failed: %#", _bootstrap.getCollection().getError().c_str() );
            return false;
        }
        // 크래시 보고 프로세스(`-crash-reporter=<폴더>`)는 헤드리스다 — 서비스 · 기동 단계를 세우지 않고 App 이 묶음을 보내고 끝낸다.
        // 이 모드에서는 크래시 핸들러(죽으면 보고 프로세스를 또 띄운다) · 리소스 루트를 세우지 않았다.
        if ( pCrashHandler->isCrashReporterRun() )
        {
            _hostRole = role;
            _headlessState.enterHeadless();
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
        const SubsystemHostMask hostMask = bDedicatedServer ? EngineSubsystemHost::kServer : EngineSubsystemHost::kClient;
        // 서버 패키지에 없는 에셋 종류(텍스처 · 셰이더 바이너리 · 오디오 — 쿠킹 표의 target_excluded_asset_kinds)는 서버가 읽지 않는다.
        ResourcePaths::setHostTarget( bDedicatedServer ? "Server" : "Client" );
        // 외부 프로파일러(서비스 범위의 `ProfilerBackend`)의 스레드 이름 — 서비스 범위가 서기 전에 둔다.
        ThreadName::setCurrentThreadName( "GameThread" );

        BLOCK( "서비스 범위" )
        {
            // 초기화 · 종료할 것이 없고 수명만 있는 공용 서비스다 — 엔진 범위의 어느 것보다 먼저 서고 나중에 사라진다(누가 소멸자에서 부르든 살아 있다).
            SW_MEMORY_SCOPE( EngineMisc );
            _services.setParentScope( &_bootstrap );
#if defined( SW_SHIPPING )
            constexpr bool kEditorServices = false; // 에디터 Undo/Redo 전용이라 배포본에는 만들지 않는다(목록의 선택 행)
#else
            constexpr bool kEditorServices = true;
#endif
            engine::ServiceSetup serviceSetup;
            serviceSetup._bEditorServices = kEditorServices;
            _pEventDispatcher             = engine::addServiceSubsystems( _services, serviceSetup )._pEventDispatcher;
            if ( _services.getCollection().initializeAll( hostMask ) == false )
            {
                SW_LOG_ERROR( "Engine services failed: %#", _services.getCollection().getError().c_str() );
                return false;
            }
            // 셸 액션은 엔진의 값 멤버다 — 개발 명령(`shell.rebind`)이 `engine::findSubsystem<ShellInputActions>()` 로 찾게 빌려 건다(`shutdown` 이 맵을 놓을 때 푼다).
            (void)_services.bind( &_shellActions ); // 새 범위 — 겹치지 않는다
        }

        // 기동 단계는 서브시스템이다 — 서브시스템이 스스로 이름 · 의존 · 호스트를 선언한다. 추가 순서는 뜻이 없다(의존이 정한다). 해제는 그 역순이다.
        engine::StartupParts startupParts;
        BLOCK( "기동 단계 구성" )
        {
            _startup.setParentScope( &_services );
            // 외부 라이브러리 코덱은 **목록이 있는 자리**에서 붙인다(EngineCompressionCodecUtil). 레지스트리는 만들 때 내장 코덱을 갖추므로 기동 전에 붙여도 된다.
            engine::StartupSetup startupSetup{ _headlessState };
            startupSetup._bDedicatedServer = bDedicatedServer;
            startupSetup._bRenderer        = true;
            startupParts                   = engine::addStartupSubsystems( _startup, std::move( startupSetup ) );
            EngineCompressionCodecUtil::registerAll( *startupParts._pCodecRegistry );
            // 다시 읽기 알림은 엔진 루프가 받는다.
            _pConfigManager = startupParts._pConfigManager;
            _pConfigManager->onConfigReloaded().add( SW_DELEGATE_METHOD( Delegate<void( const hashed_string& )>, &Engine::onConfigReloaded, this ) );
            _pInputManager = startupParts._pInputManager;
            _pAudioService = startupParts._pAudio;
            _pSceneManager = startupParts._pSceneManager;
            _pFontService  = startupParts._pFontService;
            _pUISystem     = startupParts._pUISystem;
#if defined( SW_WITH_CLIENT_CODE )
            _pRHISubsystem  = startupParts._pRHI;
            _pFrameRenderer = &startupParts._pFrameRendererSubsystem->getFrameRenderer();
            _pPacketBuilder = &startupParts._pFrameRendererSubsystem->getPacketBuilder();
#endif
#if SW_DEV_COMMANDS_ENABLED
            // 디버그 HUD 설정(개발 도구) — Dev 만 덧붙인다. 사용자 파일보다 먼저 읽어야 저장된 HUD 값이 버려지지 않는다.
            startupParts._pUserSettings->addStartupSchema( &EngineDefaultAssets::_debugHUDSettingsSchema );
#endif
        }
        // 서비스 표는 엔진 범위(부모 범위까지)에서 행마다 한 번 찾아 둔다. 필수 행이 비면 서지 않는다 — 넣지 않은 서브시스템이다.
        if ( const utf8* pMissing = engine::bindEngineServices( _startup ) )
        {
            SW_LOG_ERROR( "Required engine service '%#' is not in any subsystem scope", pMissing );
            return false;
        }
        TelemetryService& telemetryService = *startupParts._pTelemetry;

        // 초기화의 순서는 손으로 적지 않는다. 서브시스템마다 먼저 서야 하는 것을 적고, 여기서는 위상 순서로 부른다. 종료와 해제는 그 역순이다(`shutdown`).
        const bool bStarted = _startup.getCollection().initializeAll( hostMask );
        // 서지 않은 것(전용 서버 · 헤드리스 · 실패)은 nullptr 다. 실패의 이유는 App 이 종료 코드를 고를 때 읽는다.
#if defined( SW_WITH_CLIENT_CODE )
        _pRHI          = _pRHISubsystem->findRHI();
        _rhiInitResult = _pRHISubsystem->getInitResult();
        _pRenderThread = startupParts._pRenderThread->findRenderThread();
    #if !defined( SW_SHIPPING )
        _pShaderRecompiler = startupParts._pLiveShader->findShaderRecompiler();
    #else
        (void)startupParts._pLiveShader; // 배포본은 라이브 리로드가 없다
    #endif
#endif
        // Server 타깃 — 렌더 단계가 없다. RHI · 렌더 스레드 · 패킷 빌더는 nullptr 로 남고, 아래 루프는 그것을 보고 건너뛴다.
        if ( bStarted == false )
        {
            SW_LOG_ERROR( "Engine startup failed: %#", _startup.getCollection().getError().c_str() );
            return false;
        }
        // 셸 키(콘솔 여는 키 …)를 플레이어가 바꾼 것도 사용자 설정과 같은 폴더에 둔다. 셸 맵은 처음 돌 때 만들므로 그 전에 경로를 알려 준다.
        if ( bDedicatedServer == false && _headlessState.isHeadless() == false )
            _shellActions.setUserBindingsPath( ShellInputActions::makeUserBindingsPath( engine::getSubsystem<UserSettingsManager>().getUserFilePath() ) );
        // 장면별 프레임 시간 · flush 는 프레임 관찰자로 돈다(씬 틱 뒤 — `TelemetryService::onAfterSceneTick`). 텔레메트리가 섰을 때만 붙인다.
        // 떼는 것은 `shutdown` 이 단계 종료 전에 관찰자 목록을 비우는 것으로 끝난다.
        if ( telemetryService.isInitialized() )
            registerFrameObserver( &telemetryService );
        if ( bDedicatedServer )
        {
            // 실제 바이너리에서 단계가 빠졌는지 테스트(ServerBootTest)가 이 줄로 본다. 초기화 순서대로 적는다.
            const SubsystemCollection& subsystems = _startup.getCollection();
            string                     skipped;
            for ( const uint32 index : subsystems.getInitializeOrder() )
            {
                const ISubsystem& subsystem = subsystems.getSubsystem( index );
                if ( ( static_cast<uint8>( subsystem.getSubsystemHost() ) & static_cast<uint8>( EngineSubsystemHost::kServer ) ) != 0 )
                    continue;
                if ( skipped.empty() == false )
                    skipped += " ";
                skipped += subsystem.getSubsystemName();
            }
            SW_LOG_INFO( "Dedicated server: startup steps not run - %#", skipped.c_str() );
        }
        // 헤드리스 작업(셰이더 · 씬 쿠킹, 텍스처 임포트)은 RHI 이후 단계를 건너뛰고 여기서 끝난다. 누수 기준선은 그 작업 직전에 잡았다.
        if ( _headlessState.isHeadless() )
            return true;

        _profileSession.begin();
#if SW_PROFILER_TRACE_ENABLED
        ProfilerTraceExport::startExitCapture( engine::getSubsystem<FrameProfiler>() ); // `-gv_traceSaveOnExit` — 없으면 아무것도 하지 않는다
#endif

        MemoryProfiler::captureMemoryLeakBaseline();

        return true;
    }

    void Engine::shutdown()
    {
#if SW_PROFILER_TRACE_ENABLED
        // 종료 트레이스(`-gv_traceSaveOnExit`)는 서비스 · 프로파일러가 아직 있을 때 쓴다. 기동이 서비스 표 전에 멈췄으면 쓸 것이 없다.
        if ( engine::areEngineServicesBound() && _headlessState.isHeadless() == false )
            ProfilerTraceExport::finishExitCapture( engine::getSubsystem<FrameProfiler>(), _hostRole == EngineHostRole::DedicatedServer ? "Server" : "App" );
#endif
        // 프레임 관찰자는 붙인 쪽이 서비스보다 먼저 내린다(로그 리스너 · 입력을 빌려 쓴다). 남은 것은 더 부르지 않는다.
        // 목록의 용량은 기동 뒤에 잡았으므로 누수 보고 전에 해제한다.
        vector<IEngineFrameObserver*>{}.swap( _listFrameObserver );
#if SW_DEV_COMMANDS_ENABLED
        AuthorityGhost::get().shutdown(); // 작업 스레드는 이 이미지의 코드다 - 이미지가 내려가기 전에 합류한다
#endif
        AssetLoadProfiler::get().reportIfRequested( "session" );
        // 초기화한 단계만 역순으로 내린다: UI → 텔레메트리 → 씬의 디바이스 → 렌더 스레드 → 렌더러 → RHI → 씬 → 입력 · 오디오 → 모듈 이미지 → 태스크 → 셰이더 캐시.
        _startup.getCollection().shutdownAll();
        // 셸 디버그 액션 맵은 입력 매니저를 가리킨다 — 매니저가 소멸하기(Input 의 해제 자리) 전에 놓는다(`updateShellActions` 가 처음 쓸 때 만든다).
        _shellActions.reset();
        _services.unbind<ShellInputActions>();
        // 단계가 소유한 객체를 초기화 순서의 역순으로 소멸시킨다: UI → 텔레메트리 → 렌더러 쪽 → RHI →
        // 글꼴 → 씬 → 입력 · 오디오 → 태스크 → 셰이더 캐시 → 엔진 데이터 → 리소스 → 설정 → 리플렉션 → 압축. 기동이 어디서 멈췄든 모든 단계를 해제한다.
        _startup.destroyAll();
        _pRHISubsystem  = nullptr;
        _pRHI           = nullptr;
        _pFontService   = nullptr;
        _pUISystem      = nullptr;
        _pInputManager  = nullptr;
        _pAudioService  = nullptr;
        _pSceneManager  = nullptr;
        _pFrameRenderer = nullptr;
        _pPacketBuilder = nullptr;
        _pRenderThread  = nullptr;
#if !defined( SW_SHIPPING )
        _pShaderRecompiler = nullptr;
#endif
        _pConfigManager = nullptr;
        // 서비스 범위 → (표 끊기) → 부트스트랩 범위. 테스트 하네스와 같은 끝 정리다.
        _services.destroyAll();
        _pEventDispatcher = nullptr;
        engine::unbindEngineServices();
        _bootstrap.destroyAll();
        _pCommandLineSubsystem = nullptr;
        _pMemoryBudget         = nullptr;

        MemoryProfiler::reportMemoryLeaks( "Engine::shutdown" );
    }

    void Engine::onConfigReloaded( const hashed_string& configTypeName )
    {
        if ( configTypeName == GameConfig::StaticType()->_fullyQualifiedName )
        {
            const GameConfig* pGameConfig = _pConfigManager != nullptr ? _pConfigManager->getConfig<GameConfig>() : nullptr;
            if ( pGameConfig != nullptr )
                GameConfig::setActive( *pGameConfig );
            return;
        }
        const EngineConfig* pEngineConfig = _pConfigManager != nullptr ? _pConfigManager->getConfig<EngineConfig>() : nullptr;
        if ( configTypeName != EngineConfig::StaticType()->_fullyQualifiedName || pEngineConfig == nullptr )
            return;
        EngineConfig::setActive( *pEngineConfig );
        // 수직 동기화는 다음에 스왑체인을 만들 때(창 크기 변경 · 백엔드 교체) 적용된다.
        if ( _pRHI != nullptr )
            _pRHI->setPreferredVSync( pEngineConfig->_window._bVSync );
    }

    bool Engine::renderPortraits( string_view prefabList, string_view outputDirectory, uint32 size )
    {
#if defined( SW_WITH_CLIENT_CODE )
        const bool bHasDevice = _pRHI != nullptr && _pRHI->hasDevice();
        if ( bHasDevice == false || _pSceneManager == nullptr )
            return false;
        return PortraitBatchUtil::renderPrefabList( _pRHI->getDevice(), _pSceneManager->getEngineServices(), _pRenderThread, prefabList, outputDirectory, size );
#else
        // Server 타깃에는 렌더러가 없다 — 초상화를 그릴 수 없다는 것을 알리고 실패한다.
        (void)prefabList;
        (void)outputDirectory;
        (void)size;
        SW_LOG_ERROR( "--render-portraits needs the renderer - the %# target does not build it", build::kTargetName );
        return false;
#endif
    }

    void Engine::requestQuit( int32 exitCode )
    {
        if ( _bQuitRequested )
            return;
        _bQuitRequested = true;
        _exitCode       = exitCode;
        SW_LOG_INFO( "Quit requested (exit code %#)", exitCode );
    }

    void Engine::registerFrameObserver( IEngineFrameObserver* pObserver )
    {
        if ( pObserver == nullptr )
            return;
        for ( const IEngineFrameObserver* pExisting : _listFrameObserver )
        {
            if ( pExisting == pObserver )
                return;
        }
        _listFrameObserver.push_back( pObserver );
    }

    void Engine::unregisterFrameObserver( IEngineFrameObserver* pObserver )
    {
        for ( size_t index = 0; index < _listFrameObserver.size(); ++index )
        {
            if ( _listFrameObserver[index] != pObserver )
                continue;
            _listFrameObserver.erase( _listFrameObserver.begin() + static_cast<ptrdiff_t>( index ) );
            return;
        }
    }

    EngineFrameContext Engine::makeFrameContext() const
    {
        EngineFrameContext context;
        context._pInput = _pInputManager;
#if defined( SW_WITH_CLIENT_CODE )
        context._completedScreenshotCount = _pRenderThread != nullptr ? _pRenderThread->getCompletedScenarioScreenshotCount() : 0u;
#else
        context._completedScreenshotCount = 0u; // Server 타깃 — 렌더 스레드가 없어 찍은 사진이 없다
#endif
        context._deltaSeconds     = _frameDeltaSeconds;
        const Scene* pActiveScene = _pSceneManager != nullptr ? _pSceneManager->getActiveScene() : nullptr;
        if ( pActiveScene != nullptr )
            context._activeSceneID = pActiveScene->getSourcePath().empty() ? pActiveScene->getName() : pActiveScene->getSourcePath();
        return context;
    }

    void Engine::onWindowClosed()
    {
        if ( _listFrameObserver.empty() )
            return;
        const EngineFrameContext context = makeFrameContext();
        for ( IEngineFrameObserver* pObserver : _listFrameObserver )
        {
            const int32 observerExitCode = pObserver->onWindowClosed( context );
            if ( observerExitCode == IEngineFrameObserver::kRunning )
                continue;
            // 관찰자의 결과가 앞선 종료 요청(프로파일 세션)의 코드보다 이긴다.
            _exitCode       = observerExitCode;
            _bQuitRequested = true;
            return;
        }
    }

    void Engine::beginFrame( float32 deltaSeconds, float32 unscaledDeltaSeconds )
    {
        _frameDeltaSeconds = deltaSeconds;
        // 실제 경과는 호스트가 따로 적는 단계가 아니라 프레임 시작의 인자다 — 적지 않은 호스트가 생길 수 없다.
        GameTimeScale::setUnscaledDeltaTime( unscaledDeltaSeconds );
        // 관찰자의 프레임 앞 단계(시나리오의 시작 조건 · 가상 입력 붙이기 · 행동 층 단계)는 이 프레임의 입력 재생 전이다.
        if ( _listFrameObserver.empty() == false )
        {
            const EngineFrameContext context = makeFrameContext();
            for ( IEngineFrameObserver* pObserver : _listFrameObserver )
            {
                pObserver->onFrameBegin( context );
            }
        }
        if ( _pInputManager != nullptr )
            _pInputManager->beginFrame( deltaSeconds );
        // UI 는 게임 틱보다 먼저 입력을 받는다 — UI 가 먹은 행동 · 마우스 버튼은 이번 프레임 폰의 의도에 들지 않는다(플레이어 조종자가 본다).
        if ( _pUISystem != nullptr && _pUISystem->isInitialized() )
            _pUISystem->processInput( GameTimeScale::getUnscaledDeltaTime() ); // 정지 메뉴의 탐색 반복은 실제 시간

        if ( gv_dumpReflection.empty() == false )
        {
            const TypeRegistry&   registry = engine::getSubsystem<TypeRegistry>();
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

    void Engine::update( float32 deltaTime, const HostViewTargets& views, const ViewCameraProviderDelegate& sceneViewCameraProvider, bool bTickScene )
    {
        // 주 출력 — 게임 뷰가 있으면 그것(게임 카메라 · 화면 UI · 화면 사각형 뷰), 씬 뷰만 있으면 씬 뷰, 둘 다 없으면 백버퍼다.
        const HostViewTarget& mainOutput = views.getMainOutput();
        const bool            bSceneMain = views.isSceneViewMain();
        // 게임이 그려지는 픽셀 크기 — 주 출력이 있으면 그 크기(에디터 게임 뷰의 이미지), 없으면 백버퍼(게임 창)다. 창 크기가 아니다:
        // 에디터의 게임 뷰는 창의 일부라 창 크기로 종횡비를 세면 카메라가 늘어난다. 카메라(종횡비 · 픽셀 퍼펙트)와 런타임 UI 가 이 값 하나를 쓴다.
        const IWindow* const pMainWindow = IWindow::getActiveWindow();
        const bool           bHasDevice  = _pRHI != nullptr && _pRHI->hasDevice();
        const bool           bHasOutput  = mainOutput._width > 0 && mainOutput._height > 0;
        const uint32         vpWidth     = bHasOutput ? mainOutput._width : ( bHasDevice ? _pRHI->getDevice().getBackBufferWidth() : ( pMainWindow != nullptr ? pMainWindow->getWidth() : 0 ) );
        const uint32         vpHeight    = bHasOutput ? mainOutput._height : ( bHasDevice ? _pRHI->getDevice().getBackBufferHeight() : ( pMainWindow != nullptr ? pMainWindow->getHeight() : 0 ) );
        _frameDeltaSeconds               = deltaTime;

        if ( _headlessState.isHeadless() )
            return;

        FrameProfiler& profiler = engine::getSubsystem<FrameProfiler>();
        profiler.beginFrame();

        // 게임 스레드 전체를 한 구간으로 잡는다. 보고서가 RT 구간만 보여 주면 "프레임의 몇 % 를 측정했나" 에
        // 답할 수 없다. 그 답이 없으면 다음 최적화 대상을 고르는 근거도 없다.
        SW_PROFILE_SCOPE( "GT.Frame" );
#if SW_DEV_COMMANDS_ENABLED
        // 개발 명령 `profile.stall` 이 건 지연을 이 구간 안에서 소비합니다. 스파이크를 프레임 비용에 기대지 않고 만듭니다.
        FrameStall::consume();
#endif

        BLOCK( "핫 리로드 / 씬 트랜지션 / 이벤트" )
        {
#if !defined( SW_SHIPPING )
    #if defined( SW_DEBUG )
            if ( _pRHI != nullptr )
            {
                if ( ShaderRecompiler* pShaderRecompiler = getShaderRecompiler() )
                    pShaderRecompiler->update();
            }
    #endif
            pollShaderReloadHotkey();
#endif
            // 파일 대화 상자 결과를 **여기서** 메인 스레드로 넘긴다. 대화 상자는 별도 스레드가 띄운다.
            FileUtil::pumpFileDialogResults();
            engine::getSubsystem<AssetStreamingQueue>().update();

            if ( _pSceneManager != nullptr )
                _pSceneManager->updateTransitions();
            if ( _pEventDispatcher != nullptr )
                _pEventDispatcher->processEvents();
            if ( _pAudioService != nullptr )
                _pAudioService->update( deltaTime );
        }

        _sceneDeltaSeconds = bTickScene ? deltaTime : 0.0f;
        BLOCK( "Scene update" )
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick" );
            // 게임 카메라가 그리는 뷰포트 크기를 틱 전에 적는다 — 픽셀 퍼펙트 카메라가 배율을 고른다(`CameraRegistry::getViewportWidth`).
            Scene* pTickScene = ( _pSceneManager != nullptr ) ? _pSceneManager->getActiveScene() : nullptr;
            if ( pTickScene != nullptr && pTickScene->getObjectManager() != nullptr )
                pTickScene->getObjectManager()->getCameraRegistry().setViewportSize( vpWidth, vpHeight );
            // 입력의 게임 화면도 같은 값이다(포인터 비율 · 가상 커서 영역). 창 안 위치는 주 출력이 그린 이미지의 위치다(HostViewTarget::_windowX · _windowY, 백버퍼면 0).
            if ( _pInputManager != nullptr )
                _pInputManager->setGameViewRect( InputViewRect{
                    int2{          mainOutput._windowX,            mainOutput._windowY},
                    int2{static_cast<int32>( vpWidth ), static_cast<int32>( vpHeight )}
                } );
            if ( _pSceneManager != nullptr && bTickScene )
                _pSceneManager->update( deltaTime );
            // 미리보기 씬은 활성 씬의 정지 · 재생과 무관하게 자기 정책으로 돈다(에디터가 멈춘 중에도 애니메이션 창은 돈다).
            if ( _pSceneManager != nullptr )
                _pSceneManager->updatePreviewScenes( deltaTime );
        }

        // 물리 · 내비 디버그 선 — 씬 틱이 이번 프레임의 바디 · 경로를 정한 **뒤**, 관찰자 · UI · 패킷 **앞**이다(이번 프레임에 넣은 선이 endFrame 에서 확정된다).
        if ( _pSceneManager != nullptr )
            SceneDebugDraw::update( _pSceneManager->getActiveScene(), engine::getSubsystem<DebugDrawQueue>() );
#if SW_DEV_COMMANDS_ENABLED
        // 서버가 가진 위치를 고스트로 겹친다(`-gv_authorityGhostPort` 가 0 이면 아무 일도 없다, 서버 디버깅 S6).
        if ( _pSceneManager != nullptr )
            AuthorityGhost::get().update( _pSceneManager->getActiveScene(), engine::getSubsystem<DebugDrawQueue>() );
#endif

        // 관찰자 훅 `onAfterSceneTick` — 이 프레임의 게임 상태가 정해진 뒤(씬 틱 · 디버그 그리기 뒤), 런타임 UI · 렌더 패킷 앞이라는 순서가 뜻이 있다(텔레메트리의 장면별 프레임 시간).
        if ( _listFrameObserver.empty() == false )
        {
            const EngineFrameContext context = makeFrameContext();
            for ( IEngineFrameObserver* pObserver : _listFrameObserver )
            {
                pObserver->onAfterSceneTick( context );
            }
        }

        // 런타임 UI — 이번 프레임의 게임 상태로 애니메이션 · 스타일 · 레이아웃을 돌린다(게임 틱 뒤 · 렌더 패킷 앞). 뷰포트는 게임이 그려지는 화면이고,
        // UI 단위 크기 · 배율 · 안전 영역은 해상도 규칙과 사용자 배율(gv_uiScale)로 정한다.
        if ( _pUISystem != nullptr && _pUISystem->isInitialized() )
        {
#if SW_DEV_COMMANDS_ENABLED
            // 디버그 HUD — 꺼져 있으면 셸 맵 액션 하나를 읽고 끝난다. 켜져 있으면 HUD 화면이 아래 update 의 틱에서 섹션을 부른다.
            DebugHUD* const pDebugHUD = _pUISystem->getDebugHUD();
            if ( pDebugHUD != nullptr )
                pDebugHUD->update( _shellActions.getInputMap() );
#endif
            // 뷰포트 0 은 "백버퍼 전체"(게임 창 — 패킷 · 캔버스 테스트 그림과 같은 규칙)다.
            const float32 contentScale = pMainWindow != nullptr ? pMainWindow->getContentScale() : 1.0f;
            UIViewport    viewport     = _pUISystem->computeViewport( float2{ static_cast<float32>( vpWidth ), static_cast<float32>( vpHeight ) }, contentScale );
            viewport._pointerOrigin    = int2{ mainOutput._windowX, mainOutput._windowY }; // 백버퍼면 빈 출력이라 0 이다
            // 음성 이벤트의 자막 — 오디오 스레드가 시작한 클립의 자막 키를 쌓아 두고, 가져가는 곳은 여기 하나다.
            if ( _pAudioService != nullptr && _pAudioService->isInitialized() )
            {
                vector<AudioSubtitleCue> listCue;
                _pAudioService->getEngine().takeSubtitleCues( listCue );
                _pUISystem->getSubtitles().postAudioCues( listCue );
            }
            _pUISystem->update( GameTimeScale::getUnscaledDeltaTime(), viewport ); // 애니메이션 · 자막 · 알림은 정지 메뉴 아래서도 흐른다
        }

        // 이번 틱에 경로로 잡힌 머티리얼(메시의 저장된 참조)을 패킷을 내기 **전에** 올린다. 컴포넌트는 디바이스를 모른다(`MaterialCache::requestInitialize`).
        // 올리기는 bindless 표에 등록하므로 렌더 스레드가 지난 프레임을 병렬로 기록하는 동안 하면 안 된다 — 올릴 것이 있는 프레임(씬 로드 ·
        // 처음 쓰는 머티리얼의 스폰)만 렌더 스레드를 기다린다.
#if defined( SW_WITH_CLIENT_CODE )
        if ( _pRHI != nullptr && _pRHI->hasDevice() )
        {
            MaterialCache& materials = engine::getSubsystem<AssetManager>().getAssetCache<MaterialCache>();
            if ( materials.hasPendingInitialize() && _pRenderThread != nullptr )
                _pRenderThread->waitIdle();
            materials.initializePending( &_pRHI->getDevice() );
            // 밉 스트리밍이 바꿔 끼운 옛 텍스처를 게임 스레드에서 해제한다 — 이미 만든 패킷이 옛 SRV 번호를 쓰는 동안은 `releaseHandle` 이 미룬다.
            engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>().releaseRetiredTextures( &_pRHI->getDevice() );
        }

        // 패킷 · 씬 스냅샷은 FrameRenderer 단계가 만든다. 그 단계가 서지 않았으면(헤드리스 작업) 낼 패킷이 없다.
        if ( _pPacketBuilder == nullptr || _pPacketBuilder->isInitialized() == false )
            return;

        Scene* pActiveScene = _pSceneManager != nullptr ? _pSceneManager->getActiveScene() : nullptr;

        // 패킷 채우기의 순서는 여기서 보인다: 열기 → 관찰자 → 씬 · 뷰 → 캔버스 → 제출. 씬 · 뷰는 위의 씬 틱 · 디버그 그리기 · UI 갱신 **뒤**라
        // 이번 프레임의 게임 상태를 싣고, 캔버스는 UI 갱신이 칠한 목록을 싣는다.
        BLOCK( "RenderFramePacket 제출" )
        {
            SW_MEMORY_SCOPE( RenderCPU );
            RenderFramePacket& packet = _pPacketBuilder->beginPacket();
            // 관찰자가 실을 것(시나리오 `<Screenshot>` 의 경로)은 다음에 그리는 이 패킷에 싣는다.
            if ( _listFrameObserver.empty() == false )
            {
                EngineRenderPacketContext packetContext;
                packetContext._pScreenshotPath = &packet._screenshotPath;
                for ( IEngineFrameObserver* pObserver : _listFrameObserver )
                {
                    pObserver->onBuildRenderPacket( packetContext );
                }
            }

            // 시작 인자 캡처(`-gv_screenshotSettledFrames`)가 기다리는 동안만 판정한다 — 첫 씬이 플레이를 시작하기 전(로딩 화면을 열기 전)은 자리 잡지 않았다.
            if ( RenderThread::needsCaptureReadiness() )
            {
                const GameObjectManager* pManager = pActiveScene != nullptr ? pActiveScene->getObjectManager() : nullptr;
                packet._bReadyForCapture          = pManager != nullptr && pManager->hasBegunPlay() && AutomationCaptureUtil::isReadyForCapture( pManager );
            }

            _pPacketBuilder->populateScene( packet, views, sceneViewCameraProvider, pActiveScene, deltaTime, *_pRHI, _pFrameRenderer );
            // 미리보기 씬(에디터 툴 창)은 씬마다 자기 스냅샷 · 렌더 타깃으로 — 활성 씬과 섞이지 않는다.
            if ( _pSceneManager != nullptr )
                _pPacketBuilder->populateScenePreviews( packet, views, *_pSceneManager );
            RenderPacketBuilder::populateCanvas( packet, bSceneMain, _pUISystem, _pFontService, _pRHI );

            if ( _pRenderThread != nullptr )
            {
                // GT 가 여기서 기다린다면 그것은 렌더 스레드가 밀린 것이다. 링이 차면 submit 이 막는다.
                SW_PROFILE_SCOPE( "GT.Packet.submit" );
                _pRenderThread->submit( packet );
            }
        }
#else
        // Server 타깃 — 렌더러가 없어 올릴 머티리얼도 낼 패킷도 없다(뷰 · 카메라 조회는 쓰지 않는다).
        (void)bSceneMain;
        (void)sceneViewCameraProvider;
#endif
    }

    void Engine::endFrame()
    {
        engine::getSubsystem<FrameProfiler>().endFrame();
#if SW_PROFILER_TRACE_ENABLED
        ProfilerTraceExport::updateRecording( engine::getSubsystem<FrameProfiler>() ); // `profile.trace record` 의 끝 시각 확인(진행 중이 아니면 atomic 하나)
#endif
        // 외부 프로파일러(Tracy)의 주 프레임 경계 — 게임 스레드 프레임이다.
        IProfilerBackend* pProfilerBackend = ProfilerBackend::getActiveBackend();
        if ( pProfilerBackend != nullptr )
            pProfilerBackend->markFrame( nullptr );
        const bool bReportedBefore = _profileSession.hasReported();
        _profileSession.onFrameEnd();
        // CPU 메모리 태그 표 옆에 GPU 메모리 표를 둔다. 보고 세션은 Utility 층이라 Graphics 의 장부를 볼 수 없어 여기서 잇는다.
        const bool bReportedNow = bReportedBefore == false && _profileSession.hasReported();
        const bool bHasDevice   = _pRHI != nullptr && _pRHI->hasDevice();
        if ( bReportedNow && bHasDevice )
            _pRHI->getDevice().getMemoryLedger().report( _pRHI->getDevice().getBackendName() );

        if ( _pMemoryBudget != nullptr )
            _pMemoryBudget->onFrameEnd();

        // 관찰자의 프레임 뒤 단계(시나리오의 단언 · 환경 단계)는 씬 틱 · 렌더 제출 뒤, 입력 프레임을 닫기 전이다(그 프레임의 눌림 엣지가 아직 보인다).
        if ( _listFrameObserver.empty() == false )
        {
            const EngineFrameContext context = makeFrameContext();
            for ( IEngineFrameObserver* pObserver : _listFrameObserver )
            {
                const int32 observerExitCode = pObserver->onFrameEnd( context );
                if ( observerExitCode != IEngineFrameObserver::kRunning )
                    requestQuit( observerExitCode ); // 여럿이면 처음 것의 코드다
            }
        }
        // `-gv_screenshotQuit`: 요청한 사진을 모두 찍었으면 그 결과(0 또는 쓰기 실패)로 끝낸다. 관찰자(시나리오)의 코드가 먼저 요청됐으면 그것이 이긴다.
#if defined( SW_WITH_CLIENT_CODE )
        const int32 captureExitCode = RenderThread::getCaptureExitCode();
        if ( captureExitCode != RenderThread::kNoCaptureExitCode )
            requestQuit( captureExitCode );
#endif
        if ( _pInputManager != nullptr )
            _pInputManager->endFrame();
        // 이번 프레임에 넣은 디버그 도형을 보이는 목록으로 확정하고, 씬이 흘린 시간만큼 지속 시간을 줄인다(일시정지면 그대로 남는다).
        engine::getSubsystem<DebugDrawQueue>().endFrame( _sceneDeltaSeconds );
    }

    bool Engine::applyPendingBackendChange()
    {
#if !defined( SW_WITH_CLIENT_CODE )
        return false; // Server 타깃 — RHI 단계가 없어 바꿀 백엔드가 없다
#else
        if ( _pRHI == nullptr )
            return false;

        const RHIBackend requested = _pRHI->consumePendingBackendChange();
        const RHIBackend previous  = _pRHI->getCommittedBackend();
        if ( requested == previous )
            return true;

        SW_LOG_INFO( "Soft-recreating RHI: %# → %#",
                     RHI::getBackendTypeName( previous ),
                     RHI::getBackendTypeName( requested ) );

        BLOCK( "디바이스에 매인 단계 내리기" )
        {
            // RHI 에 (간접으로라도) 의존하는 단계(씬의 디바이스 · 라이브 셰이더 · 렌더 스레드 · 렌더러)를 기동 표의 역순으로, 기동과 같은
            // 본문으로 내린다. 씬 스냅샷 빌더가 든 머티리얼 · 인스턴스도 FrameRenderer 단계의 종료가 옛 디바이스가 살아 있을 때 놓는다.
            _startup.getCollection().shutdownDependentsOf( *_pRHISubsystem );

            // 옛 디바이스의 GPU 자원은 recreateDevice 안의 shutdown 이 레지스트리에 통보하며 거둔다.
            _pRHI->getDevice().waitIdle();
            engine::getSubsystem<ShaderCache>().clearCache();
        }

        if ( _pRHI->recreateDevice( requested ) == false )
        {
            SW_LOG_ERROR( "recreateDevice failed; restoring previous backend %#",
                          RHI::getBackendTypeName( previous ) );
            gv_rhiBackend = previous;
            if ( _pRHI->recreateDevice( previous ) == false )
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
        SW_LOG_INFO( "Active backend is now %# (merge batches across materials %#, native bindless sampling %#)", RHI::getBackendTypeName( _pRHI->getCommittedBackend() ),
                     _pPacketBuilder->isMergeBatchesAcrossMaterials() ? 1 : 0,
                     _pRHI->getDevice().supportsNativeBindlessSampling() ? 1 : 0 );
        return true;
#endif
    }

    bool Engine::rebindSceneAfterDeviceRecreate()
    {
        if ( _pRHI == nullptr || _pRHI->hasDevice() == false )
            return false;

        // 새 디바이스가 섰다. 레지스트리 전체에 "다시 올려라" 를 알린다. 어떤 캐시를 빠뜨렸는지
        // 기억할 필요가 없는 것이 이 구조의 요점이다(언리얼 FRenderResource::InitRHI 와 같은 자리).
        RHIRenderResource::initAllFor( &_pRHI->getDevice() );

        // 내렸던 단계를 기동과 같은 본문으로 다시 세운다: 렌더러(업로드 큐 · 배치 합치기 설정을 새 디바이스로) → 렌더 스레드 → 라이브 셰이더 →
        // 씬의 디바이스. 본문은 이미 있는 객체를 다시 쓰므로 렌더 스레드에 걸린 훅과 그것을 가리키는 포인터가 그대로다.
        const bool bRestarted = _startup.getCollection().restartStopped();
        if ( bRestarted == false )
            SW_LOG_ERROR( "Backend change: %#", _startup.getCollection().getError().c_str() );

        Scene* pScene = _pSceneManager != nullptr ? _pSceneManager->getActiveScene() : nullptr;
        if ( pScene != nullptr )
            pScene->ensureDefaultCameras();
        return bRestarted;
    }

    ShaderRecompiler* Engine::getShaderRecompiler() const
    {
#if defined( SW_SHIPPING )
        return nullptr;
#else
        return _pShaderRecompiler;
#endif
    }

    void Engine::setPresentHook( PresentHookDelegate presentHook )
    {
#if defined( SW_WITH_CLIENT_CODE )
        if ( _pRenderThread != nullptr )
            _pRenderThread->setPresentHook( std::move( presentHook ) );
#else
        (void)presentHook; // Server 타깃 — 표시할 화면이 없다
#endif
    }

    void Engine::setPostPresentHook( PresentHookDelegate postPresentHook )
    {
#if defined( SW_WITH_CLIENT_CODE )
        if ( _pRenderThread != nullptr )
            _pRenderThread->setPostPresentHook( std::move( postPresentHook ) );
#else
        (void)postPresentHook; // Server 타깃 — 표시할 화면이 없다
#endif
    }

    void Engine::updateShellActions( float32 deltaTime )
    {
        if ( _pInputManager == nullptr )
            return;
        _shellActions.update( *_pInputManager, deltaTime );
    }

    unique_ptr<InputMap> Engine::createShellInputMap( string_view inputMapPath )
    {
        return ShellInputActions::createInputMap( inputMapPath );
    }

    void Engine::pollShaderReloadHotkey()
    {
#if defined( SW_DEBUG )
        // 셰이더 리로드는 **Engine 자신의** 개발 도구다(ShaderRecompiler 를 여기서 소유한다). 그래서
        // 바깥에 콜백을 달라고 하지 않고 여기서 끝낸다. 셸 맵(`ShellInputActions`)은 입력 사실만 내주므로 단축키의 뜻은 소유자인 여기서 읽는다.
        // 모듈을 다시 올리는 일은 App 의 것이라 App 이 묻는다.
        if ( _pRHI == nullptr )
            return;
        if ( _shellActions.wasActionTriggered( InputMapDefaults::kReloadShadersAction ) == false )
            return;
        if ( ShaderRecompiler* pShaderRecompiler = getShaderRecompiler() )
        {
            pShaderRecompiler->triggerReloadAll();
            SW_LOG_INFO( "%#: force shader reload", InputMapDefaults::kReloadShadersAction );
        }
#endif
    }

    bool Engine::wasDebugActionTriggered( string_view actionName ) const
    {
        return _shellActions.wasActionTriggered( hashed_string( actionName ) );
    }
} // namespace sw
