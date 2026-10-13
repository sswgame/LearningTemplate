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
#include "Engine/Common/EngineSubsystemHost.h"
#include "Engine/Compression/EngineCompressionCodecUtil.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Config/GameSelection.h"
#include "Engine/EngineInitSequence.h"
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
#include "Engine/Localization/EngineDefaultAssetsSubsystem.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/StringTable.h"
#include "Engine/Module/ModuleImagesSubsystem.h"
#include "Engine/Module/ModuleTypeRegistry.h"
#include "Engine/Module/ModuleTypesSubsystem.h"
#include "Engine/Module/ReflectionSubsystem.h"
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

    EngineLoop::EngineLoop()
        : _bootstrap{}
        , _pConfigManager{ nullptr }
        , _shellActions{}
        , _audioService{ nullptr }
        , _frameRenderer{ nullptr }
        , _commandStack{ nullptr }
        , _listFrameObserver{}
        , _hostRole{ EngineHostRole::Client }
        , _headlessState{}
        , _bQuitRequested{ false }
        , _rhiInitResult{ RHIInitResult::NotStarted }
        , _sceneDeltaSeconds{ 0.0f }
        , _frameDeltaSeconds{ 0.0f }
        , _exitCode{ 0 }
        , _profileSession{}
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
            _hostRole                 = role;
            _headlessState._bHeadless = true;
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
                // 외부 라이브러리 코덱은 **목록이 있는 자리**에서 붙인다(EngineCompressionCodecUtil). 레지스트리는 만들 때 내장 코덱을 갖추므로 기동 전에 붙여도 된다.
                EngineCompressionCodecUtil::registerAll( *_owned._pCompressionCodecRegistry );
                _startup.add( std::move( _owned._pCompressionCodecRegistry ) );
                _startup.add( sw::make_unique<ReflectionSubsystem>( std::move( _owned._pTypeRegistry ) ) );
                _startup.add( std::move( _owned._pTaskManager ) );
                // 완료 콜백(팩 해제 · CRC · 스트리밍 완료 기록)은 태스크 워커에서 돈다 — IO 스레드는 다음 읽기를 거는 일만 한다.
                AsyncFileIOSettings fileIOSettings{};
                fileIOSettings._pTaskManager = &engine::getTaskManager();
                _owned._pAsyncFileIO->setSubsystemSettings( fileIOSettings );
                _startup.add( std::move( _owned._pAsyncFileIO ) );
            }
            _startup.add( sw::make_unique<ModuleImagesSubsystem>( &_onScenesReleased ) );
            _startup.add( sw::make_unique<ModuleTypesSubsystem>( &_moduleTypeLoader ) );
            {
                // 창 · 디바이스. RHI 객체는 여기서 만들어 단계가 소유하고(소멸 자리 = 이 단계의 해제), 단계가 선 뒤부터 `_pRHI` 로 빌려 쓴다.
                unique_ptr<RHI> rhi;
                {
                    SW_MEMORY_SCOPE( RenderCPU );
                    rhi = make_unique<RHI>();
                }
                RHI* const pRHI          = rhi.get();
                auto       initializeRHI = [this, pRHI]()
                {
                    // RenderDoc 은 그래픽 API 를 만들 때 끼어든다 — 디바이스보다 먼저 찾고(`-renderdoc` 면 올리고) 백엔드 교체 뒤에도 그대로 쓴다.
                    RenderDocCapture::initialize( _owned._pCommandLineManager->isArgumentProvided( CommandLineArgument::RENDERDOC ) );

                    // 커맨드라인이 백엔드를 명시하지 않았을 때만 설정 기본값이 이긴다.
                    RHIBackend          commandLineBackend{};
                    const EngineConfig& engineConfig = EngineConfig::getActive();
                    if ( RHIBackendUtil::findCommandLineBackend( *_owned._pCommandLineManager, commandLineBackend ) == false )
                        gv_rhiBackend = engineConfig._window._defaultRHI;

                    // 화면 값의 순서: 엔진 설정(EngineConfig) → 플레이어가 고른 사용자 설정(기본값이 아닌 것) → 명령줄.
                    const DisplaySettingsRequest& display = engine::getUserSettingsManager().getDisplayRequest();
                    if ( IWindow::getActiveWindow() == nullptr )
                    {
                        uint32 windowWidth  = display._bHasResolution ? display._width : engineConfig._window._width;
                        uint32 windowHeight = display._bHasResolution ? display._height : engineConfig._window._height;
                        // 인자를 주지 않으면 getArgument 가 false 를 돌려주므로 설정값이 그대로 남는다.
                        _owned._pCommandLineManager->getArgument( CommandLineArgument::WIDTH, windowWidth );
                        _owned._pCommandLineManager->getArgument( CommandLineArgument::HEIGHT, windowHeight );

                        unique_ptr<IWindow> defaultWindow = IWindow::createPlatformWindow();
                        if ( defaultWindow != nullptr && defaultWindow->initializeWindow( GameConfig::getActive()._windowTitle.c_str(), windowWidth, windowHeight ) )
                        {
                            // 전체 화면은 스왑체인을 만들기 전에 고른다 — 스왑체인이 처음부터 모니터 크기다.
                            if ( display._bHasMode && display._mode != WindowDisplayMode::Windowed )
                                (void)defaultWindow->setDisplayMode( display._mode, windowWidth, windowHeight );
                            // 소유권은 App::initialize 가 IWindow::getActiveWindow() 로 넘겨받는다(App 이 없는 임베드라면 부르는 쪽이 직접 소유한다).
                            IWindow::setActiveWindow( defaultWindow.release() );
                        }
                    }

                    pRHI->setPreferredVSync( display._bHasVSync ? display._bVSync : engineConfig._window._bVSync );
                    // RHI 는 창 시스템을 모른다. 표면(IRenderSurface)만 넘긴다. 창은 위에서 만들었거나 호스트가 들고 있다.
                    const bool bRHIReady = pRHI->initialize( IWindow::getActiveWindow() );
                    // 실패의 이유를 남긴다 — App 이 이것으로 종료 코드를 고른다.
                    _rhiInitResult = pRHI->getInitResult();
                    if ( bRHIReady == false )
                        return SubsystemInitResult::Failed;
                    _pRHI = pRHI;
                    // 백엔드가 정해졌으니 크래시 리포트에 남긴다 — "어느 백엔드에서 났는가" 가 범위를 좁히는 첫 질문이다.
                    CrashHandler::setContextValue( "RHI", pRHI->getDevice().getBackendName() );

                    // `-gv_crashTest=1`: 크래시 리포트 경로를 실제로 확인하는 유일한 방법이다(리포트는 크래시가 나야만 만들어진다).
                    if ( gv_crashTest != 0 )
                        CrashHandler::crashForTest( static_cast<CrashTestKind>( gv_crashTest ) );
                    return SubsystemInitResult::Succeeded;
                };
                auto shutdownRHI = [pRHI]()
                {
                    // 디바이스 생성이 실패하면 RHI 객체는 있어도 디바이스가 없다 — getDevice() 를 부르기 전에 hasDevice() 로 막는다.
                    if ( pRHI->hasDevice() )
                        pRHI->getDevice().waitIdle();
                    // GPU 자원을 든 객체는 손으로 훑지 않는다. IRHIDevice::shutdown 이 레지스트리 전체에 releaseRHI 를 부른다. 백엔드 모듈 DLL 도 여기서 내린다.
                    pRHI->shutdown();
                };
                FunctionSubsystem& rhiSubsystem = _startup.add( make_unique<FunctionSubsystem>( EngineFunctionStep::kRHI, SW_DELEGATE_LAMBDA( SubsystemInitializeDelegate, initializeRHI ),
                                                                                                SW_DELEGATE_LAMBDA( SubsystemShutdownDelegate, shutdownRHI ) ) );
                (void)rhiSubsystem.takeOwnership( std::move( rhi ) );
                _pRHISubsystem = &rhiSubsystem;
            }
            {
                // 렌더러 쪽 단계 가운데 종료가 가장 먼저다 — 씬에서 디바이스를 먼저 뗀다. 백엔드 교체 뒤 다시 서며 새 디바이스를 건다.
                auto attachSceneDevice = [this]()
                {
                    engine::getSceneManager().setRHIDevice( &_pRHI->getDevice() );
                    return SubsystemInitResult::Succeeded;
                };
                auto detachSceneDevice = []()
                { engine::getSceneManager().setRHIDevice( nullptr ); };
                _startup.add( make_unique<FunctionSubsystem>( EngineFunctionStep::kSceneRHI, SW_DELEGATE_LAMBDA( SubsystemInitializeDelegate, attachSceneDevice ),
                                                              SW_DELEGATE_LAMBDA( SubsystemShutdownDelegate, detachSceneDevice ) ) );
            }
            _pInputManager = &_startup.add( std::move( _owned._pInputManager ) );
            {
                // 설정 표는 Config 서브시스템이다 — 엔진 · 게임 설정을 읽고 주 표로 알린다(Resource 가 그 뒤에 서서 팩 루트를 안다). 다시 읽기 알림은 엔진 루프가 받는다.
                unique_ptr<ConfigManager> configManager;
                {
                    SW_MEMORY_SCOPE( EngineMisc );
                    configManager = make_unique<ConfigManager>();
                }
                _pConfigManager = configManager.get();
                _pConfigManager->onConfigReloaded().add( SW_DELEGATE_METHOD( Delegate<void( const hashed_string& )>, &EngineLoop::onConfigReloaded, this ) );
                _startup.add( std::move( configManager ) );
                _startup.add( std::move( _owned._pAssetManager ) );
                _startup.add( sw::make_unique<EngineDefaultAssetsSubsystem>( std::move( _owned._pEngineDefaultAssets ) ) );
            }
            _pAudioService = &_startup.add( std::move( _audioService ) );
            // 씬 매니저가 서브시스템이다 — 씬이 바뀌면 되돌리기 기록을 비우는 구독도 그것이 건다. 스트리밍 큐의 소멸자는 태스크를 기다리므로 Task 보다 먼저 놓는다.
            _startup.add( std::move( _owned._pAssetStreamingQueue ) );
            _pSceneManager = &_startup.add( std::move( _owned._pSceneManager ) );
            // 헤드리스 작업(셰이더 · 씬 쿠킹, 임포트)이면 그에 기대는 단계(창 · RHI · 렌더러)를 건너뛴다 — 결과는 `isHeadless` · `didHeadlessTaskFail`.
            _startup.add( sw::make_unique<HeadlessSubsystem>( bDedicatedServer, _headlessState ) );
#if SW_DEV_COMMANDS_ENABLED
            // 디버그 HUD 설정(개발 도구) — Dev 만 덧붙인다. 사용자 파일보다 먼저 읽어야 저장된 HUD 값이 버려지지 않는다.
            _owned._pUserSettingsManager->addStartupSchema( DebugHUD::kSettingsSchemaPath );
#endif
            _startup.add( std::move( _owned._pUserSettingsManager ) );
            {
                // 씬 스냅샷 · 패킷 · 업로드 큐는 그리는 쪽의 것이다. 렌더러를 세우지 않는 헤드리스 작업에는 없다.
                // 백엔드 교체로 다시 설 때(`restartStopped`)는 있는 것을 그대로 쓰고, 디바이스에 매인 설정만 새로 건다.
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
                    _pPacketBuilder->initialize( _pRHI->getDevice(), &engine::getTaskManager() );
                    if ( _pFrameRenderer->initialize( &_pRHI->getDevice(), &engine::getTaskManager() ) == false )
                    {
                        SW_LOG_ERROR( "Failed to initialize FrameRenderer!" );
                        return SubsystemInitResult::Failed;
                    }
                    return SubsystemInitResult::Succeeded;
                };
                auto shutdownFrameRenderer = [this]()
                {
                    _pPacketBuilder->shutdown();
                    _pFrameRenderer->shutdown();
                };
                FunctionSubsystem& frameRenderer =
                    _startup.add( make_unique<FunctionSubsystem>( EngineFunctionStep::kFrameRenderer, SW_DELEGATE_LAMBDA( SubsystemInitializeDelegate, initializeFrameRenderer ),
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
                    if ( pRenderThread->attach( &_pRHI->getDevice(), _pFrameRenderer ) == false )
                    {
                        SW_LOG_ERROR( "Failed to attach RenderThread!" );
                        return SubsystemInitResult::Failed;
                    }
                    return SubsystemInitResult::Succeeded;
                };
                auto stopRenderThread = [pRenderThread]()
                {
                    pRenderThread->waitIdle();
                    pRenderThread->stop();
                };
                FunctionSubsystem& renderThreadStep =
                    _startup.add( make_unique<FunctionSubsystem>( EngineFunctionStep::kRenderThread, SW_DELEGATE_LAMBDA( SubsystemInitializeDelegate, attachRenderThread ),
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
                        return SubsystemInitResult::Succeeded; // 개발 도구 하나로 기동을 세우지 않는다
                    }
                    _pShaderRecompiler = pShaderRecompiler;
                    auto onRecompiled  = [this]( string_view shaderPath, const ShaderCompileResult& result )
                    {
                        if ( _pRenderThread != nullptr )
                            _pRenderThread->waitIdle();
                        _pFrameRenderer->onShaderRecompiled( shaderPath, result );
                    };
                    pShaderRecompiler->setOnAnyShaderRecompiled( SW_DELEGATE_LAMBDA( ShaderRecompiledDelegate, onRecompiled ) );
                    return SubsystemInitResult::Succeeded;
                };
                auto shutdownLiveShader = [pShaderRecompiler]()
                { pShaderRecompiler->shutdown(); };
                FunctionSubsystem& liveShader =
                    _startup.add( make_unique<FunctionSubsystem>( EngineFunctionStep::kLiveShader, SW_DELEGATE_LAMBDA( SubsystemInitializeDelegate, initializeLiveShader ),
                                                                  SW_DELEGATE_LAMBDA( SubsystemShutdownDelegate, shutdownLiveShader ) ) );
                if ( shaderRecompiler != nullptr )
                    (void)liveShader.takeOwnership( std::move( shaderRecompiler ) );
#else
                // Debug 가 아니면 라이브 리로드가 없다 — 자리만 둔다(순서 · 백엔드 교체의 재시작 목록이 구성마다 같다).
                _startup.add( make_unique<FunctionSubsystem>( EngineFunctionStep::kLiveShader ) );
#endif
            }
            _startup.add( std::move( _owned._pShaderCache ) );
            _startup.add( std::move( _owned._pPhysicsSystem ) );
            _pFontService     = &_startup.add( std::move( _owned._pFontService ) );
            pTelemetryService = &_startup.add( std::move( _owned._pTelemetryService ) );
            _pUISystem        = &_startup.add( std::move( _owned._pUISystem ) );
        }
        TelemetryService& telemetryService = *pTelemetryService;

        // 외부 프로파일러(`-gv_tracy`)는 서비스 표가 선 직후에 켠다 — 기동 단계부터 타임라인에 남고, 켤 때 엔진 프로파일러도 같이 켠다.
        ThreadName::setCurrentThreadName( "GameThread" );
        ProfilerBackend::initialize();

        // 초기화의 순서는 손으로 적지 않는다. 서브시스템마다 먼저 서야 하는 것을 적고, 여기서는 위상 순서로 부른다. 종료와 해제는 그 역순이다(`shutdown`).
        const bool bStarted = _startup.initializeAll( bDedicatedServer ? EngineSubsystemHost::kServer : EngineSubsystemHost::kClient );
        // 메모리 태그 예산(데이터)은 설정 루트가 선 뒤에 읽는다.
        if ( bStarted )
            (void)_memoryBudgetMonitor.loadBudgetFile( FileUtil::joinPath( ResourcePaths::getProjectFolderPath(), MemoryBudgetMonitor::kBudgetFile ) ); // 틀린 표는 오류를 남기고 예산 없이 간다 — 진단 설정 하나로 기동을 세우지 않는다
        if ( bStarted == false )
        {
            SW_LOG_ERROR( "Engine startup failed: %#", _startup.getError().c_str() );
            return false;
        }
        // 셸 키(콘솔 여는 키 …)를 플레이어가 바꾼 것도 사용자 설정과 같은 폴더에 둔다. 셸 맵은 처음 돌 때 만들므로 그 전에 경로를 알려 준다.
        if ( bDedicatedServer == false && _headlessState._bHeadless == false )
            _shellActions.setUserBindingsPath( ShellInputActions::makeUserBindingsPath( engine::getUserSettingsManager().getUserFilePath() ) );
        // 장면별 프레임 시간 · flush 는 프레임 관찰자로 돈다(씬 틱 뒤 — `TelemetryService::onAfterSceneTick`). 텔레메트리가 섰을 때만 붙인다.
        // 떼는 것은 `shutdown` 이 단계 종료 전에 관찰자 목록을 비우는 것으로 끝난다.
        if ( telemetryService.isInitialized() )
            addFrameObserver( &telemetryService );
        if ( bDedicatedServer )
        {
            // 실제 바이너리에서 단계가 빠졌는지 테스트(ServerBootTest)가 이 줄로 본다. 초기화 순서대로 적는다.
            const SubsystemCollection& subsystems = _startup;
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
        if ( _headlessState._bHeadless )
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
        if ( engine::areEngineServicesBound() && _headlessState._bHeadless == false )
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
        // 단계가 소유한 객체를 초기화 순서의 역순으로 소멸시킨다: UI → 텔레메트리 → 렌더러 쪽 → RHI →
        // 글꼴 → 씬 → 입력 · 오디오 → 태스크 → 셰이더 캐시 → 엔진 데이터 → 리소스 → 설정 → 리플렉션 → 압축. 기동이 어디서 멈췄든 모든 단계를 해제한다.
        _startup.destroyAll();
        _commandStack.reset();
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
        const EngineConfig* pEngineConfig = _pConfigManager != nullptr ? _pConfigManager->getConfig<EngineConfig>() : nullptr;
        if ( configTypeName != EngineConfig::StaticType()->_fullyQualifiedName || pEngineConfig == nullptr )
            return;
        EngineConfig::setActive( *pEngineConfig );
        // 수직 동기화는 다음에 스왑체인을 만들 때(창 크기 변경 · 백엔드 교체) 적용된다.
        if ( _pRHI != nullptr )
            _pRHI->setPreferredVSync( pEngineConfig->_window._bVSync );
    }

    bool EngineLoop::renderPortraits( string_view prefabList, string_view outputDirectory, uint32 size )
    {
        if ( _pRHI == nullptr || _pRHI->hasDevice() == false )
            return false;
        return PortraitBatchUtil::renderPrefabList( _pRHI->getDevice(), _pRenderThread, prefabList, outputDirectory, size );
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

        if ( _headlessState._bHeadless )
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
            const bool           bHasDevice   = _pRHI != nullptr && _pRHI->hasDevice();
            uint32               uiWidth      = vpWidth;
            uint32               uiHeight     = vpHeight;
            if ( uiWidth == 0 || uiHeight == 0 )
            {
                uiWidth  = bHasDevice ? _pRHI->getDevice().getBackBufferWidth() : ( pWindow != nullptr ? pWindow->getWidth() : 0 );
                uiHeight = bHasDevice ? _pRHI->getDevice().getBackBufferHeight() : ( pWindow != nullptr ? pWindow->getHeight() : 0 );
            }
            const UIViewport viewport =
                _pUISystem->computeViewport( float2{ static_cast<float32>( uiWidth ), static_cast<float32>( uiHeight ) }, contentScale );
            _pUISystem->update( GameTimeScale::getUnscaledDeltaTime( deltaTime ), viewport ); // 애니메이션 · 자막 · 알림은 정지 메뉴 아래서도 흐른다
        }

        // 이번 틱에 경로로 잡힌 머티리얼(메시의 저장된 참조)을 패킷을 내기 **전에** 올린다. 컴포넌트는 디바이스를 모른다(`MaterialCache::requestInitialize`).
        // 올리기는 bindless 표에 등록하므로 렌더 스레드가 지난 프레임을 병렬로 기록하는 동안 하면 안 된다 — 올릴 것이 있는 프레임(씬 로드 ·
        // 처음 쓰는 머티리얼의 스폰)만 렌더 스레드를 기다린다.
        if ( _pRHI != nullptr && _pRHI->hasDevice() )
        {
            MaterialCache& materials = engine::getAssetManager().getAssetCache<MaterialCache>();
            if ( materials.hasPendingInitialize() && _pRenderThread != nullptr )
                _pRenderThread->waitIdle();
            materials.initializePending( &_pRHI->getDevice() );
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

            _pPacketBuilder->populateScene( packet, views, sceneViewCameraProvider, pActiveScene, deltaTime, *_pRHI, _pFrameRenderer );
            RenderPacketBuilder::populateCanvas( packet, bSceneMain, _pUISystem, _pFontService, _pRHI );

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
        if ( bReportedNow && _pRHI != nullptr && _pRHI->hasDevice() )
            _pRHI->getDevice().getMemoryLedger().report( _pRHI->getDevice().getBackendName() );

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
            _startup.shutdownDependentsOf( *_pRHISubsystem );

            // 옛 디바이스의 GPU 자원은 recreateDevice 안의 shutdown 이 레지스트리에 통보하며 거둔다.
            _pRHI->getDevice().waitIdle();
            engine::getShaderCache().clearCache();
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
    }

    bool EngineLoop::rebindSceneAfterDeviceRecreate()
    {
        if ( _pRHI == nullptr || _pRHI->hasDevice() == false )
            return false;

        // 새 디바이스가 섰다. 레지스트리 전체에 "다시 올려라" 를 알린다. 어떤 캐시를 빠뜨렸는지
        // 기억할 필요가 없는 것이 이 구조의 요점이다(언리얼 FRenderResource::InitRHI 와 같은 자리).
        RHIRenderResource::initAllFor( &_pRHI->getDevice() );

        // 내렸던 단계를 기동과 같은 본문으로 다시 세운다: 렌더러(업로드 큐 · 배치 합치기 설정을 새 디바이스로) → 렌더 스레드 → 라이브 셰이더 →
        // 씬의 디바이스. 본문은 이미 있는 객체를 다시 쓰므로 렌더 스레드에 걸린 훅과 그것을 가리키는 포인터가 그대로다.
        const bool bRestarted = _startup.restartStopped();
        if ( bRestarted == false )
            SW_LOG_ERROR( "Backend change: %#", _startup.getError().c_str() );

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

    bool EngineLoop::wasDebugActionTriggered( string_view actionName ) const
    {
        return _shellActions.wasActionTriggered( hashed_string( actionName ) );
    }
} // namespace sw
