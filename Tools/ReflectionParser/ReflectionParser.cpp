#include "pch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Task/TaskManager.h"
#include "Core/Time/CpuTimer.h"

#include "ReflectionParser/AnnotationMeta.h"
#include "ReflectionParser/AstVisitor.h"
#include "ReflectionParser/CodeGenerator.h"
#include "ReflectionParser/EmitTemplateStore.h"
#include "ReflectionParser/ParserContext.h"
#include "ReflectionParser/ParserDefines.h"
#include "ReflectionParser/ParserUtil.h"
#include "ReflectionParser/ReflectBuiltinsLoader.h"

namespace sw
{
	/** @brief 소스 전체를 읽고 리플렉션 키워드가 있으면 true. outContent에 버퍼를 남깁니다. */
	static bool loadSourceIfHasReflectionKeywords( const sw::string& filePath, sw::string& outContent )
	{
		sw::vector<uint8> fileData;
		if ( sw::FileUtil::readFile( filePath, fileData ) == false )
		{
			SW_LOG_ERROR( "[ReflectionParser] Failed to read for keyword scan: %#", filePath );
			outContent.clear();
			return false;
		}

		outContent.assign( reinterpret_cast<const utf8*>( fileData.data() ), fileData.size() );
		for ( const utf8* keyword : sw::kSourceKeywordScan )
		{
			if ( outContent.find( keyword ) != sw::string::npos )
				return true;
		}
		return false;
	}

	/** @brief ReflectionParser CLI (--input/--output/--builtins 등). */
	struct CommandLineArgs
	{
		sw::vector<sw::string> inputFiles;
		sw::string			   outputDir;
		sw::vector<sw::string> includePaths;
		sw::string			   builtinsPath;
		sw::string			   annotationMetaPath;
		sw::string			   emitTemplatesDir;
		sw::string			   emitBuiltinsGenPath; ///< --builtins 와 함께 설정 시 ReflectBuiltins.gen.cpp 전용 모드
		uint64				   maxTemplateTimestamp	   = 0;
		uint64				   builtinsTimestamp	   = 0;
		uint64				   annotationMetaTimestamp = 0;
	};

	/** @brief --input/--output/--include/--builtins 등 CLI를 채웁니다. */
	static bool parseCommandLine( int32 argc, utf8* argv[], CommandLineArgs& outArgs )
	{
		for ( int32 argIndex = 1; argIndex < argc; ++argIndex )
		{
			const string_view arg = argv[argIndex];

			if ( arg == sw::cliConstants::kInput && argIndex + 1 < argc )
			{
				outArgs.inputFiles.emplace_back( argv[++argIndex] );
			}
			else if ( arg == sw::cliConstants::kOutput && argIndex + 1 < argc )
			{
				outArgs.outputDir = argv[++argIndex];
			}
			else if ( arg == sw::cliConstants::kInclude && argIndex + 1 < argc )
			{
				outArgs.includePaths.emplace_back( argv[++argIndex] );
			}
			else if ( arg == sw::cliConstants::kBuiltins && argIndex + 1 < argc )
			{
				outArgs.builtinsPath = argv[++argIndex];
			}
			else if ( arg == sw::cliConstants::kAnnotationMeta && argIndex + 1 < argc )
			{
				outArgs.annotationMetaPath = argv[++argIndex];
			}
			else if ( arg == sw::cliConstants::kEmitTemplates && argIndex + 1 < argc )
			{
				outArgs.emitTemplatesDir = argv[++argIndex];
			}
			else if ( arg == sw::cliConstants::kEmitBuiltinsGen && argIndex + 1 < argc )
			{
				outArgs.emitBuiltinsGenPath = argv[++argIndex];
			}
			else
			{
				SW_LOG_ERROR( "[ReflectionParser] Unknown argument: %#", argv[argIndex] );
				return false;
			}
		}

		if ( outArgs.emitBuiltinsGenPath.empty() == false )
		{
			if ( outArgs.builtinsPath.empty() )
			{
				SW_LOG_ERROR( "[ReflectionParser] %# requires %#.", sw::cliConstants::kEmitBuiltinsGen, sw::cliConstants::kBuiltins );
				return false;
			}
			return true;
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

	/** @brief 생성된 파일이 텅 빈 플레이스홀더(껍데기)인지 판별합니다. */
	static bool isPlaceholder( const sw::string& existingGen )
	{
		const sw::ParserClangConfig& cfg = sw::ParserContext::getSharedConfig();
		return existingGen.find( cfg.emitPlaceholderMarker ) != sw::string::npos ||
			   ( existingGen.find( cfg.emitRegenByParserMarker ) != sw::string::npos &&
				 existingGen.find( sw::genConstants::kRegisterTypeMarker ) == sw::string::npos &&
				 existingGen.find( sw::genConstants::kRegisterEnumMarker ) == sw::string::npos &&
				 existingGen.find( sw::genConstants::kFlagOrOperatorMarker ) == sw::string::npos );
	}

	/** @brief .gen.cpp/.gen.h 가 입력·builtins·템플릿보다 최신이면 true. */
	static bool isUpToDate( const sw::string& genPath, const sw::string& inputFile, const CommandLineArgs& args )
	{
		if ( sw::FileUtil::fileExists( genPath ) == false || sw::FileUtil::fileExists( inputFile ) == false )
			return false;

		const sw::string genHeaderPath =
			sw::makeGeneratedPath( args.outputDir, inputFile, sw::ParserContext::getSharedConfig().emitHeaderExtension );
		if ( sw::FileUtil::fileExists( genHeaderPath ) == false )
			return false;

		const uint64 genTime = sw::FileUtil::getFileTimestamp( genPath );
		if ( genTime < sw::FileUtil::getFileTimestamp( inputFile ) )
			return false;
		if ( args.builtinsTimestamp > 0 && genTime < args.builtinsTimestamp )
			return false;
		if ( args.annotationMetaTimestamp > 0 && genTime < args.annotationMetaTimestamp )
			return false;
		if ( args.maxTemplateTimestamp > 0 && genTime < args.maxTemplateTimestamp )
			return false;

		// 타임스탬프가 최신인 경우에만 플레이스홀더 검사 (헤더 수 KB만 읽어 I/O 축소)
		constexpr uint32  kPlaceholderProbeBytes = 4096;
		sw::vector<uint8> genHead;
		sw::vector<uint8> headerHead;
		if ( sw::FileUtil::readFile( genPath, genHead, 0, kPlaceholderProbeBytes ) == false )
			return false;
		if ( sw::FileUtil::readFile( genHeaderPath, headerHead, 0, kPlaceholderProbeBytes ) == false )
			return false;

		const sw::string existingGen( reinterpret_cast<const utf8*>( genHead.data() ), genHead.size() );
		const sw::string existingHeader( reinterpret_cast<const utf8*>( headerHead.data() ), headerHead.size() );
		if ( isPlaceholder( existingGen ) || isPlaceholder( existingHeader ) )
			return false;

		return true;
	}

	/**
	 * @brief 빈 AST 와 같은 생성물(.gen.cpp/.gen.h)을 씁니다.
	 * @details 리플렉션 매크로가 지워진 뒤에도 예전 registrar 가 남아 계속 컴파일되는 것을 막습니다.
	 */
	static bool emitEmptyGenerated( const sw::string& inputFile, const CommandLineArgs& args )
	{
		const sw::vector<sw::ParsedTypeInfo> noTypes;
		const sw::vector<sw::ParsedEnumInfo> noEnums;

		sw::CodeGenerator generator( noTypes, noEnums, inputFile, args.outputDir );
		return generator.generate();
	}

	/** @brief 헤더 하나를 파싱·코드젠합니다. 최신이면 건너뛰고, 어노테이션이 없으면 비웁니다. */
	static void processInputFile( const sw::string& inputFile, const CommandLineArgs& args, std::atomic<int32>& errorCount )
	{
		const sw::string genPath =
			sw::makeGeneratedPath( args.outputDir, inputFile, sw::ParserContext::getSharedConfig().emitCppExtension );

		if ( isUpToDate( genPath, inputFile, args ) )
		{
			SW_LOG_INFO( "[ReflectionParser] Up-to-date, skipping AST parsing: %#", inputFile );
			return;
		}

		sw::string sourceContent;
		if ( loadSourceIfHasReflectionKeywords( inputFile, sourceContent ) == false )
		{
			if ( sourceContent.empty() )
			{
				++errorCount;
				return;
			}
			SW_LOG_INFO( "[ReflectionParser] No reflection annotations found, emitting empty output: %#", inputFile );
			if ( emitEmptyGenerated( inputFile, args ) == false )
			{
				SW_LOG_ERROR( "[ReflectionParser] Code generation failed: %#", inputFile );
				++errorCount;
			}
			return;
		}

		SW_LOG_INFO( "[ReflectionParser] ── Parsing: %#", inputFile );

		sw::vector<sw::string> includePaths = args.includePaths;
		if ( args.outputDir.empty() == false )
			includePaths.insert( includePaths.begin(), args.outputDir );

		sw::ParserContext context;
		if ( context.parse( inputFile, includePaths, &sourceContent ) == false )
		{
			SW_LOG_ERROR( "[ReflectionParser] Parse failed: %#", inputFile );
			++errorCount;
			return;
		}

		sw::AstVisitor visitor( context.getTranslationUnit() );
		visitor.visit();

		sw::CodeGenerator generator(
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

} // namespace sw

/**
 * @brief clang 설정·builtins·템플릿을 로드한 뒤 입력을 병렬 파싱합니다.
 *
 * 초심자용 단계:
 *  1) CLI 파싱
 *  2) builtins-gen 전용 모드면 여기서 종료
 *  3) builtins / AnnotationMeta / Templates 로드
 *  4) ParserContext 공유 clang 설정 1회 로드
 *  5) 타임스탬프 캐시 후 TaskManager 워커 풀에서 processInputFile
 */
int32 main( int32 argc, utf8* argv[] )
{
	sw::unique_ptr<sw::Logger> logger = sw::make_unique<sw::Logger>();
	logger->initialize();

	// 1) CLI
	sw::CommandLineArgs args;
	if ( sw::parseCommandLine( argc, argv, args ) == false )
	{
		SW_LOG_ERROR(
			"Usage: ReflectionParser %# <header.h> %# <dir> [%# <ReflectBuiltins.h>] "
			"[%# <AnnotationMeta.txt>] [%# <dir>]\n"
			"   or: ReflectionParser %# <ReflectBuiltins.h> %# <dir> "
			"%# <ReflectBuiltins.gen.cpp>",
			sw::cliConstants::kInput, sw::cliConstants::kOutput, sw::cliConstants::kBuiltins,
			sw::cliConstants::kAnnotationMeta, sw::cliConstants::kEmitTemplates,
			sw::cliConstants::kBuiltins, sw::cliConstants::kEmitTemplates, sw::cliConstants::kEmitBuiltinsGen );
		logger->shutdown();
		return 1;
	}

	// 2) ReflectBuiltins.gen.cpp 전용 모드
	if ( args.emitBuiltinsGenPath.empty() == false )
	{
		if ( args.emitTemplatesDir.empty() )
		{
			SW_LOG_ERROR( "[ReflectionParser] %# requires %#.", sw::cliConstants::kEmitBuiltinsGen, sw::cliConstants::kEmitTemplates );
			logger->shutdown();
			return 1;
		}
		if ( sw::EmitTemplateStore::instance().loadDirectory( args.emitTemplatesDir ) == false )
		{
			SW_LOG_ERROR( "[ReflectionParser] Failed to load --emit-templates: %#", args.emitTemplatesDir );
			logger->shutdown();
			return 1;
		}
		const bool ok = sw::emitReflectBuiltinsGen( args.builtinsPath, args.emitBuiltinsGenPath );
		logger->shutdown();
		return ok ? 0 : 1;
	}

	// 3) 공유 테이블 로드 (builtins / AnnotationMeta / Templates)
	if ( args.builtinsPath.empty() == false )
	{
		if ( sw::loadReflectBuiltins( args.builtinsPath ) == false )
		{
			SW_LOG_ERROR( "[ReflectionParser] Failed to load --builtins: %#", args.builtinsPath );
			logger->shutdown();
			return 1;
		}
	}
	else
	{
		SW_LOG_WARNING( "[ReflectionParser] No --builtins; scalar aliases / std containers will not be registered." );
	}

	if ( args.annotationMetaPath.empty() == false )
	{
		if ( sw::AnnotationMeta::instance().loadFile( args.annotationMetaPath ) == false )
		{
			SW_LOG_ERROR( "[ReflectionParser] Failed to load --annotation-meta: %#", args.annotationMetaPath );
			logger->shutdown();
			return 1;
		}
	}
	else
	{
		SW_LOG_WARNING( "[ReflectionParser] No --annotation-meta; PROPERTY/FUNCTION/REFLECT tokens will be ignored." );
	}

	if ( args.emitTemplatesDir.empty() == false )
	{
		if ( sw::EmitTemplateStore::instance().loadDirectory( args.emitTemplatesDir ) == false )
		{
			SW_LOG_ERROR( "[ReflectionParser] Failed to load --emit-templates: %#", args.emitTemplatesDir );
			logger->shutdown();
			return 1;
		}
	}
	else
	{
		SW_LOG_ERROR( "[ReflectionParser] %# <Templates dir> is required.", sw::cliConstants::kEmitTemplates );
		logger->shutdown();
		return 1;
	}

	// 4) clang 공통 인자 (parser_config) 1회 캐시
	if ( sw::ParserContext::ensureSharedConfig() == false )
	{
		logger->shutdown();
		return 1;
	}

	// 5) 증분 스킵용 타임스탬프 + 파일별 병렬 파싱
	if ( args.builtinsPath.empty() == false && sw::FileUtil::fileExists( args.builtinsPath ) )
		args.builtinsTimestamp = sw::FileUtil::getFileTimestamp( args.builtinsPath );

	if ( args.annotationMetaPath.empty() == false && sw::FileUtil::fileExists( args.annotationMetaPath ) )
		args.annotationMetaTimestamp = sw::FileUtil::getFileTimestamp( args.annotationMetaPath );

	if ( args.emitTemplatesDir.empty() == false )
	{
		sw::vector<sw::string> templates;
		if ( sw::FileUtil::collectFiles( args.emitTemplatesDir, sw::ParserContext::getSharedConfig().emitTemplateExtension, templates, false, false ) )
		{
			for ( const sw::string& tplPath : templates )
			{
				const uint64 tplTime = sw::FileUtil::getFileTimestamp( tplPath );
				if ( tplTime > args.maxTemplateTimestamp )
					args.maxTemplateTimestamp = tplTime;
			}
		}
	}

	std::atomic<int32> errorCount{ 0 };

	uint32 workerCount = std::thread::hardware_concurrency();
	if ( workerCount == 0 )
		workerCount = 1;
	SW_LOG_INFO( "[ReflectionParser] Parsing %# input(s) with %# worker(s).", args.inputFiles.size(), workerCount );

	sw::CpuTimer parseTimer;
	parseTimer.resetTimer();
	parseTimer.startTimer();

	if ( args.inputFiles.empty() == false )
	{
		sw::TaskManager taskManager;
		if ( taskManager.initialize( workerCount ) == false )
		{
			SW_LOG_ERROR( "[ReflectionParser] Failed to initialize TaskManager." );
			logger->shutdown();
			return 1;
		}

		for ( const sw::string& inputFile : args.inputFiles )
		{
			sw::TaskHandle handle = taskManager.emplaceTask(
				"ParseHeader",
				SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&args, &errorCount, inputFile]()
			{
				sw::processInputFile( inputFile, args, errorCount );
			} ) );
			handle.submit();
		}

		taskManager.waitAll();
		taskManager.shutdown();
	}

	parseTimer.updateTimer();
	const float32 elapsedMs = parseTimer.getDeltaTime() * 1000.0f;

	const int32 totalErrors = errorCount.load();
	if ( totalErrors == 0 )
		SW_LOG_INFO( "[ReflectionParser] Done in %# ms. All files processed successfully.", elapsedMs );
	else
		SW_LOG_ERROR( "[ReflectionParser] Done in %# ms with %# error(s).", elapsedMs, totalErrors );

	logger->shutdown();
	return totalErrors == 0 ? 0 : 1;
}
