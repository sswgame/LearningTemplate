#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Concurrency/DeadlockDetector.h"
#include "Core/Event/EventDispatcher.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Memory/FrameArenaAllocator.h"
#include "Core/Memory/MemoryProfiler.h"
#include "Core/String/StringUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Audio/IAudioSystem.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/EngineData.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Graphics/RHI/RHIBackendRegistry.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/StringTable.h"
#include "Engine/Module/LiveReloadManager.h"
#include "Engine/Module/ModuleTypeRegistry.h"
#include "Engine/Module/ReloadFileManager.h"
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

int main( int32 argc, utf8* argv[] )
{
    sw::HashedStringPool::initialize();

    // ------------------------------------------------------------------------------
    // 0) 코어 매니저 — 로거·프로파일러·커맨드라인·엔진 서비스
    // ------------------------------------------------------------------------------
    sw::unique_ptr<sw::Logger>                   logger                   = sw::make_unique<sw::Logger>();
    sw::unique_ptr<sw::DeadlockDetector>         deadlockDetector         = sw::make_unique<sw::DeadlockDetector>();
    sw::unique_ptr<sw::MemoryProfiler>           memoryProfiler           = sw::make_unique<sw::MemoryProfiler>();
    sw::unique_ptr<sw::CommandLineManager>       commandLineManager       = sw::make_unique<sw::CommandLineManager>();
    sw::unique_ptr<sw::ConfigManager>            configManager            = sw::make_unique<sw::ConfigManager>();
    sw::unique_ptr<sw::TaskManager>              taskManager              = sw::make_unique<sw::TaskManager>();
    sw::unique_ptr<sw::GlobalVariableManager>    globalVarManager         = sw::make_unique<sw::GlobalVariableManager>();
    sw::unique_ptr<sw::TypeRegistry>             typeRegistry             = sw::make_unique<sw::TypeRegistry>();
    sw::unique_ptr<sw::LocalizationManager>      localizationManager      = sw::make_unique<sw::LocalizationManager>();
    sw::unique_ptr<sw::LiveReloadManager>        liveReloadManager        = sw::make_unique<sw::LiveReloadManager>();
    sw::unique_ptr<sw::ReloadFileManager>        reloadFileManager        = sw::make_unique<sw::ReloadFileManager>();
    sw::unique_ptr<sw::SceneManager>             sceneManager             = sw::make_unique<sw::SceneManager>();
    sw::unique_ptr<sw::InputManager>             inputManager             = sw::make_unique<sw::InputManager>();
    sw::unique_ptr<sw::CommandStack>             commandStack             = sw::make_unique<sw::CommandStack>();
    sw::unique_ptr<sw::RHIBackendRegistry>       rhiRegistry              = sw::make_unique<sw::RHIBackendRegistry>();
    sw::unique_ptr<sw::IAudioSystem>             audioSystem              = sw::IAudioSystem::create();
    sw::unique_ptr<sw::EventDispatcher>          eventDispatcher          = sw::make_unique<sw::EventDispatcher>();
    sw::unique_ptr<sw::ResourceManager>          resourceManager          = sw::make_unique<sw::ResourceManager>();
    sw::unique_ptr<sw::EngineData>               engineData               = sw::make_unique<sw::EngineData>();
    sw::unique_ptr<sw::AssetStreamingQueue>      assetStreamingQueue      = sw::make_unique<sw::AssetStreamingQueue>();
    sw::unique_ptr<sw::DebugOverlayState>        debugOverlayState        = sw::make_unique<sw::DebugOverlayState>();
    sw::unique_ptr<sw::DebugDrawQueue>           debugDrawQueue           = sw::make_unique<sw::DebugDrawQueue>();
    sw::unique_ptr<sw::FrameDoubleBuffer>        frameDoubleBuffer        = sw::make_unique<sw::FrameDoubleBuffer>();
    sw::unique_ptr<sw::CompressionCodecRegistry> compressionCodecRegistry = sw::make_unique<sw::CompressionCodecRegistry>();
    sw::unique_ptr<sw::ShaderCache>              shaderCache              = sw::make_unique<sw::ShaderCache>();
    sw::unique_ptr<sw::ComponentDefaults>        componentDefaults        = sw::make_unique<sw::ComponentDefaults>();
    sw::unique_ptr<sw::FrameProfiler>            frameProfiler            = sw::make_unique<sw::FrameProfiler>();

    logger->initialize();
    deadlockDetector->initialize();
    memoryProfiler->initialize();
    compressionCodecRegistry->initialize();
    // 서비스와 **같은 인스턴스**를 Core 슬롯에도 꽂는다 — 안 꽂으면 CompressionStream 이
    // 다른 레지스트리를 보게 되어 등록한 코덱이 테스트에서만 조용히 무시된다.
    sw::CompressionCodecRegistry::setActive( compressionCodecRegistry.get() );
    shaderCache->initialize();
    commandLineManager->initialize();
    globalVarManager->registerPendingVariables( "Engine", sw::GlobalVariableRegistrar::getHead() );
    sw::GlobalVariableRegistrar::getHead() = nullptr;
    globalVarManager->registerToCommandLine( commandLineManager.get() );

    // 프레임워크 전용 플래그를 먼저 소비해 CommandLineManager 가 미지 인자를 경고하지 않게 한다.
    sw::vector<utf8*> listApplicationArg = test::TestRegistry::getInstance().configureFromArgs( argc, argv );
    commandLineManager->parse( static_cast<int32>( listApplicationArg.size() ), listApplicationArg.data() );
    globalVarManager->updateFromCommandLine( commandLineManager.get() );

    sw::EngineServices services{};
    services._pCommandLineManager       = commandLineManager.get();
    services._pGlobalVariableManager    = globalVarManager.get();
    services._pLocalizationManager      = localizationManager.get();
    services._pTaskManager              = taskManager.get();
    services._pTypeRegistry             = typeRegistry.get();
    services._pCommandStack             = commandStack.get();
    services._pSceneManager             = sceneManager.get();
    services._pInputManager             = inputManager.get();
    services._pRHIBackendRegistry       = rhiRegistry.get();
    services._pAudioSystem              = audioSystem.get();
    services._pEventDispatcher          = eventDispatcher.get();
    services._pResourceManager          = resourceManager.get();
    services._pMemoryProfiler           = memoryProfiler.get();
    services._pEngineData               = engineData.get();
    services._pAssetStreamingQueue      = assetStreamingQueue.get();
    services._pDebugOverlayState        = debugOverlayState.get();
    services._pDebugDrawQueue           = debugDrawQueue.get();
    services._pFrameDoubleBuffer        = frameDoubleBuffer.get();
    services._pCompressionCodecRegistry = compressionCodecRegistry.get();
    services._pShaderCache              = shaderCache.get();
    services._pComponentDefaults        = componentDefaults.get();
    services._pFrameProfiler            = frameProfiler.get();
    sw::engine::bindEngineServices( services );

    // ------------------------------------------------------------------------------
    // 1) 부트스트랩 — 리소스·태스크·씬·입력, 리플렉션 등록
    // ------------------------------------------------------------------------------
    // 리플렉션은 리소스보다 먼저다 — 설정(EngineConfig·GameConfig) 역직렬화가 TypeInfo 를 쓴다.
    // EngineLoop 도 같은 순서다(registerModuleTypes → ResourceManager::initialize → 설정 로드).
    sw::engine::registerModuleTypes( "Engine" );
    sw::engine::registerModuleTypes( "GameFramework" );
    typeRegistry->registerPendingTypes( "TestFramework", sw::TypeRegistrar::getHead(), sw::EnumRegistrar::getHead() );

    if ( resourceManager->initialize() == false )
        return -1;

    // 설정을 앱과 같은 순서로 활성화한다. 이게 없으면 게임 도메인(`game/<pack>`)이 통째로 빠진다 —
    // `loadAssetRegistries` 가 `GameConfig::getActive()._packRoot` 로 게임 레지스트리 경로를 만드는데,
    // 활성 설정이 없으면 그 항목이 비어 engine/common 만 실린다. 그러면 시작 시점 GUID 표가 반쪽이
    // 되고, GUID 로 옮긴 프리팹을 되찾는 경로가 죽는다.
    // 느슨한 `Resource/` 트리만 있는 Dev 에서는 `.meta` 스캔 폴백이 대신 채워 줘서 오래 드러나지
    // 않았다 — 팩이 하나라도 실리면(Shipping) `registered > 0` 이 되어 그 폴백은 돌지 않는다.
    // App 은 EngineLoop 이 같은 자리에서 `loadAssetRegistries()` 를 한 번 더 부른다.
    configManager->setRootDirectory( sw::ResourceUtil::getProjectFolderPath() );

    const sw::EngineConfig* pEngineConfig = configManager->ensureConfig<sw::EngineConfig>(
        sw::hashed_string{ "EngineConfig" }, sw::config::kFileRuntimeEngineConfig, sw::shipping_host::kEngineConfigJson );
    if ( pEngineConfig != nullptr && pEngineConfig->_listResourcePriority.empty() == false )
        sw::ResourceUtil::setSearchPriority( pEngineConfig->_listResourcePriority );

    const sw::GameConfig* pGameConfig = configManager->ensureConfig<sw::GameConfig>(
        sw::hashed_string{ "GameConfig" }, sw::config::kFileRuntimeGameConfig, sw::shipping_host::kGameConfigJson );
    if ( pGameConfig != nullptr )
        sw::GameConfig::setActive( *pGameConfig );

    resourceManager->loadAssetRegistries();

    if ( taskManager->initialize() == false )
        return -1;
    if ( reloadFileManager->initialize() == false )
        return -1;
    if ( sceneManager->initialize() == false )
        return -1;
    if ( inputManager->initialize() == false )
        return -1;

    SW_LOG_INFO( "Core services initialized. Running tests..." );
    SW_LOG_INFO( " Tip: --test_filter=Suite.*  --test_filter=-RHITest.*  --test_list" );
    int32 result = test::TestRegistry::getInstance().runAllTests();

    sceneManager->shutdown();
    inputManager->shutdown();
    audioSystem->shutdown();
    if ( resourceManager != nullptr )
        resourceManager->detachReloadFileManager();
    reloadFileManager->shutdown();
    liveReloadManager->shutdown();
    taskManager->shutdown();
    globalVarManager->shutdown();
    memoryProfiler->shutdown();
    deadlockDetector->shutdown();
    if ( compressionCodecRegistry != nullptr )
        compressionCodecRegistry->shutdown();

    // ------------------------------------------------------------------------------
    // 2) 종료 — 서비스 해제 (생성 역순)
    if ( shaderCache != nullptr )
        shaderCache->shutdown();
    if ( compressionCodecRegistry != nullptr )
        compressionCodecRegistry->shutdown();

    sw::engine::unbindEngineServices();

    frameProfiler.reset();
    componentDefaults.reset();
    shaderCache.reset();
    sw::CompressionCodecRegistry::setActive( nullptr );
    compressionCodecRegistry.reset();
    frameDoubleBuffer.reset();
    debugDrawQueue.reset();
    debugOverlayState.reset();
    assetStreamingQueue.reset();
    engineData.reset();
    eventDispatcher.reset();
    audioSystem.reset();
    rhiRegistry.reset();
    inputManager.reset();
    sceneManager.reset();
    reloadFileManager.reset();
    liveReloadManager.reset();
    localizationManager.reset();
    commandStack.reset();

    // [Note] ResourceManager는 가장 밑바탕이 되는 시스템입니다.
    // 다른 매니저들의 reset() 시 소멸자가 호출되며 들고 있던 리소스들을 해제하는데,
    // 이때 ResourceManager가 살아있어야 안전하게 해제됩니다.
    if ( resourceManager != nullptr )
        resourceManager->shutdown();
    resourceManager.reset();

    typeRegistry.reset();
    globalVarManager.reset();
    taskManager.reset();
    commandLineManager.reset();
    memoryProfiler.reset();
    deadlockDetector.reset();

    logger->shutdown();
    logger.reset();

    sw::HashedStringPool::shutdown();

    return result;
}
