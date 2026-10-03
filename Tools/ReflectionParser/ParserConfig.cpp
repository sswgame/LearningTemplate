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
                    if ( FileUtil::fileExists( candidate ) )
                        return candidate;

                    const string parent = FileUtil::getDirectoryPart( cur );
                    if ( parent.empty() || parent == cur )
                        break;
                    cur = parent;
                }
                return {};
            }

            /** @brief 설정 파일을 찾아 JSON 으로 읽고, 읽었으면 경로를 남깁니다. 없거나 깨졌으면 null 객체입니다. */
            static nlohmann::json loadDocument( const string& relPath, vector<string>& inoutListLoadedFile )
            {
                const string path = findConfigFile( relPath );
                string       text;
                if ( path.empty() || FileUtil::readTextFile( path, text ) == false || text.empty() )
                    return nlohmann::json{};

                inoutListLoadedFile.push_back( path );
                try
                {
                    return nlohmann::json::parse( text );
                }
                catch ( const nlohmann::json::parse_error& )
                {
                    return nlohmann::json{};
                }
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
                    assignIfPresent( config.*member, obj, key );
            }

            static void applyPathsSection( ParserConfig& config, const nlohmann::json& obj )
            {
                applyBindings( config, obj, {
                                                {    &ParserConfig::_llvmClangRel,     jsonKeyConstants::kLlvmClangRel},
                                                { &ParserConfig::_clangIncludeRel,  jsonKeyConstants::kClangIncludeRel},
                                                {  &ParserConfig::_msvcIncludeRel,   jsonKeyConstants::kMsvcIncludeRel},
                                                {&ParserConfig::_winSdkIncludeRel, jsonKeyConstants::kWinSdkIncludeRel},
                                                {   &ParserConfig::_winSdkUcrtRel,    jsonKeyConstants::kWinSdkUcrtRel},
                } );
            }

            static void applyClangFlagsSection( ParserConfig& config, const nlohmann::json& obj )
            {
                applyBindings( config, obj, {
                                                {         &ParserConfig::_flagIncludePrefix,      jsonKeyConstants::kFlagIncludePrefix},
                                                {               &ParserConfig::_flagIsystem,            jsonKeyConstants::kFlagIsystem},
                                                {           &ParserConfig::_flagResourceDir,        jsonKeyConstants::kFlagResourceDir},
                                                {          &ParserConfig::_flagForceInclude,       jsonKeyConstants::kFlagForceInclude},
                                                {      &ParserConfig::_flagFmsCompatibility,   jsonKeyConstants::kFlagFmsCompatibility},
                                                {         &ParserConfig::_flagFmsExtensions,      jsonKeyConstants::kFlagFmsExtensions},
                                                {&ParserConfig::_flagFmsCompatVersionPrefix, jsonKeyConstants::kFlagFmsCompatVerPrefix},
                } );
            }

            static void applyEmitSection( ParserConfig& config, const nlohmann::json& obj )
            {
                applyBindings( config, obj, {
                                                {       &ParserConfig::_emitCppExtension,          jsonKeyConstants::kEmitCppExtension},
                                                {    &ParserConfig::_emitHeaderExtension,       jsonKeyConstants::kEmitHeaderExtension},
                                                {  &ParserConfig::_emitTemplateExtension,     jsonKeyConstants::kEmitTemplateExtension},
                                                {&ParserConfig::_emitAutoGeneratedBanner,   jsonKeyConstants::kEmitAutoGeneratedBanner},
                                                {  &ParserConfig::_emitPlaceholderMarker,     jsonKeyConstants::kEmitPlaceholderMarker},
                                                {&ParserConfig::_emitRegenByParserMarker,           jsonKeyConstants::kEmitRegenMarker},
                                                {    &ParserConfig::_emitGeneratedNsOpen,       jsonKeyConstants::kEmitGeneratedNsOpen},
                                                {   &ParserConfig::_emitGeneratedNsClose,      jsonKeyConstants::kEmitGeneratedNsClose},
                                                {      &ParserConfig::_emitFlagOpsHeader,         jsonKeyConstants::kEmitFlagOpsHeader},
                                                {      &ParserConfig::_emitFlagOpsMarker,         jsonKeyConstants::kEmitFlagOpsMarker},
                                                { &ParserConfig::_emitRegisterTypeMarker,    jsonKeyConstants::kEmitRegisterTypeMarker},
                                                { &ParserConfig::_emitRegisterEnumMarker,    jsonKeyConstants::kEmitRegisterEnumMarker},
                                                {   &ParserConfig::_emitSourcePathMarker,      jsonKeyConstants::kEmitSourcePathMarker},
                                                {  &ParserConfig::_valueForbiddenMessage, jsonKeyConstants::kEmitValueForbiddenMessage},
                } );
                assignArrayIfPresent( config._listValueForbiddenBaseType, obj, jsonKeyConstants::kEmitValueForbiddenBases );
            }

            static void applyParsingSection( ParserConfig& config, const nlohmann::json& obj )
            {
                assignArrayIfPresent( config._listComponentBaseType, obj, jsonKeyConstants::kParsingComponentBaseTypes );
                assignArrayIfPresent( config._listTypeStripPrefix, obj, jsonKeyConstants::kParsingTypeStripPrefixes );
                assignIfPresent( config._defaultModule, obj, jsonKeyConstants::kParsingDefaultModule );

                const auto itRules = obj.find( jsonKeyConstants::kParsingModuleRules );
                if ( itRules == obj.end() || itRules->is_array() == false )
                    return;

                config._listModuleRule.clear();
                for ( const auto& rule : *itRules )
                {
                    if ( rule.is_object() == false )
                        continue;
                    ParserConfig::ModuleRule parsed;
                    assignIfPresent( parsed._pathContains, rule, jsonKeyConstants::kModuleRulePathContains );
                    assignIfPresent( parsed._module, rule, jsonKeyConstants::kModuleRuleModule );
                    if ( parsed._pathContains.empty() || parsed._module.empty() )
                        continue;
                    config._listModuleRule.push_back( std::move( parsed ) );
                }
            }

            static void applyTuningSection( ParserConfig& config, const nlohmann::json& obj )
            {
                config._sourceLookbackBytes = getUintOrDefault( obj, jsonKeyConstants::kSourceLookbackBytes, config._sourceLookbackBytes );
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
                    if ( section == jsonKeyConstants::kPaths )
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
                        const bool         bMachineLocal = section == jsonKeyConstants::kParserArgsSection &&
                                                   ( key == jsonKeyConstants::kArgsExtra || key == jsonKeyConstants::kArgsForceInclude );
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
                const auto     itArgs = doc.find( jsonKeyConstants::kParserArgsSection );
                if ( itArgs == doc.end() || itArgs->is_object() == false )
                    return outList;

                const nlohmann::json& argsSection = *itArgs;
                appendUnique( outList, collectStringArray( argsSection.value( jsonKeyConstants::kArgsDefault, nlohmann::json{} ) ) );

#if defined( SW_PLATFORM_WINDOWS ) || defined( SW_PLATFORM_LINUX )
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

        const nlohmann::json defaultsDoc = ParserConfigInternal::loadDocument( pathConstants::kParserConfigDefaults, _listLoadedFile );
        // 로컬은 이 기계에 딸린 키만 받는다(`keepMachineLocalKeys`) — 나머지는 커밋된 기본값이 정한다.
        const nlohmann::json localDoc = ParserConfigInternal::keepMachineLocalKeys(
            defaultsDoc, ParserConfigInternal::loadDocument( pathConstants::kParserConfig, _listLoadedFile ),
            ParserConfigInternal::findConfigFile( pathConstants::kParserConfig ) );

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
            ParserConfigInternal::mergeConfigSection( *this, defaultsDoc, localDoc, jsonKeyConstants::kPaths, ParserConfigInternal::applyPathsSection );
            ParserConfigInternal::mergeConfigSection( *this, defaultsDoc, localDoc, jsonKeyConstants::kClangFlags, ParserConfigInternal::applyClangFlagsSection );
            ParserConfigInternal::mergeConfigSection( *this, defaultsDoc, localDoc, jsonKeyConstants::kEmit, ParserConfigInternal::applyEmitSection );
            ParserConfigInternal::mergeConfigSection( *this, defaultsDoc, localDoc, jsonKeyConstants::kParsing, ParserConfigInternal::applyParsingSection );
            ParserConfigInternal::mergeConfigSection( *this, defaultsDoc, localDoc, jsonKeyConstants::kTuning, ParserConfigInternal::applyTuningSection );
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
            const nlohmann::json engineDoc = ParserConfigInternal::loadDocument( pathConstants::kToolchainConfig, _listLoadedFile );
            if ( engineDoc.is_object() )
            {
                ParserConfigInternal::assignIfPresent( llvmPath, engineDoc, jsonKeyConstants::kLlvmPath );
                ParserConfigInternal::assignIfPresent( msvcToolsDir, engineDoc, jsonKeyConstants::kMsvcToolsDir );
                ParserConfigInternal::assignIfPresent( winSdkDir, engineDoc, jsonKeyConstants::kWindowsSdkDir );
                ParserConfigInternal::assignIfPresent( winSdkVer, engineDoc, jsonKeyConstants::kWindowsSdkVersion );
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
                FileUtil::collectFolders( llvmClangDir, listClangSubFolder, false );
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
        SW_LOG_TRACE( "Loaded clang config (%# base args).", static_cast<uint32>( _listBaseArg.size() ) );
        return true;
    }

    vector<string> ParserConfig::buildArgs( const vector<string>& listIncludePath ) const
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
} // namespace sw
