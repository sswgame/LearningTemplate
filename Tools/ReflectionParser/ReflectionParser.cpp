#include "pch.h"

#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Task/TaskManager.h"
#include "Core/Time/CpuTimer.h"

#include "ReflectionParser/AnnotationApply.h"
#include "ReflectionParser/AnnotationMeta.h"
#include "ReflectionParser/AstVisitor.h"
#include "ReflectionParser/CodeEmit.h"
#include "ReflectionParser/CodeGenerator.h"
#include "ReflectionParser/EmitTemplateStore.h"
#include "ReflectionParser/ParsedReflection.h"
#include "ReflectionParser/ParserContext.h"
#include "ReflectionParser/ParserDefines.h"
#include "ReflectionParser/ParserUtil.h"
#include "ReflectionParser/ReflectBuiltinsLoader.h"

SW_LOG_CALLER( "ReflectionParser" );
namespace sw
{
	struct CommandLineArgs
	{
		sw::vector<sw::string> _listInputFiles;
		sw::vector<sw::string> _listIncludePaths;
		sw::string			   _outputDir;
		sw::string			   _builtinsPath;
		sw::string			   _annotationMetaPath;
		sw::string			   _emitTemplatesDir;
		sw::string			   _emitBuiltinsGenPath; ///< --builtins 와 함께 설정 시 ReflectBuiltins.gen.cpp 전용 모드
		uint64				   _maxTemplateTimestamp	= 0;
		uint64				   _builtinsTimestamp		= 0;
		uint64				   _annotationMetaTimestamp = 0;
	};

	/** @brief --input/--output/--include/--builtins 등 CLI를 채웁니다. */
	static bool parseCommandLine( int32 argc, utf8* argv[], CommandLineArgs& outCommandLineArgs )
	{
		for ( int32 argIndex = 1; argIndex < argc; ++argIndex )
		{
			const string_view commandLineArg = argv[argIndex];

			if ( commandLineArg == sw::cliConstants::kInput && argIndex + 1 < argc )
			{
				outCommandLineArgs._listInputFiles.emplace_back( argv[++argIndex] );
			}
			else if ( commandLineArg == sw::cliConstants::kOutput && argIndex + 1 < argc )
			{
				outCommandLineArgs._outputDir = argv[++argIndex];
			}
			else if ( commandLineArg == sw::cliConstants::kInclude && argIndex + 1 < argc )
			{
				outCommandLineArgs._listIncludePaths.emplace_back( argv[++argIndex] );
			}
			else if ( commandLineArg == sw::cliConstants::kBuiltins && argIndex + 1 < argc )
			{
				outCommandLineArgs._builtinsPath = argv[++argIndex];
			}
			else if ( commandLineArg == sw::cliConstants::kAnnotationMeta && argIndex + 1 < argc )
			{
				outCommandLineArgs._annotationMetaPath = argv[++argIndex];
			}
			else if ( commandLineArg == sw::cliConstants::kEmitTemplates && argIndex + 1 < argc )
			{
				outCommandLineArgs._emitTemplatesDir = argv[++argIndex];
			}
			else if ( commandLineArg == sw::cliConstants::kEmitBuiltinsGen && argIndex + 1 < argc )
			{
				outCommandLineArgs._emitBuiltinsGenPath = argv[++argIndex];
			}
			else
			{
				SW_LOG_ERROR( "Unknown argument: %#", argv[argIndex] );
				return false;
			}
		}

		if ( outCommandLineArgs._emitBuiltinsGenPath.empty() == false )
		{
			if ( outCommandLineArgs._builtinsPath.empty() )
			{
				SW_LOG_ERROR( "%# requires %#.", sw::cliConstants::kEmitBuiltinsGen, sw::cliConstants::kBuiltins );
				return false;
			}
			return true;
		}

		if ( outCommandLineArgs._listInputFiles.empty() )
		{
			SW_LOG_ERROR( "No --input files specified." );
			return false;
		}
		if ( outCommandLineArgs._outputDir.empty() )
		{
			SW_LOG_ERROR( "No --output directory specified." );
			return false;
		}

		return true;
	}

	/** @brief 생성된 파일이 텅 빈 플레이스홀더(껍데기)인지 판별합니다. */
	static bool isPlaceholder( const string_view existingGen )
	{
		const sw::ParserClangConfig& cfg = sw::ParserContext::getSharedConfig();
		return existingGen.find( cfg._emitPlaceholderMarker ) != string_view::npos ||
			   ( existingGen.find( cfg._emitRegenByParserMarker ) != string_view::npos &&
				 existingGen.find( cfg._emitRegisterTypeMarker ) == string_view::npos &&
				 existingGen.find( cfg._emitRegisterEnumMarker ) == string_view::npos &&
				 existingGen.find( cfg._emitFlagOpsMarker ) == string_view::npos );
	}

	/** @brief .gen.cpp/.gen.h 가 입력·builtins·템플릿보다 최신이면 true. */
	static bool isUpToDate( const sw::string& genPath, const sw::string& inputFile, const CommandLineArgs& commandLineArgs )
	{
		if ( sw::FileUtil::fileExists( genPath ) == false || sw::FileUtil::fileExists( inputFile ) == false )
			return false;

		const sw::string genHeaderPath =
			sw::ParserUtil::makeGeneratedPath( commandLineArgs._outputDir, inputFile, sw::ParserContext::getSharedConfig()._emitHeaderExtension );
		if ( sw::FileUtil::fileExists( genHeaderPath ) == false )
			return false;

		const uint64 genTime = sw::FileUtil::getFileTimestamp( genPath );
		if ( genTime < sw::FileUtil::getFileTimestamp( inputFile ) )
			return false;
		if ( commandLineArgs._builtinsTimestamp > 0 && genTime < commandLineArgs._builtinsTimestamp )
			return false;
		if ( commandLineArgs._annotationMetaTimestamp > 0 && genTime < commandLineArgs._annotationMetaTimestamp )
			return false;
		if ( commandLineArgs._maxTemplateTimestamp > 0 && genTime < commandLineArgs._maxTemplateTimestamp )
			return false;

		// 타임스탬프가 최신인 경우에만 플레이스홀더 검사 (헤더 수 KB만 읽어 I/O 축소)
		constexpr uint32  kPlaceholderProbeBytes = 4096;
		sw::vector<uint8> genHead;
		sw::vector<uint8> headerHead;
		if ( sw::FileUtil::readFile( genPath, genHead, 0, kPlaceholderProbeBytes ) == false )
			return false;
		if ( sw::FileUtil::readFile( genHeaderPath, headerHead, 0, kPlaceholderProbeBytes ) == false )
			return false;

		const string_view existingGen( reinterpret_cast<const utf8*>( genHead.data() ), genHead.size() );
		const string_view existingHeader( reinterpret_cast<const utf8*>( headerHead.data() ), headerHead.size() );
		if ( isPlaceholder( existingGen ) || isPlaceholder( existingHeader ) )
			return false;

		return true;
	}

	/**
	 * @brief 소스 파일 텍스트에서 리플렉션 핵심 매크로 키워드가 존재하는지 SIMD 벡터화 스캔으로 고속 검사합니다.
	 */
	static bool hasReflectionKeywords( string_view source )
	{
		const size_t length = source.size();
		size_t		 index	= source.find_first_of( "REPF" );
		while ( index != string_view::npos )
		{
			const utf8 c = source[index];
			if ( c == 'R' && index + 7 <= length && source.compare( index, 7, "REFLECT" ) == 0 )
				return true;
			if ( c == 'E' && index + 4 <= length && source.compare( index, 4, "ENUM" ) == 0 )
				return true;
			if ( c == 'P' && index + 8 <= length && source.compare( index, 8, "PROPERTY" ) == 0 )
				return true;
			if ( c == 'F' && index + 8 <= length && source.compare( index, 8, "FUNCTION" ) == 0 )
				return true;

			index = source.find_first_of( "REPF", index + 1 );
		}
		return false;
	}

	/**
	 * @brief 파일 앞부분에서 REFLECT/ENUM/PROPERTY/FUNCTION 키워드를 검사합니다.
	 *
	 * [목적]: 리플렉션 매크로가 전혀 없는 헤더는 무거운 libclang 파싱을 아예 건너뛰어 빌드 시간을 크게 단축합니다.
	 */
	static bool loadSourceIfHasReflectionKeywords( const sw::string& path, sw::string& outContent )
	{
		if ( sw::FileUtil::readTextFile( path, outContent ) == false )
			return false;

		return hasReflectionKeywords( outContent );
	}

	/**
	 * @brief 빈 AST 와 같은 생성물(.gen.cpp/.gen.h)을 씁니다.
	 * @details 리플렉션 매크로가 지워진 뒤에도 예전 registrar 가 남아 계속 컴파일되는 것을 막습니다.
	 */
	static bool emitEmptyGenerated( const sw::string& inputFile, const CommandLineArgs& commandLineArgs )
	{
		const sw::vector<sw::ParsedTypeInfo> noTypes;
		const sw::vector<sw::ParsedEnumInfo> noEnums;

		sw::CodeGenerator generator( noTypes, noEnums, inputFile, commandLineArgs._outputDir );
		return generator.generate();
	}

	/** @brief 헤더 하나를 파싱·코드젠합니다. 최신이면 건너뛰고, 어노테이션이 없으면 비웁니다. */
	static void processInputFile( const sw::string& inputFile, const CommandLineArgs& commandLineArgs, sw::atomic<int32>& errorCount )
	{
		const sw::string genPath =
			sw::ParserUtil::makeGeneratedPath( commandLineArgs._outputDir, inputFile, sw::ParserContext::getSharedConfig()._emitCppExtension );

		if ( isUpToDate( genPath, inputFile, commandLineArgs ) )
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
			if ( emitEmptyGenerated( inputFile, commandLineArgs ) == false )
			{
				SW_LOG_ERROR( "Code generation failed: %#", inputFile );
				++errorCount;
			}
			return;
		}

		SW_LOG_TRACE( "── Parsing: %#", inputFile );

		sw::ParserContext context;
		if ( context.parse( inputFile, commandLineArgs._listIncludePaths, &sourceContent ) == false )
		{
			SW_LOG_ERROR( "Parse failed: %#", inputFile );
			++errorCount;
			return;
		}

		sw::AstVisitor visitor( context.getTranslationUnit() );
		if ( visitor.visit() == false || visitor.hasError() )
		{
			SW_LOG_ERROR( "AST analysis failed: %#", inputFile );
			++errorCount;
			return;
		}

		sw::CodeGenerator generator(
			visitor.getCollectedTypes(),
			visitor.getCollectedEnums(),
			inputFile,
			commandLineArgs._outputDir );

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
	static bool emitFlagOpsUmbrella( const CommandLineArgs& commandLineArgs )
	{
		const ParserClangConfig& cfg	 = ParserContext::getSharedConfig();
		const string			 outPath = FileUtil::joinPath( commandLineArgs._outputDir, cfg._emitFlagOpsHeader );

		CodeEmitBuffer buffer;
		CodeEmit	   e( buffer );
		e.line( cfg._emitAutoGeneratedBanner );
		e.line( emitDirectiveConstants::kPragmaOnce );
		e.blank();
		e.line( emitDirectiveConstants::kIfndefParser );

		bool bAnyFlags = false;
		for ( const string& inputFile : commandLineArgs._listInputFiles )
		{
			const string genHeader = ParserUtil::makeGeneratedPath( commandLineArgs._outputDir, inputFile, cfg._emitHeaderExtension );
			string		 genText;
			if ( FileUtil::fileExists( genHeader ) == false || FileUtil::readTextFile( genHeader, genText ) == false )
				continue;
			if ( genText.find( cfg._emitFlagOpsMarker ) == string::npos )
				continue;

			bAnyFlags			   = true;
			const string headerInc = ParserUtil::makeHeaderIncludePath( inputFile, commandLineArgs._listIncludePaths );
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
	sw::CommandLineArgs commandLineArgs;
	if ( sw::parseCommandLine( argc, argv, commandLineArgs ) == false )
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
	if ( commandLineArgs._emitBuiltinsGenPath.empty() == false )
	{
		if ( commandLineArgs._emitTemplatesDir.empty() )
		{
			SW_LOG_ERROR( "%# requires %#.", sw::cliConstants::kEmitBuiltinsGen, sw::cliConstants::kEmitTemplates );
			logger->shutdown();
			return 1;
		}
		if ( sw::EmitTemplateStore::instance().loadDirectory( commandLineArgs._emitTemplatesDir ) == false )
		{
			SW_LOG_ERROR( "Failed to load --emit-templates: %#", commandLineArgs._emitTemplatesDir );
			logger->shutdown();
			return 1;
		}
		const bool ok = sw::emitReflectBuiltinsGen( commandLineArgs._builtinsPath, commandLineArgs._emitBuiltinsGenPath );
		logger->shutdown();
		return ok ? 0 : 1;
	}

	// 3) 출력 디렉터리를 인클루드 경로 최상단에 1회 선행 배치 (스레드별 벡터 복사/삽입 제거)
	if ( commandLineArgs._outputDir.empty() == false )
		commandLineArgs._listIncludePaths.insert( commandLineArgs._listIncludePaths.begin(), commandLineArgs._outputDir );

	// 3) 공유 테이블 로드 (builtins / AnnotationMeta / Templates)
	if ( commandLineArgs._builtinsPath.empty() == false )
	{
		if ( sw::loadReflectBuiltins( commandLineArgs._builtinsPath ) == false )
		{
			SW_LOG_ERROR( "Failed to load --builtins: %#", commandLineArgs._builtinsPath );
			logger->shutdown();
			return 1;
		}
	}
	else
	{
		SW_LOG_WARNING( "No --builtins; scalar aliases / std containers will not be registered." );
	}

	if ( commandLineArgs._annotationMetaPath.empty() == false )
	{
		if ( sw::AnnotationMeta::instance().loadFile( commandLineArgs._annotationMetaPath ) == false )
		{
			SW_LOG_ERROR( "Failed to load --annotation-meta: %#", commandLineArgs._annotationMetaPath );
			logger->shutdown();
			return 1;
		}
	}
	else
	{
		SW_LOG_WARNING( "No --annotation-meta; PROPERTY/FUNCTION/REFLECT tokens will be ignored." );
	}

	if ( commandLineArgs._emitTemplatesDir.empty() == false )
	{
		if ( sw::EmitTemplateStore::instance().loadDirectory( commandLineArgs._emitTemplatesDir ) == false )
		{
			SW_LOG_ERROR( "Failed to load --emit-templates: %#", commandLineArgs._emitTemplatesDir );
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
	if ( commandLineArgs._builtinsPath.empty() == false && sw::FileUtil::fileExists( commandLineArgs._builtinsPath ) )
		commandLineArgs._builtinsTimestamp = sw::FileUtil::getFileTimestamp( commandLineArgs._builtinsPath );

	if ( commandLineArgs._annotationMetaPath.empty() == false && sw::FileUtil::fileExists( commandLineArgs._annotationMetaPath ) )
		commandLineArgs._annotationMetaTimestamp = sw::FileUtil::getFileTimestamp( commandLineArgs._annotationMetaPath );

	if ( commandLineArgs._emitTemplatesDir.empty() == false )
	{
		sw::vector<sw::string> templates;
		if ( sw::FileUtil::collectFiles( commandLineArgs._emitTemplatesDir, sw::ParserContext::getSharedConfig()._emitTemplateExtension, templates, false, false ) )
		{
			for ( const sw::string& tplPath : templates )
			{
				const uint64 tplTime = sw::FileUtil::getFileTimestamp( tplPath );
				if ( tplTime > commandLineArgs._maxTemplateTimestamp )
					commandLineArgs._maxTemplateTimestamp = tplTime;
			}
		}
	}

	sw::atomic<int32> errorCount{ 0 };

	uint32 workerCount = std::thread::hardware_concurrency();
	if ( workerCount == 0 )
		workerCount = 1;
	SW_LOG_INFO( "Parsing %# input(s) with %# worker(s).", commandLineArgs._listInputFiles.size(), workerCount );

	sw::CpuTimer parseTimer;
	parseTimer.resetTimer();
	parseTimer.startTimer();

	if ( commandLineArgs._listInputFiles.empty() == false )
	{
		sw::TaskManager taskManager;
		if ( taskManager.initialize( workerCount ) == false )
		{
			SW_LOG_ERROR( "Failed to initialize TaskManager." );
			logger->shutdown();
			return 1;
		}

		for ( const sw::string& inputFile : commandLineArgs._listInputFiles )
		{
			sw::TaskHandle handle = taskManager.emplaceTask(
				"ParseHeader",
				SW_DELEGATE_LAMBDA( sw::TaskDelegate, [&commandLineArgs, &errorCount, inputFile]()
			{
				sw::processInputFile( inputFile, commandLineArgs, errorCount );
			} ) );
			handle.submit();
		}

		taskManager.waitAll();
		taskManager.shutdown();
	}

	if ( sw::emitFlagOpsUmbrella( commandLineArgs ) == false )
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
