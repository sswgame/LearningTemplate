#include "pch.h"

#include "TestFramework/TestHostRuntime.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/Event/EventDispatcher.h"
#include "Core/File/AsyncFileIO.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Audio/IAudioService.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/EngineBootstrap.h"
#include "Engine/EngineInitSequence.h"
#include "Engine/EngineServiceCollection.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Graphics/Debug/RenderTargetRegistry.h"
#include "Engine/Graphics/RHI/RHIBackendRegistry.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/StringTable.h"
#include "Engine/Module/ModuleTypeRegistry.h"
#include "Engine/Object/Component/ComponentDefaults.h"
#include "Engine/Object/GameObject/SceneEngineServices.h"
#include "Engine/Physics/PhysicsSystem.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/AssetStreamingQueue.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Serialization/Format/JSONSerializer.h"
#include "Engine/Utility/CommandStack.h"
#include "Engine/Utility/DebugOverlayState.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

#include "TestFramework/TestFramework.h"

#include "sw/config/ConfigConstants.h"
#include "sw/config/ShippingHostDefaults.h"

namespace
{
    /**
     * @brief 하네스의 기동 단계 본문입니다. 순서는 `EngineLoop` 과 같은 표(`EngineInitStepList.xxx`)가 정합니다.
     * @details 호스트 자신이 본문 모음이다 — 표에 남은 단계 가운데 필요한 `initialize<단계>` · `shutdown<단계>` · `destroy<단계>` 만 정적 함수로 둔다
     *          (`EngineInitNoStepBody`). 서브시스템으로 옮긴 단계는 `TestHostRuntime::start` 가 앱과 같은 선언으로 넣는다.
     *          하네스는 모듈 이미지 · 헤드리스 작업 · 글꼴 · 창 · RHI · 렌더러 · 플레이어 설정 · 텔레메트리 · UI 를 세우지 않으므로 그 단계의 함수가 없고
     *          (글꼴 · UI 테스트는 자기 FontService · UISystem 을 만든다), 오디오는 초기화하지 않고 종료만 부른다. 해제는 `EngineLoop` 과 같은 역순이다.
     */
    struct TestHost : sw::EngineInitNoStepBody
    {
        sw::EngineServiceCollection*      _pOwned{ nullptr };
        sw::unique_ptr<sw::ConfigManager> _configManager{};
        sw::unique_ptr<sw::IAudioService> _audioService{};
        sw::unique_ptr<sw::CommandStack>  _commandStack{};
        sw::DelegateHandle                _sceneObjectsInvalidatedHandle{}; ///< 씬 매니저에 단 기록 비우기 구독(씬 단계 해제가 뗀다)
        const sw::EngineConfig*           _pEngineConfig{ nullptr };

        static sw::EngineInitResult initializeConfig( TestHost& host )
        {
            // 설정은 리소스 초기화보다 먼저 읽는다. `loadAssetRegistries` 는 `GameConfig::getActive()._packRoot` 로 게임
            // 레지스트리 경로를 만든다 — 활성 설정이 없으면 시작 시점 GUID 표가 반쪽이 된다.
            host._configManager = sw::make_unique<sw::ConfigManager>();
            host._configManager->setRootDirectory( sw::ResourcePaths::getProjectFolderPath() );
#if defined( SW_SHIPPING )
            // 배포 구성은 생성된 JSON 만 읽는다. 그것이 역직렬화되지 않으면(정적 링크에서 리플렉션 등록기가 빠졌다) 모든 테스트가 C++ 기본값
            // 설정으로 돈다 — 기동 실패로 드러낸다(`sw_addTestExecutable` 의 통째 링크).
            {
                sw::EngineConfig probe;
                if ( sw::JSONSerializer::deserialize( &probe, *sw::EngineConfig::StaticType(), sw::shipping_host::kEngineConfigJSON ) == false )
                {
                    SW_LOG_ERROR( "The generated EngineConfig JSON does not deserialize in this test executable - a reflection registrar was dropped at link time" );
                    return sw::EngineInitResult::Failed;
                }
            }
#endif
            host._pEngineConfig = host._configManager->ensureConfig<sw::EngineConfig>( sw::config::kFileRuntimeEngineConfig, sw::shipping_host::kEngineConfigJSON );
            if ( host._pEngineConfig == nullptr )
                return sw::EngineInitResult::Failed;
            sw::EngineConfig::setActive( *host._pEngineConfig );
            const sw::GameConfig* pGameConfig = host._configManager->ensureConfig<sw::GameConfig>( sw::config::kFileRuntimeGameConfig, sw::shipping_host::kGameConfigJSON );
            if ( pGameConfig == nullptr )
                return sw::EngineInitResult::Failed;
            sw::GameConfig::setActive( *pGameConfig );
            return sw::EngineInitResult::Succeeded;
        }
        static void destroyConfig( TestHost& host )
        {
            host._pEngineConfig = nullptr;
            host._configManager.reset();
        }

        static sw::EngineInitResult initializeResource( TestHost& host )
        {
            if ( host._pOwned->_pAssetManager->initialize() == false )
                return sw::EngineInitResult::Failed;
            // GameConfig 가 활성화된 뒤라야 "game" 토큰이 팩 루트로 풀린다 — 그 전제는 `mountContent` 의 인자에 드러나 있다.
            if ( host._pEngineConfig != nullptr )
                host._pOwned->_pAssetManager->mountContent( host._pEngineConfig->_listResourcePriority );
            else
                host._pOwned->_pAssetManager->mountContent( {} );
            return sw::EngineInitResult::Succeeded;
        }
        static void destroyResource( TestHost& host ) { host._pOwned->destroyAssetManager(); }

        static void destroyEngineDefaultAssets( TestHost& host ) { host._pOwned->_pEngineDefaultAssets.reset(); }

        static void shutdownAudio( TestHost& host ) { host._audioService->shutdown(); }
        static void destroyAudio( TestHost& host ) { host._audioService.reset(); }

        static sw::EngineInitResult initializeScene( TestHost& host )
        {
            // 앱과 같다 — 표를 묶은 뒤 씬 쪽 묶음을 넘긴다(씬 매니저는 묶기 전에 만들어졌다).
            const sw::SceneEngineServices services = sw::SceneEngineServices::makeFromBoundEngine();
            if ( host._pOwned->_pSceneManager->initialize( services ) == false )
                return sw::EngineInitResult::Failed;
            // 앱과 같다 — 씬이 바뀌면 되돌리기 기록을 소유자가 비운다(씬 매니저보다 기록이 늦게 해제된다).
            sw::SceneObjectsInvalidatedDelegate& onInvalidated = host._pOwned->_pSceneManager->onSceneObjectsInvalidated();
            if ( host._sceneObjectsInvalidatedHandle.isValid() )
                onInvalidated.remove( host._sceneObjectsInvalidatedHandle );
            host._sceneObjectsInvalidatedHandle = onInvalidated.add( SW_DELEGATE_METHOD( sw::Delegate<void()>, &sw::CommandStack::clear, host._commandStack.get() ) );
            return sw::EngineInitResult::Succeeded;
        }
        static void shutdownScene( TestHost& host )
        {
            host._pOwned->_pSceneManager->shutdown();
            if ( host._sceneObjectsInvalidatedHandle.isValid() )
                host._pOwned->_pSceneManager->onSceneObjectsInvalidated().remove( host._sceneObjectsInvalidatedHandle );
            host._sceneObjectsInvalidatedHandle = sw::DelegateHandle{};
        }
        static void destroyScene( TestHost& host )
        {
            host._pOwned->_pSceneManager.reset();
            // 소멸자가 태스크를 기다린다 — Task 해제보다 먼저다.
            host._pOwned->_pAssetStreamingQueue.reset();
            host._commandStack.reset();
        }
    };
} // namespace

namespace test
{
    void rebindEngineServices( const sw::EngineServices& services )
    {
        // 이 파일이 호스트다(아래 `TestHostRuntime::start` 가 표를 채운다). 테스트는 이 창구로만 표를 갈아 끼운다.
        sw::engine::bindEngineServices( services );
    }
} // namespace test

namespace test
{
    /** @brief 하네스 하나의 상태입니다. 선언 순서가 소멸 순서의 역이다 — 저장소 → 부트스트랩 → 호스트 → 기동 표. */
    struct TestHostRuntime::State
    {
        sw::EngineServiceCollection _owned{};
        sw::EngineBootstrap         _bootstrap{};
        TestHost                    _host{};
        sw::EngineInitSequence      _startup{};
        bool                        _bBootstrapped{ false };
    };

    TestHostRuntime::TestHostRuntime()
        : _pState{}
    {
    }

    TestHostRuntime::~TestHostRuntime()
    {
        stop();
    }

    bool TestHostRuntime::start( int32 argc, utf8* argv[] )
    {
        if ( _pState != nullptr )
            return false;
        _pState         = sw::make_unique<State>();
        State&    state = *_pState;
        TestHost& host  = state._host;
        host._pOwned    = &state._owned;

        // ------------------------------------------------------------------------------
        // 0) 부트스트랩 — `EngineLoop` 과 같은 것(이름 풀 · 로거 · 크래시 핸들러 · 리소스 루트 · 진단 도구 · 명령줄 · 전역 변수)
        // ------------------------------------------------------------------------------
        // 목록(`EngineServiceList.xxx`)의 `EngineCreated` 서비스는 저장소가 만든다. 테스트 실행 파일은 진단 도구(교착 감지기 · 메모리 프로파일러)를 늘 켠다.
        // 프레임워크 전용 플래그를 먼저 소비해 CommandLineManager 가 미지 인자를 경고하지 않게 한다(부트스트랩이 나머지를 파싱한다).
        sw::vector<utf8*> listApplicationArg = TestRegistry::getInstance().configureFromArgs( argc, argv );
        if ( TestRegistry::getInstance().isHelpRequested() )
        {
            // 도움말은 엔진 없이 찍는다 — 기동을 기다릴 까닭이 없고, `main` 이 종료 코드 0 으로 끝낸다.
            TestRegistry::getInstance().printHelp();
            return false;
        }
        if ( state._bootstrap.initialize( state._owned, true, static_cast<int32>( listApplicationArg.size() ), listApplicationArg.data() ) == false )
            return false;
        state._bBootstrapped = true;

        state._owned.createAll();
        host._audioService = sw::IAudioService::create();
        host._commandStack = sw::make_unique<sw::CommandStack>();

        sw::EngineServices services{};
        state._bootstrap.fillServices( services );
        // HostCreated 인 자리만 손으로 꽂는다(팩토리 · 구성별 조건부).
        services._pAudioService = host._audioService.get();
        services._pCommandStack = host._commandStack.get();
        sw::engine::bindEngineServices( services );

        // ------------------------------------------------------------------------------
        // 1) 기동 단계 — 리플렉션 · 설정 · 리소스 · 태스크 · 입력 · 씬
        // ------------------------------------------------------------------------------
        // 순서는 손으로 적지 않는다. `EngineLoop` 과 같은 표(`EngineInitStepList.xxx`)를 위상 정렬한 순서로 본문(`TestHost::initialize<단계>`)을
        // 부른다 — 하네스가 앱과 다른 순서로 서는 일이 구조로 막힌다. 종료와 해제는 `stop` 이 그 역순으로 한다.
        // 서브시스템으로 옮긴 단계는 앱과 같은 객체 · 같은 선언(`EngineFunctionStep`)으로 넣는다 — 본문이 다른 것만 여기서 준다.
        sw::EngineInitSequence& startup = state._startup;
        {
            // 앱과 달리 외부 코덱을 붙이지 않는다(테스트가 코덱을 직접 갈아 끼운다). 서비스와 **같은 인스턴스**를 Core 슬롯에도 꽂는다 —
            // 안 꽂으면 CompressionStream 이 다른 레지스트리를 보게 되어 등록한 코덱이 테스트에서만 조용히 무시된다.
            sw::CompressionCodecRegistry* const pCodecRegistry        = state._owned._pCompressionCodecRegistry.get();
            auto                                initializeCompression = [pCodecRegistry]()
            {
                pCodecRegistry->initialize();
                sw::CompressionCodecRegistry::setActive( pCodecRegistry );
                return sw::EngineInitResult::Succeeded;
            };
            startup.addSubsystem( sw::make_unique<sw::FunctionSubsystem>( sw::EngineFunctionStep::kCompression,
                                                                          SW_DELEGATE_LAMBDA( sw::SubsystemInitializeDelegate, initializeCompression ) ) )
                .takeOwnership( std::move( state._owned._pCompressionCodecRegistry ) );
        }
        {
            // 하네스의 타입 공급자(엔진 · GameFramework · 테스트 타입)는 테스트 실행 파일과 함께 올라와 여기서 모두 모은다.
            sw::TypeRegistry* const pTypeRegistry        = state._owned._pTypeRegistry.get();
            auto                    initializeReflection = [pTypeRegistry]()
            {
                sw::engine::registerModuleTypes( "Engine" );
                sw::engine::registerModuleTypes( "GameFramework" );
                pTypeRegistry->registerPendingTypes( "TestFramework", sw::TypeRegistrar::getHead(), sw::EnumRegistrar::getHead() );
                if ( sw::engine::bindGlobalVariableEnumNames() )
                    return sw::EngineInitResult::Succeeded;
                sw::engine::unbindGlobalVariableEnumNames();
                return sw::EngineInitResult::Failed;
            };
            startup.addSubsystem( sw::make_unique<sw::FunctionSubsystem>( sw::EngineFunctionStep::kReflection,
                                                                          SW_DELEGATE_LAMBDA( sw::SubsystemInitializeDelegate, initializeReflection ),
                                                                          SW_DELEGATE_FUNCTION( sw::SubsystemShutdownDelegate, &sw::engine::unbindGlobalVariableEnumNames ) ) )
                .takeOwnership( std::move( state._owned._pTypeRegistry ) );
        }
        {
            sw::TaskManager* const pTaskManager   = state._owned._pTaskManager.get();
            auto                   initializeTask = [pTaskManager]()
            { return pTaskManager->initialize() ? sw::EngineInitResult::Succeeded : sw::EngineInitResult::Failed; };
            startup.addSubsystem( sw::make_unique<sw::FunctionSubsystem>( sw::EngineFunctionStep::kTask, SW_DELEGATE_LAMBDA( sw::SubsystemInitializeDelegate, initializeTask ),
                                                                          SW_DELEGATE_METHOD( sw::SubsystemShutdownDelegate, &sw::TaskManager::shutdown, pTaskManager ) ) )
                .takeOwnership( std::move( state._owned._pTaskManager ) );
        }
        {
            sw::AsyncFileIO* const pAsyncFileIO     = state._owned._pAsyncFileIO.get();
            auto                   initializeFileIO = [pAsyncFileIO]()
            {
                sw::AsyncFileIOSettings settings{};
                settings._pTaskManager = &sw::engine::getTaskManager();
                return pAsyncFileIO->initialize( settings ) ? sw::EngineInitResult::Succeeded : sw::EngineInitResult::Failed;
            };
            startup.addSubsystem( sw::make_unique<sw::FunctionSubsystem>( sw::EngineFunctionStep::kFileIO, SW_DELEGATE_LAMBDA( sw::SubsystemInitializeDelegate, initializeFileIO ),
                                                                          SW_DELEGATE_METHOD( sw::SubsystemShutdownDelegate, &sw::AsyncFileIO::shutdown, pAsyncFileIO ) ) );
        }
        // 하네스는 모듈 DLL 을 올리고 내리지 않는다 — 자리만 둔다(다른 단계가 이 이름에 의존한다).
        startup.addSubsystem( sw::make_unique<sw::FunctionSubsystem>( sw::EngineFunctionStep::kModuleImages ) );
        {
            // 타입 공급자는 리플렉션 단계가 이미 모았다. 앱과 같이 이 단계가 끝나야 씬을 읽는다(`SceneManager::requestLoadFuture` · `SceneCooker::cookAllScenes`).
            auto markModuleTypes = []()
            {
                sw::engine::getTypeRegistry().markAllModuleTypesRegistered();
                return sw::EngineInitResult::Succeeded;
            };
            startup.addSubsystem( sw::make_unique<sw::FunctionSubsystem>( sw::EngineFunctionStep::kModuleTypes, SW_DELEGATE_LAMBDA( sw::SubsystemInitializeDelegate, markModuleTypes ) ) );
        }
        {
            sw::InputManager* const pInputManager   = state._owned._pInputManager.get();
            auto                    initializeInput = [pInputManager]()
            { return pInputManager->initialize() ? sw::EngineInitResult::Succeeded : sw::EngineInitResult::Failed; };
            startup.addSubsystem( sw::make_unique<sw::FunctionSubsystem>( sw::EngineFunctionStep::kInput, SW_DELEGATE_LAMBDA( sw::SubsystemInitializeDelegate, initializeInput ),
                                                                          SW_DELEGATE_METHOD( sw::SubsystemShutdownDelegate, &sw::InputManager::shutdown, pInputManager ) ) )
                .takeOwnership( std::move( state._owned._pInputManager ) );
        }
        startup.addSubsystem( std::move( state._owned._pShaderCache ) );
        startup.addSubsystem( std::move( state._owned._pPhysicsSystem ) );
        return startup.initializeAll<TestHost>( host );
    }

    void TestHostRuntime::stop()
    {
        if ( _pState == nullptr )
            return;
        // 2) 종료 — 단계 종료 · 해제(표의 역순), 그 뒤 부트스트랩(`EngineLoop::shutdown` 과 같은 끝 정리). 기동이 중간에 졌어도 같은 길이다.
        if ( _pState->_bBootstrapped )
        {
            // 단계 앞마다 한 줄 — 종료에서 멈춘 실행(요약은 찍혔는데 마지막 `shut down` 줄이 없다)이 어느 단계에서 멈췄는지 로그에 남는다.
            // 로그 매크로는 Shipping 에서 빠지므로 표준 출력으로 찍는다.
            std::fprintf( stdout, "[TestHost] stopping: startup steps\n" );
            std::fflush( stdout );
            _pState->_startup.shutdownAll();
            std::fprintf( stdout, "[TestHost] stopping: destroying objects\n" );
            std::fflush( stdout );
            _pState->_startup.destroyAll();
            std::fprintf( stdout, "[TestHost] stopping: bootstrap\n" );
            std::fflush( stdout );
            _pState->_bootstrap.shutdown();
        }
        _pState.reset();
    }
} // namespace test
