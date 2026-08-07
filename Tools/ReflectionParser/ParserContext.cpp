/**
 * @file ParserContext.cpp
 * @brief libclang 파싱 컨텍스트 및 설정 캐시 구현
 */
#include "ParserContext.h"
#include "ParserUtil.h"
#include "Core/Common/Common.h"
#include "Core/Utility/File/FileUtil.h"

#include <mutex>

namespace sw::tool
{
	namespace
	{
		ParserClangConfig s_sharedConfig;
		CXIndex			  s_sharedIndex = nullptr;
		std::once_flag	  s_configOnce;
		std::once_flag	  s_indexOnce;
		bool			  s_configOk = false;

		std::string findConfigFile( const std::string& relPath )
		{
			std::string cur = FileUtil::getCurrentPath();
			while ( true )
			{
				std::string candidate = FileUtil::normalizePath( cur + "/" + relPath );
				if ( FileUtil::isFileExist( candidate ) )
					return candidate;

				std::string parent = FileUtil::getDirectoryPart( cur );
				if ( parent.empty() || parent == cur )
					break;
				cur = parent;
			}
			return {};
		}

		std::vector<std::string> extractJsonArray( const std::string& json, const std::string& key )
		{
			std::vector<std::string> result;
			const std::string		 keyToken = "\"" + key + "\"";
			const size_t			 pos	  = json.find( keyToken );
			if ( pos == std::string::npos )
				return result;

			const size_t lbracket = json.find( '[', pos );
			const size_t rbracket = json.find( ']', lbracket );
			if ( lbracket == std::string::npos || rbracket == std::string::npos )
				return result;

			const std::string arrayContent = json.substr( lbracket + 1, rbracket - lbracket - 1 );
			size_t			  cursor	   = 0;
			while ( cursor < arrayContent.size() )
			{
				const size_t q1 = arrayContent.find( '"', cursor );
				if ( q1 == std::string::npos )
					break;
				const size_t q2 = arrayContent.find( '"', q1 + 1 );
				if ( q2 == std::string::npos )
					break;
				result.push_back( arrayContent.substr( q1 + 1, q2 - q1 - 1 ) );
				cursor = q2 + 1;
			}
			return result;
		}

		std::string extractJsonValue( const std::string& json, const std::string& key )
		{
			const std::string keyToken = "\"" + key + "\"";
			const size_t	  pos	   = json.find( keyToken );
			if ( pos == std::string::npos )
				return {};

			const size_t colon = json.find( ':', pos );
			if ( colon == std::string::npos )
				return {};

			const size_t q1 = json.find( '"', colon );
			if ( q1 == std::string::npos )
				return {};
			const size_t q2 = json.find( '"', q1 + 1 );
			if ( q2 == std::string::npos )
				return {};
			return json.substr( q1 + 1, q2 - q1 - 1 );
		}
	} // namespace

	bool ParserClangConfig::load()
	{
		baseArgs.clear();
		bLoaded = false;

		std::string llvmPath;
		std::string msvcToolsDir;
		std::string winSdkDir;
		std::string winSdkVer;

		const std::string parserCfgPath = findConfigFile( "Config/parser_config.json" );
		if ( parserCfgPath.empty() == false )
		{
			const std::string fullJson = readTextFile( parserCfgPath );
			if ( fullJson.empty() == false )
			{
				baseArgs = extractJsonArray( fullJson, "parser_args" );
				if ( baseArgs.empty() )
					baseArgs = extractJsonArray( fullJson, "default_parser_args" );
			}
		}

		const std::string engineCfgPath = findConfigFile( "Config/engine_config.json" );
		if ( engineCfgPath.empty() == false )
		{
			const std::string fullJson = readTextFile( engineCfgPath );
			if ( fullJson.empty() == false )
			{
				llvmPath	 = extractJsonValue( fullJson, "llvm_path" );
				msvcToolsDir = extractJsonValue( fullJson, "msvc_tools_dir" );
				winSdkDir	 = extractJsonValue( fullJson, "windows_sdk_dir" );
				winSdkVer	 = extractJsonValue( fullJson, "windows_sdk_version" );
			}
		}

		if ( baseArgs.empty() )
		{
			SW_LOG_ERROR( "[ParserContext] Config/parser_config.json could not be loaded or contains no parser_args." );
			return false;
		}

		if ( llvmPath.empty() )
		{
			const utf8* envLlvm = std::getenv( "LLVM_DIR" );
			if ( envLlvm == nullptr )
				envLlvm = std::getenv( "LLVM_HOME" );
			if ( envLlvm != nullptr )
				llvmPath = envLlvm;
		}

		const std::string llvmClangDir = FileUtil::normalizePath( llvmPath + "/lib/clang" );
		if ( FileUtil::isDirectoryExist( llvmClangDir ) )
		{
			std::vector<std::string> clangSubFolders;
			FileUtil::collectFolders( llvmClangDir, clangSubFolders, false );
			for ( const std::string& folder : clangSubFolders )
			{
				const std::string clangInc = FileUtil::normalizePath( folder + "/include" );
				if ( FileUtil::isDirectoryExist( clangInc ) )
				{
					baseArgs.emplace_back( "-isystem" );
					baseArgs.emplace_back( clangInc );
					break;
				}
			}
		}

		const std::string msvcInc = FileUtil::normalizePath( msvcToolsDir + "/include" );
		if ( msvcToolsDir.empty() == false && FileUtil::isDirectoryExist( msvcInc ) )
		{
			baseArgs.emplace_back( "-isystem" );
			baseArgs.emplace_back( msvcInc );
		}

		if ( winSdkDir.empty() == false && winSdkVer.empty() == false )
		{
			const std::string ucrtPath = FileUtil::normalizePath( winSdkDir + "/Include/" + winSdkVer + "/ucrt" );
			if ( FileUtil::isDirectoryExist( ucrtPath ) )
			{
				baseArgs.emplace_back( "-isystem" );
				baseArgs.emplace_back( ucrtPath );
			}
		}

		bLoaded = true;
		SW_LOG_INFO( "[ParserContext] Cached clang config (%# base args).", static_cast<uint32>( baseArgs.size() ) );
		return true;
	}

	std::vector<std::string> ParserClangConfig::buildArgs( const std::vector<std::string>& includePaths ) const
	{
		std::vector<std::string> args = baseArgs;
		args.reserve( args.size() + includePaths.size() );
		for ( const std::string& inc : includePaths )
			args.push_back( "-I" + inc );
		return args;
	}

	bool ParserContext::ensureSharedConfig()
	{
		std::call_once( s_configOnce, []()
		{
			s_configOk = s_sharedConfig.load();
		} );
		return s_configOk;
	}

	CXIndex ParserContext::getSharedIndex()
	{
		std::call_once( s_indexOnce, []()
		{
			s_sharedIndex = clang_createIndex( 0, 0 );
		} );
		return s_sharedIndex;
	}

	const ParserClangConfig& ParserContext::getSharedConfig()
	{
		ensureSharedConfig();
		return s_sharedConfig;
	}

	void ParserContext::shutdownShared()
	{
		if ( s_sharedIndex )
		{
			clang_disposeIndex( s_sharedIndex );
			s_sharedIndex = nullptr;
		}
	}

	ParserContext::ParserContext() = default;

	ParserContext::~ParserContext()
	{
		if ( _translationUnit )
		{
			clang_disposeTranslationUnit( _translationUnit );
			_translationUnit = nullptr;
		}
	}

	bool ParserContext::parse( const std::string& filePath, const std::vector<std::string>& includePaths )
	{
		if ( ensureSharedConfig() == false )
			return false;

		CXIndex index = getSharedIndex();
		if ( index == nullptr )
		{
			SW_LOG_ERROR( "[ParserContext] Failed to create CXIndex." );
			return false;
		}

		if ( _translationUnit )
		{
			clang_disposeTranslationUnit( _translationUnit );
			_translationUnit = nullptr;
		}

		const std::vector<std::string> argStrings = getSharedConfig().buildArgs( includePaths );
		std::vector<const utf8*>	   args;
		args.reserve( argStrings.size() );
		for ( const std::string& s : argStrings )
			args.push_back( s.c_str() );

		constexpr uint32 kParseFlags =
			CXTranslationUnit_SkipFunctionBodies |
			CXTranslationUnit_DetailedPreprocessingRecord;

		_translationUnit = clang_parseTranslationUnit(
			index,
			filePath.c_str(),
			args.data(),
			static_cast<int>( args.size() ),
			nullptr, 0,
			kParseFlags );

		if ( _translationUnit == nullptr )
		{
			SW_LOG_ERROR( "[ParserContext] clang_parseTranslationUnit failed for: %#", filePath );
			return false;
		}

		const uint32 numDiags = clang_getNumDiagnostics( _translationUnit );
		bool		 hasError = false;

		for ( uint32 i = 0; i < numDiags; ++i )
		{
			CXDiagnostic		 diag = clang_getDiagnostic( _translationUnit, i );
			CXDiagnosticSeverity sev  = clang_getDiagnosticSeverity( diag );

			if ( sev >= CXDiagnostic_Error )
			{
				CXString msg = clang_formatDiagnostic( diag, clang_defaultDiagnosticDisplayOptions() );
				SW_LOG_ERROR( "[ParserContext] %#", clang_getCString( msg ) );
				clang_disposeString( msg );
				hasError = true;
			}

			clang_disposeDiagnostic( diag );
		}

		if ( hasError )
		{
			SW_LOG_ERROR( "[ParserContext] Parsing failed with errors. Check include paths with --include." );
			return false;
		}

		return true;
	}
} // namespace sw::tool
