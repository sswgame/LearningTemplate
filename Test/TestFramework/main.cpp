/**
 * @file main.cpp
 * @brief 단위 테스트 공통 진입점
 */
#include "pch.h"
#include "TestFramework/TestModuleHeads.h"
#include "TestFramework.h"
#include "Core/Common/CoreServices.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/GlobalVariable/GlobalVariableManager.h"
#include "Core/Utility/Task/TaskManager.h"
#include "Core/Utility/Module/LiveReloadManager.h"
#include "Core/Utility/File/ReloadFileManager.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Object/ComponentManager.h"
#include "Core/Game/Scene/SceneManager.h"
#include "Core/Utility/CommandLine/CommandLineManager.h"

int main( int argc, char* argv[] )
{
	printf( "STARTING MAIN\n" );
	fflush( stdout );

	std::unique_ptr<sw::Logger>				   logger			  = std::make_unique<sw::Logger>();
	std::unique_ptr<sw::CommandLineManager>	   commandLineManager = std::make_unique<sw::CommandLineManager>();
	std::unique_ptr<sw::TaskManager>		   taskManager		  = std::make_unique<sw::TaskManager>();
	std::unique_ptr<sw::GlobalVariableManager> globalVarManager	  = std::make_unique<sw::GlobalVariableManager>();
	std::unique_ptr<sw::TypeRegistry>		   typeRegistry		  = std::make_unique<sw::TypeRegistry>();
	std::unique_ptr<sw::ComponentManager>	   componentManager	  = std::make_unique<sw::ComponentManager>();
	std::unique_ptr<sw::LiveReloadManager>	   liveReloadManager  = std::make_unique<sw::LiveReloadManager>();
	std::unique_ptr<sw::ReloadFileManager>	   reloadFileManager  = std::make_unique<sw::ReloadFileManager>();
	std::unique_ptr<sw::SceneManager>		   sceneManager		  = std::make_unique<sw::SceneManager>();

	sw::Logger::initialize();
	commandLineManager->initialize();
	globalVarManager->initialize();
	globalVarManager->registerPendingVariables( "Core", sw::GlobalVariableRegistrar::getHead() );
	globalVarManager->registerPendingVariables( "TestFramework", swTestGvmHead() );
	globalVarManager->registerToCommandLine( commandLineManager.get() );

	// Consume framework-only flags first so CommandLineManager does not warn on unknowns.
	test::TestRegistry::getInstance().configureFromArgs( argc, argv );

	std::vector<char*> coreArgs;
	coreArgs.reserve( static_cast<size_t>( argc ) );
	coreArgs.push_back( argv[0] );
	for ( int i = 1; i < argc; ++i )
	{
		const std::string_view arg = argv[i] != nullptr ? argv[i] : "";
		if ( arg == "--test_list" || arg == "--gtest_list_tests" )
			continue;
		if ( arg == "--test_filter" || arg == "--gtest_filter" )
		{
			if ( i + 1 < argc )
				++i;
			continue;
		}
		if ( arg.rfind( "--test_filter=", 0 ) == 0 || arg.rfind( "--gtest_filter=", 0 ) == 0 )
			continue;
		coreArgs.push_back( argv[i] );
	}
	commandLineManager->parse( static_cast<int32>( coreArgs.size() ), coreArgs.data() );
	globalVarManager->updateFromCommandLine( commandLineManager.get() );

	sw::CoreServices services{};
	services.commandLineManager	   = commandLineManager.get();
	services.globalVariableManager = globalVarManager.get();
	services.taskManager		   = taskManager.get();
	services.typeRegistry		   = typeRegistry.get();
	services.componentManager	   = componentManager.get();
	services.sceneManager		   = sceneManager.get();
	sw::bindCoreServices( services );

	if ( taskManager->initialize() == false )
		return -1;
	if ( liveReloadManager->initialize() == false )
		return -1;
	if ( reloadFileManager->initialize() == false )
		return -1;
	if ( sceneManager->initialize() == false )
		return -1;

	typeRegistry->registerPendingTypes( "Core", sw::TypeRegistrar::getHead(), sw::EnumRegistrar::getHead() );
	typeRegistry->registerPendingTypes( "TestFramework", swTestTypeHead(), swTestEnumHead() );

	SW_LOG_INFO( "Core services initialized. Running tests..." );
	SW_LOG_INFO( " Tip: --test_filter=Suite.*  --test_filter=-RHITest.*  --test_list" );
	int result = test::TestRegistry::getInstance().runAllTests();

	sceneManager->shutdown();
	reloadFileManager->shutdown();
	liveReloadManager->shutdown();
	componentManager->shutdown();
	taskManager->shutdown();
	globalVarManager->shutdown();
	sw::unbindCoreServices();

	printf( "ENDING MAIN\n" );
	return result;
}
