#include "pch.h"

#include "Engine/EngineLoop.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Concurrency/DeadlockDetector.h"
#include "Core/Event/EventDispatcher.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Memory/FrameArenaAllocator.h"
#include "Core/Memory/MemoryProfiler.h"
#include "Core/Process/CrashHandler.h"
#include "Core/String/hashed_string.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Audio/IAudioSystem.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Compression/Lz4CompressionCodec.h"
#include "Engine/Compression/ZstdCompressionCodec.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/EngineData.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIBackendRegistry.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"
#include "Engine/Graphics/Renderer/Frame/RenderFramePacket.h"
#include "Engine/Graphics/Renderer/RenderThread.h"
#include "Engine/Graphics/Shader/Compile/LiveShaderManager.h"
#include "Engine/Graphics/Shader/Compile/ShaderBaker.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Input/ActionMap.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/StringTable.h"
#include "Engine/Module/LiveReloadManager.h"
#include "Engine/Module/ModuleTypeRegistry.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/ComponentDefaults.h"
#include "Engine/Resource/AssetStreamingQueue.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Resource/ResourcePackManager.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Utility/CommandStack.h"
#include "Engine/Utility/Debug/DebugOverlayState.h"
#include "Engine/Utility/Debug/FrameProfiler.h"
#include "Engine/Window/IWindow.h"

#include "sw/config/ConfigConstants.h"
#include "sw/config/ShippingHostDefaults.h"

namespace sw
{
    namespace
    {
        struct EngineLoopInternal
        {
            static bool cliRequestsBackend( const CommandLineManager& cli )
            {
                bool bFlag{ false };
                if ( cli.getArgument( CommandLineArgument::DIRECTX_11, bFlag ) && bFlag )
                    return true;
                if ( cli.getArgument( CommandLineArgument::DIRECTX_12, bFlag ) && bFlag )
                    return true;
                if ( cli.getArgument( CommandLineArgument::VULKAN, bFlag ) && bFlag )
                    return true;
                if ( cli.getArgument( CommandLineArgument::OPENGL, bFlag ) && bFlag )
                    return true;
                return false;
            }
        };
    } // namespace

    /**
     * @brief `-gv_crashTest=1` — RHI 초기화 직후 일부러 크래시를 냅니다 (리포트 경로 검증용).
     * @details 크래시 리포트는 크래시가 나야만 만들어진다. 그래서 "덤프가 제대로 써지는가" 는 일부러
     *          죽여 보는 것 말고는 확인할 방법이 없다 — 배포하고 나서 안 된다는 걸 알면 늦다.
     */
    SW_GLOBAL_VARIABLE_INT( gv_crashTest, 0, "일부러 크래시를 내 리포트 경로를 검증합니다 (1=크래시)" );
    /**
     * @brief `-gv_rhiSwapAtFrame=N -gv_rhiSwapTo=<backend>` — N 번째 프레임에 백엔드 교체를 요청합니다.
     * @details 교체는 에디터 메뉴에서만 일으킬 수 있어 헤드리스로 재현·검증할 길이 없었다. 요청 방식은 에디터 패널과 같다
     *          (`GlobalVariableInfo::setValueAsInt` → 변경 콜백 → BackendSwapController 가 다음 프레임에 적용).
     *          C++ 대입(`gv_rhiBackend = x`)은 콜백을 부르지 않아 아무 일도 일어나지 않는다. 0 이면 꺼져 있다.
     */
    SW_GLOBAL_VARIABLE_INT( gv_rhiSwapAtFrame, 0, "이 프레임에 백엔드 교체를 요청한다 (0=사용 안 함)" );
    SW_GLOBAL_VARIABLE_ENUM( gv_rhiSwapTo, RHIBackend, RHIBackend::DirectX12, "gv_rhiSwapAtFrame 에 바꿀 백엔드" );

} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "EngineLoop" );

    SW_EXTERN_GLOBAL_VARIABLE_BOOL( gv_useRenderThread );

    EngineLoop::EngineLoop()
        : _logger{ nullptr }
        , _deadlockDetector{ nullptr }
        , _memoryProfiler{ nullptr }
        , _commandLineManager{ nullptr }
        , _taskManager{ nullptr }
        , _globalVariableManager{ nullptr }
        , _typeRegistry{ nullptr }
        , _configManager{ nullptr }
        , _localizationManager{ nullptr }
        , _resourceManager{ nullptr }
        , _rhi{ nullptr }
        , _liveReloadManager{ nullptr }
        , _sceneManager{ nullptr }
        , _inputManager{ nullptr }
        , _mapDebugAction{ nullptr }
        , _audioSystem{ nullptr }
        , _eventDispatcher{ nullptr }
        , _frameRenderer{ nullptr }
        , _renderThread{ nullptr }
        , _engineData{ nullptr }
        , _assetStreamingQueue{ nullptr }
        , _commandStack{ nullptr }
        , _debugOverlayState{ nullptr }
        , _debugDrawQueue{ nullptr }
        , _frameDoubleBuffer{ nullptr }
        , _rhiBackendRegistry{ nullptr }
        , _bShellActionsBound{ false }
        , _bHeadless{ false }
    {
    }

    EngineLoop::~EngineLoop() = default;

    bool EngineLoop::initialize( int32 argc, utf8* pArgv[] )
    {
        HashedStringPool::initialize();

        /** @brief 설정 블록에서 채우고 아래 RHI·EngineData 블록이 이어서 읽는다. */
        const EngineConfig* pEngineConfig = nullptr;

        BLOCK( "Logger / DeadlockDetector / MemoryProfiler / CommandLine / GVM 초기화" )
        {
            _logger = make_unique<Logger>();
            _logger->initialize();

            // 로거 직후에 설치해야 이후 어디서 죽든 콜 스택이 남는다.
            CrashHandler::initialize();

            // 리소스 루트는 **로거·크래시 핸들러 다음**에 찾는다. 예전에는 `App::initialize` 맨 앞,
            // 그러니까 로거가 서기도 전에 불렀다 — 루트를 못 찾았을 때의 진단(`RootFolder` 로그와
            // 어설트 메시지)이 통째로 사라졌고, 반환값도 보지 않아 실패가 조용했다.
            // `initialize()` 는 once_flag 라 두 번째 호출은 아무것도 다시 찍지 않으므로,
            // **첫 호출이 로거 뒤에 와야** 진단이 남는다.
            if ( ResourceUtil::initialize() == false )
            {
                SW_LOG_ERROR( "리소스 루트를 찾지 못했습니다 — Resource/ 가 있는 위치에서 실행하십시오." );
                return false;
            }

            // 크래시 리포트에 함께 나갈 값들 — 덤프만으로는 알 수 없는 것들이다.
            // 백엔드는 RHI 초기화 뒤에 다시 덮어쓴다(여기서는 아직 정해지지 않았을 수 있다).
#if defined( SW_SHIPPING )
            CrashHandler::setContextValue( "Build", "Shipping" );
#elif defined( SW_DEBUG )
            CrashHandler::setContextValue( "Build", "Debug" );
#else
            CrashHandler::setContextValue( "Build", "Release" );
#endif
#if defined( SW_PLATFORM_WINDOWS )
            CrashHandler::setContextValue( "Platform", "Windows" );
#elif defined( SW_PLATFORM_LINUX )
            CrashHandler::setContextValue( "Platform", "Linux" );
#elif defined( SW_PLATFORM_MACOS )
            CrashHandler::setContextValue( "Platform", "macOS" );
#endif

#if defined( SW_DEBUG )
            _deadlockDetector = make_unique<DeadlockDetector>();
            _deadlockDetector->initialize();

            _memoryProfiler = make_unique<MemoryProfiler>();
            _memoryProfiler->initialize();
#endif

            _commandLineManager = make_unique<CommandLineManager>();
            _commandLineManager->initialize();

            _globalVariableManager = make_unique<GlobalVariableManager>();
            _globalVariableManager->registerPendingVariables( "Engine", GlobalVariableRegistrar::getHead() );
            GlobalVariableRegistrar::getHead() = nullptr;
            _globalVariableManager->registerToCommandLine( _commandLineManager.get() );
            _commandLineManager->parse( argc, pArgv );
            _globalVariableManager->updateFromCommandLine( _commandLineManager.get() );
        }

        BLOCK( "Core Services 생성 및 바인딩" )
        {
            _taskManager         = make_unique<TaskManager>();
            _typeRegistry        = make_unique<TypeRegistry>();
            _configManager       = make_unique<ConfigManager>();
            _localizationManager = make_unique<LocalizationManager>();
            _resourceManager     = make_unique<ResourceManager>();
#if !defined( SW_SHIPPING )
            _liveReloadManager = make_unique<LiveReloadManager>();
#endif
            _sceneManager        = make_unique<SceneManager>();
            _inputManager        = make_unique<InputManager>();
            _audioSystem         = IAudioSystem::create();
            _eventDispatcher     = make_unique<EventDispatcher>();
            _frameRenderer       = make_unique<FrameRenderer>();
            _engineData          = make_unique<EngineData>();
            _assetStreamingQueue = make_unique<AssetStreamingQueue>();
#if !defined( SW_SHIPPING )
            _commandStack = make_unique<CommandStack>();
#endif
            _debugOverlayState  = make_unique<DebugOverlayState>();
            _debugDrawQueue     = make_unique<DebugDrawQueue>();
            _frameDoubleBuffer  = make_unique<FrameDoubleBuffer>();
            _rhiBackendRegistry = make_unique<RHIBackendRegistry>();
            // 레지스트리는 여기가 소유하고, Core 의 CompressionStream 이 보도록 슬롯에 꽂는다 —
            // 스트림이 Core 에 있어서 엔진 서비스 테이블에는 닿지 못한다(Logger::setGlobalSink 와 같은 모양).
            _compressionCodecRegistry = make_unique<CompressionCodecRegistry>();
            _compressionCodecRegistry->initialize();
            CompressionCodecRegistry::setActive( _compressionCodecRegistry.get() );
            // 외부 라이브러리 코덱은 **여기서** 등록한다. Core 는 압축 라이브러리에 종속되지 않게 두므로
            // (ReflectionParser 가 Core 를 링크한다) LZ4/Zstd 는 Engine 이 들고 와 붙인다.
            _compressionCodecRegistry->registerCodec( make_unique<Lz4CompressionCodec>() );
            _compressionCodecRegistry->registerCodec( make_unique<ZstdCompressionCodec>() );
            _shaderCache = make_unique<ShaderCache>();
            _shaderCache->initialize();
            _componentDefaults = make_unique<ComponentDefaults>();
            _frameProfiler     = make_unique<FrameProfiler>();

            EngineServices services{};
            services._pCommandLineManager       = _commandLineManager.get();
            services._pGlobalVariableManager    = _globalVariableManager.get();
            services._pLocalizationManager      = _localizationManager.get();
            services._pTaskManager              = _taskManager.get();
            services._pTypeRegistry             = _typeRegistry.get();
            services._pSceneManager             = _sceneManager.get();
            services._pInputManager             = _inputManager.get();
            services._pAudioSystem              = _audioSystem.get();
            services._pEventDispatcher          = _eventDispatcher.get();
            services._pResourceManager          = _resourceManager.get();
            services._pMemoryProfiler           = _memoryProfiler.get();
            services._pEngineData               = _engineData.get();
            services._pAssetStreamingQueue      = _assetStreamingQueue.get();
            services._pCommandStack             = _commandStack.get();
            services._pDebugOverlayState        = _debugOverlayState.get();
            services._pDebugDrawQueue           = _debugDrawQueue.get();
            services._pFrameDoubleBuffer        = _frameDoubleBuffer.get();
            services._pRHIBackendRegistry       = _rhiBackendRegistry.get();
            services._pCompressionCodecRegistry = _compressionCodecRegistry.get();
            services._pShaderCache              = _shaderCache.get();
            services._pComponentDefaults        = _componentDefaults.get();
            services._pFrameProfiler            = _frameProfiler.get();

            engine::bindEngineServices( services );
            engine::registerModuleTypes( "Engine" );
            engine::registerModuleTypes( "GameFramework" );
        }

        // 설정은 리소스 초기화보다 **먼저** 읽는다.
        //
        // ConfigManager 가 필요한 것은 `ResourceUtil` 이 이미 찾아 둔 프로젝트 루트 하나뿐이다.
        // 반대로 `ResourceManager::mountContent` 는 GameConfig 의 `_packRoot` 가 정해져 있어야
        // 팩을 제대로 마운트하고 게임 도메인의 `assetregistry.txt` 를 읽을 수 있다.
        //
        // 예전에는 순서가 반대였고, 그래서 리소스를 먼저 세운 뒤 설정을 읽고 **팩 마운트와 레지스트리
        // 로드를 한 번 더** 해서 메웠다. 보정이 필요하다는 것 자체가 순서가 틀렸다는 신호였다.
        BLOCK( "엔진 설정 로드 (리소스 초기화보다 먼저)" )
        {
            // Config/ 는 프로젝트 루트에 있고 실행 파일은 build/<preset>/Bin 에서 돈다 — 작업 디렉터리
            // 기준으로만 찾으면 전부 "없음"이 되어 조용히 기본값으로 떨어진다. Resource/ 를 찾을 때
            // 이미 알아낸 프로젝트 루트를 기준으로 넘겨준다.
            _configManager->setRootDirectory( ResourceUtil::getProjectFolderPath() );

            const hashed_string kEngineConfigHash = hashed_string{ "EngineConfig" };
            pEngineConfig                         = _configManager->ensureConfig<EngineConfig>(
                kEngineConfigHash, config::kFileRuntimeEngineConfig, shipping_host::kEngineConfigJson );
            if ( pEngineConfig == nullptr )
                return false;

            const hashed_string kGameConfigHash = hashed_string{ "GameConfig" };
            const GameConfig*   pGameConfig     = _configManager->ensureConfig<GameConfig>(
                kGameConfigHash, config::kFileRuntimeGameConfig, shipping_host::kGameConfigJson );
            if ( pGameConfig != nullptr )
                GameConfig::setActive( *pGameConfig );
        }

        BLOCK( "Task / Resource / Scene 초기화" )
        {
            if ( _resourceManager->initialize() == false )
                return false;

            // GameConfig 가 활성화된 뒤라야 "game" 토큰이 팩 루트로 풀린다 — 그 전제는 이제
            // `mountContent` 의 인자에 드러나 있다. 설정의 우선순위 목록이 비어 있으면 지금 것을 쓴다.
            _resourceManager->mountContent( pEngineConfig->_listResourcePriority );

            if ( _taskManager->initialize() == false )
                return false;
            if ( _sceneManager->initialize() == false )
                return false;
            if ( _inputManager->initialize() == false )
                return false;
            if ( _audioSystem->initialize() == false )
                return false;
        }

        BLOCK( "EngineData 로드 및 RHI 백엔드 선정 & 초기화" )
        {
            if ( pEngineConfig->_engineData.empty() == false )
                _engineData->loadFromResource( pEngineConfig->_engineData );
            else
                _engineData->loadFromResource();

            bool bBakeShaders = false;
            if ( _commandLineManager->getArgument( CommandLineArgument::BAKE_SHADERS, bBakeShaders ) && bBakeShaders )
            {
                _bHeadless = true;
                SW_LOG_INFO( "Starting Headless (BakeShaders)..." );
                ShaderBaker::bakeAllShaders();
                return true;
            }

            if ( EngineLoopInternal::cliRequestsBackend( *_commandLineManager ) == false )
                gv_rhiBackend = pEngineConfig->_window._defaultRHI;

            if ( IWindow::getActiveWindow() == nullptr )
            {
                uint32 windowWidth  = pEngineConfig->_window._width;
                uint32 windowHeight = pEngineConfig->_window._height;
                // 인자를 주지 않으면 getArgument 가 false 를 돌려주므로 설정값이 그대로 남는다.
                _commandLineManager->getArgument( CommandLineArgument::WIDTH, windowWidth );
                _commandLineManager->getArgument( CommandLineArgument::HEIGHT, windowHeight );

                unique_ptr<IWindow> defaultWindow = IWindow::createPlatformWindow();
                if ( defaultWindow != nullptr && defaultWindow->initializeWindow( pEngineConfig->_window._title.c_str(), windowWidth, windowHeight ) )
                {
                    // 소유권은 App::initialize 가 IWindow::getActiveWindow() 로 입양합니다.
                    // (App 이 없는 임베드 시나리오라면 호출자가 getActiveWindow() 를 직접 소유해야 합니다.)
                    IWindow::setActiveWindow( defaultWindow.release() );
                }
            }

            _rhi = make_unique<RHI>();
            _rhi->setPreferredVSync( pEngineConfig->_window._bVSync );
            if ( _rhi->initialize() == false )
                return false;
            // GT 쪽 GpuScene 이 배치를 만든다 — 텍스처를 인덱스로 고를 수 있는 백엔드면 셰이더 타입 단위로 합친다(언리얼 GPUScene).
            // 백엔드가 정해졌으니 크래시 리포트에 남긴다 — 이 저장소는 백엔드가 넷이라 "어느
            // 백엔드에서 났는가" 가 범위를 좁히는 첫 질문이다.
            CrashHandler::setContextValue( "RHI", _rhi->getDevice().getBackendName() );

            // `-gv_crashTest=1` — 크래시 리포트 경로를 실제로 확인하는 유일한 방법이다. 리포트는
            // 크래시가 나야만 만들어지므로, 일부러 한 번 죽여 보지 않으면 배포 뒤에야 안 되는 걸 안다.
            if ( gv_crashTest != 0 )
            {
                SW_LOG_ERROR( "[CrashTest] 의도적으로 널 포인터를 씁니다 — 크래시 리포트 경로 검증용입니다." );
                volatile int32* pNull = nullptr;
                *pNull                = 1;
            }

            _gtGpuScene.setMergeBatchesAcrossMaterials( _rhi->getDevice().supportsNativeBindlessSampling() );

            if ( _frameRenderer->initialize( &_rhi->getDevice(), _taskManager.get() ) == false )
            {
                SW_LOG_ERROR( "Failed to initialize FrameRenderer!" );
                return false;
            }
            // LiveShaderManager 는 SW_DEBUG 에서만 만들어진다 — 없으면 건너뛴다.
            if ( LiveShaderManager* pLiveShaderManager = _rhi->getLiveShaderManager() )
            {
                // 렌더 스레드를 먼저 세운 뒤에 재컴파일을 반영한다.
                //
                // onShaderRecompiled 는 셰이더 바인딩 레이아웃 캐시 항목을 **파괴**하는데,
                // FrameRenderer::_mapPsoLayout 과 패스 컨텍스트의 1-entry 캐시가 그 실체를 가리키는
                // 생포인터를 들고 있다. 이 콜백은 게임 스레드(tick 의 핫 리로드 블록)에서 불리고
                // 렌더 스레드는 직전 패킷을 그리는 중이라, 그냥 부르면 그리는 쪽이 해제된 레이아웃을
                // 참조한다. 재컴파일은 개발 중 가끔 일어나는 일이라 이때의 스톨은 문제가 되지 않는다.
                auto onRecompiled = [this]( string_view shaderPath, const ShaderCompileResult& result )
                {
                    if ( _renderThread != nullptr )
                        _renderThread->waitIdle();
                    _frameRenderer->onShaderRecompiled( shaderPath, result );
                };
                pLiveShaderManager->setOnAnyShaderRecompiled( SW_DELEGATE_LAMBDA( ShaderRecompiledDelegate, onRecompiled ) );
            }

            _renderThread = make_unique<RenderThread>();
            if ( gv_useRenderThread )
            {
                if ( _renderThread->start( &_rhi->getDevice(), _frameRenderer.get() ) == false )
                {
                    SW_LOG_ERROR( "Failed to start RenderThread!" );
                    return false;
                }
            }
            else
            {
                if ( _renderThread->bind( &_rhi->getDevice(), _frameRenderer.get() ) == false )
                {
                    SW_LOG_ERROR( "Failed to bind RenderThread!" );
                    return false;
                }
            }

            if ( _sceneManager != nullptr )
            {
                _sceneManager->setRhiDevice( &_rhi->getDevice() );
                _sceneManager->setFrameRenderer( _frameRenderer.get() );
            }
        }

        _profileSession.begin();

        MemoryProfiler::captureMemoryLeakBaseline();

        return true;
    }

    void EngineLoop::shutdown()
    {
        BLOCK( "RHI / Window 정리" )
        {
            if ( _sceneManager != nullptr )
            {
                _sceneManager->setFrameRenderer( nullptr );
                _sceneManager->setRhiDevice( nullptr );
            }
            if ( _renderThread != nullptr )
            {
                _renderThread->waitIdle();
                _renderThread->stop();
            }
            // GT 쪽 GpuScene 도 스냅샷의 소유(머티리얼·인스턴스)를 들고 있다 — 렌더러와 같은 시점에 놓는다.
            // 소멸자에 맡기면 디바이스가 사라진 뒤에 놓게 된다.
            _gtGpuScene.clear();
            if ( _frameRenderer != nullptr )
                _frameRenderer->shutdown();
            if ( _rhi != nullptr )
            {
                // 디바이스 생성이 실패하면 RHI 객체는 있어도 **디바이스가 없다**. getDevice() 는
                // 널 참조 역참조이므로 hasDevice() 로 먼저 막는다 — 이게 없어서 "요청한 백엔드가
                // 이 빌드에 없다" 라는 정상적인 실패가 종료 경로에서 SEGFAULT 로 끝났다.
                if ( _rhi->hasDevice() )
                {
                    if ( _resourceManager != nullptr && _rhi->getDevice().getNativeDevice() != nullptr )
                    {
                        _resourceManager->getMaterialManager().shutdownAllGpu( &_rhi->getDevice() );
                        _resourceManager->getTextureManager().shutdownAllGpu( &_rhi->getDevice() );
                    }
                    _rhi->getDevice().waitIdle();
                }
                _rhi->shutdown();
            }
        }

        BLOCK( "매니저 종료 및 언바인드" )
        {
            if ( _sceneManager != nullptr )
                _sceneManager->shutdown();
            if ( _inputManager != nullptr )
                _inputManager->shutdown();
            if ( _audioSystem != nullptr )
                _audioSystem->shutdown();
            if ( _liveReloadManager != nullptr )
                _liveReloadManager->shutdown();
            if ( _taskManager != nullptr )
                _taskManager->shutdown();
            if ( _globalVariableManager != nullptr )
                _globalVariableManager->shutdown();
            if ( _memoryProfiler != nullptr )
                _memoryProfiler->shutdown();
            if ( _shaderCache != nullptr )
                _shaderCache->shutdown();
            // 코덱 레지스트리는 여기서 shutdown 하지 않는다 — 아래 reset 블록에서 슬롯을 끊고 통째로
            // 없앤다. 모듈이 등록한 코덱을 거두는 것은 등록한 모듈의 책임이다(registerCodec 주석 참고).
            if ( _logger != nullptr )
                _logger->shutdown();

            _renderThread.reset();
            _frameRenderer.reset();
            _sceneManager.reset();
            _inputManager.reset();
            _audioSystem.reset();
            _eventDispatcher.reset();
            _liveReloadManager.reset();
            _engineData.reset();
            _assetStreamingQueue.reset();
            _commandStack.reset();
            _debugOverlayState.reset();
            _debugDrawQueue.reset();
            _frameDoubleBuffer.reset();
            _rhiBackendRegistry.reset();
            _shaderCache.reset();
            _componentDefaults.reset();
            _frameProfiler.reset();
            // 슬롯부터 끊는다 — 소유자가 죽은 뒤에도 슬롯이 가리키고 있으면 엔진을 내린 다음의
            // 압축 경로가 해제된 레지스트리를 읽는다. 끊고 나면 CompressionStream 은 내장 코덱으로 문다.
            CompressionCodecRegistry::setActive( nullptr );
            _compressionCodecRegistry.reset();

            // [Note] ResourceManager는 가장 밑바탕이 되는 시스템입니다.
            // 다른 매니저들의 reset() 시 소멸자가 호출되며 들고 있던 리소스들을 해제하는데,
            // 이때 ResourceManager가 살아있어야 안전하게 해제됩니다.
            // 따라서 모든 매니저들의 소멸자가 불린 직후인 이곳에서 마지막으로 shutdown()을 호출합니다.
            if ( _resourceManager != nullptr )
                _resourceManager->shutdown();
            _resourceManager.reset();
            _typeRegistry.reset();
            _localizationManager.reset();
            _globalVariableManager.reset();
            _configManager.reset();
            _taskManager.reset();
            _rhi.reset();
            _commandLineManager.reset();
            _mapDebugAction.reset();
            _memoryProfiler.reset();
            _deadlockDetector.reset();

            engine::unbindEngineServices();

            HashedStringPool::shutdown();

            CrashHandler::shutdown();
            _logger.reset();
        }

        MemoryProfiler::reportMemoryLeaks( "EngineLoop::shutdown" );
    }

    void EngineLoop::beginFrame()
    {
        if ( _inputManager != nullptr )
            _inputManager->beginFrame();
    }

    void EngineLoop::tick( float32                           deltaTime,
                           uint64                            gameRenderTarget,
                           uint32                            vpWidth,
                           uint32                            vpHeight,
                           const ViewCameraProviderDelegate& viewCameraProvider,
                           bool                              bTickScene )
    {
        // 진단: 지정 프레임에 백엔드 교체를 요청한다 (에디터 패널과 같은 길 — setValueAsInt 가 변경 콜백을 부른다).
        if ( gv_rhiSwapAtFrame > 0 && engine::getFrameProfiler().getFrameCount() == static_cast<uint64>( gv_rhiSwapAtFrame ) &&
             gv_rhiBackend != gv_rhiSwapTo )
        {
            SW_LOG_INFO( "[SwapProbe] frame %# — requesting backend %#", gv_rhiSwapAtFrame, RHI::getBackendTypeName( gv_rhiSwapTo ) );
            // C++ 대입은 변경 콜백을 부르지 않는다 — 메뉴·콘솔이 타는 setValueAsInt 로 가야 BackendSwapController 가 받는다.
            if ( GlobalVariableInfo* pVar = engine::getGlobalVariableManager().findVariable( "gv_rhiBackend" ) )
                pVar->setValueAsInt( static_cast<int64>( gv_rhiSwapTo ) );
            gv_rhiSwapAtFrame = 0; // 한 번만 — 프로파일러가 프레임 수를 되돌리면(워밍업 뒤) 같은 번호가 다시 온다
        }

        if ( _bHeadless )
            return;

        FrameProfiler& profiler = engine::getFrameProfiler();
        profiler.beginFrame();

        BLOCK( "핫 리로드 / 씬 트랜지션 / 이벤트" )
        {
#if !defined( SW_SHIPPING )
            if ( _liveReloadManager != nullptr )
                _liveReloadManager->update();
    #if defined( SW_DEBUG )
            if ( _rhi != nullptr )
            {
                if ( LiveShaderManager* pLiveShaderManager = _rhi->getLiveShaderManager() )
                    pLiveShaderManager->update();
            }
    #endif
#endif
            // 파일 다이얼로그 결과를 **여기서** 메인 스레드로 넘긴다 — 다이얼로그는 분리 스레드가 띄운다.
            FileUtil::pumpFileDialogResults();
            engine::getAssetStreamingQueue().update();

            if ( _sceneManager != nullptr )
                _sceneManager->tickTransitions();
            if ( _eventDispatcher != nullptr )
                _eventDispatcher->processEvents();
            if ( _audioSystem != nullptr )
                _audioSystem->update( deltaTime );
        }

        BLOCK( "Scene update" )
        {
            if ( _sceneManager != nullptr && bTickScene )
                _sceneManager->tick( deltaTime );
        }

        Scene* pActiveScene = _sceneManager != nullptr ? _sceneManager->getActiveScene() : nullptr;

        BLOCK( "RenderFramePacket 제출" )
        {
            RenderFramePacket packet{};
            packet._bValid           = 1;
            packet._gameRenderTarget = gameRenderTarget;
            packet._viewportWidth    = vpWidth;
            packet._viewportHeight   = vpHeight;
            packet._cameraPos        = float3{ 0.0f, 1.2f, 3.2f };

            if ( pActiveScene != nullptr )
            {
                // 주광은 씬이 갖고, 렌더 스레드는 패킷으로만 받는다 — executePacket 은 _pScene 을
                // null 로 두므로 렌더 스레드에서 씬을 조회할 수 없다.
                DirectionalLightComponent* pLight = pActiveScene->findActiveDirectionalLight();
                if ( pLight != nullptr )
                {
                    const float3 dir          = pLight->getLightDirection();
                    const float3 color        = pLight->getColor();
                    packet._lightDirIntensity = float4{ dir._x, dir._y, dir._z, pLight->getIntensity() };
                    packet._lightColorAmbient = float4{ color._x, color._y, color._z, pLight->getAmbient() };
                    packet._lightViewProj     = pLight->castsShadow() ? pLight->buildShadowViewProj() : float4x4{};
                    packet._bHasLight         = 1;
                }

                pActiveScene->ensureDefaultCameras();
                // 위의 핫리로드/씬 전환/씬 틱이 GameObject 를 파괴했을 수 있으므로 여기서 조회한다.
                CameraComponent* pCam = viewCameraProvider.isBound() ? viewCameraProvider() : nullptr;
                if ( pCam == nullptr || pCam->isActive() == false )
                    pCam = pActiveScene->getActiveGameCamera();
                if ( pCam != nullptr )
                {
                    packet._cameraPos = pCam->getCameraPosition();
                    const float32 aspect =
                        ( packet._viewportHeight > 0 )
                            ? ( static_cast<float32>( packet._viewportWidth ) / static_cast<float32>( packet._viewportHeight ) )
                            : ( 16.0f / 9.0f );
                    packet._viewProj     = pCam->getViewProjectionMatrix( aspect );
                    packet._bHasViewProj = 1;
                }
                _gtGpuScene.buildFromScene( pActiveScene, packet._cameraPos, _taskManager.get() );
                _gtGpuScene.exportCpuSnapshot( packet._gpuScene );
            }

            if ( _renderThread != nullptr )
                _renderThread->submit( std::move( packet ) );
        }
    }

    void EngineLoop::endFrame()
    {
        engine::getFrameProfiler().endFrame();
        _profileSession.onFrameEnd();

        if ( _inputManager != nullptr )
            _inputManager->endFrame();
        engine::getDebugDrawQueue().clear();
        if ( _frameDoubleBuffer != nullptr )
            _frameDoubleBuffer->swapAndResetPrevious();
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

        BLOCK( "기존 RHI / Scene 리소스 정리" )
        {
            if ( _sceneManager != nullptr )
            {
                _sceneManager->setFrameRenderer( nullptr );
                _sceneManager->setRhiDevice( nullptr );
            }
            if ( _renderThread != nullptr )
                _renderThread->stop();
            if ( _frameRenderer != nullptr )
                _frameRenderer->shutdown();

            engine::getResourceManager().getMaterialManager().shutdownAllGpu( &_rhi->getDevice() );
            engine::getResourceManager().getTextureManager().shutdownAllGpu( &_rhi->getDevice() );

            _rhi->getDevice().waitIdle();
            if ( _shaderCache != nullptr )
                _shaderCache->clearCache();
            // GT 쪽 GpuScene 의 캐시(후보·배치)가 옛 디바이스에 올라간 머티리얼·인스턴스의 소유를 들고 있다.
            // 여기서 놓지 않으면 교체 뒤 첫 buildFromScene 의 clear() 가 그것들을 옛 디바이스와 함께 파괴한다
            // — 실제로 그 자리에서 죽었다(~MaterialInstance → shutdown(옛 디바이스)).
            _gtGpuScene.clear();
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
            rebindSceneAfterDeviceRecreate();
            return false;
        }

        rebindSceneAfterDeviceRecreate();
        SW_LOG_INFO( "Active backend is now %#", RHI::getBackendTypeName( _rhi->getCommittedBackend() ) );
        return true;
    }

    void EngineLoop::rebindSceneAfterDeviceRecreate()
    {
        if ( _rhi == nullptr || _rhi->hasDevice() == false )
            return;

        if ( engine::getResourceManager().getMaterialManager().reinitializeAll( &_rhi->getDevice() ) == false )
            SW_LOG_ERROR( "MaterialCache reinitializeAll failed after backend change." );

        if ( _frameRenderer != nullptr )
            _frameRenderer->initialize( &_rhi->getDevice(), _taskManager.get() );

        if ( _renderThread != nullptr )
        {
            if ( gv_useRenderThread )
                _renderThread->start( &_rhi->getDevice(), _frameRenderer.get() );
            else
                _renderThread->bind( &_rhi->getDevice(), _frameRenderer.get() );
        }

        if ( _sceneManager != nullptr )
        {
            _sceneManager->setRhiDevice( &_rhi->getDevice() );
            _sceneManager->setFrameRenderer( _frameRenderer.get() );
        }

        Scene* pScene = _sceneManager != nullptr ? _sceneManager->getActiveScene() : nullptr;
        if ( pScene != nullptr )
            pScene->ensureDefaultCameras();
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
        if ( _inputManager == nullptr )
            return;

        if ( _bShellActionsBound == false )
        {
            _mapDebugAction            = make_unique<ActionMap>();
            const string& inputMapPath = engine::getEngineData()._shellInputMap;
            if ( inputMapPath.empty() || _mapDebugAction->loadFromResource( inputMapPath ) == false )
                _mapDebugAction->bindDefaultFallback();
            _bShellActionsBound = true;
        }

        // _shellActions는 위 lazy init에서 항상 생성되므로 nullptr일 수 없습니다.

        if ( _mapDebugAction->hasLayer( ActionMapDefaults::kTitleLayerName ) )
            _mapDebugAction->setLayerEnabled( ActionMapDefaults::kTitleLayerName, false );
        _mapDebugAction->setInputManager( _inputManager.get() );
        _mapDebugAction->update( deltaTime );
    }

    void EngineLoop::pollDebugHotkeys( [[maybe_unused]] const Delegate<void( const utf8* )>& forceReloadCallback )
    {
#if !defined( SW_SHIPPING )
        if ( _mapDebugAction == nullptr )
            return;

        if ( _mapDebugAction->wasActionTriggered( ActionMapDefaults::kReloadShadersAction ) && _rhi != nullptr )
        {
    #if defined( SW_DEBUG )
            if ( LiveShaderManager* pLiveShaderManager = _rhi->getLiveShaderManager() )
            {
                pLiveShaderManager->triggerReloadAll();
                SW_LOG_INFO( "%#: force shader reload", ActionMapDefaults::kReloadShadersAction );
            }
    #endif
        }
        if ( _mapDebugAction->wasActionTriggered( ActionMapDefaults::kReloadGameAction ) && forceReloadCallback.isBound() )
        {
            forceReloadCallback( config::kTargetGameModule );
            SW_LOG_INFO( "%#: force SWGame reload", ActionMapDefaults::kReloadGameAction );
        }
#endif
    }

    bool EngineLoop::wasDebugActionTriggered( string_view actionName ) const
    {
        if ( _mapDebugAction == nullptr )
            return false;
        return _mapDebugAction->wasActionTriggered( actionName );
    }
} // namespace sw
