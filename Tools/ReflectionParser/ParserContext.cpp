/**
 * @file ParserContext.cpp
 * @brief libclang 파싱 컨텍스트 및 설정 캐시 구현
 */
#include "ParserContext.h"
#include "ParserUtil.h"
#include "Core/Common/Common.h"
#include "Core/Utility/File/FileUtil.h"

#include <algorithm>


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
				std::string candidate = ( cur + "/" + relPath );
				if ( FileUtil::isFileExist( candidate ) )
					return candidate;

				std::string parent = FileUtil::getDirectoryPart( cur );
				if ( parent.empty() || parent == cur )
					break;
				cur = parent;
			}
			return {};
		}

		std::string unescapeJsonString( const std::string& escaped )
		{
			std::string out;
			out.reserve( escaped.size() );
			for ( size_t i = 0; i < escaped.size(); ++i )
			{
				if ( escaped[i] == '\\' && i + 1 < escaped.size() )
				{
					const char next = escaped[i + 1];
					if ( next == '"' || next == '\\' || next == '/' )
					{
						out.push_back( next );
						++i;
						continue;
					}
					if ( next == 'n' )
					{
						out.push_back( '\n' );
						++i;
						continue;
					}
					if ( next == 't' )
					{
						out.push_back( '\t' );
						++i;
						continue;
					}
				}
				out.push_back( escaped[i] );
			}
			return out;
		}

		std::vector<std::string> extractJsonArray( const std::string& json, const std::string& key )
		{
			std::vector<std::string> result;
			const std::string		 keyToken = "\"" + key + "\"";
			const size_t			 pos	  = json.find( keyToken );
			if ( pos == std::string::npos )
				return result;

			const size_t lbracket = json.find( '[', pos );
			if ( lbracket == std::string::npos )
				return result;

			// Walk the array so strings containing ']' / escaped quotes stay intact.
			size_t	cursor	 = lbracket + 1;
			bool	inString = false;
			size_t	depth	 = 1;
			size_t	rbracket = std::string::npos;
			while ( cursor < json.size() )
			{
				const char c = json[cursor];
				if ( inString )
				{
					if ( c == '\\' && cursor + 1 < json.size() )
					{
						cursor += 2;
						continue;
					}
					if ( c == '"' )
						inString = false;
					++cursor;
					continue;
				}
				if ( c == '"' )
				{
					inString = true;
					++cursor;
					continue;
				}
				if ( c == '[' )
				{
					++depth;
					++cursor;
					continue;
				}
				if ( c == ']' )
				{
					--depth;
					if ( depth == 0 )
					{
						rbracket = cursor;
						break;
					}
					++cursor;
					continue;
				}
				++cursor;
			}
			if ( rbracket == std::string::npos )
				return result;

			const std::string arrayContent = json.substr( lbracket + 1, rbracket - lbracket - 1 );
			cursor						   = 0;
			while ( cursor < arrayContent.size() )
			{
				const size_t q1 = arrayContent.find( '"', cursor );
				if ( q1 == std::string::npos )
					break;

				size_t q2 = q1 + 1;
				while ( q2 < arrayContent.size() )
				{
					if ( arrayContent[q2] == '\\' )
					{
						q2 += ( q2 + 1 < arrayContent.size() ) ? 2 : 1;
						continue;
					}
					if ( arrayContent[q2] == '"' )
						break;
					++q2;
				}
				if ( q2 >= arrayContent.size() )
					break;

				result.push_back( unescapeJsonString( arrayContent.substr( q1 + 1, q2 - q1 - 1 ) ) );
				cursor = q2 + 1;
			}
			return result;
		}

		std::vector<std::string> defaultParserArgsFallback()
		{
			// Mirrors Config/parser_config.defaults.json — used when the local
			// generated Config/parser_config.json is missing (gitignored).
			return {
				"-std=c++17",
				"-D__REFLECT_PARSER__",
				"-DSW_API=",
				"-DREFLECT(...)=__attribute__((annotate(\"REFLECT;\" #__VA_ARGS__)))",
				"-DPROPERTY(...)=__attribute__((annotate(\"PROPERTY;\" #__VA_ARGS__)))",
				"-DFUNCTION(...)=__attribute__((annotate(\"FUNCTION;\" #__VA_ARGS__)))",
				"-DENUM(...)=__attribute__((annotate(\"ENUM;\" #__VA_ARGS__)))",
				"-x",
				"c++",
				"-w",
			};
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

#if defined( SW_PLATFORM_WINDOWS )
		constexpr const utf8* kPlatformParserKey = "windows";
#elif defined( SW_PLATFORM_LINUX )
		constexpr const utf8* kPlatformParserKey = "linux";
#elif defined( SW_PLATFORM_MACOS )
		constexpr const utf8* kPlatformParserKey = "darwin";
#else
		constexpr const utf8* kPlatformParserKey = "";
#endif

		auto loadParserArgsFromFile = [&]( const std::string& cfgPath ) -> bool
		{
			if ( cfgPath.empty() )
				return false;
			const std::string fullJson = readTextFile( cfgPath );
			if ( fullJson.empty() )
				return false;

			// Prefer default + current-OS platform args so a Windows-generated
			// parser_args (with -fms-compatibility) is never reused on Linux/macOS.
			const std::vector<std::string> defaultArgs = extractJsonArray( fullJson, "default_parser_args" );
#if defined( SW_PLATFORM_WINDOWS ) || defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
			const std::vector<std::string> platformArgs = extractJsonArray( fullJson, kPlatformParserKey );
#else
			(void)kPlatformParserKey;
			const std::vector<std::string> platformArgs{};
#endif
			if ( defaultArgs.empty() == false )
			{
				baseArgs = defaultArgs;
				baseArgs.insert( baseArgs.end(), platformArgs.begin(), platformArgs.end() );
				return true;
			}

			baseArgs = extractJsonArray( fullJson, "parser_args" );
			return baseArgs.empty() == false;
		};

		if ( loadParserArgsFromFile( findConfigFile( "Config/parser_config.json" ) ) == false )
		{
			// Generated/local file missing (gitignored) — use committed defaults.
			if ( loadParserArgsFromFile( findConfigFile( "Config/parser_config.defaults.json" ) ) == false )
			{
				baseArgs = defaultParserArgsFallback();
				SW_LOG_WARNING( "[ParserContext] Using built-in parser_args fallback (no Config/parser_config*.json)." );
			}
		}

#if !defined( SW_PLATFORM_WINDOWS )
		// Safety: strip MSVC-compat flags if a stale merged parser_args leaked in.
		baseArgs.erase(
			std::remove_if( baseArgs.begin(), baseArgs.end(),
							[]( const std::string& a )
		{
			return a == "-fms-compatibility" || a == "-fms-extensions"
				   || a.rfind( "-fms-compatibility-version", 0 ) == 0;
		} ),
			baseArgs.end() );
#endif

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
			SW_LOG_ERROR( "[ParserContext] No parser_args available (config + fallback empty)." );
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

		// Builtin headers / __GCC_ATOMIC_* need a correct Clang resource-dir (libclang ≠ gcc).
		const std::string llvmClangDir = FileUtil::normalizeSeparators( llvmPath + "/lib/clang" );
		if ( FileUtil::isDirectoryExist( llvmClangDir ) )
		{
			std::vector<std::string> clangSubFolders;
			FileUtil::collectFolders( llvmClangDir, clangSubFolders, false, false );
			for ( const std::string& folder : clangSubFolders )
			{
				const std::string resourceDir = FileUtil::normalizeSeparators( folder );
				const std::string clangInc	  = FileUtil::normalizeSeparators( folder + "/include" );
				if ( FileUtil::isDirectoryExist( clangInc ) == false )
					continue;

				baseArgs.emplace_back( "-resource-dir" );
				baseArgs.emplace_back( resourceDir );
				baseArgs.emplace_back( "-isystem" );
				baseArgs.emplace_back( clangInc );
				break;
			}
		}

#if defined( SW_PLATFORM_WINDOWS )
		const std::string msvcInc = FileUtil::normalizeSeparators( msvcToolsDir + "/include" );
		if ( msvcToolsDir.empty() == false && FileUtil::isDirectoryExist( msvcInc ) )
		{
			baseArgs.emplace_back( "-isystem" );
			baseArgs.emplace_back( msvcInc );
		}

		if ( winSdkDir.empty() == false && winSdkVer.empty() == false )
		{
			const std::string ucrtPath = FileUtil::normalizeSeparators( winSdkDir + "/Include/" + winSdkVer + "/ucrt" );
			if ( FileUtil::isDirectoryExist( ucrtPath ) )
			{
				baseArgs.emplace_back( "-isystem" );
				baseArgs.emplace_back( ucrtPath );
			}
		}
#endif

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
