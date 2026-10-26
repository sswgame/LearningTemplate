#include "pch.h"

#include "Engine/Module/ModuleCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/TopologicalSortUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"

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

            /** @brief 모듈이 이 플랫폼 · 구성 · 대상에 없으면 그 이유, 있으면 빈 문자열입니다(CMake 이유 글과 같다). */
            static string findUnavailableReason( const ModuleManifest& manifest, const ModuleResolveContext& context )
            {
                if ( ( manifest._platformMask & static_cast<uint8>( context._platform ) ) == 0 )
                    return context._platform == ModulePlatform::Windows ? "not available on Windows" : "not available on Linux";
                if ( ( manifest._configurationMask & static_cast<uint8>( context._configuration ) ) == 0 )
                    return context._configuration == ModuleConfiguration::Dev ? "not built for Dev" : "not built for Shipping";
                if ( ( manifest._targetMask & context._targetMask ) == 0 )
                    return ( context._targetMask & static_cast<uint8>( ModuleTarget::Client ) ) != 0 ? "not built for the Client target" : "not built for the Server target";
                return {};
            }

            // ------------------------------------------------------------------------------
            // 매니페스트 읽기 — `ModuleCatalog::parseManifest` 가 위에서 아래로 부른다. false 면 @p outError 에 이유가 있다.
            // ------------------------------------------------------------------------------

            /**
             * @brief JSON 글을 매니페스트 파일 형식으로 읽습니다.
             * @details 키는 대소문자까지 맞아야 한다(직렬화기는 키를 철자 그대로 찾는다). 모르는 키 · 읽지 못한 값은 orphan 으로 남는다.
             *          범위 밖 값(`Rejected`)이 있어도 모은 키를 모두 적는다 — 첫 실패에서 돌아가면 같은 파일의 오타가 다음 수정까지 숨는다(C-60).
             */
            [[nodiscard]] static bool deserializeManifestFile( string_view jsonText, const string& where, ModuleManifestFile& outFile, string& outError )
            {
                SerializeContext          context = SerializeContext::deriveFromDefault( SerializePurpose::Authoring );
                vector<SchemaOrphanValue> listOrphan;
                const SoftReadResult      result = JSONSerializer::deserializeSoft( &outFile, *ModuleManifestFile::StaticType(), jsonText, &listOrphan, nullptr, context );
                if ( result == SoftReadResult::NotParsed )
                {
                    outError = where + ": not valid JSON, or the root is not an object";
                    return false;
                }
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
                {
                    outError = where + ": " + problem;
                    return false;
                }
                return true;
            }

            /** @brief 이름을 정하고 파일 이름과 맞는지 봅니다. 파일 이름이 곧 모듈 이름이다 — 복사본 폴더(`Bin/Modules`)에서 이름이 겹치지 않고, 찾는 쪽이 파일 이름으로 바로 찾는다. */
            [[nodiscard]] static bool readName( const ModuleManifestFile& file, string_view sourcePath, const string& where, ModuleManifest& inoutManifest, string& outError )
            {
                inoutManifest._name = file._name;
                if ( inoutManifest._name.empty() )
                {
                    outError = where + ": _name is missing";
                    return false;
                }
                if ( sourcePath.empty() == false )
                {
                    const string fileName = FileUtil::getFileNamePart( sourcePath );
                    if ( StringUtil::equals( fileName, inoutManifest._name + ModuleCatalog::kManifestExtension, false ) == false )
                    {
                        outError = where + ": the file must be named '" + inoutManifest._name + ModuleCatalog::kManifestExtension + "'";
                        return false;
                    }
                }
                return true;
            }

            /** @brief 판 · 종류 · 설명과 플랫폼 · 구성 · 대상 비트를 읽습니다. */
            [[nodiscard]] static bool readIdentity( const ModuleManifestFile& file, const string& errorContext, ModuleManifest& inoutManifest, string& outError )
            {
                if ( ModuleVersion::parse( file._version, inoutManifest._version ) == false )
                {
                    outError = errorContext + ": _version must be 'major.minor.patch'";
                    return false;
                }
                if ( file._kind == kModuleKindUnset )
                {
                    outError = errorContext + ": _kind is missing";
                    return false;
                }
                inoutManifest._kind        = file._kind;
                inoutManifest._description = file._description;

                inoutManifest._platformMask      = toMask( file._listPlatform );
                inoutManifest._configurationMask = toMask( file._listConfiguration );
                inoutManifest._targetMask        = toMask( file._listTarget );
                if ( inoutManifest._platformMask == 0 )
                {
                    outError = errorContext + ": _listPlatform must list Windows and/or Linux";
                    return false;
                }
                if ( inoutManifest._configurationMask == 0 )
                {
                    outError = errorContext + ": _listConfiguration must list Dev and/or Shipping";
                    return false;
                }
                if ( inoutManifest._targetMask == 0 )
                {
                    outError = errorContext + ": _listTarget must list Client and/or Server";
                    return false;
                }
                inoutManifest._bEnabledByDefault = file._bEnabledByDefault;
                return true;
            }

            /** @brief 의존 목록을 읽습니다. 이름이 없거나 자기 자신이면, 최소 판이 깨졌으면 false 입니다. */
            [[nodiscard]] static bool readDependencies( const ModuleManifestFile& file, const string& errorContext, ModuleManifest& inoutManifest, string& outError )
            {
                for ( size_t index = 0; index < file._listDependency.size(); ++index )
                {
                    const ModuleDependencyFile& entry = file._listDependency[index];
                    ModuleDependency            dependency{};
                    dependency._name = entry._name;
                    if ( dependency._name.empty() || dependency._name == inoutManifest._name )
                    {
                        outError = errorContext + ": dependency " + to_string( index ) + " has no name or names the module itself";
                        return false;
                    }
                    if ( entry._minVersion.empty() == false && ModuleVersion::parse( entry._minVersion, dependency._minVersion ) == false )
                    {
                        outError = errorContext + ": dependency '" + dependency._name + "' has a bad _minVersion";
                        return false;
                    }
                    inoutManifest._listDependency.push_back( std::move( dependency ) );
                }
                return true;
            }

            /** @brief 프로젝트(Game 모듈)의 켜기/끄기 표를 읽습니다. 다른 종류가 표를 가지거나 이름이 빈 줄이 있으면 false 입니다. */
            [[nodiscard]] static bool readOverrides( ModuleManifestFile& inoutFile, const string& errorContext, ModuleManifest& inoutManifest, string& outError )
            {
                if ( inoutFile._listModuleOverride.empty() == false && inoutManifest._kind != ModuleKind::Game )
                {
                    outError = errorContext + ": only a Game module (the project) has _listModuleOverride";
                    return false;
                }
                for ( size_t index = 0; index < inoutFile._listModuleOverride.size(); ++index )
                {
                    if ( inoutFile._listModuleOverride[index]._name.empty() )
                    {
                        outError = errorContext + ": module override " + to_string( index ) + " must be { \"_name\": ..., \"_bEnabled\": true|false }";
                        return false;
                    }
                }
                inoutManifest._listModuleOverride = std::move( inoutFile._listModuleOverride );
                return true;
            }

            /** @brief 에디터 확장은 Dev 전용이고 EditorModule 에 의존한다(언리얼 Type "Editor" 모듈과 같다) — CMake `sw_readModuleManifest` 와 같은 검사. */
            [[nodiscard]] static bool validateEditorExtension( const ModuleManifest& manifest, const string& errorContext, string& outError )
            {
                if ( manifest._kind != ModuleKind::EditorExtension )
                    return true;
                if ( manifest._configurationMask != static_cast<uint8>( ModuleConfiguration::Dev ) )
                {
                    outError = errorContext + ": an EditorExtension module is built for Dev only (_listConfiguration [\"Dev\"])";
                    return false;
                }
                bool bDependsOnEditor = false;
                for ( const ModuleDependency& dependency : manifest._listDependency )
                {
                    bDependsOnEditor = bDependsOnEditor || dependency._name == config::kTargetEditorModule;
                }
                if ( bDependsOnEditor == false )
                {
                    outError = errorContext + ": an EditorExtension module must list " + string( config::kTargetEditorModule ) + " in _listDependency";
                    return false;
                }
                return true;
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
            [[nodiscard]] static bool collectOverrides( const ModuleCatalog& catalog, string_view projectModule, vector<int8>& outListOverride, string& outError )
            {
                outListOverride.assign( catalog.getManifestCount(), -1 );
                const ModuleManifest* pProject = catalog.findManifest( projectModule );
                if ( pProject == nullptr )
                    return true;
                for ( const ModuleOverride& entry : pProject->_listModuleOverride )
                {
                    const ModuleManifest* pTarget = catalog.findManifest( entry._name );
                    if ( pTarget == nullptr )
                    {
                        outError = pProject->_sourcePath + ": module override names an unknown module '" + entry._name + "'";
                        return false;
                    }
                    outListOverride[indexOf( catalog, pTarget )] = entry._bEnabled ? 1 : 0;
                }
                return true;
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
                            break;
                        }
                    }
                }
            }

            /** @brief 3) 켜진 모듈의 의존은 모두 있고 · 켜져 있고 · 버전이 맞아야 한다. 의존 칸 번호 목록을 만든다. */
            [[nodiscard]] static bool collectDependencies( const ModuleCatalog& catalog, const vector<uint8>& listActive, const vector<string>& listReason, vector<vector<uint32>>& outListDependency,
                                                           string& outError )
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
                        {
                            outError = "Module '" + manifest._name + "' depends on '" + dependency._name + "', which has no manifest (missing module)";
                            return false;
                        }
                        const size_t dependencyIndex = indexOf( catalog, pDependency );
                        if ( listActive[dependencyIndex] == SW_FALSE )
                        {
                            outError = "Module '" + manifest._name + "' depends on '" + dependency._name + "', which is " + listReason[dependencyIndex];
                            return false;
                        }
                        if ( pDependency->_version.isAtLeast( dependency._minVersion ) == false )
                        {
                            outError = "Module '" + manifest._name + "' needs '" + dependency._name + "' " + dependency._minVersion.toString() + " or later, but " +
                                       pDependency->_version.toString() + " is present";
                            return false;
                        }
                        outListDependency[index].push_back( static_cast<uint32>( dependencyIndex ) );
                    }
                }
                return true;
            }

            /** @brief 4) 켜진 모듈만으로 위상 정렬해 적재 순서를 만든다. 순환이면 그 경로를 말한다. */
            [[nodiscard]] static bool sortLoadOrder( const ModuleCatalog& catalog, const vector<uint8>& listActive, const vector<vector<uint32>>& listDependency, vector<string>& outListLoadOrder,
                                                     string& outError )
            {
                const size_t           manifestCount = catalog.getManifestCount();
                vector<uint32>         listActiveIndex;
                vector<string_view>    listActiveName;
                vector<vector<uint32>> listActiveDependency;
                vector<uint32>         listCompactOf( manifestCount, 0 );
                for ( size_t index = 0; index < manifestCount; ++index )
                {
                    if ( listActive[index] == SW_FALSE )
                        continue;
                    listCompactOf[index] = static_cast<uint32>( listActiveIndex.size() );
                    listActiveIndex.push_back( static_cast<uint32>( index ) );
                    listActiveName.push_back( catalog.getManifests()[index]._name );
                }
                for ( const uint32 index : listActiveIndex )
                {
                    vector<uint32> listCompact;
                    for ( const uint32 dependencyIndex : listDependency[index] )
                    {
                        listCompact.push_back( listCompactOf[dependencyIndex] );
                    }
                    listActiveDependency.push_back( std::move( listCompact ) );
                }

                vector<uint32> listOrder;
                vector<uint32> listUnsorted;
                if ( TopologicalSortUtil::sortByDependency( listActiveName, listActiveDependency, listOrder, listUnsorted ) == false )
                {
                    vector<uint32> listCycle;
                    outError = "Module dependency cycle:";
                    if ( TopologicalSortUtil::findCycle( listActiveDependency, listUnsorted, listCycle ) )
                    {
                        for ( size_t step = 0; step < listCycle.size(); ++step )
                        {
                            outError += ( step == 0 ? " " : " -> " ) + string( listActiveName[listCycle[step]] );
                        }
                    }
                    return false;
                }
                for ( const uint32 compactIndex : listOrder )
                {
                    outListLoadOrder.push_back( string( listActiveName[compactIndex] ) );
                }
                return true;
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
        return to_string( _major ) + "." + to_string( _minor ) + "." + to_string( _patch );
    }

    bool ModuleResolution::isActive( string_view name ) const
    {
        return VectorUtil::contains( _listLoadOrder, name );
    }
} // namespace sw

namespace sw
{
    bool ModuleCatalog::parseManifest( string_view jsonText, string_view sourcePath, ModuleManifest& outManifest, string& outError )
    {
        using Internal          = ModuleCatalogInternal;
        outManifest             = ModuleManifest{};
        outManifest._sourcePath = string( sourcePath );
        const string where      = sourcePath.empty() ? string( "<module manifest>" ) : string( sourcePath );

        ModuleManifestFile file;
        if ( Internal::deserializeManifestFile( jsonText, where, file, outError ) == false )
            return false;
        if ( Internal::readName( file, sourcePath, where, outManifest, outError ) == false )
            return false;

        const string errorContext = where + " (" + outManifest._name + ")";
        return Internal::readIdentity( file, errorContext, outManifest, outError ) && Internal::readDependencies( file, errorContext, outManifest, outError ) &&
               Internal::readOverrides( file, errorContext, outManifest, outError ) && Internal::validateEditorExtension( outManifest, errorContext, outError );
    }

    bool ModuleCatalog::addManifest( ModuleManifest manifest, string& outError )
    {
        const ModuleManifest* pExisting = findManifest( manifest._name );
        if ( pExisting != nullptr )
        {
            outError = "Module '" + manifest._name + "' is declared twice (" + pExisting->_sourcePath + ", " + manifest._sourcePath + ")";
            return false;
        }
        _listManifest.push_back( std::move( manifest ) );
        return true;
    }

    bool ModuleCatalog::loadDirectory( string_view directoryPath, string& outError )
    {
        if ( FileUtil::isDirectory( directoryPath ) == false )
        {
            outError = "Module catalog folder does not exist: " + string( directoryPath );
            return false;
        }
        vector<string> listFile;
        if ( FileUtil::collectFiles( directoryPath, ".json", listFile, false ) == false )
        {
            outError = "Cannot list the module catalog folder: " + string( directoryPath );
            return false;
        }
        std::sort( listFile.begin(), listFile.end() );
        for ( const string& filePath : listFile )
        {
            if ( StringUtil::endsWith( filePath, kManifestExtension, true ) == false )
                continue;
            string text;
            if ( FileUtil::readTextFile( filePath, text ) == false )
            {
                outError = "Cannot read module manifest " + filePath;
                return false;
            }
            ModuleManifest manifest;
            if ( parseManifest( text, filePath, manifest, outError ) == false || addManifest( std::move( manifest ), outError ) == false )
                return false;
        }
        return true;
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

    bool ModuleCatalog::resolve( const ModuleResolveContext& context, ModuleResolution& outResolution, string& outError ) const
    {
        using Internal = ModuleCatalogInternal;
        outResolution  = ModuleResolution{};

        vector<int8> listOverride;
        if ( Internal::collectOverrides( *this, context._projectModule, listOverride, outError ) == false )
            return false;

        vector<uint8>  listActive;
        vector<string> listReason;
        Internal::decideActive( *this, context, listOverride, listActive, listReason );
        Internal::deactivateEditorExtensions( *this, listActive, listReason );

        vector<vector<uint32>> listDependency;
        if ( Internal::collectDependencies( *this, listActive, listReason, listDependency, outError ) == false )
            return false;
        if ( Internal::sortLoadOrder( *this, listActive, listDependency, outResolution._listLoadOrder, outError ) == false )
            return false;

        for ( size_t index = 0; index < _listManifest.size(); ++index )
        {
            if ( listActive[index] == SW_FALSE )
                outResolution._listInactive.push_back( ModuleInactiveEntry{ _listManifest[index]._name, listReason[index] } );
        }
        std::sort( outResolution._listInactive.begin(), outResolution._listInactive.end(), []( const ModuleInactiveEntry& lhs, const ModuleInactiveEntry& rhs )
        { return lhs._name < rhs._name; } );
        return true;
    }

    ModulePlatform ModuleCatalog::getCurrentPlatform()
    {
#if defined( SW_PLATFORM_WINDOWS )
        return ModulePlatform::Windows;
#else
        return ModulePlatform::Linux;
#endif
    }

    ModuleConfiguration ModuleCatalog::getCurrentConfiguration()
    {
#if defined( SW_SHIPPING )
        return ModuleConfiguration::Shipping;
#else
        return ModuleConfiguration::Dev;
#endif
    }

    uint8 ModuleCatalog::getBuildTargetMask()
    {
        uint8 mask = 0;
#if defined( SW_WITH_CLIENT_CODE )
        mask |= static_cast<uint8>( ModuleTarget::Client );
#endif
#if defined( SW_WITH_SERVER_CODE )
        mask |= static_cast<uint8>( ModuleTarget::Server );
#endif
        return mask;
    }
} // namespace sw
