#include "pch.h"

#include "Engine/Module/ModuleCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/TopologicalSortUtil.h"
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
            if ( ch != '.' || bDigit == false || partIndex == 2 )
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
        for ( const string& active : _listLoadOrder )
        {
            if ( active == name )
                return true;
        }
        return false;
    }
} // namespace sw

namespace sw
{
    bool ModuleCatalog::parseManifest( string_view jsonText, string_view sourcePath, ModuleManifest& outManifest, string& outError )
    {
        outManifest             = ModuleManifest{};
        outManifest._sourcePath = string( sourcePath );
        const string where      = sourcePath.empty() ? string( "<module manifest>" ) : string( sourcePath );

        // 키는 대소문자까지 맞아야 한다(기본 문맥은 대소문자를 무시해 오타를 삼킨다). 모르는 키 · 읽지 못한 값은 orphan 으로 남는다.
        SerializeContext context = SerializeContext::deriveFromDefault();
        context.setIgnoreCaseKeys( false );
        ModuleManifestFile        file;
        vector<SchemaOrphanValue> listOrphan;
        if ( JSONSerializer::deserializeSoft( &file, *ModuleManifestFile::StaticType(), jsonText, &listOrphan, nullptr, context ) == false )
        {
            outError = where + ": not valid JSON, or the root is not an object";
            return false;
        }
        if ( listOrphan.empty() == false )
        {
            const SchemaOrphanValue& orphan = listOrphan.front();
            outError                        = where + ": unknown key '" + string( orphan._writtenName.empty() ? orphan._name.c_str() : orphan._writtenName.c_str() ) +
                       "' or a value that cannot be read (" + orphan._text + ")";
            return false;
        }

        outManifest._name = file._name;
        if ( outManifest._name.empty() )
        {
            outError = where + ": _name is missing";
            return false;
        }
        // 파일 이름이 곧 모듈 이름이다 — 복사본 폴더(`Bin/Modules`)에서 이름이 겹치지 않고, 찾는 쪽이 파일 이름으로 바로 찾는다.
        if ( sourcePath.empty() == false )
        {
            const string fileName = FileUtil::getFileNamePart( sourcePath );
            if ( StringUtil::equals( fileName, outManifest._name + kManifestExtension, false ) == false )
            {
                outError = where + ": the file must be named '" + outManifest._name + kManifestExtension + "'";
                return false;
            }
        }
        const string errorContext = where + " (" + outManifest._name + ")";

        if ( ModuleVersion::parse( file._version, outManifest._version ) == false )
        {
            outError = errorContext + ": _version must be 'major.minor.patch'";
            return false;
        }
        if ( file._kind == kModuleKindUnset )
        {
            outError = errorContext + ": _kind is missing";
            return false;
        }
        outManifest._kind        = file._kind;
        outManifest._description = file._description;

        outManifest._platformMask      = ModuleCatalogInternal::toMask( file._listPlatform );
        outManifest._configurationMask = ModuleCatalogInternal::toMask( file._listConfiguration );
        outManifest._targetMask        = ModuleCatalogInternal::toMask( file._listTarget );
        if ( outManifest._platformMask == 0 )
        {
            outError = errorContext + ": _listPlatform must list Windows and/or Linux";
            return false;
        }
        if ( outManifest._configurationMask == 0 )
        {
            outError = errorContext + ": _listConfiguration must list Dev and/or Shipping";
            return false;
        }
        if ( outManifest._targetMask == 0 )
        {
            outError = errorContext + ": _listTarget must list Client and/or Server";
            return false;
        }
        outManifest._bEnabledByDefault = file._bEnabledByDefault;

        for ( size_t index = 0; index < file._listDependency.size(); ++index )
        {
            const ModuleDependencyFile& entry = file._listDependency[index];
            ModuleDependency            dependency{};
            dependency._name = entry._name;
            if ( dependency._name.empty() || dependency._name == outManifest._name )
            {
                outError = errorContext + ": dependency " + to_string( index ) + " has no name or names the module itself";
                return false;
            }
            if ( entry._minVersion.empty() == false && ModuleVersion::parse( entry._minVersion, dependency._minVersion ) == false )
            {
                outError = errorContext + ": dependency '" + dependency._name + "' has a bad _minVersion";
                return false;
            }
            outManifest._listDependency.push_back( std::move( dependency ) );
        }

        if ( file._listModuleOverride.empty() == false && outManifest._kind != ModuleKind::Game )
        {
            outError = errorContext + ": only a Game module (the project) has _listModuleOverride";
            return false;
        }
        for ( size_t index = 0; index < file._listModuleOverride.size(); ++index )
        {
            if ( file._listModuleOverride[index]._name.empty() )
            {
                outError = errorContext + ": module override " + to_string( index ) + " must be { \"_name\": ..., \"_bEnabled\": true|false }";
                return false;
            }
        }
        outManifest._listModuleOverride = std::move( file._listModuleOverride );

        // 에디터 확장은 Dev 전용이고 EditorModule 에 의존한다(언리얼 Type "Editor" 모듈과 같다) — CMake `sw_readModuleManifest` 와 같은 검사.
        if ( outManifest._kind == ModuleKind::EditorExtension )
        {
            if ( outManifest._configurationMask != static_cast<uint8>( ModuleConfiguration::Dev ) )
            {
                outError = errorContext + ": an EditorExtension module is built for Dev only (_listConfiguration [\"Dev\"])";
                return false;
            }
            bool bDependsOnEditor = false;
            for ( const ModuleDependency& dependency : outManifest._listDependency )
            {
                bDependsOnEditor = bDependsOnEditor || dependency._name == config::kTargetEditorModule;
            }
            if ( bDependsOnEditor == false )
            {
                outError = errorContext + ": an EditorExtension module must list " + string( config::kTargetEditorModule ) + " in _listDependency";
                return false;
            }
        }
        return true;
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

    bool ModuleCatalog::replaceManifest( ModuleManifest manifest, string& outError )
    {
        for ( ModuleManifest& existing : _listManifest )
        {
            if ( existing._name == manifest._name )
            {
                existing = std::move( manifest );
                return true;
            }
        }
        outError = "Module '" + manifest._name + "' has no manifest to replace (" + manifest._sourcePath + ")";
        return false;
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
        for ( const ModuleManifest& manifest : _listManifest )
        {
            if ( manifest._name == name )
                return &manifest;
        }
        return nullptr;
    }

    bool ModuleCatalog::resolve( const ModuleResolveContext& context, ModuleResolution& outResolution, string& outError ) const
    {
        outResolution = ModuleResolution{};

        // 1) 프로젝트의 켜기/끄기 표. 모르는 이름은 오류다(꺼 두려던 모듈의 이름을 틀리면 켜진 채 남는다).
        const ModuleManifest* pProject = findManifest( context._projectModule );
        vector<int8>          listOverride( _listManifest.size(), -1 ); // -1 = 표에 없음, 0 = 끔, 1 = 켬
        if ( pProject != nullptr )
        {
            for ( const ModuleOverride& entry : pProject->_listModuleOverride )
            {
                const ModuleManifest* pTarget = findManifest( entry._name );
                if ( pTarget == nullptr )
                {
                    outError = pProject->_sourcePath + ": module override names an unknown module '" + entry._name + "'";
                    return false;
                }
                listOverride[static_cast<size_t>( pTarget - _listManifest.data() )] = entry._bEnabled ? 1 : 0;
            }
        }

        // 2) 켜짐 = 표 > 기본값, 그리고 이 플랫폼 · 구성에 있음. 프로젝트는 늘 켜져 있다.
        const size_t   manifestCount = _listManifest.size();
        vector<uint8>  listActive( manifestCount, SW_FALSE );
        vector<string> listReason( manifestCount );
        for ( size_t index = 0; index < manifestCount; ++index )
        {
            const ModuleManifest& manifest    = _listManifest[index];
            const bool            bProject    = manifest._name == context._projectModule;
            const bool            bEnabled    = bProject || ( listOverride[index] >= 0 ? listOverride[index] == 1 : manifest._bEnabledByDefault );
            const string          unavailable = ModuleCatalogInternal::findUnavailableReason( manifest, context );
            if ( bEnabled == false )
                listReason[index] = listOverride[index] == 0 ? "disabled by the project" : "disabled by default";
            else if ( unavailable.empty() == false )
                listReason[index] = unavailable;
            else
                listActive[index] = SW_TRUE;
        }

        // 2-2) 에디터 확장은 의존(EditorModule · 키트)이 하나라도 꺼져 있으면 함께 꺼진다 — 오류가 아니다(그 확장만 없다). CMake 의 `sw_resolveModuleManifests` 와 같다.
        for ( size_t index = 0; index < manifestCount; ++index )
        {
            const ModuleManifest& manifest = _listManifest[index];
            if ( listActive[index] == SW_FALSE || manifest._kind != ModuleKind::EditorExtension )
                continue;
            for ( const ModuleDependency& dependency : manifest._listDependency )
            {
                const ModuleManifest* pDependency = findManifest( dependency._name );
                if ( pDependency == nullptr )
                    break; // 없는 의존은 아래 3) 이 오류로 알린다
                const size_t dependencyIndex = static_cast<size_t>( pDependency - _listManifest.data() );
                if ( listActive[dependencyIndex] == SW_FALSE )
                {
                    listActive[index] = SW_FALSE;
                    listReason[index] = "needs " + dependency._name + ", which is " + listReason[dependencyIndex];
                    break;
                }
            }
        }

        // 3) 켜진 모듈의 의존은 모두 있고 · 켜져 있고 · 버전이 맞아야 한다.
        vector<vector<uint32>> listDependency( manifestCount );
        for ( size_t index = 0; index < manifestCount; ++index )
        {
            if ( listActive[index] == SW_FALSE )
                continue;
            const ModuleManifest& manifest = _listManifest[index];
            for ( const ModuleDependency& dependency : manifest._listDependency )
            {
                const ModuleManifest* pDependency = findManifest( dependency._name );
                if ( pDependency == nullptr )
                {
                    outError = "Module '" + manifest._name + "' depends on '" + dependency._name + "', which has no manifest (missing module)";
                    return false;
                }
                const size_t dependencyIndex = static_cast<size_t>( pDependency - _listManifest.data() );
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
                listDependency[index].push_back( static_cast<uint32>( dependencyIndex ) );
            }
        }

        // 4) 켜진 모듈만으로 위상 정렬한다. 순환이면 그 경로를 말한다.
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
            listActiveName.push_back( _listManifest[index]._name );
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
            outResolution._listLoadOrder.push_back( string( listActiveName[compactIndex] ) );
        }
        for ( size_t index = 0; index < manifestCount; ++index )
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
