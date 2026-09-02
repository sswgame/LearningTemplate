#include "pch.h"

#include "ReflectionParser/ParserContext.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Common/Common.h"

#include "ReflectionParser/ParserDefines.h"

#include <nlohmann/json.hpp>

SW_LOG_CALLER( "ParserContext" );
namespace sw
{
    namespace
    {
        struct ParserContextInternal
        {
            inline static ParserClangConfig s_sharedConfig{};
            inline static std::once_flag    s_configOnce{};
            inline static bool              s_configOk{ false };

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

            // ------------------------------------------------------------------------------
            // nlohmann/json 기반 헬퍼 — parseDocument / applyXxx / mergeConfigSection
            // ------------------------------------------------------------------------------

            /** @brief JSON 문자열을 파싱합니다. 실패 시 null 객체를 반환합니다. */
            static nlohmann::json parseDocument( const string& text )
            {
                if ( text.empty() )
                    return nlohmann::json{};
                try
                {
                    return nlohmann::json::parse( text );
                }
                catch ( const nlohmann::json::parse_error& )
                {
                    return nlohmann::json{};
                }
            }

            static string rewriteLegacyClangArg( const string& arg )
            {
                if ( arg == "-fno-spellchecking" )
                    return "-fno-spell-checking";
                return arg;
            }

            static void appendUnique( vector<string>& inoutListDst, const vector<string>& listSrc )
            {
                for ( const string& item : listSrc )
                {
                    const string normalized = rewriteLegacyClangArg( item );
                    if ( std::find( inoutListDst.begin(), inoutListDst.end(), normalized ) == inoutListDst.end() )
                        inoutListDst.push_back( normalized );
                }
            }

            /** @brief JSON 객체에서 pKey의 문자열 값을 dst에 덮어씁니다 (키 없으면 그대로). */
            static void assignIfPresent( string& dst, const nlohmann::json& obj, const utf8* pKey )
            {
                const auto it = obj.find( pKey );
                if ( it != obj.end() && it->is_string() )
                    dst = it->get_ref<const std::string&>().c_str();
            }

            /** @brief JSON 객체에서 pKey의 uint 값을 읽습니다 (키 없으면 defaultValue). */
            static uint32 getUintOrDefault( const nlohmann::json& obj, const utf8* pKey, uint32 defaultValue )
            {
                const auto it = obj.find( pKey );
                if ( it != obj.end() && it->is_number_unsigned() )
                    return it->get<uint32>();
                return defaultValue;
            }

            /** @brief JSON 배열 항목들을 string 벡터로 변환합니다. */
            static vector<string> collectStringArray( const nlohmann::json& arr )
            {
                vector<string> listResult;
                if ( arr.is_array() == false )
                    return listResult;
                for ( const auto& item : arr )
                {
                    if ( item.is_string() )
                        listResult.push_back( item.get_ref<const std::string&>().c_str() );
                }
                return listResult;
            }

            struct StringBinding
            {
                string ParserClangConfig::* _member;
                const utf8*                 _key;
            };

            static void applyBindings( ParserClangConfig& config, const nlohmann::json& obj,
                                       std::initializer_list<StringBinding> listBinding )
            {
                for ( const auto& [member, key] : listBinding )
                    assignIfPresent( config.*member, obj, key );
            }

            static void applyPathsSection( ParserClangConfig& config, const nlohmann::json& obj )
            {
                applyBindings( config, obj, {
                                                {    &ParserClangConfig::_llvmClangRel,     jsonKeyConstants::kLlvmClangRel},
                                                { &ParserClangConfig::_clangIncludeRel,  jsonKeyConstants::kClangIncludeRel},
                                                {  &ParserClangConfig::_msvcIncludeRel,   jsonKeyConstants::kMsvcIncludeRel},
                                                {&ParserClangConfig::_winSdkIncludeRel, jsonKeyConstants::kWinSdkIncludeRel},
                                                {   &ParserClangConfig::_winSdkUcrtRel,    jsonKeyConstants::kWinSdkUcrtRel},
                } );
            }

            static void applyClangFlagsSection( ParserClangConfig& config, const nlohmann::json& obj )
            {
                applyBindings( config, obj, {
                                                {         &ParserClangConfig::_flagIncludePrefix,      jsonKeyConstants::kFlagIncludePrefix},
                                                {               &ParserClangConfig::_flagIsystem,            jsonKeyConstants::kFlagIsystem},
                                                {           &ParserClangConfig::_flagResourceDir,        jsonKeyConstants::kFlagResourceDir},
                                                {          &ParserClangConfig::_flagForceInclude,       jsonKeyConstants::kFlagForceInclude},
                                                {      &ParserClangConfig::_flagFmsCompatibility,   jsonKeyConstants::kFlagFmsCompatibility},
                                                {         &ParserClangConfig::_flagFmsExtensions,      jsonKeyConstants::kFlagFmsExtensions},
                                                {&ParserClangConfig::_flagFmsCompatVersionPrefix, jsonKeyConstants::kFlagFmsCompatVerPrefix},
                } );
            }

            static void applyEmitSection( ParserClangConfig& config, const nlohmann::json& obj )
            {
                applyBindings( config, obj, {
                                                {       &ParserClangConfig::_emitCppExtension,        jsonKeyConstants::kEmitCppExtension},
                                                {    &ParserClangConfig::_emitHeaderExtension,     jsonKeyConstants::kEmitHeaderExtension},
                                                {  &ParserClangConfig::_emitTemplateExtension,   jsonKeyConstants::kEmitTemplateExtension},
                                                {&ParserClangConfig::_emitAutoGeneratedBanner, jsonKeyConstants::kEmitAutoGeneratedBanner},
                                                {  &ParserClangConfig::_emitPlaceholderMarker,   jsonKeyConstants::kEmitPlaceholderMarker},
                                                {&ParserClangConfig::_emitRegenByParserMarker,         jsonKeyConstants::kEmitRegenMarker},
                                                {    &ParserClangConfig::_emitGeneratedNsOpen,     jsonKeyConstants::kEmitGeneratedNsOpen},
                                                {   &ParserClangConfig::_emitGeneratedNsClose,    jsonKeyConstants::kEmitGeneratedNsClose},
                                                {      &ParserClangConfig::_emitFlagOpsHeader,       jsonKeyConstants::kEmitFlagOpsHeader},
                                                {      &ParserClangConfig::_emitFlagOpsMarker,       jsonKeyConstants::kEmitFlagOpsMarker},
                                                { &ParserClangConfig::_emitRegisterTypeMarker,  jsonKeyConstants::kEmitRegisterTypeMarker},
                                                { &ParserClangConfig::_emitRegisterEnumMarker,  jsonKeyConstants::kEmitRegisterEnumMarker},
                } );

                const auto itForbidden = obj.find( jsonKeyConstants::kEmitValueForbiddenBases );
                if ( itForbidden != obj.end() && itForbidden->is_array() )
                    config._listValueForbiddenBaseType = collectStringArray( *itForbidden );

                const auto itForbiddenMsg = obj.find( jsonKeyConstants::kEmitValueForbiddenMessage );
                if ( itForbiddenMsg != obj.end() && itForbiddenMsg->is_string() )
                    config._valueForbiddenMessage = itForbiddenMsg->get_ref<const std::string&>().c_str();
            }

            static void applyParsingSection( ParserClangConfig& config, const nlohmann::json& obj )
            {
                const auto itComponents = obj.find( jsonKeyConstants::kParsingComponentBaseTypes );
                if ( itComponents != obj.end() && itComponents->is_array() )
                    config._listComponentBaseType = collectStringArray( *itComponents );

                const auto itPrefixes = obj.find( jsonKeyConstants::kParsingTypeStripPrefixes );
                if ( itPrefixes != obj.end() && itPrefixes->is_array() )
                    config._listTypeStripPrefix = collectStringArray( *itPrefixes );

                const auto itRules = obj.find( jsonKeyConstants::kParsingModuleRules );
                if ( itRules != obj.end() && itRules->is_array() )
                {
                    config._listModuleRule.clear();
                    for ( const auto& rule : *itRules )
                    {
                        if ( rule.is_object() == false )
                            continue;
                        const auto itContains = rule.find( jsonKeyConstants::kModuleRulePathContains );
                        const auto itModule   = rule.find( jsonKeyConstants::kModuleRuleModule );
                        if ( itContains == rule.end() || itModule == rule.end() )
                            continue;
                        if ( itContains->is_string() == false || itModule->is_string() == false )
                            continue;
                        ParserClangConfig::ModuleRule parsed;
                        parsed._pathContains = itContains->get_ref<const std::string&>().c_str();
                        parsed._module       = itModule->get_ref<const std::string&>().c_str();
                        config._listModuleRule.push_back( std::move( parsed ) );
                    }
                }

                const auto itDefaultModule = obj.find( jsonKeyConstants::kParsingDefaultModule );
                if ( itDefaultModule != obj.end() && itDefaultModule->is_string() )
                    config._defaultModule = itDefaultModule->get_ref<const std::string&>().c_str();
            }

            static void applyTuningSection( ParserClangConfig& config, const nlohmann::json& obj )
            {
                config._sourceLookbackBytes = getUintOrDefault( obj, jsonKeyConstants::kSourceLookbackBytes, config._sourceLookbackBytes );
            }

            using ApplyFn = void ( * )( ParserClangConfig&, const nlohmann::json& );

            static void mergeConfigSection( ParserClangConfig& config, const nlohmann::json& defaultsDoc,
                                            const nlohmann::json& localDoc, const utf8* sectionKey, ApplyFn applyFn )
            {
                const auto itDefaults = defaultsDoc.find( sectionKey );
                if ( itDefaults != defaultsDoc.end() && itDefaults->is_object() )
                    applyFn( config, *itDefaults );
                const auto itLocal = localDoc.find( sectionKey );
                if ( itLocal != localDoc.end() && itLocal->is_object() )
                    applyFn( config, *itLocal );
            }

            static vector<string> loadArgsFromDocument( const nlohmann::json& doc, const utf8* pPlatformKey )
            {
                vector<string> outList;
                const auto     itArgs = doc.find( jsonKeyConstants::kParserArgsSection );
                if ( itArgs == doc.end() || itArgs->is_object() == false )
                    return outList;

                const nlohmann::json& argsSection = *itArgs;
                appendUnique( outList, collectStringArray( argsSection.value( jsonKeyConstants::kArgsDefault, nlohmann::json{} ) ) );

#if defined( SW_PLATFORM_WINDOWS ) || defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
                {
                    const auto            itPlatform  = argsSection.find( jsonKeyConstants::kArgsPlatform );
                    const nlohmann::json& platformSrc = ( itPlatform != argsSection.end() && itPlatform->is_object() )
                                                          ? *itPlatform
                                                          : argsSection;
                    appendUnique( outList, collectStringArray( platformSrc.value( pPlatformKey, nlohmann::json{} ) ) );
                }
#else
                (void)pPlatformKey;
#endif

                appendUnique( outList, collectStringArray( argsSection.value( jsonKeyConstants::kArgsExtra, nlohmann::json{} ) ) );
                return outList;
            }

            static vector<string> loadForceIncludeFromDocument( const nlohmann::json& doc )
            {
                const auto itArgs = doc.find( jsonKeyConstants::kParserArgsSection );
                if ( itArgs == doc.end() || itArgs->is_object() == false )
                    return {};
                return collectStringArray( itArgs->value( jsonKeyConstants::kArgsForceInclude, nlohmann::json{} ) );
            }

#if !defined( SW_PLATFORM_WINDOWS )
            static bool isMsvcCompatArg( const string& arg, const string& fmsCompatibility, const string& fmsExtensions,
                                         const string& fmsCompatVersionPrefix )
            {
                if ( arg == fmsCompatibility )
                    return true;
                if ( arg == fmsExtensions )
                    return true;
                if ( StringUtil::startsWith( arg, fmsCompatVersionPrefix ) )
                    return true;
                return false;
            }

            static void eraseMsvcCompatArgs( vector<string>& inoutListArg, const ParserClangConfig& config )
            {
                size_t writeIndex = 0;
                for ( size_t readIndex = 0; readIndex < inoutListArg.size(); ++readIndex )
                {
                    if ( isMsvcCompatArg( inoutListArg[readIndex], config._flagFmsCompatibility, config._flagFmsExtensions,
                                          config._flagFmsCompatVersionPrefix ) )
                        continue;
                    if ( writeIndex != readIndex )
                        inoutListArg[writeIndex] = inoutListArg[readIndex];
                    ++writeIndex;
                }
                inoutListArg.resize( writeIndex );
            }
#endif

            static void loadSharedConfigOnce()
            {
                s_configOk = s_sharedConfig.load();
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ParserClangConfig::ParserClangConfig() noexcept
        : _listBaseArg{
    }
        , _listForceInclude{}
        , _llvmClangRel{ "lib/clang" }
        , _clangIncludeRel{ "include" }
        , _msvcIncludeRel{ "include" }
        , _winSdkIncludeRel{ "Include" }
        , _winSdkUcrtRel{ "ucrt" }
        , _flagIncludePrefix{ "-I" }
        , _flagIsystem{ "-isystem" }
        , _flagResourceDir{ "-resource-dir" }
        , _flagForceInclude{ "-include" }
        , _flagFmsCompatibility{ "-fms-compatibility" }
        , _flagFmsExtensions{ "-fms-extensions" }
        , _flagFmsCompatVersionPrefix{ "-fms-compatibility-version" }
        , _emitCppExtension{ ".gen.cpp" }
        , _emitHeaderExtension{ ".gen.h" }
        , _emitTemplateExtension{ ".tpl" }
        , _emitAutoGeneratedBanner{ "// AUTO-GENERATED -- DO NOT EDIT" }
        , _emitPlaceholderMarker{ "AUTO-GENERATED placeholder" }
        , _emitRegenByParserMarker{ "regenerated by ReflectionParser" }
        , _emitGeneratedNsOpen{ "namespace sw::generated\n{\n" }
        , _emitGeneratedNsClose{ "} // namespace sw::generated\n\n" }
        , _emitFlagOpsHeader{ "FlagOps.gen.h" }
        , _emitFlagOpsMarker{ "operator|" }
        , _emitRegisterTypeMarker{ "RegisterType" }
        , _emitRegisterEnumMarker{ "RegisterEnum" }
        , _listModuleRule{ { "GameFramework", "GameFramework" }, { "Games", "SWGame" }, { "SWGame", "SWGame" }, { "Editor", "EditorModule" }, { "App", "App" } },
        _defaultModule{ "Engine" },
        _listValueForbiddenBaseType{ "sw::Component", "sw::GameObject" },
        _valueForbiddenMessage{ "GameObject or Component cannot be stored by value inside a PROPERTY(). Use a pointer, GameObjectPtr, or ComponentPtr instead." },
        _listComponentBaseType{ "Component", "sw::Component", "SceneComponent", "sw::SceneComponent" },
        _listTypeStripPrefix{ "const ", "volatile ", "class ", "struct ", "enum " },
        _sourceLookbackBytes{ 512 },
        _bLoaded{ SW_FALSE },
        _reserved{ 0 },
        _padding{ 0 }
    {
    }

    bool ParserClangConfig::load()
    {
        _listBaseArg.clear();
        _listForceInclude.clear();
        _bLoaded = SW_FALSE;

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

        string       defaultsText;
        string       localText;
        const string defaultsPath = ParserContextInternal::findConfigFile( pathConstants::kParserConfigDefaults );
        const string localPath    = ParserContextInternal::findConfigFile( pathConstants::kParserConfig );
        if ( defaultsPath.empty() == false )
            FileUtil::readTextFile( defaultsPath, defaultsText );
        if ( localPath.empty() == false )
            FileUtil::readTextFile( localPath, localText );

        const nlohmann::json defaultsDoc = ParserContextInternal::parseDocument( defaultsText );
        const nlohmann::json localDoc    = ParserContextInternal::parseDocument( localText );

        BLOCK( "Load Base Arguments from Config" )
        {
            vector<string> listMerged = ParserContextInternal::loadArgsFromDocument( defaultsDoc, kPlatformParserKey );
            ParserContextInternal::appendUnique( listMerged, ParserContextInternal::loadArgsFromDocument( localDoc, kPlatformParserKey ) );
            _listBaseArg = std::move( listMerged );

#if !defined( SW_PLATFORM_WINDOWS )
            ParserContextInternal::eraseMsvcCompatArgs( _listBaseArg, *this );
#endif
        }

        BLOCK( "Load paths / clang_flags / emit / parsing / tuning" )
        {
            ParserContextInternal::mergeConfigSection( *this, defaultsDoc, localDoc, jsonKeyConstants::kPaths, ParserContextInternal::applyPathsSection );
            ParserContextInternal::mergeConfigSection( *this, defaultsDoc, localDoc, jsonKeyConstants::kClangFlags, ParserContextInternal::applyClangFlagsSection );
            ParserContextInternal::mergeConfigSection( *this, defaultsDoc, localDoc, jsonKeyConstants::kEmit, ParserContextInternal::applyEmitSection );
            ParserContextInternal::mergeConfigSection( *this, defaultsDoc, localDoc, jsonKeyConstants::kParsing, ParserContextInternal::applyParsingSection );
            ParserContextInternal::mergeConfigSection( *this, defaultsDoc, localDoc, jsonKeyConstants::kTuning, ParserContextInternal::applyTuningSection );
        }

        BLOCK( "Load force_include" )
        {
            ParserContextInternal::appendUnique( _listForceInclude, ParserContextInternal::loadForceIncludeFromDocument( defaultsDoc ) );
            ParserContextInternal::appendUnique( _listForceInclude, ParserContextInternal::loadForceIncludeFromDocument( localDoc ) );
        }

        BLOCK( "Load Engine Config" )
        {
            const string engineCfgPath = ParserContextInternal::findConfigFile( pathConstants::kToolchainConfig );
            if ( engineCfgPath.empty() == false )
            {
                string engineCfgText;
                FileUtil::readTextFile( engineCfgPath, engineCfgText );
                const nlohmann::json engineDoc = ParserContextInternal::parseDocument( engineCfgText );
                ParserContextInternal::assignIfPresent( llvmPath, engineDoc, jsonKeyConstants::kLlvmPath );
                ParserContextInternal::assignIfPresent( msvcToolsDir, engineDoc, jsonKeyConstants::kMsvcToolsDir );
                ParserContextInternal::assignIfPresent( winSdkDir, engineDoc, jsonKeyConstants::kWindowsSdkDir );
                ParserContextInternal::assignIfPresent( winSdkVer, engineDoc, jsonKeyConstants::kWindowsSdkVersion );
            }
        }

        if ( _listBaseArg.empty() )
        {
            SW_LOG_ERROR( "No parser_args available (config empty)." );
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

            const string llvmClangDir = FileUtil::joinPath( llvmPath, _llvmClangRel );
            if ( FileUtil::directoryExists( llvmClangDir ) )
            {
                vector<string> listClangSubFolder;
                FileUtil::collectFolders( llvmClangDir, listClangSubFolder, false, false );
                for ( const string& folder : listClangSubFolder )
                {
                    const string resourceDir = FileUtil::normalizeSeparators( folder );
                    const string clangInc    = FileUtil::joinPath( folder, _clangIncludeRel );
                    if ( FileUtil::directoryExists( clangInc ) == false )
                        continue;

                    _listBaseArg.emplace_back( _flagResourceDir );
                    _listBaseArg.emplace_back( resourceDir );
                    _listBaseArg.emplace_back( _flagIsystem );
                    _listBaseArg.emplace_back( clangInc );
                    break;
                }
            }
        }

#if defined( SW_PLATFORM_WINDOWS )
        BLOCK( "Locate MSVC and Windows SDK Includes" )
        {
            const string msvcInc = FileUtil::joinPath( msvcToolsDir, _msvcIncludeRel );
            if ( msvcToolsDir.empty() == false && FileUtil::directoryExists( msvcInc ) )
            {
                _listBaseArg.emplace_back( _flagIsystem );
                _listBaseArg.emplace_back( msvcInc );
            }

            if ( winSdkDir.empty() == false && winSdkVer.empty() == false )
            {
                const string ucrtPath = FileUtil::joinPath(
                    FileUtil::joinPath( FileUtil::joinPath( winSdkDir, _winSdkIncludeRel ), winSdkVer ),
                    _winSdkUcrtRel );
                if ( FileUtil::directoryExists( ucrtPath ) )
                {
                    _listBaseArg.emplace_back( _flagIsystem );
                    _listBaseArg.emplace_back( ucrtPath );
                }
            }
        }
#endif

        _bLoaded = SW_TRUE;
        SW_LOG_TRACE( "Cached clang config (%# base args).", static_cast<uint32>( _listBaseArg.size() ) );
        return true;
    }

    vector<string> ParserClangConfig::buildArgs( const vector<string>& listIncludePath ) const
    {
        vector<string> listArg = _listBaseArg;
        listArg.reserve( listArg.size() + listIncludePath.size() + _listForceInclude.size() * 2 );
        for ( const string& includePath : listIncludePath )
            listArg.push_back( _flagIncludePrefix + includePath );
        for ( const string& forceInclude : _listForceInclude )
        {
            listArg.push_back( _flagForceInclude );
            listArg.push_back( forceInclude );
        }
        return listArg;
    }

    bool ParserContext::ensureSharedConfig()
    {
        std::call_once( ParserContextInternal::s_configOnce, ParserContextInternal::loadSharedConfigOnce );
        return ParserContextInternal::s_configOk;
    }

    const ParserClangConfig& ParserContext::getSharedConfig()
    {
        ensureSharedConfig();
        return ParserContextInternal::s_sharedConfig;
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

    bool ParserContext::parse( const string& filePath, const vector<string>& listIncludePath,
                               const string* pUnsavedContents )
    {
        if ( ensureSharedConfig() == false )
            return false;

        if ( _index == nullptr )
        {
            SW_LOG_ERROR( "Failed to create CXIndex." );
            return false;
        }

        if ( _translationUnit != nullptr )
        {
            clang_disposeTranslationUnit( _translationUnit );
            _translationUnit = nullptr;
        }

        const vector<string> listArgString = getSharedConfig().buildArgs( listIncludePath );
        vector<const utf8*>  listArgPtr;

        BLOCK( "Build Arguments" )
        {
            listArgPtr.reserve( listArgString.size() );
            for ( const string& arg : listArgString )
                listArgPtr.push_back( arg.c_str() );
        }

        BLOCK( "Parse Translation Unit" )
        {
            // DetailedPreprocessingRecord는 annotate 매크로 경로에서 불필요하며 TU 비용이 큼
            constexpr uint32 kParseFlags =
                CXTranslationUnit_SkipFunctionBodies |
                CXTranslationUnit_Incomplete;

            CXUnsavedFile  unsaved{};
            CXUnsavedFile* pUnsavedFile = nullptr;
            uint32         unsavedCount = 0;
            if ( pUnsavedContents != nullptr && pUnsavedContents->empty() == false )
            {
                unsaved.Filename = filePath.c_str();
                unsaved.Contents = pUnsavedContents->c_str();
                unsaved.Length   = static_cast<uint32>( pUnsavedContents->size() );
                pUnsavedFile     = &unsaved;
                unsavedCount     = 1;
            }

            _translationUnit = clang_parseTranslationUnit(
                _index,
                filePath.c_str(),
                listArgPtr.data(),
                static_cast<int32>( listArgPtr.size() ),
                pUnsavedFile, unsavedCount,
                kParseFlags );
        }

        if ( _translationUnit == nullptr )
        {
            SW_LOG_ERROR( "clang_parseTranslationUnit failed for: %#", filePath );
            return false;
        }

        const uint32 numDiags  = clang_getNumDiagnostics( _translationUnit );
        bool         bHasError = false;

        BLOCK( "Check Diagnostics" )
        {
            for ( uint32 diagIndex = 0; diagIndex < numDiags; ++diagIndex )
            {
                CXDiagnostic         diag = clang_getDiagnostic( _translationUnit, diagIndex );
                CXDiagnosticSeverity sev  = clang_getDiagnosticSeverity( diag );

                if ( sev >= CXDiagnostic_Error )
                {
                    CXString msg = clang_formatDiagnostic( diag, clang_defaultDiagnosticDisplayOptions() );
                    SW_LOG_ERROR( "%#", clang_getCString( msg ) );
                    clang_disposeString( msg );
                    bHasError = true;
                }

                clang_disposeDiagnostic( diag );
            }
        }

        if ( bHasError )
        {
            SW_LOG_ERROR( "Parsing failed with errors. Check include paths with --include." );
            return false;
        }

        return true;
    }
} // namespace sw
