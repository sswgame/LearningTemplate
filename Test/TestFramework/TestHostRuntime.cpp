#include "pch.h"

#include "TestFramework/TestHostRuntime.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Event/EventDispatcher.h"
#include "Core/File/AsyncFileIo.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/String/StringUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Audio/IAudioSystem.h"
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
#include "Engine/Physics/PhysicsSystem.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/AssetStreamingQueue.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Serialization/Format/JsonSerializer.h"
#include "Engine/Utility/CommandStack.h"
#include "Engine/Utility/DebugOverlayState.h"
#include "Engine/Utility/Profiling/FrameProfiler.h"

#include "GameFramework/Base/Framework/GameService.h"

#include "TestFramework/TestFramework.h"

#include "sw/config/ConfigConstants.h"
#include "sw/config/ShippingHostDefaults.h"

namespace
{
    /**
     * @brief 하네스의 기동 단계 본문입니다. 순서는 `EngineLoop` 과 같은 표(`EngineInitStepList.xxx`)가 정합니다.
     * @details 줄마다 `<단계>StartupStep` 하나다(`EngineInitStepDefaults` 상속 — 필요한 것만 정의). 하네스는 창 · RHI · 렌더러 · 헤드리스
     *          작업을 세우지 않으므로 그 단계는 기본값이고, 오디오는 초기화하지 않고 종료만 부른다. 해제는 `EngineLoop` 과 같은 역순이다.
     */
    struct TestHost
    {
        sw::EngineServiceCollection*      _pOwned{ nullptr };
        sw::unique_ptr<sw::ConfigManager> _configManager{};
        sw::unique_ptr<sw::IAudioSystem>  _audioSystem{};
        sw::unique_ptr<sw::CommandStack>  _commandStack{};
        const sw::EngineConfig*           _pEngineConfig{ nullptr };

        using Defaults = sw::EngineInitStepDefaults<TestHost>;

        struct CompressionStartupStep : Defaults
        {
            static sw::EngineInitResult initialize( TestHost& host )
            {
                host._pOwned->_pCompressionCodecRegistry->initialize();
                // 서비스와 **같은 인스턴스**를 Core 슬롯에도 꽂는다 — 안 꽂으면 CompressionStream 이
                // 다른 레지스트리를 보게 되어 등록한 코덱이 테스트에서만 조용히 무시된다.
                sw::CompressionCodecRegistry::setActive( host._pOwned->_pCompressionCodecRegistry.get() );
                return sw::EngineInitResult::Succeeded;
            }
            // `EngineLoop` 과 같이 종료는 하지 않는다 — Core 슬롯이 이 레지스트리를 가리키는 동안 코덱을 비우면 뒤 단계의 해제(팩 내리기)가
            // 빈 레지스트리를 본다. 슬롯을 끊고 통째로 없애는 것은 해제다.
            static void destroy( TestHost& host ) { host._pOwned->destroyCompressionCodecRegistry(); }
        };

        struct ReflectionStartupStep : Defaults
        {
            static sw::EngineInitResult initialize( TestHost& host )
            {
                // 리플렉션은 설정보다 먼저다 — 설정(EngineConfig·GameConfig) 역직렬화가 TypeInfo 를 쓴다.
                sw::engine::registerModuleTypes( "Engine" );
                sw::engine::registerModuleTypes( "GameFramework" );
                host._pOwned->_pTypeRegistry->registerPendingTypes( "TestFramework", sw::TypeRegistrar::getHead(), sw::EnumRegistrar::getHead() );
                if ( sw::engine::bindGlobalVariableEnumNames() == false )
                    return sw::EngineInitResult::Failed;
                return sw::EngineInitResult::Succeeded;
            }
            static void destroy( TestHost& host )
            {
                sw::engine::unbindGlobalVariableEnumNames();
                host._pOwned->_pTypeRegistry.reset();
            }
        };

        struct ConfigStartupStep : Defaults
        {
            static sw::EngineInitResult initialize( TestHost& host )
            {
                // 설정은 리소스 초기화보다 먼저 읽는다. `loadAssetRegistries` 는 `GameConfig::getActive()._packRoot` 로 게임
                // 레지스트리 경로를 만든다 — 활성 설정이 없으면 시작 시점 GUID 표가 반쪽이 된다.
                host._configManager = sw::make_unique<sw::ConfigManager>();
                host._configManager->setRootDirectory( sw::ResourceUtil::getProjectFolderPath() );
#if defined( SW_SHIPPING )
                // 배포 구성은 생성된 JSON 만 읽는다. 그것이 역직렬화되지 않으면(정적 링크에서 리플렉션 등록기가 빠졌다) 모든 시험이 C++ 기본값
                // 설정으로 돈다 — 기동 실패로 드러낸다(`sw_addTestExecutable` 의 통째 링크).
                {
                    sw::EngineConfig probe;
                    if ( sw::JsonSerializer::deserialize( &probe, *sw::EngineConfig::StaticType(), sw::shipping_host::kEngineConfigJson ) == false )
                    {
                        SW_LOG_ERROR( "The generated EngineConfig JSON does not deserialize in this test executable - a reflection registrar was dropped at link time" );
                        return sw::EngineInitResult::Failed;
                    }
                }
#endif
                host._pEngineConfig = host._configManager->ensureConfig<sw::EngineConfig>( sw::config::kFileRuntimeEngineConfig, sw::shipping_host::kEngineConfigJson );
                if ( host._pEngineConfig == nullptr )
                    return sw::EngineInitResult::Failed;
                sw::EngineConfig::setActive( *host._pEngineConfig );
                const sw::GameConfig* pGameConfig = host._configManager->ensureConfig<sw::GameConfig>( sw::config::kFileRuntimeGameConfig, sw::shipping_host::kGameConfigJson );
                if ( pGameConfig == nullptr )
                    return sw::EngineInitResult::Failed;
                sw::GameConfig::setActive( *pGameConfig );
                return sw::EngineInitResult::Succeeded;
            }
            static void destroy( TestHost& host )
            {
                host._pEngineConfig = nullptr;
                host._configManager.reset();
            }
        };

        struct ResourceStartupStep : Defaults
        {
            static sw::EngineInitResult initialize( TestHost& host )
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
            static void destroy( TestHost& host ) { host._pOwned->destroyAssetManager(); }
        };

        struct EngineDefaultAssetsStartupStep : Defaults
        {
            static void destroy( TestHost& host ) { host._pOwned->_pEngineDefaultAssets.reset(); }
        };

        struct ShaderCacheStartupStep : Defaults
        {
            static sw::EngineInitResult initialize( TestHost& host )
            {
                return host._pOwned->_pShaderCache->initialize() ? sw::EngineInitResult::Succeeded : sw::EngineInitResult::Failed;
            }
            static void shutdown( TestHost& host ) { host._pOwned->_pShaderCache->shutdown(); }
            static void destroy( TestHost& host ) { host._pOwned->_pShaderCache.reset(); }
        };

        struct TaskStartupStep : Defaults
        {
            static sw::EngineInitResult initialize( TestHost& host )
            {
                return host._pOwned->_pTaskManager->initialize() ? sw::EngineInitResult::Succeeded : sw::EngineInitResult::Failed;
            }
            static void shutdown( TestHost& host ) { host._pOwned->_pTaskManager->shutdown(); }
            static void destroy( TestHost& host ) { host._pOwned->_pTaskManager.reset(); }
        };

        struct FileIoStartupStep : Defaults
        {
            static sw::EngineInitResult initialize( TestHost& host )
            {
                sw::AsyncFileIoSettings settings{};
                settings._pTaskManager = host._pOwned->_pTaskManager.get();
                return host._pOwned->_pAsyncFileIo->initialize( settings ) ? sw::EngineInitResult::Succeeded : sw::EngineInitResult::Failed;
            }
            static void shutdown( TestHost& host ) { host._pOwned->_pAsyncFileIo->shutdown(); }
        };

        struct AudioStartupStep : Defaults
        {
            static void shutdown( TestHost& host ) { host._audioSystem->shutdown(); }
            static void destroy( TestHost& host ) { host._audioSystem.reset(); }
        };

        struct InputStartupStep : Defaults
        {
            static sw::EngineInitResult initialize( TestHost& host )
            {
                return host._pOwned->_pInputManager->initialize() ? sw::EngineInitResult::Succeeded : sw::EngineInitResult::Failed;
            }
            static void shutdown( TestHost& host ) { host._pOwned->_pInputManager->shutdown(); }
            static void destroy( TestHost& host ) { host._pOwned->_pInputManager.reset(); }
        };

        struct ModuleTypesStartupStep : Defaults
        {
            static sw::EngineInitResult initialize( TestHost& host )
            {
                // 하네스의 타입 공급자(엔진 · GameFramework · 시험 타입)는 시험 실행 파일과 함께 올라와 리플렉션 단계가 이미 모았다.
                // 앱과 같이 이 단계가 끝나야 씬을 읽는다(`SceneManager::requestLoadFuture` · `SceneCooker::cookAllScenes`).
                host._pOwned->_pTypeRegistry->markAllModuleTypesRegistered();
                return sw::EngineInitResult::Succeeded;
            }
        };

        struct PhysicsStartupStep : Defaults
        {
            static sw::EngineInitResult initialize( TestHost& host )
            {
                return host._pOwned->_pPhysicsSystem->initialize() ? sw::EngineInitResult::Succeeded : sw::EngineInitResult::Failed;
            }
            static void shutdown( TestHost& host ) { host._pOwned->_pPhysicsSystem->shutdown(); }
        };

        struct SceneStartupStep : Defaults
        {
            static sw::EngineInitResult initialize( TestHost& host )
            {
                return host._pOwned->_pSceneManager->initialize() ? sw::EngineInitResult::Succeeded : sw::EngineInitResult::Failed;
            }
            static void shutdown( TestHost& host ) { host._pOwned->_pSceneManager->shutdown(); }
            static void destroy( TestHost& host )
            {
                host._pOwned->_pSceneManager.reset();
                // 소멸자가 태스크를 기다린다 — Task 해제보다 먼저다.
                host._pOwned->_pAssetStreamingQueue.reset();
                host._commandStack.reset();
            }
        };

        // 하네스가 세우지 않는 단계다(모듈 이미지 · 헤드리스 작업 · 창 · RHI · 렌더러).
        using ModuleImagesStartupStep  = Defaults;
        using HeadlessStartupStep      = Defaults;
        using FontsStartupStep         = Defaults; // 글꼴 시험은 자기 FontSystem 을 만든다(가짜 래스터라이저 · 시험 카탈로그)
        using UserSettingsStartupStep  = Defaults;
        using RHIStartupStep           = Defaults;
        using FrameRendererStartupStep = Defaults;
        using RenderThreadStartupStep  = Defaults;
        using LiveShaderStartupStep    = Defaults;
        using SceneRhiStartupStep      = Defaults;
        using TelemetryStartupStep     = Defaults;
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

    TestHostRuntime::~TestHostRuntime() { stop(); }

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
        // 목록(`EngineServiceList.xxx`)의 `EngineCreated` 서비스는 저장소가 만든다. 시험 실행 파일은 진단 도구(교착 감지기 · 메모리 프로파일러)를 늘 켠다.
        if ( state._bootstrap.initialize( state._owned, true ) == false )
            return false;
        state._bBootstrapped = true;
        // 프레임워크 전용 플래그를 먼저 소비해 CommandLineManager 가 미지 인자를 경고하지 않게 한다.
        sw::vector<utf8*> listApplicationArg = TestRegistry::getInstance().configureFromArgs( argc, argv );
        state._bootstrap.parseCommandLine( static_cast<int32>( listApplicationArg.size() ), listApplicationArg.data() );

        state._owned.createAll();
        host._audioSystem  = sw::IAudioSystem::create();
        host._commandStack = sw::make_unique<sw::CommandStack>();

        sw::EngineServices services{};
        state._bootstrap.fillServices( services );
        // HostCreated 인 자리만 손으로 꽂는다(팩토리 · 구성별 조건부).
        services._pAudioSystem  = host._audioSystem.get();
        services._pCommandStack = host._commandStack.get();
        sw::engine::bindEngineServices( services );

        // ------------------------------------------------------------------------------
        // 1) 기동 단계 — 리플렉션 · 설정 · 리소스 · 태스크 · 입력 · 씬
        // ------------------------------------------------------------------------------
        // 순서는 손으로 적지 않는다. `EngineLoop` 과 같은 표(`EngineInitStepList.xxx`)를 위상 정렬한 순서로 단계 구조체(`TestHost::<단계>StartupStep`)를
        // 부른다 — 하네스가 앱과 다른 순서로 서는 일이 구조로 막힌다. 종료와 해제는 `stop` 이 그 역순으로 한다.
        return state._startup.initializeAll( host );
    }

    void TestHostRuntime::stop()
    {
        if ( _pState == nullptr )
            return;
        // 2) 종료 — 단계 종료 · 해제(표의 역순), 그 뒤 부트스트랩(`EngineLoop::shutdown` 과 같은 끝 정리). 기동이 중간에 졌어도 같은 길이다.
        if ( _pState->_bBootstrapped )
        {
            _pState->_startup.shutdownAll();
            _pState->_startup.destroyAll();
            _pState->_bootstrap.shutdown();
        }
        _pState.reset();
    }
} // namespace test
