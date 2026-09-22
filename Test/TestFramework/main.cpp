#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Concurrency/DeadlockDetector.h"
#include "Core/Event/EventDispatcher.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Memory/MemoryProfiler.h"
#include "Core/String/StringUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Audio/IAudioSystem.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/EngineData.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/EngineOwnedServices.h"
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

    sw::unique_ptr<sw::Logger>           logger           = sw::make_unique<sw::Logger>();
    sw::unique_ptr<sw::DeadlockDetector> deadlockDetector = sw::make_unique<sw::DeadlockDetector>();
    sw::unique_ptr<sw::MemoryProfiler>   memoryProfiler   = sw::make_unique<sw::MemoryProfiler>();
    sw::unique_ptr<sw::ConfigManager>    configManager    = sw::make_unique<sw::ConfigManager>();
    sw::unique_ptr<sw::CommandStack>     commandStack     = sw::make_unique<sw::CommandStack>();
    sw::unique_ptr<sw::IAudioSystem>     audioSystem      = sw::IAudioSystem::create();
    logger->initialize();
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
    owned._pCompressionCodecRegistry->initialize();
    // 서비스와 **같은 인스턴스**를 Core 슬롯에도 꽂는다 — 안 꽂으면 CompressionStream 이
    // 다른 레지스트리를 보게 되어 등록한 코덱이 테스트에서만 조용히 무시된다.
    sw::CompressionCodecRegistry::setActive( owned._pCompressionCodecRegistry.get() );
    owned._pShaderCache->initialize();
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
    services._pAudioSystem    = audioSystem.get();
    services._pMemoryProfiler = memoryProfiler.get();
    services._pCommandStack   = commandStack.get();
    sw::engine::bindEngineServices( services );

    // ------------------------------------------------------------------------------
    // 1) 부트스트랩 — 리소스·태스크·씬·입력, 리플렉션 등록
    // ------------------------------------------------------------------------------
    // 리플렉션은 설정보다 먼저다 — 설정(EngineConfig·GameConfig) 역직렬화가 TypeInfo 를 쓴다.
    sw::engine::registerModuleTypes( "Engine" );
    sw::engine::registerModuleTypes( "GameFramework" );
    owned._pTypeRegistry->registerPendingTypes( "TestFramework", sw::TypeRegistrar::getHead(), sw::EnumRegistrar::getHead() );

    // 설정은 리소스 초기화보다 **먼저** 읽는다 — EngineLoop 과 같은 순서다.
    //
    // `ResourceManager::initialize` 가 `mountStartupPacks` · `loadAssetRegistries` 를 하는데,
    // `loadAssetRegistries` 는 `GameConfig::getActive()._packRoot` 로 게임 레지스트리 경로를 만든다.
    // 활성 설정이 없으면 그 항목이 비어 engine/common 만 실리고, 시작 시점 GUID 표가 반쪽이 되어
    // GUID 로 옮긴 프리팹을 되찾는 경로가 죽는다.
    // 느슨한 `Resource/` 트리만 있는 Dev 에서는 `.meta` 스캔 폴백이 대신 채워 줘서 오래 드러나지
    // 않았다 — 팩이 하나라도 실리면(Shipping) `registered > 0` 이 되어 그 폴백은 돌지 않는다.
    //
    // 예전에는 리소스를 먼저 세운 뒤 설정을 읽고 `loadAssetRegistries()` 를 한 번 더 불러 메웠다.
    // 게다가 `setSearchPriority` 가 `GameConfig::setActive` **보다 먼저**라, 그 재계산에서도 "game"
    // 토큰이 풀리지 않았다.
    configManager->setRootDirectory( sw::ResourceUtil::getProjectFolderPath() );

    const sw::EngineConfig* pEngineConfig = configManager->ensureConfig<sw::EngineConfig>(
        sw::config::kFileRuntimeEngineConfig, sw::shipping_host::kEngineConfigJson );

    const sw::GameConfig* pGameConfig = configManager->ensureConfig<sw::GameConfig>(
        sw::config::kFileRuntimeGameConfig, sw::shipping_host::kGameConfigJson );
    if ( pGameConfig != nullptr )
        sw::GameConfig::setActive( *pGameConfig );

    if ( owned._pResourceManager->initialize() == false )
        return -1;

    // GameConfig 가 활성화된 뒤라야 "game" 토큰이 팩 루트로 풀린다 — 그 전제는 `mountContent` 의
    // 인자에 드러나 있다. 우선순위 적용 · 팩 마운트 · 레지스트리 적재가 한 호출로 묶여 있다.
    if ( pEngineConfig != nullptr )
        owned._pResourceManager->mountContent( pEngineConfig->_listResourcePriority );
    else
        owned._pResourceManager->mountContent( {} );

    if ( owned._pTaskManager->initialize() == false )
        return -1;
    if ( owned._pSceneManager->initialize() == false )
        return -1;
    if ( owned._pInputManager->initialize() == false )
        return -1;

    SW_LOG_INFO( "Core services initialized. Running tests..." );
    SW_LOG_INFO( " Tip: --test_filter=Suite.*  --test_filter=-RHITest.*  --test_list" );
    int32 result = test::TestRegistry::getInstance().runAllTests();

    owned._pSceneManager->shutdown();
    owned._pInputManager->shutdown();
    audioSystem->shutdown();
    owned._pTaskManager->shutdown();
    owned._pGlobalVariableManager->shutdown();
    memoryProfiler->shutdown();
    deadlockDetector->shutdown();
    if ( owned._pCompressionCodecRegistry != nullptr )
        owned._pCompressionCodecRegistry->shutdown();

    // ------------------------------------------------------------------------------
    // 2) 종료 — 서비스 해제 (생성 역순)
    if ( owned._pShaderCache != nullptr )
        owned._pShaderCache->shutdown();
    if ( owned._pCompressionCodecRegistry != nullptr )
        owned._pCompressionCodecRegistry->shutdown();

    sw::engine::unbindEngineServices();

    owned._pFrameProfiler.reset();
    owned._pRenderTargetRegistry.reset();
    owned._pComponentDefaults.reset();
    owned._pShaderCache.reset();
    sw::CompressionCodecRegistry::setActive( nullptr );
    owned._pCompressionCodecRegistry.reset();
    owned._pDebugDrawQueue.reset();
    owned._pDebugOverlayState.reset();
    owned._pAssetStreamingQueue.reset();
    owned._pEngineData.reset();
    owned._pEventDispatcher.reset();
    audioSystem.reset();
    owned._pRHIBackendRegistry.reset();
    owned._pInputManager.reset();
    owned._pSceneManager.reset();
    owned._pLocalizationManager.reset();
    commandStack.reset();

    // [Note] ResourceManager는 가장 밑바탕이 되는 시스템입니다.
    // 다른 매니저들의 reset() 시 소멸자가 호출되며 들고 있던 리소스들을 해제하는데,
    // 이때 ResourceManager가 살아있어야 안전하게 해제됩니다.
    if ( owned._pResourceManager != nullptr )
        owned._pResourceManager->shutdown();
    owned._pResourceManager.reset();

    owned._pTypeRegistry.reset();
    owned._pGlobalVariableManager.reset();
    owned._pTaskManager.reset();
    owned._pCommandLineManager.reset();
    // 위에서 순서대로 놓은 것 말고 남은 것을 쓸어 담는다(EngineLoop 과 같은 자리).
    owned.destroyAll();

    memoryProfiler.reset();
    deadlockDetector.reset();

    logger->shutdown();
    logger.reset();

    sw::HashedStringPool::shutdown();

    return result;
}
