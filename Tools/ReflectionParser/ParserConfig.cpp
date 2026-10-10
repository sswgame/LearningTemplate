#include "pch.h"

#include "ReflectionParser/ParserConfig.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Common/Common.h"

#include "ReflectionParser/ParserDefines.h"

#include <nlohmann/json.hpp>

SW_LOG_CALLER( "ParserConfig" );
namespace sw
{
    namespace
    {
        struct ParserConfigInternal
        {
            /** @brief 현재 디렉터리에서 위로 올라가며 상대 경로의 파일을 찾습니다. 없으면 빈 문자열입니다. */
            static string findConfigFile( const string& relPath )
            {
                string cur = FileUtil::getCurrentPath();
                while ( true )
                {
                    const string candidate = FileUtil::joinPath( cur, relPath );
                    if ( FileUtil::isRegularFile( candidate ) )
                        return candidate;

                    const string parent = FileUtil::getDirectoryPart( cur );
                    if ( parent.empty() || parent == cur )
                        break;
                    cur = parent;
                }
                return {};
            }

            /**
             * @brief 설정 파일을 찾아 JSON 으로 읽고, 읽었으면 경로를 남깁니다.
             * @return 파일이 없거나 비었으면 null 객체와 true(선택 파일), 있는데 못 읽거나 JSON 이 깨졌으면 false 입니다.
             * @details 깨진 설정을 기본값으로 대신하지 않는다 — 어느 칸이 왜 무시됐는지 남지 않고 libclang 오류가 엉뚱한 자리에서 난다.
             */
            [[nodiscard]] static bool loadDocument( const string& relPath, vector<string>& inoutListLoadedFile, nlohmann::json& outDocument )
            {
                outDocument       = nlohmann::json{};
                const string path = findConfigFile( relPath );
                if ( path.empty() )
                    return true;

                string text;
                if ( FileUtil::readTextFile( path, text ) == false )
                {
                    SW_LOG_ERROR( "Parser setting file '%#' exists but cannot be read", path );
                    return false;
                }
                inoutListLoadedFile.push_back( path );
                if ( text.empty() )
                    return true;
                try
                {
                    outDocument = nlohmann::json::parse( text );
                }
                catch ( const nlohmann::json::parse_error& exception )
                {
                    SW_LOG_ERROR( "Parser setting file '%#' is not valid JSON: %#", path, exception.what() );
                    return false;
                }
                return true;
            }

            static void appendUnique( vector<string>& inoutListDst, const vector<string>& listSrc )
            {
                for ( const string& item : listSrc )
                {
                    if ( std::find( inoutListDst.begin(), inoutListDst.end(), item ) == inoutListDst.end() )
                        inoutListDst.push_back( item );
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

            /** @brief 키가 있고 배열이면 목록을 통째로 바꿉니다. */
            static void assignArrayIfPresent( vector<string>& outListValue, const nlohmann::json& obj, const utf8* pKey )
            {
                const auto it = obj.find( pKey );
                if ( it != obj.end() && it->is_array() )
                    outListValue = collectStringArray( *it );
            }

            struct StringBinding
            {
                string ParserConfig::* _member;
                const utf8*            _pKey;
            };

            static void applyBindings( ParserConfig& config, const nlohmann::json& obj, std::initializer_list<StringBinding> listBinding )
            {
                for ( const auto& [member, key] : listBinding )
                {
                    assignIfPresent( config.*member, obj, key );
                }
            }

            static void applyPathsSection( ParserConfig& config, const nlohmann::json& obj )
            {
                applyBindings( config, obj, {
                                                {    &ParserConfig::_llvmClangRel,     jsonkey::kLlvmClangRel},
                                                { &ParserConfig::_clangIncludeRel,  jsonkey::kClangIncludeRel},
                                                {  &ParserConfig::_msvcIncludeRel,   jsonkey::kMsvcIncludeRel},
                                                {&ParserConfig::_winSdkIncludeRel, jsonkey::kWinSdkIncludeRel},
                                                {   &ParserConfig::_winSdkUcrtRel,    jsonkey::kWinSdkUcrtRel},
                } );
            }

            static void applyClangFlagsSection( ParserConfig& config, const nlohmann::json& obj )
            {
                applyBindings( config, obj, {
                                                {         &ParserConfig::_flagIncludePrefix,      jsonkey::kFlagIncludePrefix},
                                                {               &ParserConfig::_flagIsystem,            jsonkey::kFlagIsystem},
                                                {           &ParserConfig::_flagResourceDir,        jsonkey::kFlagResourceDir},
                                                {          &ParserConfig::_flagForceInclude,       jsonkey::kFlagForceInclude},
                                                {      &ParserConfig::_flagFmsCompatibility,   jsonkey::kFlagFmsCompatibility},
                                                {         &ParserConfig::_flagFmsExtensions,      jsonkey::kFlagFmsExtensions},
                                                {&ParserConfig::_flagFmsCompatVersionPrefix, jsonkey::kFlagFmsCompatVerPrefix},
                } );
            }

            static void applyEmitSection( ParserConfig& config, const nlohmann::json& obj )
            {
                applyBindings( config, obj, {
                                                {       &ParserConfig::_emitCppExtension,          jsonkey::kEmitCppExtension},
                                                {    &ParserConfig::_emitHeaderExtension,       jsonkey::kEmitHeaderExtension},
                                                {  &ParserConfig::_emitTemplateExtension,     jsonkey::kEmitTemplateExtension},
                                                {&ParserConfig::_emitAutoGeneratedBanner,   jsonkey::kEmitAutoGeneratedBanner},
                                                {  &ParserConfig::_emitPlaceholderMarker,     jsonkey::kEmitPlaceholderMarker},
                                                {&ParserConfig::_emitRegenByParserMarker,           jsonkey::kEmitRegenMarker},
                                                {    &ParserConfig::_emitGeneratedNsOpen,       jsonkey::kEmitGeneratedNsOpen},
                                                {   &ParserConfig::_emitGeneratedNsClose,      jsonkey::kEmitGeneratedNsClose},
                                                {      &ParserConfig::_emitFlagOpsHeader,         jsonkey::kEmitFlagOpsHeader},
                                                {      &ParserConfig::_emitFlagOpsMarker,         jsonkey::kEmitFlagOpsMarker},
                                                { &ParserConfig::_emitRegisterTypeMarker,    jsonkey::kEmitRegisterTypeMarker},
                                                { &ParserConfig::_emitRegisterEnumMarker,    jsonkey::kEmitRegisterEnumMarker},
                                                {   &ParserConfig::_emitSourcePathMarker,      jsonkey::kEmitSourcePathMarker},
                                                {  &ParserConfig::_valueForbiddenMessage, jsonkey::kEmitValueForbiddenMessage},
                } );
                assignArrayIfPresent( config._listValueForbiddenBaseType, obj, jsonkey::kEmitValueForbiddenBases );
            }

            static void applyParsingSection( ParserConfig& config, const nlohmann::json& obj )
            {
                assignArrayIfPresent( config._listComponentBaseType, obj, jsonkey::kParsingComponentBaseTypes );
                assignArrayIfPresent( config._listTypeStripPrefix, obj, jsonkey::kParsingTypeStripPrefixes );
                assignIfPresent( config._defaultModule, obj, jsonkey::kParsingDefaultModule );

                const auto itRules = obj.find( jsonkey::kParsingModuleRules );
                if ( itRules == obj.end() || itRules->is_array() == false )
                    return;

                config._listModuleRule.clear();
                for ( const auto& rule : *itRules )
                {
                    if ( rule.is_object() == false )
                        continue;
                    ParserConfig::ModuleRule parsed;
                    assignIfPresent( parsed._pathContains, rule, jsonkey::kModuleRulePathContains );
                    assignIfPresent( parsed._module, rule, jsonkey::kModuleRuleModule );
                    if ( parsed._pathContains.empty() || parsed._module.empty() )
                        continue;
                    config._listModuleRule.push_back( std::move( parsed ) );
                }
            }

            static void applyTuningSection( ParserConfig& config, const nlohmann::json& obj )
            {
                config._sourceLookbackBytes = getUintOrDefault( obj, jsonkey::kSourceLookbackBytes, config._sourceLookbackBytes );
            }

            /**
             * @brief 로컬 문서에서 **이 기계에 딸린 키만** 남깁니다 — `paths.*` 와 `parser_args.extra` · `parser_args.force_include`.
             * @details 나머지 키는 기본값과 다르면 경고하고 버린다(기본값이 이긴다) — 셋업이 로컬에 기본값 **전체 사본**을 써 두므로, 로컬이
             *          키마다 이기면 나중에 커밋된 기본값을 고쳐도 그 기계는 옛 값을 계속 쓴다(예: `flag_ops_marker` 가 바뀌면 FlagOps 우산이 비어
             *          Engine 빌드가 깨진다). 기본값에 없는 키(지운 이름)도 알린다.
             */
            static nlohmann::json keepMachineLocalKeys( const nlohmann::json& defaultsDoc, const nlohmann::json& localDoc, const string& localPath )
            {
                nlohmann::json kept = nlohmann::json::object();
                if ( localDoc.is_object() == false )
                    return kept;
                for ( auto sectionIt = localDoc.begin(); sectionIt != localDoc.end(); ++sectionIt )
                {
                    const std::string& section = sectionIt.key();
                    if ( section == jsonkey::kPaths )
                    {
                        kept[section] = sectionIt.value();
                        continue;
                    }
                    const auto defaultsIt = defaultsDoc.is_object() ? defaultsDoc.find( section ) : defaultsDoc.end();
                    if ( defaultsIt == defaultsDoc.end() )
                    {
                        SW_LOG_WARNING( "%#: '%#' is not a parser setting (an old name?) and is ignored - remove it", localPath, section.c_str() );
                        continue;
                    }
                    if ( sectionIt.value().is_object() == false || defaultsIt->is_object() == false )
                    {
                        if ( *defaultsIt != sectionIt.value() )
                            SW_LOG_WARNING( "%#: '%#' differs from parser_config.defaults.json and is ignored - only paths.*, parser_args.extra and "
                                            "parser_args.force_include are local settings",
                                            localPath, section.c_str() );
                        continue;
                    }
                    for ( auto keyIt = sectionIt.value().begin(); keyIt != sectionIt.value().end(); ++keyIt )
                    {
                        const std::string& key           = keyIt.key();
                        const bool         bMachineLocal = section == jsonkey::kParserArgsSection &&
                                                   ( key == jsonkey::kArgsExtra || key == jsonkey::kArgsForceInclude );
                        if ( bMachineLocal )
                        {
                            kept[section][key] = keyIt.value();
                            continue;
                        }
                        const auto defaultIt = defaultsIt->find( key );
                        if ( defaultIt == defaultsIt->end() || *defaultIt != keyIt.value() )
                        {
                            SW_LOG_WARNING( "%#: '%#.%#' differs from parser_config.defaults.json and is ignored - only paths.*, parser_args.extra "
                                            "and parser_args.force_include are local settings (remove it, or rerun Scripts/setup/SetupEnvironment.py)",
                                            localPath, section.c_str(), key.c_str() );
                        }
                    }
                }
                return kept;
            }

            using ApplyFn = void ( * )( ParserConfig&, const nlohmann::json& );

            /** @brief 기본값 문서 다음 로컬 문서 순으로 한 섹션을 덮어씁니다(로컬이 이긴다). */
            static void mergeConfigSection( ParserConfig& config, const nlohmann::json& defaultsDoc, const nlohmann::json& localDoc,
                                            const utf8* sectionKey, ApplyFn applyFn )
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
                const auto     itArgs = doc.find( jsonkey::kParserArgsSection );
                if ( itArgs == doc.end() || itArgs->is_object() == false )
                    return outList;

                const nlohmann::json& argsSection = *itArgs;
                appendUnique( outList, collectStringArray( argsSection.value( jsonkey::kArgsDefault, nlohmann::json{} ) ) );

#if defined( SW_PLATFORM_WINDOWS ) || defined( SW_PLATFORM_LINUX )
                {
                    const auto            itPlatform  = argsSection.find( jsonkey::kArgsPlatform );
                    const nlohmann::json& platformSrc = ( itPlatform != argsSection.end() && itPlatform->is_object() )
                                                          ? *itPlatform
                                                          : argsSection;
                    appendUnique( outList, collectStringArray( platformSrc.value( pPlatformKey, nlohmann::json{} ) ) );
                }
#else
                (void)pPlatformKey;
#endif

                appendUnique( outList, collectStringArray( argsSection.value( jsonkey::kArgsExtra, nlohmann::json{} ) ) );
                return outList;
            }

            static vector<string> loadForceIncludeFromDocument( const nlohmann::json& doc )
            {
                const auto itArgs = doc.find( jsonkey::kParserArgsSection );
                if ( itArgs == doc.end() || itArgs->is_object() == false )
                    return {};
                return collectStringArray( itArgs->value( jsonkey::kArgsForceInclude, nlohmann::json{} ) );
            }

#if !defined( SW_PLATFORM_WINDOWS )
            static bool isMsvcCompatArg( const string& arg, const ParserConfig& config )
            {
                if ( arg == config._flagFmsCompatibility || arg == config._flagFmsExtensions )
                    return true;
                return StringUtil::startsWith( arg, config._flagFmsCompatVersionPrefix );
            }

            static void eraseMsvcCompatArgs( vector<string>& inoutListArg, const ParserConfig& config )
            {
                size_t writeIndex = 0;
                for ( size_t readIndex = 0; readIndex < inoutListArg.size(); ++readIndex )
                {
                    if ( isMsvcCompatArg( inoutListArg[readIndex], config ) )
                        continue;
                    if ( writeIndex != readIndex )
                        inoutListArg[writeIndex] = inoutListArg[readIndex];
                    ++writeIndex;
                }
                inoutListArg.resize( writeIndex );
            }
#endif
        };
    } // namespace
} // namespace sw

namespace sw
{
    ParserConfig::ParserConfig() noexcept
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
        , _emitFlagOpsMarker{ "IsBitFlagEnum" }
        , _emitRegisterTypeMarker{ "RegisterType" }
        , _emitRegisterEnumMarker{ "RegisterEnum" }
        , _emitSourcePathMarker{ "// Source: " }
        , _listModuleRule{ { "GameFramework", "GameFramework" }, { "Games", "SWGame" }, { "SWGame", "SWGame" }, { "Editor", "EditorModule" }, { "App", "App" } },
        _defaultModule{ "Engine" },
        _listValueForbiddenBaseType{ "sw::Component", "sw::GameObject" },
        _valueForbiddenMessage{ "GameObject or Component cannot be stored by value inside a PROPERTY(). Use a pointer, or a GameObjectHandle / ComponentHandle for a reference kept across frames." },
        _listComponentBaseType{ "Component", "sw::Component", "SceneComponent", "sw::SceneComponent" },
        _listTypeStripPrefix{ "const ", "volatile ", "class ", "struct ", "enum " },
        _listLoadedFile{},
        _sourceLookbackBytes{ 512 },
        _bLoaded{ SW_FALSE },
        _reserved{ 0 },
        _padding{ 0 }
    {
    }

    bool ParserConfig::load()
    {
        _listBaseArg.clear();
        _listForceInclude.clear();
        _listLoadedFile.clear();
        _bLoaded = SW_FALSE;

#if defined( SW_PLATFORM_WINDOWS )
        constexpr const utf8* kPlatformParserKey = "windows";
#elif defined( SW_PLATFORM_LINUX )
        constexpr const utf8* kPlatformParserKey = "linux";
#else
        constexpr const utf8* kPlatformParserKey = "";
#endif

        nlohmann::json defaultsDoc;
        nlohmann::json rawLocalDoc;
        if ( ParserConfigInternal::loadDocument( parserpath::kParserConfigDefaults, _listLoadedFile, defaultsDoc ) == false ||
             ParserConfigInternal::loadDocument( parserpath::kParserConfig, _listLoadedFile, rawLocalDoc ) == false )
            return false;
        // 로컬은 이 기계에 딸린 키만 받는다(`keepMachineLocalKeys`) — 나머지는 커밋된 기본값이 정한다.
        const nlohmann::json localDoc =
            ParserConfigInternal::keepMachineLocalKeys( defaultsDoc, rawLocalDoc, ParserConfigInternal::findConfigFile( parserpath::kParserConfig ) );

        BLOCK( "Load Base Arguments from Config" )
        {
            vector<string> listMerged = ParserConfigInternal::loadArgsFromDocument( defaultsDoc, kPlatformParserKey );
            ParserConfigInternal::appendUnique( listMerged, ParserConfigInternal::loadArgsFromDocument( localDoc, kPlatformParserKey ) );
            _listBaseArg = std::move( listMerged );

#if !defined( SW_PLATFORM_WINDOWS )
            ParserConfigInternal::eraseMsvcCompatArgs( _listBaseArg, *this );
#endif
        }

        BLOCK( "Load paths / clang_flags / emit / parsing / tuning" )
        {
            ParserConfigInternal::mergeConfigSection( *this, defaultsDoc, localDoc, jsonkey::kPaths, ParserConfigInternal::applyPathsSection );
            ParserConfigInternal::mergeConfigSection( *this, defaultsDoc, localDoc, jsonkey::kClangFlags, ParserConfigInternal::applyClangFlagsSection );
            ParserConfigInternal::mergeConfigSection( *this, defaultsDoc, localDoc, jsonkey::kEmit, ParserConfigInternal::applyEmitSection );
            ParserConfigInternal::mergeConfigSection( *this, defaultsDoc, localDoc, jsonkey::kParsing, ParserConfigInternal::applyParsingSection );
            ParserConfigInternal::mergeConfigSection( *this, defaultsDoc, localDoc, jsonkey::kTuning, ParserConfigInternal::applyTuningSection );
        }

        BLOCK( "Load force_include" )
        {
            ParserConfigInternal::appendUnique( _listForceInclude, ParserConfigInternal::loadForceIncludeFromDocument( defaultsDoc ) );
            ParserConfigInternal::appendUnique( _listForceInclude, ParserConfigInternal::loadForceIncludeFromDocument( localDoc ) );
        }

        string llvmPath;
        string msvcToolsDir;
        string winSdkDir;
        string winSdkVer;
        BLOCK( "Load Engine Config" )
        {
            nlohmann::json engineDoc;
            if ( ParserConfigInternal::loadDocument( parserpath::kToolchainConfig, _listLoadedFile, engineDoc ) == false )
                return false;
            if ( engineDoc.is_object() )
            {
                ParserConfigInternal::assignIfPresent( llvmPath, engineDoc, jsonkey::kLlvmPath );
                ParserConfigInternal::assignIfPresent( msvcToolsDir, engineDoc, jsonkey::kMsvcToolsDir );
                ParserConfigInternal::assignIfPresent( winSdkDir, engineDoc, jsonkey::kWindowsSdkDir );
                ParserConfigInternal::assignIfPresent( winSdkVer, engineDoc, jsonkey::kWindowsSdkVersion );
            }
        }

        if ( _listBaseArg.empty() )
        {
            SW_LOG_ERROR( "No parser_args available (config empty)." );
            return false;
        }

        BLOCK( "Target Macros" )
        {
            // 헤더는 CMake 가 정의하는 타깃 매크로만 읽고 `Core/Common/TargetMacroCheck.h` 가 그것을 실제 컴파일러와 대조한다.
            // libclang 은 CMake 를 거치지 않으므로 같은 매크로를 여기서 넘긴다. libclang 은 기본 타깃(이 파서를 지은 기계)으로 읽고,
            // 언제나 clang 이다.
#if defined( SW_PLATFORM_WINDOWS )
            _listBaseArg.emplace_back( "-DSW_PLATFORM_WINDOWS" );
#elif defined( SW_PLATFORM_LINUX )
            _listBaseArg.emplace_back( "-DSW_PLATFORM_LINUX" );
#endif
#if defined( SW_X64 )
            _listBaseArg.emplace_back( "-DSW_X64" );
#elif defined( SW_ARM64 )
            _listBaseArg.emplace_back( "-DSW_ARM64" );
#endif
            _listBaseArg.emplace_back( "-DSW_COMPILER_CLANG" );
            // 빌드 타깃 종류 — 파서는 같은 빌드 폴더의 sw_global_options 로 지어지므로 자기 매크로가 곧 그 빌드의 것이다.
#if defined( SW_WITH_CLIENT_CODE )
            _listBaseArg.emplace_back( "-DSW_WITH_CLIENT_CODE" );
#endif
#if defined( SW_WITH_SERVER_CODE )
            _listBaseArg.emplace_back( "-DSW_WITH_SERVER_CODE" );
#endif
        }

        BLOCK( "Locate LLVM and Clang Resource Directory" )
        {
            if ( llvmPath.empty() )
            {
                const utf8* pEnvLlvm = std::getenv( jsonkey::kEnvLlvmDir );
                if ( pEnvLlvm == nullptr )
                    pEnvLlvm = std::getenv( jsonkey::kEnvLlvmHome );
                if ( pEnvLlvm != nullptr )
                    llvmPath = pEnvLlvm;
            }

            const string llvmClangDir = FileUtil::joinPath( llvmPath, _llvmClangRel );
            if ( FileUtil::isDirectory( llvmClangDir ) )
            {
                vector<string> listClangSubFolder;
                FileUtil::collectFolders( llvmClangDir, listClangSubFolder, false );
                for ( const string& folder : listClangSubFolder )
                {
                    const string resourceDir = FileUtil::normalizeSeparators( folder );
                    const string clangInc    = FileUtil::joinPath( folder, _clangIncludeRel );
                    if ( FileUtil::isDirectory( clangInc ) == false )
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
            if ( msvcToolsDir.empty() == false && FileUtil::isDirectory( msvcInc ) )
            {
                _listBaseArg.emplace_back( _flagIsystem );
                _listBaseArg.emplace_back( msvcInc );
            }

            if ( winSdkDir.empty() == false && winSdkVer.empty() == false )
            {
                const string ucrtPath = FileUtil::joinPath(
                    FileUtil::joinPath( FileUtil::joinPath( winSdkDir, _winSdkIncludeRel ), winSdkVer ),
                    _winSdkUcrtRel );
                if ( FileUtil::isDirectory( ucrtPath ) )
                {
                    _listBaseArg.emplace_back( _flagIsystem );
                    _listBaseArg.emplace_back( ucrtPath );
                }
            }
        }
#endif

        _bLoaded = SW_TRUE;
        SW_LOG_TRACE( "Loaded clang config (%# base args).", static_cast<uint32>( _listBaseArg.size() ) );
        return true;
    }

    vector<string> ParserConfig::makeArgs( const vector<string>& listIncludePath ) const
    {
        vector<string> listArg = _listBaseArg;
        listArg.reserve( listArg.size() + listIncludePath.size() + _listForceInclude.size() * 2 );
        for ( const string& includePath : listIncludePath )
        {
            listArg.push_back( _flagIncludePrefix + includePath );
        }
        for ( const string& forceInclude : _listForceInclude )
        {
            listArg.push_back( _flagForceInclude );
            listArg.push_back( forceInclude );
        }
        return listArg;
    }
} // namespace sw
