/**
 * @file ReflectionParser.cpp
 * @brief ReflectionParser 진입점 — Core TaskManager로 병렬 파싱
 */
#include "AstVisitor.h"
#include "CodeGenerator.h"
#include "ParserContext.h"
#include "ParserUtil.h"
#include "Core/Common/Common.h"
#include "Core/Common/CoreServices.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/GlobalVariable/GlobalVariableManager.h"
#include "Core/Utility/Task/TaskManager.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Object/ComponentManager.h"
#include "Core/Utility/CommandLine/CommandLineManager.h"
#include "Core/Utility/File/FileUtil.h"
#include "Core/Utility/Delegate/Delegate.h"

namespace
{
	bool hasReflectionKeywords( const std::string& filePath )
	{
		std::vector<uint8> fileData;
		if ( sw::FileUtil::readFile( filePath, fileData ) == false )
			return true;

		const std::string_view content( reinterpret_cast<const utf8*>( fileData.data() ), fileData.size() );
		return sw::tool::containsKeyword( content, "REFLECT" ) ||
			   sw::tool::containsKeyword( content, "PROPERTY" ) ||
			   sw::tool::containsKeyword( content, "FUNCTION" ) ||
			   sw::tool::containsKeyword( content, "ENUM" );
	}

	struct CommandLineArgs
	{
		std::vector<std::string> inputFiles;
		std::string				 outputDir;
		std::vector<std::string> includePaths;
	};

	bool parseCommandLine( int32 argc, utf8* argv[], CommandLineArgs& outArgs )
	{
		for ( int32 i = 1; i < argc; ++i )
		{
			const std::string_view arg = argv[i];

			if ( arg == "--input" && i + 1 < argc )
			{
				outArgs.inputFiles.emplace_back( argv[++i] );
			}
			else if ( arg == "--output" && i + 1 < argc )
			{
				outArgs.outputDir = argv[++i];
			}
			else if ( arg == "--include" && i + 1 < argc )
			{
				outArgs.includePaths.emplace_back( argv[++i] );
			}
			else
			{
				SW_LOG_ERROR( "[ReflectionParser] Unknown argument: %#", argv[i] );
				return false;
			}
		}

		if ( outArgs.inputFiles.empty() )
		{
			SW_LOG_ERROR( "[ReflectionParser] No --input files specified." );
			return false;
		}
		if ( outArgs.outputDir.empty() )
		{
			SW_LOG_ERROR( "[ReflectionParser] No --output directory specified." );
			return false;
		}

		return true;
	}

	void processInputFile( const std::string& inputFile, const CommandLineArgs& args, std::atomic<int32>& errorCount )
	{
		const std::string genPath = sw::tool::makeGeneratedCppPath( args.outputDir, inputFile );

		if ( sw::FileUtil::isFileExist( genPath ) && sw::FileUtil::isFileExist( inputFile ) )
		{
			if ( sw::FileUtil::getFileTimestamp( genPath ) >= sw::FileUtil::getFileTimestamp( inputFile ) )
			{
				SW_LOG_INFO( "[ReflectionParser] Up-to-date, skipping AST parsing: %#", inputFile );
				return;
			}
		}

		if ( hasReflectionKeywords( inputFile ) == false )
		{
			SW_LOG_INFO( "[ReflectionParser] No reflection annotations found, skipping AST: %#", inputFile );
			return;
		}

		SW_LOG_INFO( "[ReflectionParser] ── Parsing: %#", inputFile );

		sw::tool::ParserContext context;
		if ( context.parse( inputFile, args.includePaths ) == false )
		{
			SW_LOG_ERROR( "[ReflectionParser] Parse failed: %#", inputFile );
			++errorCount;
			return;
		}

		sw::tool::AstVisitor visitor( context.getTranslationUnit() );
		visitor.visit();

		sw::tool::CodeGenerator generator(
			visitor.getCollectedTypes(),
			visitor.getCollectedEnums(),
			inputFile,
			args.outputDir );

		if ( generator.generate() == false )
		{
			SW_LOG_ERROR( "[ReflectionParser] Code generation failed: %#", inputFile );
			++errorCount;
			return;
		}

		if ( generator.getOutputFilePath().empty() == false )
			SW_LOG_INFO( "[ReflectionParser] Generated  : %#", generator.getOutputFilePath() );
	}
}

int32 main( int32 argc, utf8* argv[] )
{
	auto logger				= std::make_unique<sw::Logger>();
	auto commandLineManager = std::make_unique<sw::CommandLineManager>();
	auto taskManager		= std::make_unique<sw::TaskManager>();
	auto globalVarManager	= std::make_unique<sw::GlobalVariableManager>();
	auto typeRegistry		= std::make_unique<sw::TypeRegistry>();
	auto compManager		= std::make_unique<sw::ComponentManager>();

	sw::CoreServices services{};
	services.commandLineManager	   = commandLineManager.get();
	services.globalVariableManager = globalVarManager.get();
	services.taskManager		   = taskManager.get();
	services.typeRegistry		   = typeRegistry.get();
	services.componentManager	   = compManager.get();
	sw::bindCoreServices( services );

	sw::Logger::initialize();
	commandLineManager->initialize();
	taskManager->initialize();
	globalVarManager->initialize();
	globalVarManager->registerPendingVariables( "ReflectionParser", sw::GlobalVariableRegistrar::getHead() );
	globalVarManager->registerToCommandLine( commandLineManager.get() );
	commandLineManager->parse( argc, argv );
	globalVarManager->updateFromCommandLine( commandLineManager.get() );

	CommandLineArgs args;
	if ( parseCommandLine( argc, argv, args ) == false )
	{
		SW_LOG_ERROR( "Usage: ReflectionParser --input <header.h> [--input ...] --output <dir> [--include <dir> ...]" );
		taskManager->shutdown();
		sw::unbindCoreServices();
		return 1;
	}

	typeRegistry->registerPendingTypes( "ReflectionParser", sw::TypeRegistrar::getHead(), sw::EnumRegistrar::getHead() );

	if ( sw::tool::ParserContext::ensureSharedConfig() == false )
	{
		taskManager->shutdown();
		sw::unbindCoreServices();
		return 1;
	}

	std::atomic<int32> errorCount{ 0 };

	for ( const std::string& inputFile : args.inputFiles )
	{
		const std::string taskName = "Reflect:" + sw::FileUtil::getFileNamePart( inputFile );
		taskManager->emplaceTask( taskName, SW_DELEGATE_LAMBDA( sw::TaskDelegate, [inputFile, &args, &errorCount]()
		{
			processInputFile( inputFile, args, errorCount );
		} ) );
	}

	taskManager->waitAll();

	const int32 totalErrors = errorCount.load();
	if ( totalErrors == 0 )
		SW_LOG_INFO( "[ReflectionParser] Done. All files processed successfully." );
	else
		SW_LOG_ERROR( "[ReflectionParser] Done with %# error(s).", totalErrors );

	sw::tool::ParserContext::shutdownShared();
	taskManager->shutdown();
	sw::unbindCoreServices();
	return totalErrors == 0 ? 0 : 1;
}
