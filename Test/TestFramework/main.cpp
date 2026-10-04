#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Event/EventDispatcher.h"
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
#include "Engine/Graphics/RHI/RHIBackendRegistry.h"
#include "Engine/Graphics/Renderer/Debug/DebugDrawQueue.h"
#include "Engine/Graphics/Renderer/Debug/RenderTargetRegistry.h"
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
#include "Engine/Utility/CommandStack.h"
#include "Engine/Utility/Debug/DebugOverlayState.h"
#include "Engine/Utility/Debug/FrameProfiler.h"

#include "GameFramework/Framework/GameService.h"

#include "TestFramework/TestFramework.h"

#include "sw/config/ConfigConstants.h"
#include "sw/config/ShippingHostDefaults.h"

namespace test
{
    void rebindEngineServices( const sw::EngineServices& services )
    {
        // 이 파일이 호스트다(위의 main 이 표를 채운다). 테스트는 이 창구로만 표를 갈아 끼운다.
        sw::engine::bindEngineServices( services );
    }
} // namespace test

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
                host._pEngineConfig               = host._configManager->ensureConfig<sw::EngineConfig>( sw::config::kFileRuntimeEngineConfig, sw::shipping_host::kEngineConfigJson );
                const sw::GameConfig* pGameConfig = host._configManager->ensureConfig<sw::GameConfig>( sw::config::kFileRuntimeGameConfig, sw::shipping_host::kGameConfigJson );
                if ( pGameConfig != nullptr )
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
        using RHIStartupStep           = Defaults;
        using FrameRendererStartupStep = Defaults;
        using RenderThreadStartupStep  = Defaults;
        using LiveShaderStartupStep    = Defaults;
        using SceneRhiStartupStep      = Defaults;
    };
} // namespace

int main( int32 argc, utf8* argv[] )
{
    // ------------------------------------------------------------------------------
    // 0) 부트스트랩 — `EngineLoop` 과 같은 것(이름 풀 · 로거 · 크래시 핸들러 · 리소스 루트 · 진단 도구 · 명령줄 · 전역 변수)
    // ------------------------------------------------------------------------------
    // 목록(`EngineServiceList.xxx`)의 `EngineCreated` 서비스는 저장소가 만든다. 선언 순서가 소멸 순서의 역이다:
    // 저장소 → 부트스트랩 → 호스트(단계가 소유하는 하네스 몫) 순으로 두어, 어디서 돌아가도 호스트 · 부트스트랩 · 저장소 순으로 사라진다.
    sw::EngineServiceCollection owned;
    sw::EngineBootstrap         bootstrap;
    TestHost                    host{};
    host._pOwned = &owned;

    // 시험 실행 파일은 진단 도구(교착 감지기 · 메모리 프로파일러)를 늘 켠다.
    if ( bootstrap.initialize( owned, true ) == false )
        return -1;
    // 프레임워크 전용 플래그를 먼저 소비해 CommandLineManager 가 미지 인자를 경고하지 않게 한다.
    sw::vector<utf8*> listApplicationArg = test::TestRegistry::getInstance().configureFromArgs( argc, argv );
    bootstrap.parseCommandLine( static_cast<int32>( listApplicationArg.size() ), listApplicationArg.data() );

    owned.createAll();
    host._audioSystem  = sw::IAudioSystem::create();
    host._commandStack = sw::make_unique<sw::CommandStack>();

    sw::EngineServices services{};
    bootstrap.fillServices( services );
    // HostCreated 인 자리만 손으로 꽂는다(팩토리 · 구성별 조건부).
    services._pAudioSystem  = host._audioSystem.get();
    services._pCommandStack = host._commandStack.get();
    sw::engine::bindEngineServices( services );

    // ------------------------------------------------------------------------------
    // 1) 기동 단계 — 리플렉션 · 설정 · 리소스 · 태스크 · 입력 · 씬
    // ------------------------------------------------------------------------------
    // 순서는 손으로 적지 않는다. `EngineLoop` 과 같은 표(`EngineInitStepList.xxx`)를 위상 정렬한 순서로 단계 구조체(`TestHost::<단계>StartupStep`)를
    // 부른다 — 하네스가 앱과 다른 순서로 서는 일이 구조로 막힌다. 종료와 해제는 아래 `destroyAll` 이 그 역순으로 한다.
    sw::EngineInitSequence startup;
    int32                  result = -1;
    if ( startup.initializeAll( host ) )
    {
        SW_LOG_INFO( "Core services initialized. Running tests..." );
        SW_LOG_INFO( " Tip: --test_filter=Suite.*  --test_filter=-RHITest.*  --test_list" );
        result = test::TestRegistry::getInstance().runAllTests();
    }

    // ------------------------------------------------------------------------------
    // 2) 종료 — 단계 종료 · 해제(표의 역순), 그 뒤 부트스트랩(`EngineLoop::shutdown` 과 같은 끝 정리)
    // ------------------------------------------------------------------------------
    startup.shutdownAll();
    startup.destroyAll();
    bootstrap.shutdown();
    return result;
}
