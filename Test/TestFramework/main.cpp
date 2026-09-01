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
#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Graphics/RHI/RHIBackendRegistry.h"
#include "Engine/Graphics/Shader/ShaderCache.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/StringTable.h"
#include "Engine/Object/Component/ComponentDefaults.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Resource/AssetStreamingQueue.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Utility/CommandStack.h"
#include "Engine/Utility/Debug/DebugOverlayState.h"
#include "Engine/Utility/Module/LiveReloadManager.h"
#include "Engine/Utility/Module/ModuleTypeRegistry.h"
#include "Engine/Utility/Module/ReloadFileManager.h"

#include "GameFramework/Base/GameService.h"

#include "TestFramework/TestFramework.h"

int main( int32 argc, utf8* argv[] )
{
	sw::HashedStringPool::initialize();

	// ------------------------------------------------------------------------------
	// 0) 코어 매니저 — 로거·프로파일러·커맨드라인·엔진 서비스
	// ------------------------------------------------------------------------------
	sw::unique_ptr<sw::Logger>					 logger					  = sw::make_unique<sw::Logger>();
	sw::unique_ptr<sw::DeadlockDetector>		 deadlockDetector		  = sw::make_unique<sw::DeadlockDetector>();
	sw::unique_ptr<sw::MemoryProfiler>			 memoryProfiler			  = sw::make_unique<sw::MemoryProfiler>();
	sw::unique_ptr<sw::CommandLineManager>		 commandLineManager		  = sw::make_unique<sw::CommandLineManager>();
	sw::unique_ptr<sw::TaskManager>				 taskManager			  = sw::make_unique<sw::TaskManager>();
	sw::unique_ptr<sw::GlobalVariableManager>	 globalVarManager		  = sw::make_unique<sw::GlobalVariableManager>();
	sw::unique_ptr<sw::TypeRegistry>			 typeRegistry			  = sw::make_unique<sw::TypeRegistry>();
	sw::unique_ptr<sw::LocalizationManager>		 localizationManager	  = sw::make_unique<sw::LocalizationManager>();
	sw::unique_ptr<sw::LiveReloadManager>		 liveReloadManager		  = sw::make_unique<sw::LiveReloadManager>();
	sw::unique_ptr<sw::ReloadFileManager>		 reloadFileManager		  = sw::make_unique<sw::ReloadFileManager>();
	sw::unique_ptr<sw::SceneManager>			 sceneManager			  = sw::make_unique<sw::SceneManager>();
	sw::unique_ptr<sw::InputManager>			 inputManager			  = sw::make_unique<sw::InputManager>();
	sw::unique_ptr<sw::CommandStack>			 commandStack			  = sw::make_unique<sw::CommandStack>();
	sw::unique_ptr<sw::RHIBackendRegistry>		 rhiRegistry			  = sw::make_unique<sw::RHIBackendRegistry>();
	sw::unique_ptr<sw::IAudioSystem>			 audioSystem			  = sw::IAudioSystem::create();
	sw::unique_ptr<sw::EventDispatcher>			 eventDispatcher		  = sw::make_unique<sw::EventDispatcher>();
	sw::unique_ptr<sw::ResourceManager>			 resourceManager		  = sw::make_unique<sw::ResourceManager>();
	sw::unique_ptr<sw::EngineData>				 engineData				  = sw::make_unique<sw::EngineData>();
	sw::unique_ptr<sw::AssetStreamingQueue>		 assetStreamingQueue	  = sw::make_unique<sw::AssetStreamingQueue>();
	sw::unique_ptr<sw::DebugOverlayState>		 debugOverlayState		  = sw::make_unique<sw::DebugOverlayState>();
	sw::unique_ptr<sw::DebugDrawQueue>			 debugDrawQueue			  = sw::make_unique<sw::DebugDrawQueue>();
	sw::unique_ptr<sw::FrameDoubleBuffer>		 frameDoubleBuffer		  = sw::make_unique<sw::FrameDoubleBuffer>();
	sw::unique_ptr<sw::CompressionCodecRegistry> compressionCodecRegistry = sw::make_unique<sw::CompressionCodecRegistry>();
	sw::unique_ptr<sw::ShaderCache>				 shaderCache			  = sw::make_unique<sw::ShaderCache>();
	sw::unique_ptr<sw::ComponentDefaults>		 componentDefaults		  = sw::make_unique<sw::ComponentDefaults>();

	logger->initialize();
	deadlockDetector->initialize();
	memoryProfiler->initialize();
	compressionCodecRegistry->initialize();
	shaderCache->initialize();
	commandLineManager->initialize();
	globalVarManager->registerPendingVariables( "Engine", sw::GlobalVariableRegistrar::getHead() );
	globalVarManager->registerPendingVariables( "TestFramework", sw::GlobalVariableRegistrar::getHead() );
	globalVarManager->registerToCommandLine( commandLineManager.get() );

	// 프레임워크 전용 플래그를 먼저 소비해 CommandLineManager 가 미지 인자를 경고하지 않게 한다.
	test::TestRegistry::getInstance().configureFromArgs( argc, argv );

	sw::vector<utf8*> listCoreArg;
	listCoreArg.reserve( static_cast<size_t>( argc ) );
	listCoreArg.push_back( argv[0] );
	for ( int32 argIndex = 1; argIndex < argc; ++argIndex )
	{
		const std::string_view arg = argv[argIndex] != nullptr ? argv[argIndex] : "";
		if ( arg == "--test_list" || arg == "--gtest_list_tests" )
			continue;
		if ( arg == "--test_filter" || arg == "--gtest_filter" )
		{
			if ( argIndex + 1 < argc )
				++argIndex;
			continue;
		}
		if ( sw::StringUtil::startsWith( arg, "--test_filter=" ) || sw::StringUtil::startsWith( arg, "--gtest_filter=" ) )
			continue;
		listCoreArg.push_back( argv[argIndex] );
	}
	commandLineManager->parse( static_cast<int32>( listCoreArg.size() ), listCoreArg.data() );
	globalVarManager->updateFromCommandLine( commandLineManager.get() );

	sw::EngineServices services{};
	services._pCommandLineManager		= commandLineManager.get();
	services._pGlobalVariableManager	= globalVarManager.get();
	services._pLocalizationManager		= localizationManager.get();
	services._pTaskManager				= taskManager.get();
	services._pTypeRegistry				= typeRegistry.get();
	services._pCommandStack				= commandStack.get();
	services._pSceneManager				= sceneManager.get();
	services._pInputManager				= inputManager.get();
	services._pRHIBackendRegistry		= rhiRegistry.get();
	services._pAudioSystem				= audioSystem.get();
	services._pEventDispatcher			= eventDispatcher.get();
	services._pResourceManager			= resourceManager.get();
	services._pMemoryProfiler			= memoryProfiler.get();
	services._pEngineData				= engineData.get();
	services._pAssetStreamingQueue		= assetStreamingQueue.get();
	services._pDebugOverlayState		= debugOverlayState.get();
	services._pDebugDrawQueue			= debugDrawQueue.get();
	services._pFrameDoubleBuffer		= frameDoubleBuffer.get();
	services._pCompressionCodecRegistry = compressionCodecRegistry.get();
	services._pShaderCache				= shaderCache.get();
	services._pComponentDefaults		= componentDefaults.get();
	sw::engine::bindEngineServices( services );

	// ------------------------------------------------------------------------------
	// 1) 부트스트랩 — 리소스·태스크·씬·입력, 리플렉션 등록
	// ------------------------------------------------------------------------------
	if ( resourceManager->initialize() == false )
		return -1;

	if ( taskManager->initialize() == false )
		return -1;
	if ( reloadFileManager->initialize() == false )
		return -1;
	if ( sceneManager->initialize() == false )
		return -1;
	if ( inputManager->initialize() == false )
		return -1;

	sw::engine::registerModuleTypes( "Engine" );
	sw::engine::registerModuleTypes( "GameFramework" );
	typeRegistry->registerPendingTypes( "TestFramework", sw::TypeRegistrar::getHead(), sw::EnumRegistrar::getHead() );

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

	componentDefaults.reset();
	shaderCache.reset();
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
