#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Concurrency/DeadlockDetector.h"
#include "Core/Event/EventDispatcher.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Memory/MemoryProfiler.h"
#include "Core/Process/CrashHandler.h"
#include "Core/String/StringUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Audio/IAudioSystem.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/EngineData.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/EngineOwnedServices.h"
#include "Engine/EngineStartupSequence.h"
#include "Engine/Graphics/RHI/RHIBackendRegistry.h"
#include "Engine/Graphics/Renderer/Debug/DebugDrawQueue.h"
#include "Engine/Graphics/Renderer/Debug/RenderTargetRegistry.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/StringTable.h"
#include "Engine/Module/ModuleTypeRegistry.h"
#include "Engine/Object/Component/ComponentDefaults.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Resource/AssetStreamingQueue.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Utility/CommandStack.h"
#include "Engine/Utility/Debug/DebugOverlayState.h"
#include "Engine/Utility/Debug/FrameProfiler.h"

#include "GameFramework/Base/GameService.h"

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
     * @brief 하네스의 기동 단계 본문입니다. 순서는 `EngineLoop` 과 같은 표(`EngineStartupStepList.xxx`)가 정합니다.
     * @details 줄마다 `<단계>StartupStep` 하나다(`EngineStartupStepDefaults` 상속 — 필요한 것만 정의). 하네스는 창 · RHI · 렌더러 · 헤드리스
     *          작업을 세우지 않으므로 그 단계는 기본값이고, 오디오는 초기화하지 않고 종료만 부른다. 해제는 `EngineLoop` 과 같은 역순이다.
     */
    struct TestHost
    {
        sw::EngineOwnedServices*          _pOwned{ nullptr };
        sw::unique_ptr<sw::ConfigManager> _configManager{};
        sw::unique_ptr<sw::IAudioSystem>  _audioSystem{};
        sw::unique_ptr<sw::CommandStack>  _commandStack{};
        const sw::EngineConfig*           _pEngineConfig{ nullptr };

        using Defaults = sw::EngineStartupStepDefaults<TestHost>;

        struct CompressionStartupStep : Defaults
        {
            static sw::EngineStartupResult initialize( TestHost& host )
            {
                host._pOwned->_pCompressionCodecRegistry->initialize();
                // 서비스와 **같은 인스턴스**를 Core 슬롯에도 꽂는다 — 안 꽂으면 CompressionStream 이
                // 다른 레지스트리를 보게 되어 등록한 코덱이 테스트에서만 조용히 무시된다.
                sw::CompressionCodecRegistry::setActive( host._pOwned->_pCompressionCodecRegistry.get() );
                return sw::EngineStartupResult::Succeeded;
            }
            static void shutdown( TestHost& host ) { host._pOwned->_pCompressionCodecRegistry->shutdown(); }
            static void destroy( TestHost& host ) { host._pOwned->destroyCompressionCodecRegistry(); }
        };

        struct ReflectionStartupStep : Defaults
        {
            static sw::EngineStartupResult initialize( TestHost& host )
            {
                // 리플렉션은 설정보다 먼저다 — 설정(EngineConfig·GameConfig) 역직렬화가 TypeInfo 를 쓴다.
                sw::engine::registerModuleTypes( "Engine" );
                sw::engine::registerModuleTypes( "GameFramework" );
                host._pOwned->_pTypeRegistry->registerPendingTypes( "TestFramework", sw::TypeRegistrar::getHead(), sw::EnumRegistrar::getHead() );
                return sw::EngineStartupResult::Succeeded;
            }
            static void destroy( TestHost& host ) { host._pOwned->_pTypeRegistry.reset(); }
        };

        struct ConfigStartupStep : Defaults
        {
            static sw::EngineStartupResult initialize( TestHost& host )
            {
                // 설정은 리소스 초기화보다 먼저 읽는다. `loadAssetRegistries` 는 `GameConfig::getActive()._packRoot` 로 게임
                // 레지스트리 경로를 만든다 — 활성 설정이 없으면 시작 시점 GUID 표가 반쪽이 된다.
                host._configManager = sw::make_unique<sw::ConfigManager>();
                host._configManager->setRootDirectory( sw::ResourceUtil::getProjectFolderPath() );
                host._pEngineConfig               = host._configManager->ensureConfig<sw::EngineConfig>( sw::config::kFileRuntimeEngineConfig, sw::shipping_host::kEngineConfigJson );
                const sw::GameConfig* pGameConfig = host._configManager->ensureConfig<sw::GameConfig>( sw::config::kFileRuntimeGameConfig, sw::shipping_host::kGameConfigJson );
                if ( pGameConfig != nullptr )
                    sw::GameConfig::setActive( *pGameConfig );
                return sw::EngineStartupResult::Succeeded;
            }
            static void destroy( TestHost& host )
            {
                host._pEngineConfig = nullptr;
                host._configManager.reset();
            }
        };

        struct ResourceStartupStep : Defaults
        {
            static sw::EngineStartupResult initialize( TestHost& host )
            {
                if ( host._pOwned->_pResourceManager->initialize() == false )
                    return sw::EngineStartupResult::Failed;
                // GameConfig 가 활성화된 뒤라야 "game" 토큰이 팩 루트로 풀린다 — 그 전제는 `mountContent` 의 인자에 드러나 있다.
                if ( host._pEngineConfig != nullptr )
                    host._pOwned->_pResourceManager->mountContent( host._pEngineConfig->_listResourcePriority );
                else
                    host._pOwned->_pResourceManager->mountContent( {} );
                return sw::EngineStartupResult::Succeeded;
            }
            static void destroy( TestHost& host ) { host._pOwned->destroyResourceManager(); }
        };

        struct EngineDataStartupStep : Defaults
        {
            static void destroy( TestHost& host ) { host._pOwned->_pEngineData.reset(); }
        };

        struct ShaderCacheStartupStep : Defaults
        {
            static sw::EngineStartupResult initialize( TestHost& host )
            {
                return host._pOwned->_pShaderCache->initialize() ? sw::EngineStartupResult::Succeeded : sw::EngineStartupResult::Failed;
            }
            static void shutdown( TestHost& host ) { host._pOwned->_pShaderCache->shutdown(); }
            static void destroy( TestHost& host ) { host._pOwned->_pShaderCache.reset(); }
        };

        struct TaskStartupStep : Defaults
        {
            static sw::EngineStartupResult initialize( TestHost& host )
            {
                return host._pOwned->_pTaskManager->initialize() ? sw::EngineStartupResult::Succeeded : sw::EngineStartupResult::Failed;
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
            static sw::EngineStartupResult initialize( TestHost& host )
            {
                return host._pOwned->_pInputManager->initialize() ? sw::EngineStartupResult::Succeeded : sw::EngineStartupResult::Failed;
            }
            static void shutdown( TestHost& host ) { host._pOwned->_pInputManager->shutdown(); }
            static void destroy( TestHost& host ) { host._pOwned->_pInputManager.reset(); }
        };

        struct SceneStartupStep : Defaults
        {
            static sw::EngineStartupResult initialize( TestHost& host )
            {
                return host._pOwned->_pSceneManager->initialize() ? sw::EngineStartupResult::Succeeded : sw::EngineStartupResult::Failed;
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
    sw::HashedStringPool::initialize();

    // ------------------------------------------------------------------------------
    // 0) 코어 매니저 — 로거·프로파일러·커맨드라인·엔진 서비스
    // ------------------------------------------------------------------------------
    // 목록(`EngineServiceList.xxx`)의 `owned=1` 서비스는 이 저장소가 만든다 — 하네스가 스무 줄을
    // 따로 적던 자리다. 목록에 줄을 더하면 여기도 같이 자란다(엔진 호스트와 같은 기계).
    sw::EngineOwnedServices owned;
    owned.createAll();

    // 단계가 소유하는 하네스 몫(설정 · 오디오 · 커맨드 스택)은 호스트가 든다 — 해제가 같은 표의 역순이다.
    TestHost host{};
    host._pOwned       = &owned;
    host._audioSystem  = sw::IAudioSystem::create();
    host._commandStack = sw::make_unique<sw::CommandStack>();

    sw::unique_ptr<sw::Logger>           logger           = sw::make_unique<sw::Logger>();
    sw::unique_ptr<sw::DeadlockDetector> deadlockDetector = sw::make_unique<sw::DeadlockDetector>();
    sw::unique_ptr<sw::MemoryProfiler>   memoryProfiler   = sw::make_unique<sw::MemoryProfiler>();
    logger->initialize();
    // 로거 직후에 설치해야 이후 어디서 죽든 콜 스택이 남는다(EngineLoop 과 같은 자리). 예전에는 테스트 실행 파일에
    // 핸들러가 없어서 간헐 세그폴트가 "SEGFAULT" 한 단어로만 남았다 — 세 번을 보고도 자리를 몰랐다.
    sw::CrashHandler::initialize();
    // 리소스 루트는 로거 다음에 찾는다(EngineLoop 과 같은 순서) — 실패했을 때의 진단이 남아야 하고,
    // 아래 `configManager->setRootDirectory` 가 여기서 정해지는 프로젝트 루트를 바로 쓴다.
    // 예전에는 `owned._pResourceManager->initialize()` 가 대신 불러 줬는데, 그건 설정보다 뒤였다.
    if ( sw::ResourceUtil::initialize() == false )
    {
        SW_LOG_ERROR( "리소스 루트를 찾지 못했습니다 — Resource/ 가 있는 위치에서 실행하십시오." );
        return -1;
    }
    deadlockDetector->initialize();
    memoryProfiler->initialize();
    owned._pCommandLineManager->initialize();
    owned._pGlobalVariableManager->registerPendingVariables( "Engine", sw::GlobalVariableRegistrar::getHead() );
    sw::GlobalVariableRegistrar::getHead() = nullptr;
    owned._pGlobalVariableManager->registerToCommandLine( owned._pCommandLineManager.get() );

    // 프레임워크 전용 플래그를 먼저 소비해 CommandLineManager 가 미지 인자를 경고하지 않게 한다.
    sw::vector<utf8*> listApplicationArg = test::TestRegistry::getInstance().configureFromArgs( argc, argv );
    owned._pCommandLineManager->parse( static_cast<int32>( listApplicationArg.size() ), listApplicationArg.data() );
    owned._pGlobalVariableManager->updateFromCommandLine( owned._pCommandLineManager.get() );

    sw::EngineServices services{};
    owned.bindInto( services );
    // owned=0 인 자리만 손으로 꽂는다(팩토리 · 구성별 조건부).
    services._pAudioSystem    = host._audioSystem.get();
    services._pMemoryProfiler = memoryProfiler.get();
    services._pCommandStack   = host._commandStack.get();
    sw::engine::bindEngineServices( services );

    // ------------------------------------------------------------------------------
    // 1) 기동 단계 — 리플렉션 · 설정 · 리소스 · 태스크 · 입력 · 씬
    // ------------------------------------------------------------------------------
    // 순서는 손으로 적지 않는다. `EngineLoop` 과 같은 표(`EngineStartupStepList.xxx`)를 위상 정렬한 순서로 단계 구조체(`TestHost::<단계>StartupStep`)를
    // 부른다 — 하네스가 앱과 다른 순서로 서는 일이 구조로 막힌다. 종료와 해제는 아래 `shutdownAll` · `destroyAll` 이 그 역순으로 한다.
    sw::EngineStartupSequence startup;
    if ( startup.initializeAll( host ) == false )
        return -1;

    SW_LOG_INFO( "Core services initialized. Running tests..." );
    SW_LOG_INFO( " Tip: --test_filter=Suite.*  --test_filter=-RHITest.*  --test_list" );
    int32 result = test::TestRegistry::getInstance().runAllTests();

    // ------------------------------------------------------------------------------
    // 2) 종료 — 단계 종료 · 해제(표의 역순), 그 뒤 표 밖 부트스트랩(`EngineLoop::shutdownBootstrap` 과 같은 순서)
    // ------------------------------------------------------------------------------
    startup.shutdownAll();
    startup.destroyAll();

    owned._pGlobalVariableManager->shutdown();
    // 단계에 속하지 않는 서비스를 목록의 역순으로 놓는다.
    owned.destroyAll();
    sw::engine::unbindEngineServices();

    // 로거 스레드를 **먼저** 세운다. 그 스레드도 메모리를 풀며 프로파일러를 부르기 때문이다(`Memory::free` → `recordFree`).
    logger->shutdown();
    memoryProfiler->shutdown();
    deadlockDetector->shutdown();
    memoryProfiler.reset();
    deadlockDetector.reset();

    sw::HashedStringPool::shutdown();
    sw::CrashHandler::shutdown();
    logger.reset();

    return result;
}
