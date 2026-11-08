#include "pch.h"

#include "GameFramework/Kits/Feature/Content/ModSupport/ModManifest.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetPaths.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    namespace
    {
        struct ModManifestInternal
        {
            /** @brief 필수 속성 @p pName 의 글입니다. 없으면 이유를 남기고 false 입니다. */
            [[nodiscard]] static bool readText( const XMLNode& node, const utf8* pName, string_view sourceName, string_view& outText )
            {
                const utf8* pValue = node.findAttribute( pName );
                if ( pValue == nullptr )
                {
                    SW_LOG_ERROR_ID( "ModManifest.MissingField", "Mod manifest '%#': <%#> needs the '%#' attribute", sourceName, node.getName(), pName );
                    return false;
                }
                outText = pValue;
                return true;
            }

            [[nodiscard]] static bool readUInt( const XMLNode& node, const utf8* pName, string_view sourceName, uint32& outValue )
            {
                string_view text;
                if ( readText( node, pName, sourceName, text ) == false )
                    return false;
                int32 value = 0;
                if ( StringUtil::parseInt( text, value ) == false || value < 0 )
                {
                    SW_LOG_ERROR_ID( "ModManifest.BadNumber", "Mod manifest '%#': '%#' of <%#> is '%#', not a whole number of 0 or more", sourceName, pName, node.getName(), text );
                    return false;
                }
                outValue = static_cast<uint32>( value );
                return true;
            }

            [[nodiscard]] static bool readID( const XMLNode& node, string_view sourceName, string& outID )
            {
                string_view text;
                if ( readText( node, "id", sourceName, text ) == false )
                    return false;
                if ( ModManifest::isValidID( text ) == false )
                {
                    SW_LOG_ERROR_ID( "ModManifest.BadID", "Mod manifest '%#': id '%#' of <%#> must be 1 to %# characters of a-z, 0-9 and _", sourceName, text, node.getName(),
                                     ModManifest::kMaxIDLength );
                    return false;
                }
                outID = string{ text };
                return true;
            }

            [[nodiscard]] static bool readVersion( const XMLNode& node, const utf8* pName, string_view sourceName, ModVersion& outVersion )
            {
                string_view text;
                if ( readText( node, pName, sourceName, text ) == false )
                    return false;
                if ( ModVersion::parse( text, outVersion ) == false )
                {
                    SW_LOG_ERROR_ID( "ModManifest.BadVersion", "Mod manifest '%#': '%#' of <%#> is '%#', not major.minor.patch (each 0 to 65535)", sourceName, pName, node.getName(), text );
                    return false;
                }
                return true;
            }

            /**
             * @brief 덮어쓰기 경로를 확인합니다. 정규형(소문자 · `/` · `..` 없음)이고, 도메인 아래 파일이며, 모드 도메인(`mods/`) 밖이어야 합니다.
             * @details 정규화해서 받지 않고 정규형만 받는다 — 매니페스트와 팩 항목이 같은 글이어야 팩 검사가 글 비교 하나로 끝나고, 대소문자 · 구분자가 다른 같은 경로가 둘로 갈리지 않는다.
             */
            [[nodiscard]] static bool checkOverridePath( string_view path, string_view sourceName )
            {
                const string normalized = AssetPaths::normalizeAssetID( path );
                if ( normalized.empty() || normalized != path || path.find( '/' ) == string_view::npos || StringUtil::endsWith( path, "/" ) )
                {
                    SW_LOG_ERROR_ID( "ModManifest.BadOverride", "Mod manifest '%#': override '%#' is not a resource id in normal form (lowercase, '/', a file under a domain, no '..')",
                                     sourceName, path );
                    return false;
                }
                if ( StringUtil::startsWith( path, string( ModManifest::kModDomainRoot ) + "/" ) )
                {
                    SW_LOG_ERROR_ID( "ModManifest.OverridesModContent", "Mod manifest '%#': override '%#' is under '%#/' - a mod overrides base game resources, not another mod's",
                                     sourceName, path, ModManifest::kModDomainRoot );
                    return false;
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool ModVersion::parse( string_view text, ModVersion& outVersion )
    {
        const vector<string_view> listPart = StringUtil::split( text, "." );
        if ( listPart.size() != 3 )
            return false;
        uint16 arrValue[3]{};
        for ( size_t index = 0; index < 3; ++index )
        {
            int32 value = 0;
            if ( listPart[index].empty() || StringUtil::parseInt( listPart[index], value ) == false || value < 0 || value > 65535 )
                return false;
            arrValue[index] = static_cast<uint16>( value );
        }
        outVersion = ModVersion{ arrValue[0], arrValue[1], arrValue[2] };
        return true;
    }

    string ModVersion::toString() const
    {
        return formatToString( "%#.%#.%#", _major, _minor, _patch );
    }

    bool ModManifest::isValidID( string_view id )
    {
        if ( id.empty() || id.size() > kMaxIDLength )
            return false;
        for ( const utf8 ch : id )
        {
            const bool bAllowed = ( ch >= 'a' && ch <= 'z' ) || ( ch >= '0' && ch <= '9' ) || ch == '_';
            if ( bAllowed == false )
                return false;
        }
        return true;
    }

    string ModManifest::makeContentPrefix( string_view id )
    {
        return formatToString( "%#/%#/", kModDomainRoot, id );
    }

    string ModManifest::makeLayerRoot( string_view id )
    {
        return formatToString( "%#/%#/%#", kModDomainRoot, id, kLayerFolder );
    }

    bool ModManifest::parseXMLText( string_view text, string_view sourceName, ModManifest& outManifest )
    {
        using Internal = ModManifestInternal;
        XMLDocument doc;
        if ( doc.parse( text, sourceName ) == false )
        {
            SW_LOG_ERROR_ID( "ModManifest.NotXML", "Mod manifest '%#' could not be parsed: %#", sourceName, doc.getLastError() );
            return false;
        }
        const XMLNode root = doc.getRoot( kRootElement );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR_ID( "ModManifest.WrongRoot", "Mod manifest '%#' has no <%#> root", sourceName, kRootElement );
            return false;
        }

        ModManifest manifest;
        string_view priorityText;
        if ( Internal::readID( root, sourceName, manifest._id ) == false || Internal::readVersion( root, "version", sourceName, manifest._version ) == false ||
             Internal::readUInt( root, "minContentVersion", sourceName, manifest._minContentVersion ) == false ||
             Internal::readUInt( root, "maxContentVersion", sourceName, manifest._maxContentVersion ) == false || Internal::readText( root, "priority", sourceName, priorityText ) == false )
            return false;
        if ( StringUtil::parseInt( priorityText, manifest._priority ) == false )
        {
            SW_LOG_ERROR_ID( "ModManifest.BadNumber", "Mod manifest '%#': priority '%#' is not a whole number", sourceName, priorityText );
            return false;
        }
        if ( manifest._minContentVersion > manifest._maxContentVersion )
        {
            SW_LOG_ERROR_ID( "ModManifest.BadContentRange", "Mod manifest '%#': minContentVersion %# is above maxContentVersion %#", sourceName, manifest._minContentVersion,
                             manifest._maxContentVersion );
            return false;
        }

        // 원소 순회 · 모르는 원소(스크립트 · 네이티브 코드를 가리키는 원소는 형식에 없다) 오류는 카탈로그 표 한 곳이 한다. 셋 다 없어도 맞는 매니페스트라 세지 않는다.
        using Element                          = XMLCatalogElement<ModManifest>;
        static constexpr Element kArrElement[] = {
            Element::of<&ModManifest::readDependency>( "Dependency" ).uncounted(),
            Element::of<&ModManifest::readConflict>( "Conflict" ).uncounted(),
            Element::of<&ModManifest::readOverride>( "Override" ).uncounted(),
            Element::of<&ModManifest::readLayer>( "Layer" ).uncounted(),
        };
        if ( XMLCatalogReadResult::readInDocumentOrder( manifest, root, sourceName, kArrElement ).isClean() == false )
            return false;
        for ( const ModDependency& dependency : manifest._listDependency )
        {
            if ( VectorUtil::contains( manifest._listConflict, dependency._id ) )
            {
                SW_LOG_ERROR_ID( "ModManifest.DependsOnConflict", "Mod manifest '%#': '%#' is both a dependency and a conflict", sourceName, dependency._id );
                return false;
            }
        }
        outManifest = std::move( manifest );
        return true;
    }

    bool ModManifest::readDependency( const XMLNode& node, string_view sourceName )
    {
        using Internal = ModManifestInternal;
        ModDependency dependency;
        string_view   optionalText;
        if ( Internal::readID( node, sourceName, dependency._id ) == false || Internal::readVersion( node, "minVersion", sourceName, dependency._minVersion ) == false ||
             Internal::readText( node, "optional", sourceName, optionalText ) == false )
            return false;
        if ( StringUtil::tryParseBool( optionalText, dependency._bOptional ) == false )
        {
            SW_LOG_ERROR_ID( "ModManifest.BadBool", "Mod manifest '%#': optional '%#' of dependency '%#' is not true or false", sourceName, optionalText, dependency._id );
            return false;
        }
        if ( dependency._id == _id || findDependency( dependency._id ) != nullptr )
        {
            SW_LOG_ERROR_ID( "ModManifest.BadDependency", "Mod manifest '%#': dependency '%#' is the mod itself or listed twice", sourceName, dependency._id );
            return false;
        }
        _listDependency.push_back( std::move( dependency ) );
        return true;
    }

    bool ModManifest::readConflict( const XMLNode& node, string_view sourceName )
    {
        string conflictID;
        if ( ModManifestInternal::readID( node, sourceName, conflictID ) == false )
            return false;
        if ( conflictID == _id || VectorUtil::contains( _listConflict, conflictID ) )
        {
            SW_LOG_ERROR_ID( "ModManifest.BadConflict", "Mod manifest '%#': conflict '%#' is the mod itself or listed twice", sourceName, conflictID );
            return false;
        }
        _listConflict.push_back( std::move( conflictID ) );
        return true;
    }

    bool ModManifest::readOverride( const XMLNode& node, string_view sourceName )
    {
        string_view path;
        if ( ModManifestInternal::readText( node, "path", sourceName, path ) == false || ModManifestInternal::checkOverridePath( path, sourceName ) == false )
            return false;
        if ( VectorUtil::contains( _listOverride, string{ path } ) || VectorUtil::contains( _listLayer, string{ path } ) )
        {
            SW_LOG_ERROR_ID( "ModManifest.DuplicateOverride", "Mod manifest '%#': '%#' is listed twice (as an override or a layer)", sourceName, path );
            return false;
        }
        _listOverride.emplace_back( path );
        return true;
    }

    bool ModManifest::readLayer( const XMLNode& node, string_view sourceName )
    {
        // 층은 덮어쓰기와 같은 경로 규칙이다(정규형 · 기본 리소스만) — 한 리소스를 통째로 바꾸면서 그 위에 차이를 얹는 것은 뜻이 없어 겹치면 거절한다.
        string_view path;
        if ( ModManifestInternal::readText( node, "path", sourceName, path ) == false || ModManifestInternal::checkOverridePath( path, sourceName ) == false )
            return false;
        if ( VectorUtil::contains( _listOverride, string{ path } ) || VectorUtil::contains( _listLayer, string{ path } ) )
        {
            SW_LOG_ERROR_ID( "ModManifest.DuplicateOverride", "Mod manifest '%#': '%#' is listed twice (as an override or a layer)", sourceName, path );
            return false;
        }
        _listLayer.emplace_back( path );
        return true;
    }

    const ModDependency* ModManifest::findDependency( string_view id ) const
    {
        for ( const ModDependency& dependency : _listDependency )
        {
            if ( dependency._id == id )
                return &dependency;
        }
        return nullptr;
    }
} // namespace sw
