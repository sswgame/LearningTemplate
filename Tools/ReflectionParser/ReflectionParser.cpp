#include "pch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Task/TaskManager.h"
#include "Core/Time/CpuTimer.h"

#include "ReflectionParser/AnnotationMeta.h"
#include "ReflectionParser/AstVisitor.h"
#include "ReflectionParser/CodeEmit.h"
#include "ReflectionParser/CodeGenerator.h"
#include "ReflectionParser/EmitTemplateStore.h"
#include "ReflectionParser/ParserContext.h"
#include "ReflectionParser/ParserDefines.h"
#include "ReflectionParser/ParserUtil.h"
#include "ReflectionParser/ReflectBuiltinsLoader.h"

SW_LOG_CALLER( "ReflectionParser" );
namespace sw
{
	/** @brief 소스 전체를 읽고 리플렉션 키워드가 있으면 true. outContent에 버퍼를 남깁니다. */
	static bool loadSourceIfHasReflectionKeywords( const sw::string& filePath, sw::string& outContent )
	{
		sw::vector<uint8> fileData;
		if ( sw::FileUtil::readFile( filePath, fileData ) == false )
		{
			SW_LOG_ERROR( "Failed to read for keyword scan: %#", filePath );
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
		sw::vector<sw::string> _listInputFiles;
		sw::string			   _outputDir;
		sw::vector<sw::string> _listIncludePaths;
		sw::string			   _builtinsPath;
		sw::string			   _annotationMetaPath;
		sw::string			   _emitTemplatesDir;
		sw::string			   _emitBuiltinsGenPath; ///< --builtins 와 함께 설정 시 ReflectBuiltins.gen.cpp 전용 모드
		uint64				   _maxTemplateTimestamp	= 0;
		uint64				   _builtinsTimestamp		= 0;
		uint64				   _annotationMetaTimestamp = 0;
	};

	/** @brief --input/--output/--include/--builtins 등 CLI를 채웁니다. */
	static bool parseCommandLine( int32 argc, utf8* argv[], CommandLineArgs& outArgs )
	{
		for ( int32 argIndex = 1; argIndex < argc; ++argIndex )
		{
			const string_view arg = argv[argIndex];

			if ( arg == sw::cliConstants::kInput && argIndex + 1 < argc )
			{
				outArgs._listInputFiles.emplace_back( argv[++argIndex] );
			}
			else if ( arg == sw::cliConstants::kOutput && argIndex + 1 < argc )
			{
				outArgs._outputDir = argv[++argIndex];
			}
			else if ( arg == sw::cliConstants::kInclude && argIndex + 1 < argc )
			{
				outArgs._listIncludePaths.emplace_back( argv[++argIndex] );
			}
			else if ( arg == sw::cliConstants::kBuiltins && argIndex + 1 < argc )
			{
				outArgs._builtinsPath = argv[++argIndex];
			}
			else if ( arg == sw::cliConstants::kAnnotationMeta && argIndex + 1 < argc )
			{
				outArgs._annotationMetaPath = argv[++argIndex];
			}
			else if ( arg == sw::cliConstants::kEmitTemplates && argIndex + 1 < argc )
			{
				outArgs._emitTemplatesDir = argv[++argIndex];
			}
			else if ( arg == sw::cliConstants::kEmitBuiltinsGen && argIndex + 1 < argc )
			{
				outArgs._emitBuiltinsGenPath = argv[++argIndex];
			}
			else
			{
				SW_LOG_ERROR( "Unknown argument: %#", argv[argIndex] );
				return false;
			}
		}

		if ( outArgs._emitBuiltinsGenPath.empty() == false )
		{
			if ( outArgs._builtinsPath.empty() )
			{
				SW_LOG_ERROR( "%# requires %#.", sw::cliConstants::kEmitBuiltinsGen, sw::cliConstants::kBuiltins );
				return false;
			}
			return true;
		}

		if ( outArgs._listInputFiles.empty() )
		{
			SW_LOG_ERROR( "No --input files specified." );
			return false;
		}
		if ( outArgs._outputDir.empty() )
		{
			SW_LOG_ERROR( "No --output directory specified." );
			return false;
		}

		return true;
	}

	/** @brief 생성된 파일이 텅 빈 플레이스홀더(껍데기)인지 판별합니다. */
	static bool isPlaceholder( const sw::string& existingGen )
	{
		const sw::ParserClangConfig& cfg = sw::ParserContext::getSharedConfig();
		return existingGen.find( cfg._emitPlaceholderMarker ) != sw::string::npos ||
			   ( existingGen.find( cfg._emitRegenByParserMarker ) != sw::string::npos &&
				 existingGen.find( cfg._emitRegisterTypeMarker ) == sw::string::npos &&
				 existingGen.find( cfg._emitRegisterEnumMarker ) == sw::string::npos &&
				 existingGen.find( cfg._emitFlagOpsMarker ) == sw::string::npos );
	}

	/** @brief .gen.cpp/.gen.h 가 입력·builtins·템플릿보다 최신이면 true. */
	static bool isUpToDate( const sw::string& genPath, const sw::string& inputFile, const CommandLineArgs& args )
	{
		if ( sw::FileUtil::fileExists( genPath ) == false || sw::FileUtil::fileExists( inputFile ) == false )
			return false;

		const sw::string genHeaderPath =
			sw::ParserUtil::makeGeneratedPath( args._outputDir, inputFile, sw::ParserContext::getSharedConfig()._emitHeaderExtension );
		if ( sw::FileUtil::fileExists( genHeaderPath ) == false )
			return false;

		const uint64 genTime = sw::FileUtil::getFileTimestamp( genPath );
		if ( genTime < sw::FileUtil::getFileTimestamp( inputFile ) )
			return false;
		if ( args._builtinsTimestamp > 0 && genTime < args._builtinsTimestamp )
			return false;
		if ( args._annotationMetaTimestamp > 0 && genTime < args._annotationMetaTimestamp )
			return false;
		if ( args._maxTemplateTimestamp > 0 && genTime < args._maxTemplateTimestamp )
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

		sw::CodeGenerator generator( noTypes, noEnums, inputFile, args._outputDir );
		return generator.generate();
	}

	/** @brief 헤더 하나를 파싱·코드젠합니다. 최신이면 건너뛰고, 어노테이션이 없으면 비웁니다. */
	static void processInputFile( const sw::string& inputFile, const CommandLineArgs& args, std::atomic<int32>& errorCount )
	{
		const sw::string genPath =
			sw::ParserUtil::makeGeneratedPath( args._outputDir, inputFile, sw::ParserContext::getSharedConfig()._emitCppExtension );

		if ( isUpToDate( genPath, inputFile, args ) )
		{
			SW_LOG_TRACE( "Up-to-date, skipping AST parsing: %#", inputFile );
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
			SW_LOG_TRACE( "No reflection annotations found, emitting empty output: %#", inputFile );
			if ( emitEmptyGenerated( inputFile, args ) == false )
			{
				SW_LOG_ERROR( "Code generation failed: %#", inputFile );
				++errorCount;
			}
			return;
		}

		SW_LOG_TRACE( "── Parsing: %#", inputFile );

		sw::vector<sw::string> includePaths = args._listIncludePaths;
		if ( args._outputDir.empty() == false )
			includePaths.insert( includePaths.begin(), args._outputDir );

		sw::ParserContext context;
		if ( context.parse( inputFile, includePaths, &sourceContent ) == false )
		{
			SW_LOG_ERROR( "Parse failed: %#", inputFile );
			++errorCount;
			return;
		}

		sw::AstVisitor visitor( context.getTranslationUnit() );
		visitor.visit();

		sw::CodeGenerator generator(
			visitor.getCollectedTypes(),
			visitor.getCollectedEnums(),
			inputFile,
			args._outputDir );

		if ( generator.generate() == false )
		{
			SW_LOG_ERROR( "Code generation failed: %#", inputFile );
			++errorCount;
			return;
		}

		if ( generator.getOutputFilePath().empty() == false )
			SW_LOG_TRACE( "Generated  : %#", generator.getOutputFilePath() );
	}

	/** @brief ENUM(Flags) 비트 연산자 우산 헤더를 씁니다. */
	static bool emitFlagOpsUmbrella( const CommandLineArgs& args )
	{
		const ParserClangConfig& cfg	 = ParserContext::getSharedConfig();
		const string			 outPath = FileUtil::joinPath( args._outputDir, cfg._emitFlagOpsHeader );

		CodeEmitBuffer buffer;
		CodeEmit	   e( buffer );
		e.line( cfg._emitAutoGeneratedBanner );
		e.line( emitDirectiveConstants::kPragmaOnce );
		e.blank();
		e.line( emitDirectiveConstants::kIfndefParser );

		bool bAnyFlags = false;
		for ( const string& inputFile : args._listInputFiles )
		{
			const string genHeader = ParserUtil::makeGeneratedPath( args._outputDir, inputFile, cfg._emitHeaderExtension );
			string		 genText;
			if ( FileUtil::fileExists( genHeader ) == false || FileUtil::readTextFile( genHeader, genText ) == false )
				continue;
			if ( genText.find( cfg._emitFlagOpsMarker ) == string::npos )
				continue;

			bAnyFlags			   = true;
			const string headerInc = ParserUtil::makeHeaderIncludePath( inputFile, args._listIncludePaths );
			const string genInc	   = FileUtil::getFileNamePart( genHeader );
			e.linef( "#include \"%#\"", headerInc );
			e.linef( "#include \"%#\"", genInc );
			e.blank();
		}

		if ( bAnyFlags == false )
			e.line( emitDirectiveConstants::kNoEnumFlags );
		e.line( emitDirectiveConstants::kEndif );

		const string newContent( buffer.view() );
		if ( FileUtil::fileExists( outPath ) )
		{
			string existingContent;
			FileUtil::readTextFile( outPath, existingContent );
			if ( existingContent.empty() == false && existingContent == newContent )
				return true;
		}
		if ( FileUtil::writeTextFile( outPath, newContent ) == false )
		{
			SW_LOG_ERROR( "Failed to write %#", outPath );
			return false;
		}
		SW_LOG_TRACE( "Generated  : %#", outPath );
		return true;
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
	if ( args._emitBuiltinsGenPath.empty() == false )
	{
		if ( args._emitTemplatesDir.empty() )
		{
			SW_LOG_ERROR( "%# requires %#.", sw::cliConstants::kEmitBuiltinsGen, sw::cliConstants::kEmitTemplates );
			logger->shutdown();
			return 1;
		}
		if ( sw::EmitTemplateStore::instance().loadDirectory( args._emitTemplatesDir ) == false )
		{
			SW_LOG_ERROR( "Failed to load --emit-templates: %#", args._emitTemplatesDir );
			logger->shutdown();
			return 1;
		}
		const bool ok = sw::emitReflectBuiltinsGen( args._builtinsPath, args._emitBuiltinsGenPath );
		logger->shutdown();
		return ok ? 0 : 1;
	}

	// 3) 공유 테이블 로드 (builtins / AnnotationMeta / Templates)
	if ( args._builtinsPath.empty() == false )
	{
		if ( sw::loadReflectBuiltins( args._builtinsPath ) == false )
		{
			SW_LOG_ERROR( "Failed to load --builtins: %#", args._builtinsPath );
			logger->shutdown();
			return 1;
		}
	}
	else
	{
		SW_LOG_WARNING( "No --builtins; scalar aliases / std containers will not be registered." );
	}

	if ( args._annotationMetaPath.empty() == false )
	{
		if ( sw::AnnotationMeta::instance().loadFile( args._annotationMetaPath ) == false )
		{
			SW_LOG_ERROR( "Failed to load --annotation-meta: %#", args._annotationMetaPath );
			logger->shutdown();
			return 1;
		}
	}
	else
	{
		SW_LOG_WARNING( "No --annotation-meta; PROPERTY/FUNCTION/REFLECT tokens will be ignored." );
	}

	if ( args._emitTemplatesDir.empty() == false )
	{
		if ( sw::EmitTemplateStore::instance().loadDirectory( args._emitTemplatesDir ) == false )
		{
			SW_LOG_ERROR( "Failed to load --emit-templates: %#", args._emitTemplatesDir );
			logger->shutdown();
			return 1;
		}
	}
	else
	{
		SW_LOG_ERROR( "%# <Templates dir> is required.", sw::cliConstants::kEmitTemplates );
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
	if ( args._builtinsPath.empty() == false && sw::FileUtil::fileExists( args._builtinsPath ) )
		args._builtinsTimestamp = sw::FileUtil::getFileTimestamp( args._builtinsPath );

	if ( args._annotationMetaPath.empty() == false && sw::FileUtil::fileExists( args._annotationMetaPath ) )
		args._annotationMetaTimestamp = sw::FileUtil::getFileTimestamp( args._annotationMetaPath );

	if ( args._emitTemplatesDir.empty() == false )
	{
		sw::vector<sw::string> templates;
		if ( sw::FileUtil::collectFiles( args._emitTemplatesDir, sw::ParserContext::getSharedConfig()._emitTemplateExtension, templates, false, false ) )
		{
			for ( const sw::string& tplPath : templates )
			{
				const uint64 tplTime = sw::FileUtil::getFileTimestamp( tplPath );
				if ( tplTime > args._maxTemplateTimestamp )
					args._maxTemplateTimestamp = tplTime;
			}
		}
	}

	std::atomic<int32> errorCount{ 0 };

	uint32 workerCount = std::thread::hardware_concurrency();
	if ( workerCount == 0 )
		workerCount = 1;
	SW_LOG_INFO( "Parsing %# input(s) with %# worker(s).", args._listInputFiles.size(), workerCount );

	sw::CpuTimer parseTimer;
	parseTimer.resetTimer();
	parseTimer.startTimer();

	if ( args._listInputFiles.empty() == false )
	{
		sw::TaskManager taskManager;
		if ( taskManager.initialize( workerCount ) == false )
		{
			SW_LOG_ERROR( "Failed to initialize TaskManager." );
			logger->shutdown();
			return 1;
		}

		for ( const sw::string& inputFile : args._listInputFiles )
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

	if ( sw::emitFlagOpsUmbrella( args ) == false )
		errorCount.fetch_add( 1 );

	parseTimer.updateTimer();
	const float32 elapsedMs = parseTimer.getDeltaTime() * 1000.0f;

	const int32 totalErrors = errorCount.load();
	if ( totalErrors == 0 )
		SW_LOG_INFO( "Done in %# ms. All files processed successfully.", elapsedMs );
	else
		SW_LOG_ERROR( "Done in %# ms with %# error(s).", elapsedMs, totalErrors );

	logger->shutdown();
	return totalErrors == 0 ? 0 : 1;
}
