#include "pch.h"

#include "Engine/EngineLoop.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Common/BuildInfo.h"
#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Concurrency/ThreadName.h"
#include "Core/Container/StringUtil.h"
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/Event/EventDispatcher.h"
#include "Core/File/AsyncFileIO.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MatrixMath.h"
#include "Core/String/hashed_string.h"
#include "Core/String/string_splitter.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Animation/Facial/LipSync.h"
#include "Engine/Animation/Retarget/PoseRetargeter.h"
#include "Engine/Audio/IAudioService.h"
#include "Engine/Audio/LipSyncImport.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Compression/EngineCompressionCodecUtil.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Config/GameSelection.h"
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
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/StringTable.h"
#include "Engine/Module/ModuleTypeRegistry.h"
#include "Engine/Object/Component/ComponentDefaults.h"
#include "Engine/Object/GameObject/CameraRegistry.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/SceneEngineServices.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Object/SceneAnimation/AnimationCrowd.h"
#include "Engine/Object/SceneAnimation/VertexAnimationCooker.h"
#include "Engine/Physics/PhysicsSystem.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Profiling/ProfilerBackend.h"
#include "Engine/Profiling/ProfilerTraceExport.h"
#include "Engine/Reflection/ReflectionDocWriter.h"
#include "Engine/Renderer/Capture/PortraitBatchUtil.h"
#include "Engine/Renderer/Capture/RenderDocCapture.h"
#include "Engine/Renderer/Cook/ShaderCookDriver.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/RenderFramePacket.h"
#include "Engine/Renderer/RenderThread.h"
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
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneNavigationCooker.h"
#include "Engine/Telemetry/TelemetryService.h"
#include "Engine/Text/FontService.h"
#include "Engine/UI/Debug/DebugHUD.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UserSettings/HardwareProbe.h"
#include "Engine/UserSettings/UserSettingsManager.h"
#include "Engine/UserSettings/UserSettingsVariables.h"
#include "Engine/Utility/CommandStack.h"
#include "Engine/Utility/EngineFrameObserver.h"
#include "Engine/Utility/GameTimeScale.h"
#include "Engine/Window/IWindow.h"

#include "sw/config/ConfigConstants.h"
#include "sw/config/ShippingHostDefaults.h"

#if defined( SW_SHIPPING )
    #include "sw/config/ShippingGameDefaults.h" // 구운 게임의 프리셋 — Dev 는 디스크에서 고른 게임의 것을 읽어 활성 게임과 무관하다
#endif

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
     * @brief `-gv_dumpReflection=CameraComponent,CameraRole`: 첫 프레임에 그 타입 · enum 의 등록 내용을 로그로 남깁니다(`TypeRegistry::describeType`).
     * @details 첫 프레임이라 게임 · 에디터 모듈의 타입까지 등록된 뒤다. 한 번 찍고 비운다.
     */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_dumpReflection, "", "첫 프레임에 이 이름들(쉼표로 여럿)의 리플렉션 등록 내용을 로그로 남긴다 — 타입 · enum (비우면 사용 안 함)" );

} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "EngineLoop" );

    // ------------------------------------------------------------------------------
    // 기동 단계 본문 — 표(`EngineInitStepList.xxx`)의 단계마다 무엇을 세우고(`initialize<단계>`) · 내리고(`shutdown<단계>`) ·
    // 해제하는지(`destroy<단계>`)를 필요한 것만 적는다. 없는 함수는 아무것도 하지 않는다(`EngineInitNoStepBody`). 순서는 표의 의존이
    // 정한다: 초기화는 위상 순서, 종료와 해제는 그 역순. 종료는 초기화한 단계에만 불린다. 해제는 모든 종료 뒤에 **모든 단계**에
    // 불리므로(기동이 어디서 멈췄든) null 안전해야 한다. 아래는 읽기 좋게 초기화 순서대로 적었을 뿐이다.
    // 이 모음은 옮기는 중이다 — 새 단계는 여기에 함수를 더하지 않고 `ISubsystem` 을 상속하거나 `FunctionSubsystem` 으로 적는다(`initialize`).
    // ------------------------------------------------------------------------------

    struct EngineLoop::StartupStepBody : EngineInitNoStepBody
    {
        static EngineInitResult initializeRHI( EngineLoop& loop )
        {
            // RenderDoc 은 그래픽 API 를 만들 때 끼어든다 — 디바이스보다 먼저 찾고(`-renderdoc` 면 올리고) 백엔드 교체 뒤에도 그대로 쓴다.
            RenderDocCapture::initialize( loop._owned._pCommandLineManager->isArgumentProvided( CommandLineArgument::RENDERDOC ) );

            // 커맨드라인이 백엔드를 명시하지 않았을 때만 설정 기본값이 이긴다.
            RHIBackend commandLineBackend{};
            if ( RHIBackendUtil::findCommandLineBackend( *loop._owned._pCommandLineManager, commandLineBackend ) == false )
                gv_rhiBackend = loop._pEngineConfig->_window._defaultRHI;

            // 화면 값의 순서: 엔진 설정(EngineConfig) → 플레이어가 고른 사용자 설정(기본값이 아닌 것) → 명령줄.
            const DisplaySettingsRequest& display = engine::getUserSettingsManager().getDisplayRequest();
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
            const bool bRHIReady = loop._rhi->initialize( IWindow::getActiveWindow() );
            // 단계가 내려가면(destroy 는 모든 단계를 돈다) _rhi 가 사라진다 — 실패의 이유를 그 전에 남긴다. App 이 이것으로 종료 코드를 고른다.
            loop._rhiInitResult = loop._rhi->getInitResult();
            if ( bRHIReady == false )
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
        static void shutdownRHI( EngineLoop& loop )
        {
            // 디바이스 생성이 실패하면 RHI 객체는 있어도 **디바이스가 없다.** getDevice() 는
            // 널 참조를 역참조하므로 hasDevice() 로 먼저 막는다.
            if ( loop._rhi->hasDevice() )
                loop._rhi->getDevice().waitIdle();
            // GPU 자원을 든 객체들은 여기서 손으로 훑지 않는다. IRHIDevice::shutdown 이 내려가기 직전에
            // 레지스트리 전체에 releaseRHI 를 부른다(RHIRenderResource). 백엔드 모듈 DLL 도 여기서 내린다.
            loop._rhi->shutdown();
        }
        // 종료가 디바이스와 백엔드 모듈을 이미 내렸다. 남은 것은 디바이스가 없는 팩토리 객체다.
        static void destroyRHI( EngineLoop& loop ) { loop._rhi.reset(); }
    };

    EngineLoop::EngineLoop()
        : _bootstrap{}
        , _pConfigManager{ nullptr }
        , _rhi{ nullptr }
        , _shellActions{}
        , _audioService{ nullptr }
        , _frameRenderer{ nullptr }
        , _commandStack{ nullptr }
        , _sceneObjectsInvalidatedHandle{}
        , _listFrameObserver{}
        , _hostRole{ EngineHostRole::Client }
        , _bHeadless{ false }
        , _bHeadlessTaskFailed{ false }
        , _bQuitRequested{ false }
        , _rhiInitResult{ RHIInitResult::NotStarted }
        , _sceneDeltaSeconds{ 0.0f }
        , _frameDeltaSeconds{ 0.0f }
        , _exitCode{ 0 }
        , _profileSession{}
        , _startup{}
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
        , _pEngineConfig{ nullptr }
    {
    }

    EngineLoop::~EngineLoop() = default;

    bool EngineLoop::initialize( int32 argc, utf8* pArgv[], EngineHostRole role )
    {
        // 이름 풀 · 로거 · 크래시 핸들러 · 리소스 루트 · 진단 도구(Debug) · 명령줄 · 전역 변수 — 테스트 하네스와 같은 부트스트랩이다.
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
        ResourcePaths::setHostTarget( bDedicatedServer ? "Server" : "Client" );
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
                _audioService = bDedicatedServer ? IAudioService::createNull() : IAudioService::create();
            }
#if !defined( SW_SHIPPING )
            {
                SW_MEMORY_SCOPE( EngineMisc );
                _commandStack = make_unique<CommandStack>();
            }
#endif
            // 렌더러는 서비스 표에 실리므로(아래) 여기서 만든다. 초기화와 해제는 FrameRenderer 단계가 한다.
            {
                SW_MEMORY_SCOPE( RenderCPU );
                _frameRenderer = make_unique<FrameRenderer>();
            }

            EngineServices services{};
            _bootstrap.fillServices( services );
            // HostCreated 인 자리만 손으로 연결한다.
            services._pAudioService = _audioService.get();
            services._pCommandStack = _commandStack.get();
            // 렌더러는 씬이 아니라 **호스트**가 내준다. 에디터가 뷰 모드를 바꾸려고 찾는 창구다.
            services._pFrameRenderer = _frameRenderer.get();

            engine::bindEngineServices( services );
        }

        // 기동 단계는 서브시스템이다 — 서비스는 스스로 이름 · 의존 · 대상을 선언하고, 클래스가 없는 단계는 `EngineFunctionStep` 의 선언에 본문을 준다.
        // 추가 순서는 뜻이 없다(의존이 정한다). 서비스의 소유는 기동 순서로 넘어가고, 해제는 그 역순 자리에서 소멸이다.
        // 서비스 표는 위에서 같은 객체를 가리키도록 이미 묶였다(저장소가 만들고, 소유만 여기로 넘긴다).
        TelemetryService* pTelemetryService = nullptr;
        BLOCK( "기동 단계 구성" )
        {
            // 단계 객체 · 본문 델리게이트의 할당은 엔진 기타 줄로 센다(만드는 객체는 제 용도 태그를 다시 건다).
            SW_MEMORY_SCOPE( EngineMisc );
            {
                CompressionCodecRegistry* const pCodecRegistry = _owned._pCompressionCodecRegistry.get();
                // Core 의 CompressionStream 은 엔진 서비스 표에 닿지 못해 슬롯으로 본다(Logger::setGlobalSink 와 같은 모양). 슬롯은 레지스트리의 소멸자가 끊는다.
                // 종료는 하지 않는다 — 모듈이 등록한 코덱을 거두는 것은 등록한 모듈의 책임이다(registerCodec 주석 참고).
                auto initializeCompression = [pCodecRegistry]()
                {
                    pCodecRegistry->initialize();
                    CompressionCodecRegistry::setActive( pCodecRegistry );
                    // 외부 라이브러리 코덱은 **목록이 있는 자리**에서 붙인다(EngineCompressionCodecUtil).
                    EngineCompressionCodecUtil::registerAll( *pCodecRegistry );
                    return EngineInitResult::Succeeded;
                };
                _startup.addSubsystem( make_unique<FunctionSubsystem>( EngineFunctionStep::kCompression, SW_DELEGATE_LAMBDA( SubsystemInitializeDelegate, initializeCompression ) ) )
                    .takeOwnership( std::move( _owned._pCompressionCodecRegistry ) );
            }
            {
                // 이 프로세스에 이미 올라온 등록기만 모은다. 배포 구성은 GameFramework · 킷 · 게임이 정적 링크라 "Engine" 이 다 모으고,
                // 개발 구성의 모듈 DLL 은 `ModuleTypes` 단계에서 호스트 로더가 올리며 제 이름으로 등록한다.
                // 명령줄의 enum 이름(`-gv_rhiBackend=Vulkan`)은 enum 표가 선 지금 적용한다. 모르는 이름이면 기본값으로 돌지 않고 멈춘다.
                auto initializeReflection = []()
                {
                    engine::registerModuleTypes( "Engine" );
                    if ( engine::bindGlobalVariableEnumNames() )
                        return EngineInitResult::Succeeded;
                    engine::unbindGlobalVariableEnumNames();
                    return EngineInitResult::Failed;
                };
                // enum 이름 풀이는 타입 표를 본다 — 표가 소멸하기 전(종료)에 뗀다.
                _startup.addSubsystem( make_unique<FunctionSubsystem>( EngineFunctionStep::kReflection, SW_DELEGATE_LAMBDA( SubsystemInitializeDelegate, initializeReflection ),
                                                                       SW_DELEGATE_FUNCTION( SubsystemShutdownDelegate, &engine::unbindGlobalVariableEnumNames ) ) )
                    .takeOwnership( std::move( _owned._pTypeRegistry ) );
            }
            {
                TaskManager* const pTaskManager   = _owned._pTaskManager.get();
                auto               initializeTask = [pTaskManager]()
                { return pTaskManager->initialize() ? EngineInitResult::Succeeded : EngineInitResult::Failed; };
                _startup.addSubsystem( make_unique<FunctionSubsystem>( EngineFunctionStep::kTask, SW_DELEGATE_LAMBDA( SubsystemInitializeDelegate, initializeTask ),
                                                                       SW_DELEGATE_METHOD( SubsystemShutdownDelegate, &TaskManager::shutdown, pTaskManager ) ) )
                    .takeOwnership( std::move( _owned._pTaskManager ) );
            }
            {
                // 완료 콜백(팩 해제 · CRC · 스트리밍 완료 기록)은 태스크 워커에서 돈다 — IO 스레드는 다음 읽기를 거는 일만 한다.
                // 종료는 큐를 비우고 걸린 읽기와 완료 콜백(태스크)을 다 기다린다. Task 보다 먼저, 모듈을 내리기 전에 내려간다.
                AsyncFileIO* const pAsyncFileIO     = _owned._pAsyncFileIO.get();
                auto               initializeFileIO = [pAsyncFileIO]()
                {
                    AsyncFileIOSettings settings{};
                    settings._pTaskManager = &engine::getTaskManager();
                    return pAsyncFileIO->initialize( settings ) ? EngineInitResult::Succeeded : EngineInitResult::Failed;
                };
                _startup.addSubsystem( make_unique<FunctionSubsystem>( EngineFunctionStep::kFileIO, SW_DELEGATE_LAMBDA( SubsystemInitializeDelegate, initializeFileIO ),
                                                                       SW_DELEGATE_METHOD( SubsystemShutdownDelegate, &AsyncFileIO::shutdown, pAsyncFileIO ) ) );
            }
            {
                // 종료 쪽 자리 — 씬은 방금 사라졌고 서비스는 아직 살아 있다. 모듈 DLL 을 내리기에 알맞은 유일한 자리다.
                // Engine 은 거기서 무슨 일이 일어나는지 모른다(App 이 핫 리로드를 건다 — 훅은 기동 뒤에 걸린다).
                auto releaseModuleImages = [this]()
                {
                    if ( _onScenesReleased.isBound() )
                        _onScenesReleased();
                };
                _startup.addSubsystem( make_unique<FunctionSubsystem>( EngineFunctionStep::kModuleImages, SubsystemInitializeDelegate{},
                                                                       SW_DELEGATE_LAMBDA( SubsystemShutdownDelegate, releaseModuleImages ) ) );
            }
            {
                // 호스트가 동적으로 올리는 타입 공급자(개발 구성의 GameFramework · 킷 · 게임 DLL)를 여기서 올린다. 로더가 없으면 올릴 것이 없다
                // (배포 구성은 정적 링크라 리플렉션 단계가 이미 다 모았다). 이 뒤로 씬을 읽는다 — 헤드리스 쿠킹이 이 단계에 의존한다.
                auto loadModuleTypes = [this]()
                {
                    if ( _moduleTypeLoader.isBound() && _moduleTypeLoader() == false )
                    {
                        SW_LOG_ERROR( "The host could not load its type-providing modules - scenes are not read" );
                        return EngineInitResult::Failed;
                    }
                    engine::getTypeRegistry().markAllModuleTypesRegistered();
                    return EngineInitResult::Succeeded;
                };
                _startup.addSubsystem( make_unique<FunctionSubsystem>( EngineFunctionStep::kModuleTypes, SW_DELEGATE_LAMBDA( SubsystemInitializeDelegate, loadModuleTypes ) ) );
            }
            {
                // 렌더러 쪽 단계 가운데 종료가 가장 먼저다 — 씬에서 디바이스를 먼저 뗀다. 백엔드 교체 뒤 다시 서며 새 디바이스를 건다.
                auto attachSceneDevice = [this]()
                {
                    engine::getSceneManager().setRHIDevice( &_rhi->getDevice() );
                    return EngineInitResult::Succeeded;
                };
                auto detachSceneDevice = []()
                { engine::getSceneManager().setRHIDevice( nullptr ); };
                _startup.addSubsystem( make_unique<FunctionSubsystem>( EngineFunctionStep::kSceneRHI, SW_DELEGATE_LAMBDA( SubsystemInitializeDelegate, attachSceneDevice ),
                                                                       SW_DELEGATE_LAMBDA( SubsystemShutdownDelegate, detachSceneDevice ) ) );
            }
            {
                InputManager* const pInputManager   = _owned._pInputManager.get();
                auto                initializeInput = [pInputManager]()
                { return pInputManager->initialize() ? EngineInitResult::Succeeded : EngineInitResult::Failed; };
                _pInputManager = &_startup
                                      .addSubsystem( make_unique<FunctionSubsystem>( EngineFunctionStep::kInput, SW_DELEGATE_LAMBDA( SubsystemInitializeDelegate, initializeInput ),
                                                                                     SW_DELEGATE_METHOD( SubsystemShutdownDelegate, &InputManager::shutdown, pInputManager ) ) )
                                      .takeOwnership( std::move( _owned._pInputManager ) );
            }
            {
                // 설정은 리소스 초기화보다 먼저 읽는다(Resource 의 의존). `AssetManager::mountContent` 는 GameConfig 의 `_packRoot` 가
                // 정해져 있어야 팩을 제대로 마운트하고 게임 도메인의 `assetregistry.txt` 를 읽는다.
                // Config/ 는 프로젝트 루트에 있고 실행 파일은 build/<preset>/Bin 에서 돈다. 작업 디렉터리 기준으로만 찾으면
                // 모두 "없음" 이 되어 조용히 기본값으로 떨어지므로, Resource/ 를 찾을 때 알아낸 프로젝트 루트를 넘긴다.
                unique_ptr<ConfigManager> configManager;
                {
                    SW_MEMORY_SCOPE( EngineMisc );
                    configManager = make_unique<ConfigManager>();
                }
                _pConfigManager       = configManager.get();
                auto initializeConfig = [this]()
                {
                    _pConfigManager->setRootDirectory( ResourcePaths::getProjectFolderPath() );
                    ConfigManager::setPrimary( _pConfigManager ); // 거두는 것은 설정 표의 소멸자다
                    _pConfigManager->onConfigReloaded().add( SW_DELEGATE_METHOD( Delegate<void( const hashed_string& )>, &EngineLoop::onConfigReloaded, this ) );

                    _pEngineConfig = _pConfigManager->ensureConfig<EngineConfig>( config::kFileRuntimeEngineConfig, shipping_host::kEngineConfigJSON );
                    if ( _pEngineConfig == nullptr )
                        return EngineInitResult::Failed;
                    EngineConfig::setActive( *_pEngineConfig );

                    // 틀린 설정 파일은 nullptr 이다(키 이름은 이미 오류로 남았다) — 기본값으로 뜨면 고친 값이 무시된 것을 아무도 모른다.
                    // Dev 는 고른 게임(`-game=`)의 파일만 읽는다 — 활성 게임의 구운 사본으로 대체하면 다른 게임의 팩 루트로 뜬다.
#if defined( SW_SHIPPING )
                    const utf8* const pGameConfigJSON = shipping_host::kGameConfigJSON;
#else
                    const utf8* const pGameConfigJSON = nullptr;
#endif
                    const GameConfig* pGameConfig = _pConfigManager->ensureConfig<GameConfig>( GameSelection::getConfigPath(), pGameConfigJSON );
                    if ( pGameConfig == nullptr )
                        return EngineInitResult::Failed;
                    GameConfig::setActive( *pGameConfig );

                    // 메모리 태그 예산(데이터). 틀린 표는 오류를 남기고 예산 없이 간다 — 진단 설정 하나로 기동을 세우지 않는다.
                    (void)_memoryBudgetMonitor.loadBudgetFile( FileUtil::joinPath( ResourcePaths::getProjectFolderPath(), MemoryBudgetMonitor::kBudgetFile ) );
                    return EngineInitResult::Succeeded;
                };
                _startup.addSubsystem( make_unique<FunctionSubsystem>( EngineFunctionStep::kConfig, SW_DELEGATE_LAMBDA( SubsystemInitializeDelegate, initializeConfig ) ) )
                    .takeOwnership( std::move( configManager ) );
            }
            {
                // GameConfig 가 활성화된 뒤라야 "game" 토큰이 팩 루트로 풀린다. 그 전제는 `mountContent` 의 인자에 드러나 있다.
                // 씬 쿠킹(Headless 단계)의 입력은 소스 트리다 — 팩은 그 산출물이다.
                AssetManager* const pAssetManager      = _owned._pAssetManager.get();
                auto                initializeResource = [this, pAssetManager]()
                {
                    if ( pAssetManager->initialize() == false )
                        return EngineInitResult::Failed;
                    bool bCookScenes = false;
                    _owned._pCommandLineManager->getArgument( CommandLineArgument::COOK_SCENES, bCookScenes );
                    const ContentSource contentSource = bCookScenes ? ContentSource::SourceTree : ContentSource::Cooked;
                    pAssetManager->mountContent( _pEngineConfig->_listResourcePriority, contentSource );
                    return EngineInitResult::Succeeded;
                };
                _startup.addSubsystem( make_unique<FunctionSubsystem>( EngineFunctionStep::kResource, SW_DELEGATE_LAMBDA( SubsystemInitializeDelegate, initializeResource ) ) )
                    .takeOwnership<&AssetManager::shutdown>( std::move( _owned._pAssetManager ) );
            }
            {
                // 문화권 표 · 엔진 문자열 — 플레이어 설정(언어)이 이 뒤(`UserSettings`)에 적용되고, 게임은 자기 프로젝트를 위에 올린다.
                EngineDefaultAssets* const pDefaultAssets                = _owned._pEngineDefaultAssets.get();
                auto                       initializeEngineDefaultAssets = [this, pDefaultAssets]()
                {
                    if ( pDefaultAssets->loadFromResource( path::kEngineDefaultAssets ) == false )
                        SW_LOG_WARNING( "Engine data could not be read - using built-in defaults" );
                    LocalizationManager& localization = *_owned._pLocalizationManager;
                    if ( pDefaultAssets->_cultureTable.empty() == false )
                        (void)localization.loadCultureTable( pDefaultAssets->_cultureTable ); // 실패는 loadCultureTable 이 오류로 남긴다
                    if ( pDefaultAssets->_localizationProject.empty() == false && localization.mountProject( pDefaultAssets->_localizationProject, LocalizationScope::Engine ) == false )
                        SW_LOG_ERROR( "Engine localization project '%#' is not (fully) loaded", pDefaultAssets->_localizationProject.c_str() );
                    string commandLineLanguage;
                    if ( _owned._pCommandLineManager->getArgument( CommandLineArgument::LANGUAGE, commandLineLanguage ) && localization.hasLanguage( commandLineLanguage ) )
                        localization.setCurrentLanguage( commandLineLanguage );
                    return EngineInitResult::Succeeded;
                };
                _startup.addSubsystem( make_unique<FunctionSubsystem>( EngineFunctionStep::kEngineDefaultAssets, SW_DELEGATE_LAMBDA( SubsystemInitializeDelegate, initializeEngineDefaultAssets ) ) )
                    .takeOwnership( std::move( _owned._pEngineDefaultAssets ) );
            }
            {
                IAudioService* const pAudioService   = _audioService.get();
                auto                 initializeAudio = [pAudioService]()
                { return pAudioService->initialize() ? EngineInitResult::Succeeded : EngineInitResult::Failed; };
                _pAudioService = &_startup
                                      .addSubsystem( make_unique<FunctionSubsystem>( EngineFunctionStep::kAudio, SW_DELEGATE_LAMBDA( SubsystemInitializeDelegate, initializeAudio ),
                                                                                     SW_DELEGATE_METHOD( SubsystemShutdownDelegate, &IAudioService::shutdown, pAudioService ) ) )
                                      .takeOwnership( std::move( _audioService ) );
            }
            {
                // 씬 매니저는 표를 묶기 전에 만들어졌다 — 묶은 표에서 씬 쪽 묶음을 골라 여기서 넘긴다. 씬과 오브젝트 매니저는 이것만 쓴다.
                // 되돌리기 기록은 나간 씬의 오브젝트를 가리킨다 — 씬이 바뀌면 소유자(여기)가 비운다. 씬 매니저는 기록을 모른다.
                // 구독은 핸들로 들고 종료가 뗀다(다시 초기화해도 겹치지 않는다). 구독 대상은 Engine 코드라 핫 리로드와 무관하다.
                SceneManager* const pSceneManager   = _owned._pSceneManager.get();
                CommandStack* const pCommandStack   = _commandStack.get();
                auto                initializeScene = [this, pSceneManager, pCommandStack]()
                {
                    if ( pSceneManager->initialize( SceneEngineServices::makeFromBoundEngine() ) == false )
                        return EngineInitResult::Failed;
                    SceneObjectsInvalidatedDelegate& onInvalidated = pSceneManager->onSceneObjectsInvalidated();
                    if ( _sceneObjectsInvalidatedHandle.isValid() )
                        onInvalidated.remove( _sceneObjectsInvalidatedHandle );
                    _sceneObjectsInvalidatedHandle = DelegateHandle{};
                    if ( pCommandStack != nullptr )
                        _sceneObjectsInvalidatedHandle = onInvalidated.add( SW_DELEGATE_METHOD( Delegate<void()>, &CommandStack::clear, pCommandStack ) );
                    return EngineInitResult::Succeeded;
                };
                // 종료도 알린다(기록을 비운다) — 그 뒤에 구독을 뗀다.
                auto shutdownScene = [this, pSceneManager]()
                {
                    pSceneManager->shutdown();
                    if ( _sceneObjectsInvalidatedHandle.isValid() )
                        pSceneManager->onSceneObjectsInvalidated().remove( _sceneObjectsInvalidatedHandle );
                    _sceneObjectsInvalidatedHandle = DelegateHandle{};
                };
                FunctionSubsystem& scene = _startup.addSubsystem( make_unique<FunctionSubsystem>(
                    EngineFunctionStep::kScene, SW_DELEGATE_LAMBDA( SubsystemInitializeDelegate, initializeScene ), SW_DELEGATE_LAMBDA( SubsystemShutdownDelegate, shutdownScene ) ) );
                // 넘긴 역순으로 소멸한다: 씬 매니저 → 에셋 스트리밍 큐(소멸자가 태스크를 기다린다 — Task 보다 먼저) → 에디터 되돌리기 기록(씬 객체를 가리킨다).
                if ( _commandStack != nullptr )
                    (void)scene.takeOwnership( std::move( _commandStack ) );
                (void)scene.takeOwnership( std::move( _owned._pAssetStreamingQueue ) );
                _pSceneManager = &scene.takeOwnership( std::move( _owned._pSceneManager ) );
            }
            // 헤드리스 작업(셰이더 · 씬 쿠킹, 임포트)이면 그에 기대는 단계(창 · RHI · 렌더러)를 건너뛴다 — 결과는 `isHeadless` · `didHeadlessTaskFail`.
            _startup.addSubsystem( make_unique<FunctionSubsystem>( EngineFunctionStep::kHeadless, SW_DELEGATE_METHOD( SubsystemInitializeDelegate, &EngineLoop::runHeadlessTask, this ) ) );
            {
                UserSettingsManager* const pUserSettings = _owned._pUserSettingsManager.get();
                _startup
                    .addSubsystem( make_unique<FunctionSubsystem>( EngineFunctionStep::kUserSettings, SW_DELEGATE_METHOD( SubsystemInitializeDelegate, &EngineLoop::loadUserSettings, this ),
                                                                   SW_DELEGATE_METHOD( SubsystemShutdownDelegate, &UserSettingsManager::shutdown, pUserSettings ) ) )
                    .takeOwnership( std::move( _owned._pUserSettingsManager ) );
            }
            {
                // 씬 스냅샷 · 패킷 · 업로드 큐는 그리는 쪽의 것이다. 렌더러를 세우지 않는 헤드리스 작업에는 없다.
                // 백엔드 교체로 다시 설 때(`restartStoppedSteps`)는 있는 것을 그대로 쓰고, 디바이스에 매인 설정만 새로 건다.
                // GT 쪽 GPUScene 도 스냅샷의 소유(머티리얼 · 인스턴스)를 들고 있다 — 렌더러와 같은 시점에, **디바이스가 살아 있을 때**(종료) 놓는다.
                unique_ptr<RenderPacketBuilder> packetBuilder;
                {
                    SW_MEMORY_SCOPE( RenderCPU );
                    packetBuilder = make_unique<RenderPacketBuilder>();
                }
                _pPacketBuilder              = packetBuilder.get();
                _pFrameRenderer              = _frameRenderer.get();
                auto initializeFrameRenderer = [this]()
                {
                    _pPacketBuilder->initialize( _rhi->getDevice(), &engine::getTaskManager() );
                    if ( _pFrameRenderer->initialize( &_rhi->getDevice(), &engine::getTaskManager() ) == false )
                    {
                        SW_LOG_ERROR( "Failed to initialize FrameRenderer!" );
                        return EngineInitResult::Failed;
                    }
                    return EngineInitResult::Succeeded;
                };
                auto shutdownFrameRenderer = [this]()
                {
                    _pPacketBuilder->shutdown();
                    _pFrameRenderer->shutdown();
                };
                FunctionSubsystem& frameRenderer =
                    _startup.addSubsystem( make_unique<FunctionSubsystem>( EngineFunctionStep::kFrameRenderer, SW_DELEGATE_LAMBDA( SubsystemInitializeDelegate, initializeFrameRenderer ),
                                                                           SW_DELEGATE_LAMBDA( SubsystemShutdownDelegate, shutdownFrameRenderer ) ) );
                // 넘긴 역순으로 소멸한다: 패킷 빌더(해제 `destroy` 뒤) → 렌더러.
                (void)frameRenderer.takeOwnership( std::move( _frameRenderer ) );
                (void)frameRenderer.takeOwnership<&RenderPacketBuilder::destroy>( std::move( packetBuilder ) );
            }
            {
                // 다시 설 때는 같은 객체에 붙인다 — 호스트가 건 프레젠트 훅과 렌더 스레드 포인터(ModuleHost)가 그대로 남는다. 클라이언트만 만든다(전용 서버는 이 단계를 돌리지 않는다).
                RenderThread*            pRenderThread = nullptr;
                unique_ptr<RenderThread> renderThread;
                if ( bDedicatedServer == false )
                {
                    SW_MEMORY_SCOPE( RenderCPU );
                    renderThread  = make_unique<RenderThread>();
                    pRenderThread = renderThread.get();
                }
                auto attachRenderThread = [this, pRenderThread]()
                {
                    _pRenderThread = pRenderThread;
                    if ( pRenderThread->attach( &_rhi->getDevice(), _pFrameRenderer ) == false )
                    {
                        SW_LOG_ERROR( "Failed to attach RenderThread!" );
                        return EngineInitResult::Failed;
                    }
                    return EngineInitResult::Succeeded;
                };
                auto stopRenderThread = [pRenderThread]()
                {
                    pRenderThread->waitIdle();
                    pRenderThread->stop();
                };
                FunctionSubsystem& renderThreadStep =
                    _startup.addSubsystem( make_unique<FunctionSubsystem>( EngineFunctionStep::kRenderThread, SW_DELEGATE_LAMBDA( SubsystemInitializeDelegate, attachRenderThread ),
                                                                           SW_DELEGATE_LAMBDA( SubsystemShutdownDelegate, stopRenderThread ) ) );
                if ( renderThread != nullptr )
                    (void)renderThreadStep.takeOwnership( std::move( renderThread ) );
            }
            {
                // 셰이더 라이브 리로드는 개발 도구다. Debug 클라이언트에서만 만든다(Shipping 에는 코드 자체가 없다).
                // onShaderRecompiled 는 셰이더 바인딩 레이아웃 캐시 항목을 **파괴**하는데, FrameRenderer::_mapPSOLayout 과 패스 컨텍스트의
                // 1-entry 캐시가 그 실체를 가리키는 생포인터를 들고 있다. 이 콜백은 게임 스레드(tick 의 핫 리로드 블록)에서 불리고
                // 렌더 스레드는 직전 패킷을 그리는 중이라, 렌더 스레드를 세운 뒤에 반영한다. 재컴파일은 개발 중 가끔 일어나는 일이라 이때의 스톨은 문제가 되지 않는다.
#if defined( SW_DEBUG )
                unique_ptr<ShaderRecompiler> shaderRecompiler;
                if ( bDedicatedServer == false )
                {
                    SW_MEMORY_SCOPE( Shader );
                    shaderRecompiler = make_unique<ShaderRecompiler>();
                }
                ShaderRecompiler* const pShaderRecompiler    = shaderRecompiler.get();
                auto                    initializeLiveShader = [this, pShaderRecompiler]()
                {
                    _pShaderRecompiler = nullptr;
                    if ( pShaderRecompiler->initialize( "Shaders" ) == false )
                    {
                        SW_LOG_ERROR( "Failed to initialize ShaderRecompiler!" );
                        return EngineInitResult::Succeeded; // 개발 도구 하나로 기동을 세우지 않는다
                    }
                    _pShaderRecompiler = pShaderRecompiler;
                    auto onRecompiled  = [this]( string_view shaderPath, const ShaderCompileResult& result )
                    {
                        if ( _pRenderThread != nullptr )
                            _pRenderThread->waitIdle();
                        _pFrameRenderer->onShaderRecompiled( shaderPath, result );
                    };
                    pShaderRecompiler->setOnAnyShaderRecompiled( SW_DELEGATE_LAMBDA( ShaderRecompiledDelegate, onRecompiled ) );
                    return EngineInitResult::Succeeded;
                };
                auto shutdownLiveShader = [pShaderRecompiler]()
                { pShaderRecompiler->shutdown(); };
                FunctionSubsystem& liveShader =
                    _startup.addSubsystem( make_unique<FunctionSubsystem>( EngineFunctionStep::kLiveShader, SW_DELEGATE_LAMBDA( SubsystemInitializeDelegate, initializeLiveShader ),
                                                                           SW_DELEGATE_LAMBDA( SubsystemShutdownDelegate, shutdownLiveShader ) ) );
                if ( shaderRecompiler != nullptr )
                    (void)liveShader.takeOwnership( std::move( shaderRecompiler ) );
#else
                // Debug 가 아니면 라이브 리로드가 없다 — 자리만 둔다(순서 · 백엔드 교체의 재시작 목록이 구성마다 같다).
                _startup.addSubsystem( make_unique<FunctionSubsystem>( EngineFunctionStep::kLiveShader ) );
#endif
            }
            _startup.addSubsystem( std::move( _owned._pShaderCache ) );
            _startup.addSubsystem( std::move( _owned._pPhysicsSystem ) );
            _pFontService     = &_startup.addSubsystem( std::move( _owned._pFontService ) );
            pTelemetryService = &_startup.addSubsystem( std::move( _owned._pTelemetryService ) );
            _pUISystem        = &_startup.addSubsystem( std::move( _owned._pUISystem ) );
        }
        TelemetryService& telemetryService = *pTelemetryService;

        // 외부 프로파일러(`-gv_tracy`)는 서비스 표가 선 직후에 켠다 — 기동 단계부터 타임라인에 남고, 켤 때 엔진 프로파일러도 같이 켠다.
        ThreadName::setCurrentThreadName( "GameThread" );
        ProfilerBackend::initialize();

        // 초기화(`initialize()`)의 순서는 손으로 적지 않는다. 단계와 서브시스템마다 먼저 서야 하는 것을 적고, 여기서는 위상 순서로
        // 표의 본문 모음(`StartupStepBody::initialize<단계>`)과 서브시스템(`initializeSubsystem`)을 부른다. 종료와 해제는 그 역순이다(`shutdown`).
        const bool bStarted = _startup.initializeAll<StartupStepBody>( *this, bDedicatedServer ? EngineInitTarget::Server : EngineInitTarget::Client );
        if ( bStarted == false )
            return false;
        // 장면별 프레임 시간 · flush 는 프레임 관찰자로 돈다(씬 틱 뒤 — `TelemetryService::onAfterSceneTick`). 텔레메트리가 섰을 때만 붙인다.
        // 떼는 것은 `shutdown` 이 단계 종료 전에 관찰자 목록을 비우는 것으로 끝난다.
        if ( telemetryService.isInitialized() )
            addFrameObserver( &telemetryService );
        if ( bDedicatedServer )
        {
            // 실제 바이너리에서 단계가 빠졌는지 테스트(ServerBootTest)가 이 줄로 본다. 초기화 순서대로 적는다.
            const SubsystemCollection& subsystems = _startup.getSubsystems();
            string                     skipped;
            for ( const uint32 index : subsystems.getInitializeOrder() )
            {
                const ISubsystem& subsystem = subsystems.getSubsystem( index );
                if ( ( static_cast<uint8>( subsystem.getSubsystemTarget() ) & static_cast<uint8>( EngineInitTarget::Server ) ) != 0 )
                    continue;
                if ( skipped.empty() == false )
                    skipped += " ";
                skipped += subsystem.getSubsystemName();
            }
            SW_LOG_INFO( "Dedicated server: startup steps not run - %#", skipped.c_str() );
        }
        // 헤드리스 작업(셰이더 · 씬 쿠킹, 텍스처 임포트)은 RHI 이후 단계를 건너뛰고 여기서 끝난다. 누수 기준선은 그 작업 직전에 잡았다.
        if ( _bHeadless )
            return true;

        _profileSession.begin();
#if SW_PROFILER_TRACE_ENABLED
        ProfilerTraceExport::startExitCapture( engine::getFrameProfiler() ); // `-gv_traceSaveOnExit` — 없으면 아무것도 하지 않는다
#endif

        MemoryProfiler::captureMemoryLeakBaseline();

        return true;
    }

    void EngineLoop::shutdown()
    {
#if SW_PROFILER_TRACE_ENABLED
        // 종료 트레이스(`-gv_traceSaveOnExit`)는 서비스 · 프로파일러가 아직 있을 때 쓴다. 기동이 서비스 표 전에 멈췄으면 쓸 것이 없다.
        if ( engine::areEngineServicesBound() && _bHeadless == false )
            ProfilerTraceExport::finishExitCapture( engine::getFrameProfiler(), _hostRole == EngineHostRole::DedicatedServer ? "Server" : "App" );
#endif
        // 프레임 관찰자는 붙인 쪽이 서비스보다 먼저 내린다(로그 리스너 · 입력을 빌려 쓴다). 남은 것은 더 부르지 않는다.
        // 목록의 용량은 기동 뒤에 잡았으므로 누수 보고 전에 해제한다.
        vector<IEngineFrameObserver*>{}.swap( _listFrameObserver );
        AssetLoadProfiler::get().reportIfRequested( "session" );
        // 초기화한 단계만 역순으로 내린다: UI → 텔레메트리 → 씬의 디바이스 → 렌더 스레드 → 렌더러 → RHI → 씬 → 입력 · 오디오 → 모듈 이미지 → 태스크 → 셰이더 캐시.
        _startup.shutdownAll();
        // 셸 디버그 액션 맵은 입력 매니저를 가리킨다 — 매니저가 소멸하기(Input 의 해제 자리) 전에 놓는다(`updateShellActions` 가 처음 쓸 때 만든다).
        _shellActions.reset();
        // 단계가 소유한 객체를 초기화 순서의 역순으로 해제한다(표의 줄은 `StartupStepBody::destroy<단계>`, 서브시스템은 소멸): UI → 텔레메트리 → 렌더러 쪽 → RHI →
        // 글꼴 → 씬 → 입력 · 오디오 → 태스크 → 셰이더 캐시 → 엔진 데이터 → 리소스 → 설정 → 리플렉션 → 압축. 기동이 어디서 멈췄든 모든 단계를 해제한다.
        _startup.destroyAll();
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
        _pEngineConfig  = nullptr; // 설정 표가 든 객체를 가리켰다
        // 할당 관찰을 떼고 출력을 비운다. 출력 객체(Tracy)는 프로세스 수명이라 남아 있는 구간을 닫을 수 있다.
        ProfilerBackend::shutdown();
        // 표 밖 부트스트랩을 세운 역순으로 내린다(테스트 하네스와 같은 끝 정리).
        _bootstrap.shutdown();

        MemoryProfiler::reportMemoryLeaks( "EngineLoop::shutdown" );
    }

    void EngineLoop::onConfigReloaded( const hashed_string& configTypeName )
    {
        if ( configTypeName == GameConfig::StaticType()->_fullyQualifiedName )
        {
            const GameConfig* pGameConfig = _pConfigManager != nullptr ? _pConfigManager->getConfig<GameConfig>() : nullptr;
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

    EngineInitResult EngineLoop::runHeadlessTask()
    {
        // 전용 서버가 하는 헤드리스 작업은 씬 쿠킹(서버 팩)까지다. 셰이더 쿠킹(DXC) · 원본 임포트(에디터 모듈)는 App 의 일이다 — 조용히 넘기지 않고 실패한다.
        if ( _hostRole == EngineHostRole::DedicatedServer )
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
                if ( _owned._pCommandLineManager->getArgument( argument, bGiven ) && bGiven )
                {
                    SW_LOG_ERROR( "This headless task runs in the client build (App), not in the dedicated server" );
                    _bHeadless           = true;
                    _bHeadlessTaskFailed = true;
                    return EngineInitResult::SkipDependents;
                }
            }
        }

        // 헤드리스 작업은 보통 실행이 누수 기준선을 잡는 자리(`initialize` 끝)에 닿지 않는다. 기준선이 없으면 종료 때 살아 있는 블록을 모두
        // 누수로 찍으므로, 작업 직전에 같은 기준선을 잡는다 — 작업이 만든 것을 종료까지 놓지 않을 때만 누수로 보인다.
        bool bCookShaders = false;
        if ( _owned._pCommandLineManager->getArgument( CommandLineArgument::COOK_SHADERS, bCookShaders ) && bCookShaders )
        {
            MemoryProfiler::captureMemoryLeakBaseline();
            _bHeadless = true;
            SW_LOG_INFO( "Starting Headless (CookShaders)..." );
            const ShaderCookSummary summary = ShaderCookDriver::cookAllShaders();
            _bHeadlessTaskFailed            = summary.isClean() == false;
            return EngineInitResult::SkipDependents;
        }

        // 리플렉션 문서도 같은 자리다 — 모든 타입 공급자가 등록을 끝낸 뒤(ModuleTypes)라 게임 · 킷의 타입까지 든다.
        string reflectionDocsDir;
        if ( _owned._pCommandLineManager->getArgument( CommandLineArgument::WRITE_REFLECTION_DOCS, reflectionDocsDir ) && reflectionDocsDir.empty() == false )
        {
            _bHeadless           = true;
            _bHeadlessTaskFailed = ReflectionDocWriter::writeMarkdown( engine::getTypeRegistry(), reflectionDocsDir ) == 0;
            return EngineInitResult::SkipDependents;
        }

        // 리타깃 굽기(`--bake-retarget=<프로필>,<원본 클립>,<출력 클립>`) — 클립 · 스켈레톤 · 코덱이 모두 엔진 것이라 여기서 한다.
        string bakeRetarget;
        if ( _owned._pCommandLineManager->getArgument( CommandLineArgument::BAKE_RETARGET, bakeRetarget ) && bakeRetarget.empty() == false )
        {
            MemoryProfiler::captureMemoryLeakBaseline();
            _bHeadless = true;
            const string_splitter      parts( bakeRetarget, { "," } );
            const vector<string_view>& listArgument = parts.getSplitList();
            const bool                 bWellFormed  = listArgument.size() == 3;
            if ( bWellFormed == false )
                SW_LOG_ERROR( "--bake-retarget expects <profile>,<source clip>,<output clip>" );
            _bHeadlessTaskFailed = bWellFormed == false ||
                                   RetargetBakeUtil::bakeClipFile( StringUtil::trim( listArgument[0] ), StringUtil::trim( listArgument[1] ), StringUtil::trim( listArgument[2] ) ) == false;
            return EngineInitResult::SkipDependents;
        }

        // 씬 쿠킹도 같은 자리다. 엔티티 상태를 바이너리로 구우려면 리플렉션이 필요하고,
        // 그것은 엔진 안에만 있어서 `CookAssets.py` 가 이쪽으로 넘겨준다.
        bool bCookScenes = false;
        if ( _owned._pCommandLineManager->getArgument( CommandLineArgument::COOK_SCENES, bCookScenes ) && bCookScenes )
        {
            MemoryProfiler::captureMemoryLeakBaseline();
            _bHeadless = true;
            string cookedDir;
            _owned._pCommandLineManager->getArgument( CommandLineArgument::COOKED_DIR, cookedDir );
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
            _bHeadlessTaskFailed = sceneCount == 0 || sceneFailedCount > 0 || prefabFailedCount > 0 || registryFailedCount > 0 ||
                                   vertexAnimationFailedCount > 0 || navMeshFailedCount > 0;
            return EngineInitResult::SkipDependents;
        }

        // 립싱크 분석 — 음성마다 비즘 트랙을 곁에 쓴다. 분석 표가 엔진 데이터라 엔진이 한다.
        bool bImportLipSync = false;
        if ( _owned._pCommandLineManager->getArgument( CommandLineArgument::IMPORT_LIPSYNC, bImportLipSync ) && bImportLipSync )
        {
            MemoryProfiler::captureMemoryLeakBaseline();
            _bHeadless = true;
            SW_LOG_INFO( "Starting Headless (ImportLipSync)..." );
            LipSyncSettings         lipSyncSettings{};
            uint32                  lipSyncFailedCount = 0;
            [[maybe_unused]] uint32 lipSyncCount       = 0; // 로그로만 쓴다(Shipping 은 빠진다)
            if ( lipSyncSettings.loadFromResource( LipSyncSettings::kResourcePath ) )
                lipSyncCount = LipSyncImport::importAll( ResourcePaths::getRootFolderPath(), lipSyncSettings, lipSyncFailedCount );
            else
                ++lipSyncFailedCount;
            SW_LOG_INFO( "Imported %# viseme tracks (%# failures).", lipSyncCount, lipSyncFailedCount );
            _bHeadlessTaskFailed = lipSyncFailedCount > 0;
            return EngineInitResult::SkipDependents;
        }

        // 로컬라이제이션 도구(글 수집 · PO 교환)와 원본 임포트 · 대조(텍스처 · 모델 · 높이장)는 에디터 모듈이 한다(엔진은 에디터를 모른다).
        // 여기서는 창 · RHI 없이 세우기만 하고, 모듈을 올려 부르는 것은 App 이다(`EditorModuleHost::runLocalizationWithEditorModule` ·
        // `importAssetsWithEditorModule`). 이 단계는 타입 공급자 모듈 뒤라 글 수집이 리플렉션 프로퍼티(`Meta = "Localizable"`)를 본다.
        bool   bGatherText = false;
        bool   bCheckText  = false;
        bool   bExportPo   = false;
        string importPoPath;
        _owned._pCommandLineManager->getArgument( CommandLineArgument::GATHER_TEXT, bGatherText );
        _owned._pCommandLineManager->getArgument( CommandLineArgument::CHECK_TEXT, bCheckText );
        _owned._pCommandLineManager->getArgument( CommandLineArgument::EXPORT_PO, bExportPo );
        _owned._pCommandLineManager->getArgument( CommandLineArgument::IMPORT_PO, importPoPath );
        const bool bAnyLocalization = bGatherText || bCheckText || bExportPo || importPoPath.empty() == false;

        bool bImportTextures     = false;
        bool bCheckTextures      = false;
        bool bImportModels       = false;
        bool bCheckModels        = false;
        bool bImportHeightfields = false;
        bool bCheckHeightfields  = false;
        _owned._pCommandLineManager->getArgument( CommandLineArgument::IMPORT_TEXTURES, bImportTextures );
        _owned._pCommandLineManager->getArgument( CommandLineArgument::CHECK_TEXTURES, bCheckTextures );
        _owned._pCommandLineManager->getArgument( CommandLineArgument::IMPORT_MODELS, bImportModels );
        _owned._pCommandLineManager->getArgument( CommandLineArgument::CHECK_MODELS, bCheckModels );
        _owned._pCommandLineManager->getArgument( CommandLineArgument::IMPORT_HEIGHTFIELDS, bImportHeightfields );
        _owned._pCommandLineManager->getArgument( CommandLineArgument::CHECK_HEIGHTFIELDS, bCheckHeightfields );
        const bool bAnyImport = bImportTextures || bCheckTextures || bImportModels || bCheckModels || bImportHeightfields || bCheckHeightfields;
        if ( bAnyLocalization || bAnyImport )
        {
            MemoryProfiler::captureMemoryLeakBaseline();
            _bHeadless = true;
            SW_LOG_INFO( "Starting Headless (%#)...", bAnyLocalization ? "localization tools" : "asset import/check" );
            return EngineInitResult::SkipDependents;
        }
        return EngineInitResult::Succeeded;
    }

    EngineInitResult EngineLoop::loadUserSettings()
    {
        UserSettingsManager& settings = engine::getUserSettingsManager();
        UserSettingsTargets  targets;
        targets._pGlobalVariableManager = _owned._pGlobalVariableManager.get();
        targets._pCommandLineManager    = _owned._pCommandLineManager.get();
        targets._pInputMap              = &engine::getInputManager().getInputMap();
        targets._pAudioService          = _pAudioService;
        targets._pLocalizationManager   = _owned._pLocalizationManager.get();
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
#if SW_DEV_COMMANDS_ENABLED
        // 디버그 HUD 설정(개발 도구) — Dev 만 덧붙인다. 사용자 파일보다 먼저 읽어야 저장된 HUD 값이 버려지지 않는다.
        if ( settings.loadSchema( DebugHUD::kSettingsSchemaPath ) == false )
            SW_LOG_ERROR( "Debug HUD user settings schema '%#' is not loaded", DebugHUD::kSettingsSchemaPath );
#endif
        for ( const auto& [settingID, value] : gameConfig._mapUserSettingDefault )
        {
            (void)settings.setGameDefault( hashed_string( settingID ), value );
        }

        // 사용자 폴더의 파일이다(세이브 게임과 별개). 자동화는 `-gv_userSettingsFile` 로 사용자 폴더를 건드리지 않는다.
        const string gameName = FileUtil::getFileNamePart( FileUtil::trimTrailingSlashes( gameConfig._packRoot ) );
        settings.setUserFilePath( gv_userSettingsFile.empty() ? UserSettingsManager::makeDefaultUserFilePath( gameName ) : gv_userSettingsFile );
        // 셸 키(콘솔 여는 키 …)를 플레이어가 바꾼 것도 같은 사용자 폴더에 둔다. 셸 맵은 처음 돌 때 만들므로 그 전에 경로를 알려 준다.
        _shellActions.setUserBindingsPath( ShellInputActions::makeUserBindingsPath( settings.getUserFilePath() ) );
        if ( settings.loadUserFile( settings.getUserFilePath() ) == false )
            (void)settings.applyAutoDetectedPreset( HardwareProbe::probe() ); // 맞는 프리셋이 없으면 기본값 그대로다
        settings.reapplyAll();
        return EngineInitResult::Succeeded;
    }

    bool EngineLoop::renderPortraits( string_view prefabList, string_view outputDirectory, uint32 size )
    {
        if ( _rhi == nullptr || _rhi->hasDevice() == false )
            return false;
        return PortraitBatchUtil::renderPrefabList( _rhi->getDevice(), _pRenderThread, prefabList, outputDirectory, size );
    }

    void EngineLoop::requestQuit( int32 exitCode )
    {
        if ( _bQuitRequested )
            return;
        _bQuitRequested = true;
        _exitCode       = exitCode;
        SW_LOG_INFO( "Quit requested (exit code %#)", exitCode );
    }

    void EngineLoop::addFrameObserver( IEngineFrameObserver* pObserver )
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

    void EngineLoop::removeFrameObserver( IEngineFrameObserver* pObserver )
    {
        for ( size_t index = 0; index < _listFrameObserver.size(); ++index )
        {
            if ( _listFrameObserver[index] != pObserver )
                continue;
            _listFrameObserver.erase( _listFrameObserver.begin() + static_cast<ptrdiff_t>( index ) );
            return;
        }
    }

    EngineFrameContext EngineLoop::makeFrameContext() const
    {
        EngineFrameContext context;
        context._pInput                   = _pInputManager;
        context._completedScreenshotCount = _pRenderThread != nullptr ? _pRenderThread->getCompletedScenarioScreenshotCount() : 0u;
        context._deltaSeconds             = _frameDeltaSeconds;
        const Scene* pActiveScene         = _pSceneManager != nullptr ? _pSceneManager->getActiveScene() : nullptr;
        if ( pActiveScene != nullptr )
            context._activeSceneID = pActiveScene->getSourcePath().empty() ? pActiveScene->getName() : pActiveScene->getSourcePath();
        return context;
    }

    void EngineLoop::onWindowClosed()
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

    void EngineLoop::beginFrame( float32 deltaSeconds )
    {
        _frameDeltaSeconds = deltaSeconds;
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
            _pUISystem->processInput( GameTimeScale::getUnscaledDeltaTime( deltaSeconds ) ); // 정지 메뉴의 탐색 반복은 실제 시간

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
        _frameDeltaSeconds               = deltaTime;

        if ( _bHeadless )
            return;

        FrameProfiler& profiler = engine::getFrameProfiler();
        profiler.beginFrame();

        // 게임 스레드 전체를 한 구간으로 잡는다. 보고서가 RT 구간만 보여 주면 "프레임의 몇 % 를 측정했나" 에
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

            if ( _pSceneManager != nullptr )
                _pSceneManager->tickTransitions();
            if ( _owned._pEventDispatcher != nullptr )
                _owned._pEventDispatcher->processEvents();
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
            if ( _pSceneManager != nullptr && bTickScene )
                _pSceneManager->tick( deltaTime );
        }

        // 물리 · 내비 디버그 선 — 씬 틱이 이번 프레임의 바디 · 경로를 정한 **뒤**, 관찰자 · UI · 패킷 **앞**이다(이번 프레임에 넣은 선이 endFrame 에서 확정된다).
        if ( _pSceneManager != nullptr )
            SceneDebugDraw::update( _pSceneManager->getActiveScene(), engine::getDebugDrawQueue() );

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
            const UIViewport viewport =
                _pUISystem->computeViewport( float2{ static_cast<float32>( uiWidth ), static_cast<float32>( uiHeight ) }, contentScale );
            _pUISystem->update( GameTimeScale::getUnscaledDeltaTime( deltaTime ), viewport ); // 애니메이션 · 자막 · 알림은 정지 메뉴 아래서도 흐른다
        }

        // 이번 틱에 경로로 잡힌 머티리얼(메시의 저장된 참조)을 패킷을 내기 **전에** 올린다. 컴포넌트는 디바이스를 모른다(`MaterialCache::requestInitialize`).
        // 올리기는 bindless 표에 등록하므로 렌더 스레드가 지난 프레임을 병렬로 기록하는 동안 하면 안 된다 — 올릴 것이 있는 프레임(씬 로드 ·
        // 처음 쓰는 머티리얼의 스폰)만 렌더 스레드를 기다린다.
        if ( _rhi != nullptr && _rhi->hasDevice() )
        {
            MaterialCache& materials = engine::getAssetManager().getAssetCache<MaterialCache>();
            if ( materials.hasPendingInitialize() && _pRenderThread != nullptr )
                _pRenderThread->waitIdle();
            materials.initializePending( &_rhi->getDevice() );
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

            _pPacketBuilder->populateScene( packet, views, sceneViewCameraProvider, pActiveScene, deltaTime, *_rhi, _pFrameRenderer );
            RenderPacketBuilder::populateCanvas( packet, bSceneMain, _pUISystem, _pFontService, _rhi.get() );

            if ( _pRenderThread != nullptr )
            {
                // GT 가 여기서 기다린다면 그것은 렌더 스레드가 밀린 것이다. 링이 차면 submit 이 막는다.
                SW_PROFILE_SCOPE( "GT.Packet.submit" );
                _pRenderThread->submit( packet );
            }
        }
    }

    void EngineLoop::endFrame()
    {
        engine::getFrameProfiler().endFrame();
#if SW_PROFILER_TRACE_ENABLED
        ProfilerTraceExport::tickRecording( engine::getFrameProfiler() ); // `profile.trace record` 의 끝 시각 확인(진행 중이 아니면 atomic 하나)
#endif
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
        if ( _pInputManager != nullptr )
            _pInputManager->endFrame();
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

            // 옛 디바이스의 GPU 자원은 recreateDevice 안의 shutdown 이 레지스트리에 통보하며 거둔다.
            _rhi->getDevice().waitIdle();
            engine::getShaderCache().clearCache();
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
                     _pPacketBuilder->isMergeBatchesAcrossMaterials() ? 1 : 0,
                     _rhi->getDevice().supportsNativeBindlessSampling() ? 1 : 0 );
        return true;
    }

    bool EngineLoop::rebindSceneAfterDeviceRecreate()
    {
        if ( _rhi == nullptr || _rhi->hasDevice() == false )
            return false;

        // 새 디바이스가 섰다. 레지스트리 전체에 "다시 올려라" 를 알린다. 어떤 캐시를 빠뜨렸는지
        // 기억할 필요가 없는 것이 이 구조의 요점이다(언리얼 FRenderResource::InitRHI 와 같은 자리).
        RHIRenderResource::initAllFor( &_rhi->getDevice() );

        // 내렸던 단계를 기동과 같은 본문으로 다시 세운다: 렌더러(업로드 큐 · 배치 합치기 설정을 새 디바이스로) → 렌더 스레드 → 라이브 셰이더 →
        // 씬의 디바이스. 본문은 이미 있는 객체를 다시 쓰므로 렌더 스레드에 걸린 훅과 그것을 가리키는 포인터가 그대로다.
        const bool bRestarted = _startup.restartStoppedSteps();

        Scene* pScene = _pSceneManager != nullptr ? _pSceneManager->getActiveScene() : nullptr;
        if ( pScene != nullptr )
            pScene->ensureDefaultCameras();
        return bRestarted;
    }

    ShaderRecompiler* EngineLoop::getShaderRecompiler() const
    {
#if defined( SW_SHIPPING )
        return nullptr;
#else
        return _pShaderRecompiler;
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
        if ( _pRenderThread != nullptr )
            _pRenderThread->setPresentHook( std::move( presentHook ) );
    }

    void EngineLoop::setPostPresentHook( PresentHookDelegate postPresentHook )
    {
        if ( _pRenderThread != nullptr )
            _pRenderThread->setPostPresentHook( std::move( postPresentHook ) );
    }

    void EngineLoop::updateShellActions( float32 deltaTime )
    {
        if ( _pInputManager == nullptr )
            return;
        _shellActions.update( *_pInputManager, deltaTime );
    }

    unique_ptr<InputMap> EngineLoop::createShellInputMap( string_view inputMapPath )
    {
        return ShellInputActions::createInputMap( inputMapPath );
    }

    void EngineLoop::pollShaderReloadHotkey()
    {
#if defined( SW_DEBUG )
        // 셰이더 리로드는 **Engine 자신의** 개발 도구다(ShaderRecompiler 를 여기서 소유한다). 그래서
        // 바깥에 콜백을 달라고 하지 않고 여기서 끝낸다. 셸 맵(`ShellInputActions`)은 입력 사실만 내주므로 단축키의 뜻은 소유자인 여기서 읽는다.
        // 모듈을 다시 올리는 일은 App 의 것이라 App 이 묻는다.
        if ( _rhi == nullptr )
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

    bool EngineLoop::wasDebugActionTriggered( string_view actionName ) const
    {
        return _shellActions.wasActionTriggered( hashed_string( actionName ) );
    }
} // namespace sw
