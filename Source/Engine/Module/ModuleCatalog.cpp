#include "pch.h"

#include "Engine/Module/ModuleCatalog.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/DependencyGraph.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/Format/JSONSerializer.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
    namespace
    {
        struct ModuleCatalogInternal
        {
            /** @brief 낱말 목록을 비트로 모읍니다(중복은 같은 비트). */
            template <typename TEnum>
            static uint8 toMask( const vector<TEnum>& listValue )
            {
                uint8 mask = 0;
                for ( const TEnum value : listValue )
                {
                    mask |= static_cast<uint8>( value );
                }
                return mask;
            }

            /**
             * @brief 모듈이 도는 플랫폼 비트입니다. 기본은 전부이고 `_listExcludedPlatform` 이 뺍니다(플랫폼 하나를 더할 때 매니페스트 87 개를 고치지 않는다).
             * @details 에디터 · 에디터 확장은 종류가 데스크톱 전용이라 적지 않아도 Android · Web 에서 빠집니다. CMake `sw_readManifestPlatforms` 와 같은 규칙입니다.
             */
            static uint8 computePlatformMask( const ModuleManifestFile& file )
            {
                constexpr uint8 kDesktop     = static_cast<uint8>( ModulePlatform::Windows ) | static_cast<uint8>( ModulePlatform::Linux );
                constexpr uint8 kAll         = kDesktop | static_cast<uint8>( ModulePlatform::Android ) | static_cast<uint8>( ModulePlatform::Web );
                const bool      bDesktopOnly = file._kind == ModuleKind::Editor || file._kind == ModuleKind::EditorExtension;
                return static_cast<uint8>( ( bDesktopOnly ? kDesktop : kAll ) & ~toMask( file._listExcludedPlatform ) );
            }

            /** @brief 모듈이 이 플랫폼 · 구성 · 대상에 없으면 그 이유, 있으면 빈 문자열입니다(CMake 이유 글과 같다). */
            static string findUnavailableReason( const ModuleManifest& manifest, const ModuleResolveContext& context )
            {
                if ( ( manifest._platformMask & static_cast<uint8>( context._platform ) ) == 0 )
                    return string( "not available on " ) + getEnumName( context._platform );
                if ( ( manifest._configurationMask & static_cast<uint8>( context._configuration ) ) == 0 )
                    return context._configuration == ModuleConfiguration::Dev ? "not built for Dev" : "not built for Shipping";
                if ( ( manifest._targetMask & context._targetMask ) == 0 )
                    return ( context._targetMask & static_cast<uint8>( ModuleTarget::Client ) ) != 0 ? "not built for the Client target" : "not built for the Server target";
                return {};
            }

            // ------------------------------------------------------------------------------
            // 매니페스트 읽기 — `ModuleCatalog::parseManifest` 가 위에서 아래로 부른다. 실패는 문장(리터럴)과 대상(어느 파일 · 모듈의 무엇)이다.
            // ------------------------------------------------------------------------------

            /**
             * @brief JSON 글을 매니페스트 파일 형식으로 읽습니다.
             * @details 키는 대소문자까지 맞아야 한다(직렬화기는 키를 철자 그대로 찾는다). 모르는 키 · 읽지 못한 값은 orphan 으로 남는다.
             *          범위 밖 값(`Rejected`)이 있어도 모은 키를 모두 적는다 — 첫 실패에서 돌아가면 같은 파일의 오타가 다음 수정까지 숨는다(C-60).
             */
            [[nodiscard]] static expected<void> deserializeManifestFile( string_view jsonText, const string& where, ModuleManifestFile& outFile )
            {
                SerializeContext          context = SerializeContext::deriveFromDefault( SerializePurpose::Authoring );
                vector<SchemaOrphanValue> listOrphan;
                const SoftReadResult      result = JSONSerializer::deserializeSoft( &outFile, *ModuleManifestFile::StaticType(), jsonText, &listOrphan, nullptr, context );
                if ( result == SoftReadResult::NotParsed )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "not valid JSON, or the root is not an object", where } );
                string problem;
                for ( const SchemaOrphanValue& orphan : listOrphan )
                {
                    problem += problem.empty() ? string() : string( "; " );
                    problem += "unknown key '" + string( orphan._writtenName.empty() ? orphan._name.c_str() : orphan._writtenName.c_str() ) + "' or a value that cannot be read (" +
                               orphan._text + ")";
                }
                if ( result == SoftReadResult::Rejected )
                    problem += problem.empty() ? string( "a value outside its PROPERTY( Min, Max )" ) : string( "; a value outside its PROPERTY( Min, Max )" );
                if ( problem.empty() == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "unknown key, unreadable value or a value outside its range", where }.appendDetail( ": " ).appendDetail( problem ) ); // 목록은 대상 길이에서 잘린다
                return {};
            }

            /** @brief 이름을 정하고 파일 이름과 맞는지 봅니다. 파일 이름이 곧 모듈 이름이다 — 복사본 폴더(`Bin/Modules`)에서 이름이 겹치지 않고, 찾는 쪽이 파일 이름으로 바로 찾는다. */
            [[nodiscard]] static expected<void> readName( const ModuleManifestFile& file, string_view sourcePath, const string& where, ModuleManifest& inoutManifest )
            {
                inoutManifest._name = file._name;
                if ( inoutManifest._name.empty() )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "_name is missing", where } );
                if ( sourcePath.empty() == false )
                {
                    const string fileName = FileUtil::getFileNamePart( sourcePath );
                    if ( StringUtil::equals( fileName, inoutManifest._name + ModuleCatalog::kManifestExtension, false ) == false )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "the manifest file must be named after the module", where }
                                               .appendDetail( " -> " )
                                               .appendDetail( inoutManifest._name )
                                               .appendDetail( ModuleCatalog::kManifestExtension ) );
                }
                return {};
            }

            /** @brief 판 · 종류 · 설명과 플랫폼 · 구성 · 대상 비트를 읽습니다. */
            [[nodiscard]] static expected<void> readIdentity( const ModuleManifestFile& file, const string& errorContext, ModuleManifest& inoutManifest )
            {
                if ( ModuleVersion::parse( file._version, inoutManifest._version ) == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "_version must be 'major.minor.patch'", errorContext } );
                if ( file._kind == kModuleKindUnset )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "_kind is missing", errorContext } );
                inoutManifest._kind        = file._kind;
                inoutManifest._description = file._description;

                inoutManifest._platformMask      = computePlatformMask( file );
                inoutManifest._configurationMask = toMask( file._listConfiguration );
                inoutManifest._targetMask        = toMask( file._listTarget );
                if ( inoutManifest._platformMask == 0 )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "_listExcludedPlatform leaves no platform", errorContext } );
                if ( inoutManifest._configurationMask == 0 )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "_listConfiguration must list Dev and/or Shipping", errorContext } );
                if ( inoutManifest._targetMask == 0 )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "_listTarget must list Client and/or Server", errorContext } );
                inoutManifest._bEnabledByDefault = file._bEnabledByDefault;
                return {};
            }

            /** @brief 의존 목록을 읽습니다. 이름이 없거나 자기 자신이면, 최소 판이 깨졌으면 false 입니다. */
            [[nodiscard]] static expected<void> readDependencies( const ModuleManifestFile& file, const string& errorContext, ModuleManifest& inoutManifest )
            {
                for ( size_t index = 0; index < file._listDependency.size(); ++index )
                {
                    const ModuleDependencyFile& entry = file._listDependency[index];
                    ModuleDependency            dependency{};
                    dependency._name = entry._name;
                    if ( dependency._name.empty() || dependency._name == inoutManifest._name )
                    {
                        StringBuilder<constant::buffer::kMax32> indexBuilder;
                        indexBuilder.appendFormat( ": dependency %#", index );
                        return unexpected( Error{ ErrorKind::InvalidArgument, "a dependency has no name or names the module itself", errorContext }.appendDetail( indexBuilder.view() ) );
                    }
                    if ( entry._minVersion.empty() == false && ModuleVersion::parse( entry._minVersion, dependency._minVersion ) == false )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "a dependency has a bad _minVersion", errorContext }.appendDetail( ": " ).appendDetail( dependency._name ) );
                    inoutManifest._listDependency.push_back( std::move( dependency ) );
                }
                return {};
            }

            /** @brief 프로젝트(Game 모듈)의 켜기/끄기 표를 읽습니다. 다른 종류가 표를 가지거나 이름이 빈 줄이 있으면 false 입니다. */
            [[nodiscard]] static expected<void> readOverrides( ModuleManifestFile& inoutFile, const string& errorContext, ModuleManifest& inoutManifest )
            {
                if ( inoutFile._listModuleOverride.empty() == false && inoutManifest._kind != ModuleKind::Game )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "only a Game module (the project) has _listModuleOverride", errorContext } );
                for ( size_t index = 0; index < inoutFile._listModuleOverride.size(); ++index )
                {
                    if ( inoutFile._listModuleOverride[index]._name.empty() )
                    {
                        StringBuilder<constant::buffer::kMax32> indexBuilder;
                        indexBuilder.appendFormat( ": override %#", index );
                        return unexpected( Error{ ErrorKind::InvalidArgument, "a module override must be { \"_name\": ..., \"_bEnabled\": true|false }", errorContext }.appendDetail( indexBuilder.view() ) );
                    }
                }
                inoutManifest._listModuleOverride = std::move( inoutFile._listModuleOverride );
                return {};
            }

            /** @brief 에디터 확장은 Dev 전용이고 EditorModule 에 의존한다(언리얼 Type "Editor" 모듈과 같다) — CMake `sw_readModuleManifest` 와 같은 검사. */
            [[nodiscard]] static expected<void> validateEditorExtension( const ModuleManifest& manifest, const string& errorContext )
            {
                if ( manifest._kind != ModuleKind::EditorExtension )
                    return {};
                if ( manifest._configurationMask != static_cast<uint8>( ModuleConfiguration::Dev ) )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "an EditorExtension module is built for Dev only (_listConfiguration [\"Dev\"])", errorContext } );
                bool bDependsOnEditor = false;
                for ( const ModuleDependency& dependency : manifest._listDependency )
                {
                    bDependsOnEditor = bDependsOnEditor || dependency._name == config::kTargetEditorModule;
                }
                if ( bDependsOnEditor == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "an EditorExtension module must list the editor module in _listDependency", errorContext }
                                           .appendDetail( ": " )
                                           .appendDetail( config::kTargetEditorModule ) );
                return {};
            }

            // ------------------------------------------------------------------------------
            // 해석 — `ModuleCatalog::resolve` 가 위에서 아래로 부른다. 목록은 매니페스트 순서(`getManifests`)와 같은 칸이다.
            // ------------------------------------------------------------------------------

            /** @brief 매니페스트 @p pManifest 가 목록의 몇 번째인지입니다. */
            static size_t indexOf( const ModuleCatalog& catalog, const ModuleManifest* pManifest )
            {
                return static_cast<size_t>( pManifest - catalog.getManifests().data() );
            }

            /** @brief 1) 프로젝트의 켜기/끄기 표를 칸으로 옮깁니다(-1 = 표에 없음, 0 = 끔, 1 = 켬). 모르는 이름은 오류다(꺼 두려던 모듈의 이름을 틀리면 켜진 채 남는다). */
            [[nodiscard]] static expected<void> collectOverrides( const ModuleCatalog& catalog, string_view projectModule, vector<int8>& outListOverride )
            {
                outListOverride.assign( catalog.getManifestCount(), -1 );
                const ModuleManifest* pProject = catalog.findManifest( projectModule );
                if ( pProject == nullptr )
                    return {};
                for ( const ModuleOverride& entry : pProject->_listModuleOverride )
                {
                    const ModuleManifest* pTarget = catalog.findManifest( entry._name );
                    if ( pTarget == nullptr )
                        return unexpected( Error{ ErrorKind::NotFound, "a module override names an unknown module", entry._name }.appendDetail( " in " ).appendDetail( pProject->_sourcePath ) );
                    outListOverride[indexOf( catalog, pTarget )] = entry._bEnabled ? 1 : 0;
                }
                return {};
            }

            /** @brief 2) 켜짐 = 표 > 기본값, 그리고 이 플랫폼 · 구성에 있음. 프로젝트는 늘 켜져 있다. 꺼진 이유는 @p outListReason 에 둔다. */
            static void decideActive( const ModuleCatalog& catalog, const ModuleResolveContext& context, const vector<int8>& listOverride, vector<uint8>& outListActive, vector<string>& outListReason )
            {
                const size_t manifestCount = catalog.getManifestCount();
                outListActive.assign( manifestCount, static_cast<uint8>( SW_FALSE ) );
                outListReason.assign( manifestCount, string() );
                for ( size_t index = 0; index < manifestCount; ++index )
                {
                    const ModuleManifest& manifest    = catalog.getManifests()[index];
                    const bool            bProject    = manifest._name == context._projectModule;
                    const bool            bEnabled    = bProject || ( listOverride[index] >= 0 ? listOverride[index] == 1 : manifest._bEnabledByDefault );
                    const string          unavailable = findUnavailableReason( manifest, context );
                    if ( bEnabled == false )
                        outListReason[index] = listOverride[index] == 0 ? "disabled by the project" : "disabled by default";
                    else if ( unavailable.empty() == false )
                        outListReason[index] = unavailable;
                    else
                        outListActive[index] = SW_TRUE;
                }
            }

            /** @brief 2-2) 에디터 확장은 의존(EditorModule · 키트)이 하나라도 꺼져 있으면 함께 꺼진다 — 오류가 아니다(그 확장만 없다). CMake 의 `sw_resolveModuleManifests` 와 같다. */
            static void deactivateEditorExtensions( const ModuleCatalog& catalog, vector<uint8>& inoutListActive, vector<string>& inoutListReason )
            {
                // 확장이 확장에 의존할 수 있고 목록은 이름 순이다. 뒤의 확장이 꺼지면 앞의 확장도 꺼져야 하므로 바뀌는 것이 없을 때까지 돈다.
                bool bChanged = true;
                while ( bChanged )
                {
                    bChanged = false;
                    for ( size_t index = 0; index < catalog.getManifestCount(); ++index )
                    {
                        const ModuleManifest& manifest = catalog.getManifests()[index];
                        if ( inoutListActive[index] == SW_FALSE || manifest._kind != ModuleKind::EditorExtension )
                            continue;
                        for ( const ModuleDependency& dependency : manifest._listDependency )
                        {
                            const ModuleManifest* pDependency = catalog.findManifest( dependency._name );
                            if ( pDependency == nullptr )
                                break; // 없는 의존은 다음 단계(`collectDependencies`)가 오류로 알린다
                            const size_t dependencyIndex = indexOf( catalog, pDependency );
                            if ( inoutListActive[dependencyIndex] == SW_FALSE )
                            {
                                inoutListActive[index] = SW_FALSE;
                                inoutListReason[index] = "needs " + dependency._name + ", which is " + inoutListReason[dependencyIndex];
                                bChanged               = true;
                                break;
                            }
                        }
                    }
                }
            }

            /** @brief 3) 켜진 모듈의 의존은 모두 있고 · 켜져 있고 · 버전이 맞아야 한다. 의존 칸 번호 목록을 만든다. */
            [[nodiscard]] static expected<void> collectDependencies( const ModuleCatalog& catalog, const vector<uint8>& listActive, const vector<string>& listReason,
                                                                     vector<vector<uint32>>& outListDependency )
            {
                outListDependency.assign( catalog.getManifestCount(), vector<uint32>() );
                for ( size_t index = 0; index < catalog.getManifestCount(); ++index )
                {
                    if ( listActive[index] == SW_FALSE )
                        continue;
                    const ModuleManifest& manifest = catalog.getManifests()[index];
                    for ( const ModuleDependency& dependency : manifest._listDependency )
                    {
                        const ModuleManifest* pDependency = catalog.findManifest( dependency._name );
                        if ( pDependency == nullptr )
                            return unexpected( Error{ ErrorKind::NotFound, "a module depends on a module that has no manifest (missing module)", manifest._name }
                                                   .appendDetail( " -> " )
                                                   .appendDetail( dependency._name ) );
                        const size_t dependencyIndex = indexOf( catalog, pDependency );
                        if ( listActive[dependencyIndex] == SW_FALSE )
                            return unexpected( Error{ ErrorKind::NotFound, "a module depends on an inactive module", manifest._name }
                                                   .appendDetail( " -> " )
                                                   .appendDetail( dependency._name )
                                                   .appendDetail( ": " )
                                                   .appendDetail( listReason[dependencyIndex] ) );
                        if ( pDependency->_version.isAtLeast( dependency._minVersion ) == false )
                            return unexpected( Error{ ErrorKind::Unsupported, "a module needs a newer version of its dependency", manifest._name }
                                                   .appendDetail( " needs " )
                                                   .appendDetail( dependency._name )
                                                   .appendDetail( " " )
                                                   .appendDetail( dependency._minVersion.toString() )
                                                   .appendDetail( " or later, " )
                                                   .appendDetail( pDependency->_version.toString() )
                                                   .appendDetail( " is present" ) );
                        outListDependency[index].push_back( static_cast<uint32>( dependencyIndex ) );
                    }
                }
                return {};
            }

            /** @brief 4) 켜진 모듈만으로 위상 정렬해 적재 순서를 만든다. 순환이면 그 경로를 말한다. */
            [[nodiscard]] static expected<void> sortLoadOrder( const ModuleCatalog& catalog, const vector<uint8>& listActive, const vector<vector<uint32>>& listDependency, vector<string>& outListLoadOrder )
            {
                const size_t        manifestCount = catalog.getManifestCount();
                vector<uint32>      listActiveIndex;
                vector<string_view> listActiveName;
                vector<uint32>      listCompactOf( manifestCount, 0 );
                for ( size_t index = 0; index < manifestCount; ++index )
                {
                    if ( listActive[index] == SW_FALSE )
                        continue;
                    listCompactOf[index] = static_cast<uint32>( listActiveIndex.size() );
                    listActiveIndex.push_back( static_cast<uint32>( index ) );
                    listActiveName.push_back( catalog.getManifests()[index]._name );
                }
                DependencyGraph graph{ static_cast<uint32>( listActiveIndex.size() ) };
                for ( uint32 compactIndex = 0; compactIndex < static_cast<uint32>( listActiveIndex.size() ); ++compactIndex )
                {
                    for ( const uint32 dependencyIndex : listDependency[listActiveIndex[compactIndex]] )
                    {
                        graph.addDependency( compactIndex, listCompactOf[dependencyIndex] );
                    }
                }

                const DependencyOrder order = graph.sortByName( listActiveName );
                if ( order.isSorted() == false )
                {
                    Error error{ ErrorKind::Cycle, "module dependency cycle" };
                    for ( size_t step = 0; step < order._listCycle.size(); ++step )
                    {
                        error.appendDetail( step == 0 ? "" : " -> " ).appendDetail( listActiveName[order._listCycle[step]] );
                    }
                    return unexpected( error );
                }
                for ( const uint32 compactIndex : order._listOrder )
                {
                    outListLoadOrder.push_back( string( listActiveName[compactIndex] ) );
                }
                return {};
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool ModuleVersion::parse( string_view text, ModuleVersion& outVersion )
    {
        uint32 arrPart[3]{};
        size_t partIndex = 0;
        bool   bDigit    = false;
        for ( const utf8 ch : text )
        {
            if ( '0' <= ch && ch <= '9' )
            {
                arrPart[partIndex] = arrPart[partIndex] * 10 + static_cast<uint32>( ch - '0' );
                if ( arrPart[partIndex] > 1000000 )
                    return false;
                bDigit = true;
                continue;
            }
            const bool bBadVersionSeparator = ch != '.' || bDigit == false || partIndex == 2;
            if ( bBadVersionSeparator )
                return false;
            ++partIndex;
            bDigit = false;
        }
        if ( partIndex != 2 || bDigit == false )
            return false;
        outVersion._major = arrPart[0];
        outVersion._minor = arrPart[1];
        outVersion._patch = arrPart[2];
        return true;
    }

    bool ModuleVersion::isAtLeast( const ModuleVersion& minimum ) const
    {
        if ( _major != minimum._major )
            return _major > minimum._major;
        if ( _minor != minimum._minor )
            return _minor > minimum._minor;
        return _patch >= minimum._patch;
    }

    string ModuleVersion::toString() const
    {
        StringBuilder<constant::buffer::kMax256> resultBuilder;
        resultBuilder.appendFormat( "%#.%#.%#", _major, _minor, _patch );
        return string( resultBuilder.view() );
    }

    bool ModuleResolution::isActive( string_view name ) const
    {
        return VectorUtil::contains( _listLoadOrder, name );
    }
} // namespace sw

namespace sw
{
    expected<void> ModuleCatalog::parseManifest( string_view jsonText, string_view sourcePath, ModuleManifest& outManifest )
    {
        using Internal          = ModuleCatalogInternal;
        outManifest             = ModuleManifest{};
        outManifest._sourcePath = string( sourcePath );
        const string where      = sourcePath.empty() ? string( "<module manifest>" ) : string( sourcePath );

        ModuleManifestFile file;
        if ( expected<void> read = Internal::deserializeManifestFile( jsonText, where, file ); read.has_value() == false )
            return read;
        if ( expected<void> read = Internal::readName( file, sourcePath, where, outManifest ); read.has_value() == false )
            return read;

        const string errorContext = where + " (" + outManifest._name + ")";
        if ( expected<void> read = Internal::readIdentity( file, errorContext, outManifest ); read.has_value() == false )
            return read;
        if ( expected<void> read = Internal::readDependencies( file, errorContext, outManifest ); read.has_value() == false )
            return read;
        if ( expected<void> read = Internal::readOverrides( file, errorContext, outManifest ); read.has_value() == false )
            return read;
        return Internal::validateEditorExtension( outManifest, errorContext );
    }

    expected<void> ModuleCatalog::addManifest( ModuleManifest manifest )
    {
        const ModuleManifest* pExisting = findManifest( manifest._name );
        if ( pExisting != nullptr )
            return unexpected( Error{ ErrorKind::AlreadyExists, "a module is declared twice", manifest._name }
                                   .appendDetail( " (" )
                                   .appendDetail( pExisting->_sourcePath )
                                   .appendDetail( ", " )
                                   .appendDetail( manifest._sourcePath )
                                   .appendDetail( ")" ) );
        _listManifest.push_back( std::move( manifest ) );
        return {};
    }

    expected<void> ModuleCatalog::loadDirectory( string_view directoryPath )
    {
        if ( FileUtil::isDirectory( directoryPath ) == false )
            return unexpected( Error{ ErrorKind::NotFound, "the module catalog folder does not exist", directoryPath } );
        vector<string> listFile;
        if ( FileUtil::collectFiles( directoryPath, ".json", listFile, false ) == false )
            return unexpected( Error{ ErrorKind::ExternalFailure, "the module catalog folder cannot be listed", directoryPath } );
        VectorUtil::sort( listFile );
        for ( const string& filePath : listFile )
        {
            if ( StringUtil::endsWith( filePath, kManifestExtension, true ) == false )
                continue;
            string text;
            if ( FileUtil::readTextFile( filePath, text ) == false )
                return unexpected( Error{ ErrorKind::ExternalFailure, "a module manifest cannot be read", filePath } );
            ModuleManifest manifest;
            if ( expected<void> parsed = parseManifest( text, filePath, manifest ); parsed.has_value() == false )
                return parsed;
            if ( expected<void> added = addManifest( std::move( manifest ) ); added.has_value() == false )
                return added;
        }
        return {};
    }

    bool ModuleCatalog::setProjectOverride( string_view projectModule, string_view moduleName, bool bEnabled )
    {
        for ( ModuleManifest& manifest : _listManifest )
        {
            if ( manifest._name != projectModule )
                continue;
            for ( ModuleOverride& entry : manifest._listModuleOverride )
            {
                if ( entry._name == moduleName )
                {
                    entry._bEnabled = bEnabled;
                    return true;
                }
            }
            manifest._listModuleOverride.push_back( ModuleOverride{ string( moduleName ), bEnabled } );
            return true;
        }
        return false;
    }

    const ModuleManifest* ModuleCatalog::findManifest( string_view name ) const
    {
        return VectorUtil::findBy<&ModuleManifest::_name>( _listManifest, name );
    }

    expected<void> ModuleCatalog::resolve( const ModuleResolveContext& context, ModuleResolution& outResolution ) const
    {
        using Internal = ModuleCatalogInternal;
        outResolution  = ModuleResolution{};

        vector<int8> listOverride;
        if ( expected<void> collected = Internal::collectOverrides( *this, context._projectModule, listOverride ); collected.has_value() == false )
            return collected;

        vector<uint8>  listActive;
        vector<string> listReason;
        Internal::decideActive( *this, context, listOverride, listActive, listReason );
        Internal::deactivateEditorExtensions( *this, listActive, listReason );

        vector<vector<uint32>> listDependency;
        if ( expected<void> collected = Internal::collectDependencies( *this, listActive, listReason, listDependency ); collected.has_value() == false )
            return collected;
        if ( expected<void> sorted = Internal::sortLoadOrder( *this, listActive, listDependency, outResolution._listLoadOrder ); sorted.has_value() == false )
            return sorted;

        for ( size_t index = 0; index < _listManifest.size(); ++index )
        {
            if ( listActive[index] == SW_FALSE )
                outResolution._listInactive.push_back( ModuleInactiveEntry{ _listManifest[index]._name, listReason[index] } );
        }
        VectorUtil::sort( outResolution._listInactive, []( const ModuleInactiveEntry& lhs, const ModuleInactiveEntry& rhs )
        { return lhs._name < rhs._name; } );
        return {};
    }

    ModulePlatform ModuleCatalog::getCurrentPlatform()
    {
#if defined( SW_PLATFORM_WINDOWS )
        return ModulePlatform::Windows;
#elif defined( SW_PLATFORM_LINUX )
        return ModulePlatform::Linux;
#elif defined( SW_PLATFORM_ANDROID )
        return ModulePlatform::Android;
#elif defined( SW_PLATFORM_WEB )
        return ModulePlatform::Web;
#else
    #error "Unsupported platform"
#endif
    }

    ModuleConfiguration ModuleCatalog::getCurrentConfiguration()
    {
        return build::kShipping ? ModuleConfiguration::Shipping : ModuleConfiguration::Dev;
    }

    uint8 ModuleCatalog::getBuildTargetMask()
    {
        uint8 mask = 0;
        if constexpr ( build::kWithClientCode )
        {
            mask |= static_cast<uint8>( ModuleTarget::Client );
        }
        if constexpr ( build::kWithServerCode )
        {
            mask |= static_cast<uint8>( ModuleTarget::Server );
        }
        return mask;
    }
} // namespace sw
