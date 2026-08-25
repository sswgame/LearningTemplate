
#include "ParserContext.h"
#include "ParserDefines.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Common/Common.h"

namespace sw
{
	namespace
	{
		static ParserClangConfig s_sharedConfig;
		static std::once_flag	 s_configOnce;
		static bool				 s_configOk = false;

		static string findConfigFile( const string& relPath )
		{
			string cur = FileUtil::getCurrentPath();
			while ( true )
			{
				const string candidate = FileUtil::joinPath( cur, relPath );
				if ( FileUtil::fileExists( candidate ) )
					return candidate;

				const string parent = FileUtil::getDirectoryPart( cur );
				if ( parent.empty() || parent == cur )
					break;
				cur = parent;
			}
			return {};
		}

		static string jsonKeyToken( const string& key )
		{
			StringBuilder<constant::kMaxBuffer128> builder;
			builder.appendFormat( "\"%#\"", key );
			return string( builder.view() );
		}

		static size_t findJsonKey( const string& json, const string& keyToken )
		{
			size_t pos = json.find( keyToken );
			while ( pos != string::npos )
			{
				size_t after = pos + keyToken.size();
				while ( after < json.size() &&
						( json[after] == ' ' || json[after] == '\t' || json[after] == '\r' || json[after] == '\n' ) )
					++after;

				if ( after < json.size() && json[after] == ':' )
					return pos;

				pos = json.find( keyToken, pos + 1 );
			}
			return string::npos;
		}

		static string unescapeJsonString( const string& escaped )
		{
			string out;
			out.reserve( escaped.size() );
			for ( size_t charIndex = 0; charIndex < escaped.size(); ++charIndex )
			{
				if ( escaped[charIndex] == '\\' && charIndex + 1 < escaped.size() )
				{
					const utf8 next = escaped[charIndex + 1];
					if ( next == '"' || next == '\\' || next == '/' )
					{
						out.push_back( next );
						++charIndex;
						continue;
					}
					if ( next == 'n' )
					{
						out.push_back( '\n' );
						++charIndex;
						continue;
					}
					if ( next == 't' )
					{
						out.push_back( '\t' );
						++charIndex;
						continue;
					}
				}
				out.push_back( escaped[charIndex] );
			}
			return out;
		}

		static vector<string> extractJsonArray( const string& json, const string& key )
		{
			vector<string> resultList;
			const string   keyToken = jsonKeyToken( key );
			const size_t   pos		= findJsonKey( json, keyToken );
			if ( pos == string::npos )
				return resultList;

			const size_t lbracket = json.find( '[', pos );
			if ( lbracket == string::npos )
				return resultList;

			size_t cursor	= lbracket + 1;
			bool   inString = false;
			size_t depth	= 1;
			size_t rbracket = string::npos;
			while ( cursor < json.size() )
			{
				const utf8 c = json[cursor];
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
			if ( rbracket == string::npos )
				return resultList;

			const string arrayContent = json.substr( lbracket + 1, rbracket - lbracket - 1 );
			cursor					  = 0;
			while ( cursor < arrayContent.size() )
			{
				const size_t q1 = arrayContent.find( '"', cursor );
				if ( q1 == string::npos )
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

				resultList.push_back( unescapeJsonString( arrayContent.substr( q1 + 1, q2 - q1 - 1 ) ) );
				cursor = q2 + 1;
			}
			return resultList;
		}

		static string rewriteLegacyClangArg( const string& arg )
		{
			if ( arg == "-fno-spellchecking" )
				return "-fno-spell-checking";
			return arg;
		}

		static void appendUnique( vector<string>& dstList, const vector<string>& srcList )
		{
			for ( const string& item : srcList )
			{
				const string normalized = rewriteLegacyClangArg( item );
				if ( std::find( dstList.begin(), dstList.end(), normalized ) == dstList.end() )
					dstList.push_back( normalized );
			}
		}

		static string extractJsonValue( const string& json, const string& key )
		{
			const string keyToken = jsonKeyToken( key );
			const size_t pos	  = findJsonKey( json, keyToken );
			if ( pos == string::npos )
				return {};

			const size_t colon = json.find( ':', pos );
			if ( colon == string::npos )
				return {};

			const size_t q1 = json.find( '"', colon );
			if ( q1 == string::npos )
				return {};

			size_t q2 = q1 + 1;
			while ( q2 < json.size() )
			{
				if ( json[q2] == '\\' )
				{
					q2 += ( q2 + 1 < json.size() ) ? 2 : 1;
					continue;
				}
				if ( json[q2] == '"' )
					break;
				++q2;
			}
			if ( q2 >= json.size() )
				return {};

			return unescapeJsonString( json.substr( q1 + 1, q2 - q1 - 1 ) );
		}

		static string extractJsonObjectBody( const string& json, const string& key )
		{
			const string keyToken = jsonKeyToken( key );
			const size_t pos	  = findJsonKey( json, keyToken );
			if ( pos == string::npos )
				return {};

			const size_t lbrace = json.find( '{', pos + keyToken.size() );
			if ( lbrace == string::npos )
				return {};

			size_t cursor	= lbrace + 1;
			bool   inString = false;
			size_t depth	= 1;
			while ( cursor < json.size() )
			{
				const utf8 c = json[cursor];
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
				if ( c == '{' )
				{
					++depth;
					++cursor;
					continue;
				}
				if ( c == '}' )
				{
					--depth;
					if ( depth == 0 )
						return json.substr( lbrace + 1, cursor - lbrace - 1 );
					++cursor;
					continue;
				}
				++cursor;
			}
			return {};
		}

		static uint32 extractJsonUint( const string& json, const string& key, uint32 defaultValue )
		{
			const string keyToken = jsonKeyToken( key );
			const size_t pos	  = findJsonKey( json, keyToken );
			if ( pos == string::npos )
				return defaultValue;

			const size_t colon = json.find( ':', pos );
			if ( colon == string::npos )
				return defaultValue;

			size_t cursor = colon + 1;
			while ( cursor < json.size() &&
					( json[cursor] == ' ' || json[cursor] == '\t' || json[cursor] == '\r' || json[cursor] == '\n' ) )
				++cursor;

			if ( cursor >= json.size() || json[cursor] < '0' || json[cursor] > '9' )
				return defaultValue;

			uint32 value = 0;
			while ( cursor < json.size() && json[cursor] >= '0' && json[cursor] <= '9' )
			{
				value = value * 10u + static_cast<uint32>( json[cursor] - '0' );
				++cursor;
			}
			return value;
		}

		static void assignIfPresent( string& dst, const string& json, const utf8* key )
		{
			const string value = extractJsonValue( json, key );
			if ( value.empty() == false )
				dst = value;
		}

		static void applyPathsSection( ParserClangConfig& config, const string& body )
		{
			assignIfPresent( config.llvmClangRel, body, jsonKeyConstants::kLlvmClangRel );
			assignIfPresent( config.clangIncludeRel, body, jsonKeyConstants::kClangIncludeRel );
			assignIfPresent( config.msvcIncludeRel, body, jsonKeyConstants::kMsvcIncludeRel );
			assignIfPresent( config.winSdkIncludeRel, body, jsonKeyConstants::kWinSdkIncludeRel );
			assignIfPresent( config.winSdkUcrtRel, body, jsonKeyConstants::kWinSdkUcrtRel );
		}

		static void applyClangFlagsSection( ParserClangConfig& config, const string& body )
		{
			assignIfPresent( config.flagIncludePrefix, body, jsonKeyConstants::kFlagIncludePrefix );
			assignIfPresent( config.flagIsystem, body, jsonKeyConstants::kFlagIsystem );
			assignIfPresent( config.flagResourceDir, body, jsonKeyConstants::kFlagResourceDir );
			assignIfPresent( config.flagFmsCompatibility, body, jsonKeyConstants::kFlagFmsCompatibility );
			assignIfPresent( config.flagFmsExtensions, body, jsonKeyConstants::kFlagFmsExtensions );
			assignIfPresent( config.flagFmsCompatVersionPrefix, body, jsonKeyConstants::kFlagFmsCompatVerPrefix );
		}

		static void applyEmitSection( ParserClangConfig& config, const string& body )
		{
			assignIfPresent( config.emitCppExtension, body, jsonKeyConstants::kEmitCppExtension );
			assignIfPresent( config.emitHeaderExtension, body, jsonKeyConstants::kEmitHeaderExtension );
			assignIfPresent( config.emitTemplateExtension, body, jsonKeyConstants::kEmitTemplateExtension );
			assignIfPresent( config.emitAutoGeneratedBanner, body, jsonKeyConstants::kEmitAutoGeneratedBanner );
			assignIfPresent( config.emitPlaceholderMarker, body, jsonKeyConstants::kEmitPlaceholderMarker );
			assignIfPresent( config.emitRegenByParserMarker, body, jsonKeyConstants::kEmitRegenMarker );
			assignIfPresent( config.emitGeneratedNsOpen, body, jsonKeyConstants::kEmitGeneratedNsOpen );
			assignIfPresent( config.emitGeneratedNsClose, body, jsonKeyConstants::kEmitGeneratedNsClose );
		}

		static void applyTuningSection( ParserClangConfig& config, const string& body )
		{
			config.sourceLookbackBytes =
				extractJsonUint( body, jsonKeyConstants::kSourceLookbackBytes, config.sourceLookbackBytes );
		}

		static void mergeConfigSection( ParserClangConfig& config, const string& defaultsJson, const string& localJson,
										const utf8* sectionKey, void ( *applyFn )( ParserClangConfig&, const string& ) )
		{
			const string fromDefaults = extractJsonObjectBody( defaultsJson, sectionKey );
			const string fromLocal	  = extractJsonObjectBody( localJson, sectionKey );
			if ( fromDefaults.empty() == false )
				applyFn( config, fromDefaults );
			if ( fromLocal.empty() == false )
				applyFn( config, fromLocal );
		}

		static vector<string> loadArgsFromDocument( const string& json, const utf8* platformKey )
		{
			vector<string> outList;
			const string   argsSection = extractJsonObjectBody( json, jsonKeyConstants::kParserArgsSection );
			if ( argsSection.empty() == false )
			{
				appendUnique( outList, extractJsonArray( argsSection, jsonKeyConstants::kArgsDefault ) );
				const string platformObj = extractJsonObjectBody( argsSection, jsonKeyConstants::kArgsPlatform );
				const string platformSrc = platformObj.empty() ? argsSection : platformObj;
#if defined( SW_PLATFORM_WINDOWS ) || defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
				appendUnique( outList, extractJsonArray( platformSrc, platformKey ) );
#else
				(void)platformKey;
				(void)platformSrc;
#endif
				appendUnique( outList, extractJsonArray( argsSection, jsonKeyConstants::kArgsExtra ) );
				return outList;
			}

			return outList;
		}

#if !defined( SW_PLATFORM_WINDOWS )
		static bool isMsvcCompatArg( const string& arg, const string& fmsCompatibility, const string& fmsExtensions,
									 const string& fmsCompatVersionPrefix )
		{
			if ( arg == fmsCompatibility )
				return true;
			if ( arg == fmsExtensions )
				return true;
			if ( arg.rfind( fmsCompatVersionPrefix, 0 ) == 0 )
				return true;
			return false;
		}

		static void eraseMsvcCompatArgs( vector<string>& argList, const ParserClangConfig& config )
		{
			size_t writeIndex = 0;
			for ( size_t readIndex = 0; readIndex < argList.size(); ++readIndex )
			{
				if ( isMsvcCompatArg( argList[readIndex], config.flagFmsCompatibility, config.flagFmsExtensions,
									  config.flagFmsCompatVersionPrefix ) )
					continue;
				if ( writeIndex != readIndex )
					argList[writeIndex] = argList[readIndex];
				++writeIndex;
			}
			argList.resize( writeIndex );
		}
#endif

		static void loadSharedConfigOnce()
		{
			s_configOk = s_sharedConfig.load();
		}
	} // namespace

	bool ParserClangConfig::load()
	{
		baseArgList.clear();
		bLoaded = false;

		string llvmPath;
		string msvcToolsDir;
		string winSdkDir;
		string winSdkVer;

#if defined( SW_PLATFORM_WINDOWS )
		constexpr const utf8* kPlatformParserKey = "windows";
#elif defined( SW_PLATFORM_LINUX )
		constexpr const utf8* kPlatformParserKey = "linux";
#elif defined( SW_PLATFORM_MACOS )
		constexpr const utf8* kPlatformParserKey = "darwin";
#else
		constexpr const utf8* kPlatformParserKey = "";
#endif

		string		 defaultsJson;
		string		 localJson;
		const string defaultsPath = findConfigFile( pathConstants::kParserConfigDefaults );
		const string localPath	  = findConfigFile( pathConstants::kParserConfig );
		if ( defaultsPath.empty() == false )
			FileUtil::readTextFile( defaultsPath, defaultsJson );
		if ( localPath.empty() == false )
			FileUtil::readTextFile( localPath, localJson );

		BLOCK( "Load Base Arguments from Config" )
		{
			vector<string> mergedList = loadArgsFromDocument( defaultsJson, kPlatformParserKey );
			appendUnique( mergedList, loadArgsFromDocument( localJson, kPlatformParserKey ) );
			baseArgList = std::move( mergedList );

#if !defined( SW_PLATFORM_WINDOWS )
			eraseMsvcCompatArgs( baseArgList, *this );
#endif
		}

		BLOCK( "Load paths / clang_flags / emit / tuning" )
		{
			mergeConfigSection( *this, defaultsJson, localJson, jsonKeyConstants::kPaths, applyPathsSection );
			mergeConfigSection( *this, defaultsJson, localJson, jsonKeyConstants::kClangFlags, applyClangFlagsSection );
			mergeConfigSection( *this, defaultsJson, localJson, jsonKeyConstants::kEmit, applyEmitSection );
			mergeConfigSection( *this, defaultsJson, localJson, jsonKeyConstants::kTuning, applyTuningSection );
		}

		BLOCK( "Load Engine Config" )
		{
			const string engineCfgPath = findConfigFile( pathConstants::kToolchainConfig );
			if ( engineCfgPath.empty() == false )
			{
				string fullJson;
				FileUtil::readTextFile( engineCfgPath, fullJson );
				if ( fullJson.empty() == false )
				{
					llvmPath	 = extractJsonValue( fullJson, jsonKeyConstants::kLlvmPath );
					msvcToolsDir = extractJsonValue( fullJson, jsonKeyConstants::kMsvcToolsDir );
					winSdkDir	 = extractJsonValue( fullJson, jsonKeyConstants::kWindowsSdkDir );
					winSdkVer	 = extractJsonValue( fullJson, jsonKeyConstants::kWindowsSdkVersion );
				}
			}
		}

		if ( baseArgList.empty() )
		{
			SW_LOG_ERROR( "[ParserContext] No parser_args available (config empty)." );
			return false;
		}

		BLOCK( "Locate LLVM and Clang Resource Directory" )
		{
			if ( llvmPath.empty() )
			{
				const utf8* pEnvLlvm = std::getenv( jsonKeyConstants::kEnvLlvmDir );
				if ( pEnvLlvm == nullptr )
					pEnvLlvm = std::getenv( jsonKeyConstants::kEnvLlvmHome );
				if ( pEnvLlvm != nullptr )
					llvmPath = pEnvLlvm;
			}

			const string llvmClangDir = FileUtil::joinPath( llvmPath, llvmClangRel );
			if ( FileUtil::directoryExists( llvmClangDir ) )
			{
				vector<string> clangSubFolderList;
				FileUtil::collectFolders( llvmClangDir, clangSubFolderList, false, false );
				for ( const string& folder : clangSubFolderList )
				{
					const string resourceDir = FileUtil::normalizeSeparators( folder );
					const string clangInc	 = FileUtil::joinPath( folder, clangIncludeRel );
					if ( FileUtil::directoryExists( clangInc ) == false )
						continue;

					baseArgList.emplace_back( flagResourceDir );
					baseArgList.emplace_back( resourceDir );
					baseArgList.emplace_back( flagIsystem );
					baseArgList.emplace_back( clangInc );
					break;
				}
			}
		}

#if defined( SW_PLATFORM_WINDOWS )
		BLOCK( "Locate MSVC and Windows SDK Includes" )
		{
			const string msvcInc = FileUtil::joinPath( msvcToolsDir, msvcIncludeRel );
			if ( msvcToolsDir.empty() == false && FileUtil::directoryExists( msvcInc ) )
			{
				baseArgList.emplace_back( flagIsystem );
				baseArgList.emplace_back( msvcInc );
			}

			if ( winSdkDir.empty() == false && winSdkVer.empty() == false )
			{
				const string ucrtPath = FileUtil::joinPath(
					FileUtil::joinPath( FileUtil::joinPath( winSdkDir, winSdkIncludeRel ), winSdkVer ),
					winSdkUcrtRel );
				if ( FileUtil::directoryExists( ucrtPath ) )
				{
					baseArgList.emplace_back( flagIsystem );
					baseArgList.emplace_back( ucrtPath );
				}
			}
		}
#endif

		bLoaded = true;
		SW_LOG_INFO( "[ParserContext] Cached clang config (%# base args).", static_cast<uint32>( baseArgList.size() ) );
		return true;
	}

	vector<string> ParserClangConfig::buildArgs( const vector<string>& includePathList ) const
	{
		vector<string> argList = baseArgList;
		argList.reserve( argList.size() + includePathList.size() );
		for ( const string& includePath : includePathList )
			argList.push_back( flagIncludePrefix + includePath );
		return argList;
	}

	bool ParserContext::ensureSharedConfig()
	{
		std::call_once( s_configOnce, loadSharedConfigOnce );
		return s_configOk;
	}

	const ParserClangConfig& ParserContext::getSharedConfig()
	{
		ensureSharedConfig();
		return s_sharedConfig;
	}

	ParserContext::ParserContext()
		: _index{ nullptr }
		, _translationUnit{ nullptr }
	{
		_index = clang_createIndex( 0, 0 );
	}

	ParserContext::~ParserContext()
	{
		if ( _translationUnit != nullptr )
		{
			clang_disposeTranslationUnit( _translationUnit );
			_translationUnit = nullptr;
		}
		if ( _index != nullptr )
		{
			clang_disposeIndex( _index );
			_index = nullptr;
		}
	}

	bool ParserContext::parse( const string& filePath, const vector<string>& includePathList,
							   const string* unsavedContents )
	{
		if ( ensureSharedConfig() == false )
			return false;

		if ( _index == nullptr )
		{
			SW_LOG_ERROR( "[ParserContext] Failed to create CXIndex." );
			return false;
		}

		if ( _translationUnit != nullptr )
		{
			clang_disposeTranslationUnit( _translationUnit );
			_translationUnit = nullptr;
		}

		const vector<string> argStringList = getSharedConfig().buildArgs( includePathList );
		vector<const utf8*>	 argPtrList;

		BLOCK( "Build Arguments" )
		{
			argPtrList.reserve( argStringList.size() );
			for ( const string& arg : argStringList )
				argPtrList.push_back( arg.c_str() );
		}

		BLOCK( "Parse Translation Unit" )
		{
			// DetailedPreprocessingRecord는 annotate 매크로 경로에서 불필요하며 TU 비용이 큼
			constexpr uint32 kParseFlags =
				CXTranslationUnit_SkipFunctionBodies |
				CXTranslationUnit_Incomplete;

			CXUnsavedFile  unsaved{};
			CXUnsavedFile* unsavedPtr	= nullptr;
			uint32		   unsavedCount = 0;
			if ( unsavedContents != nullptr && unsavedContents->empty() == false )
			{
				unsaved.Filename = filePath.c_str();
				unsaved.Contents = unsavedContents->c_str();
				unsaved.Length	 = static_cast<uint32>( unsavedContents->size() );
				unsavedPtr		 = &unsaved;
				unsavedCount	 = 1;
			}

			_translationUnit = clang_parseTranslationUnit(
				_index,
				filePath.c_str(),
				argPtrList.data(),
				static_cast<int32>( argPtrList.size() ),
				unsavedPtr, unsavedCount,
				kParseFlags );
		}

		if ( _translationUnit == nullptr )
		{
			SW_LOG_ERROR( "[ParserContext] clang_parseTranslationUnit failed for: %#", filePath );
			return false;
		}

		const uint32 numDiags  = clang_getNumDiagnostics( _translationUnit );
		bool		 bHasError = false;

		BLOCK( "Check Diagnostics" )
		{
			for ( uint32 diagIndex = 0; diagIndex < numDiags; ++diagIndex )
			{
				CXDiagnostic		 diag = clang_getDiagnostic( _translationUnit, diagIndex );
				CXDiagnosticSeverity sev  = clang_getDiagnosticSeverity( diag );

				if ( sev >= CXDiagnostic_Error )
				{
					CXString msg = clang_formatDiagnostic( diag, clang_defaultDiagnosticDisplayOptions() );
					SW_LOG_ERROR( "[ParserContext] %#", clang_getCString( msg ) );
					clang_disposeString( msg );
					bHasError = true;
				}

				clang_disposeDiagnostic( diag );
			}
		}

		if ( bHasError )
		{
			SW_LOG_ERROR( "[ParserContext] Parsing failed with errors. Check include paths with --include." );
			return false;
		}

		return true;
	}
} // namespace sw
