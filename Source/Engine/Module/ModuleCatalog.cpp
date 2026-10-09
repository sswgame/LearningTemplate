#include "pch.h"

#include "Engine/Module/ModuleCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/TopologicalSortUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Serialization/Json/JsonDocument.h"

namespace sw
{
    namespace
    {
        struct ModuleCatalogInternal
        {
            /** @brief 종류 낱말 표입니다(`ModuleKind` 값 순서 — CMake `ModuleManifest.cmake` 와 같은 낱말). */
            static constexpr const utf8* kArrKindName[] = { "GameFramework", "Kit", "Game", "Editor", "Rhi" };

            /** @brief 매니페스트가 가질 수 있는 키입니다. 모르는 키는 오류다(오타를 기본값으로 삼키지 않는다). */
            static constexpr const utf8* kArrManifestKey[]   = { "_name", "_version", "_kind",
                                                                 "_description", "_listDependency", "_listPlatform",
                                                                 "_listConfiguration", "_listTarget", "_bEnabledByDefault", "_listModuleOverride" };
            static constexpr const utf8* kArrDependencyKey[] = { "_name", "_minVersion" };
            static constexpr const utf8* kArrOverrideKey[]   = { "_name", "_bEnabled" };

            /** @brief @p value 의 멤버가 모두 @p arrAllowed 안이면 true. 아니면 첫 모르는 이름을 @p outUnknown 에 담습니다. */
            template <size_t N>
            static bool hasOnlyKnownKeys( const JsonValue& value, const utf8* const ( &arrAllowed )[N], string& outUnknown )
            {
                for ( const string& memberName : value.getMemberNames() )
                {
                    bool bKnown = false;
                    for ( const utf8* pAllowed : arrAllowed )
                    {
                        bKnown = bKnown || memberName == pAllowed;
                    }
                    if ( bKnown == false )
                    {
                        outUnknown = memberName;
                        return false;
                    }
                }
                return true;
            }

            /** @brief 낱말 배열을 비트로 읽습니다. 모르는 낱말 · 빈 배열이면 false 입니다. */
            template <typename TEnum, size_t N>
            [[nodiscard]] static bool parseMask( const JsonValue& list, const utf8* const ( &arrWord )[N], const TEnum ( &arrValue )[N], uint8& outMask, string& outBadWord )
            {
                outMask = 0;
                if ( list.isArray() == false || list.size() == 0 )
                    return false;
                for ( size_t index = 0; index < list.size(); ++index )
                {
                    const string word   = list.at( index ).asString();
                    bool         bFound = false;
                    for ( size_t wordIndex = 0; wordIndex < N; ++wordIndex )
                    {
                        if ( word == arrWord[wordIndex] )
                        {
                            outMask |= static_cast<uint8>( arrValue[wordIndex] );
                            bFound = true;
                        }
                    }
                    if ( bFound == false )
                    {
                        outBadWord = word;
                        return false;
                    }
                }
                return true;
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

        JsonDocument document;
        if ( document.tryParse( jsonText ) == false )
        {
            outError = where + ": not valid JSON";
            return false;
        }
        const JsonValue root = document.getRoot();
        string          unknown;
        if ( root.isObject() == false )
        {
            outError = where + ": the root must be an object";
            return false;
        }
        if ( ModuleCatalogInternal::hasOnlyKnownKeys( root, ModuleCatalogInternal::kArrManifestKey, unknown ) == false )
        {
            outError = where + ": unknown key '" + unknown + "'";
            return false;
        }

        outManifest._name = root.get( "_name" ).asString();
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
        const string context = where + " (" + outManifest._name + ")";

        if ( ModuleVersion::parse( root.get( "_version" ).asString(), outManifest._version ) == false )
        {
            outError = context + ": _version must be 'major.minor.patch'";
            return false;
        }

        const string kindWord   = root.get( "_kind" ).asString();
        bool         bKindFound = false;
        for ( size_t kindIndex = 0; kindIndex < SW_COUNT_OF( ModuleCatalogInternal::kArrKindName ); ++kindIndex )
        {
            if ( kindWord == ModuleCatalogInternal::kArrKindName[kindIndex] )
            {
                outManifest._kind = static_cast<ModuleKind>( kindIndex );
                bKindFound        = true;
            }
        }
        if ( bKindFound == false )
        {
            outError = context + ": unknown _kind '" + kindWord + "'";
            return false;
        }

        outManifest._description = root.get( "_description" ).asString();

        static constexpr const utf8*    kArrPlatformWord[]  = { "Windows", "Linux" };
        static constexpr ModulePlatform kArrPlatformValue[] = { ModulePlatform::Windows, ModulePlatform::Linux };
        string                          badWord;
        if ( ModuleCatalogInternal::parseMask( root.get( "_listPlatform" ), kArrPlatformWord, kArrPlatformValue, outManifest._platformMask, badWord ) == false )
        {
            outError = context + ": _listPlatform must list Windows and/or Linux" + ( badWord.empty() ? string{} : " (got '" + badWord + "')" );
            return false;
        }
        static constexpr const utf8*         kArrConfigurationWord[]  = { "Dev", "Shipping" };
        static constexpr ModuleConfiguration kArrConfigurationValue[] = { ModuleConfiguration::Dev, ModuleConfiguration::Shipping };
        badWord.clear();
        if ( ModuleCatalogInternal::parseMask( root.get( "_listConfiguration" ), kArrConfigurationWord, kArrConfigurationValue, outManifest._configurationMask, badWord ) ==
             false )
        {
            outError = context + ": _listConfiguration must list Dev and/or Shipping" + ( badWord.empty() ? string{} : " (got '" + badWord + "')" );
            return false;
        }
        static constexpr const utf8*  kArrTargetWord[]  = { "Client", "Server" };
        static constexpr ModuleTarget kArrTargetValue[] = { ModuleTarget::Client, ModuleTarget::Server };
        badWord.clear();
        if ( ModuleCatalogInternal::parseMask( root.get( "_listTarget" ), kArrTargetWord, kArrTargetValue, outManifest._targetMask, badWord ) == false )
        {
            outError = context + ": _listTarget must list Client and/or Server" + ( badWord.empty() ? string{} : " (got '" + badWord + "')" );
            return false;
        }

        const JsonValue enabledByDefault = root.get( "_bEnabledByDefault" );
        if ( enabledByDefault.isValid() && enabledByDefault.isBool() == false )
        {
            outError = context + ": _bEnabledByDefault must be true or false";
            return false;
        }
        outManifest._bEnabledByDefault = enabledByDefault.isValid() ? enabledByDefault.asBool() : true;

        const JsonValue dependencyList = root.get( "_listDependency" );
        if ( dependencyList.isValid() && dependencyList.isArray() == false )
        {
            outError = context + ": _listDependency must be an array";
            return false;
        }
        for ( size_t index = 0; dependencyList.isValid() && index < dependencyList.size(); ++index )
        {
            const JsonValue  entry = dependencyList.at( index );
            ModuleDependency dependency{};
            if ( entry.isObject() == false || ModuleCatalogInternal::hasOnlyKnownKeys( entry, ModuleCatalogInternal::kArrDependencyKey, unknown ) == false )
            {
                outError = context + ": dependency " + to_string( index ) + ( entry.isObject() ? " has an unknown key '" + unknown + "'" : string( " is not an object" ) );
                return false;
            }
            dependency._name = entry.get( "_name" ).asString();
            if ( dependency._name.empty() || dependency._name == outManifest._name )
            {
                outError = context + ": dependency " + to_string( index ) + " has no name or names the module itself";
                return false;
            }
            const JsonValue minVersion = entry.get( "_minVersion" );
            if ( minVersion.isValid() && ModuleVersion::parse( minVersion.asString(), dependency._minVersion ) == false )
            {
                outError = context + ": dependency '" + dependency._name + "' has a bad _minVersion";
                return false;
            }
            outManifest._listDependency.push_back( std::move( dependency ) );
        }

        const JsonValue overrideList = root.get( "_listModuleOverride" );
        if ( overrideList.isValid() )
        {
            if ( outManifest._kind != ModuleKind::Game || overrideList.isArray() == false )
            {
                outError = context + ": only a Game module (the project) has _listModuleOverride, and it is an array";
                return false;
            }
            for ( size_t index = 0; index < overrideList.size(); ++index )
            {
                const JsonValue entry = overrideList.at( index );
                if ( entry.isObject() == false || ModuleCatalogInternal::hasOnlyKnownKeys( entry, ModuleCatalogInternal::kArrOverrideKey, unknown ) == false ||
                     entry.get( "_bEnabled" ).isBool() == false || entry.get( "_name" ).asString().empty() )
                {
                    outError = context + ": module override " + to_string( index ) + " must be { \"_name\": ..., \"_bEnabled\": true|false }";
                    return false;
                }
                outManifest._listModuleOverride.push_back( ModuleOverride{ entry.get( "_name" ).asString(), entry.get( "_bEnabled" ).asBool() } );
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

    const utf8* ModuleCatalog::getKindName( ModuleKind kind )
    {
        const size_t kindIndex = static_cast<size_t>( kind );
        return kindIndex < SW_COUNT_OF( ModuleCatalogInternal::kArrKindName ) ? ModuleCatalogInternal::kArrKindName[kindIndex] : "Unknown";
    }
} // namespace sw
