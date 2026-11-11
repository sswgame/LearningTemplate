#include "pch.h"

#include "GameFramework/Kits/Feature/Content/ModSupport/ModManifest.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetPaths.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"

#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    namespace
    {
        struct ModManifestInternal
        {
            /** @brief 읽은 id @p text 의 꼴(a-z · 0-9 · _, 1 ~ kMaxIDLength 자)을 봅니다. 틀리면 이유를 남기고 false 입니다. */
            [[nodiscard]] static bool checkID( string_view text, const utf8* pElement, string_view sourceName )
            {
                if ( ModManifest::isValidID( text ) == false )
                {
                    SW_LOG_ERROR_ID( "ModManifest.BadID", "Mod manifest '%#': id '%#' of <%#> must be 1 to %# characters of a-z, 0-9 and _", sourceName, text, pElement,
                                     ModManifest::kMaxIDLength );
                    return false;
                }
                return true;
            }

            /** @brief 읽은 판 글 @p text(`major.minor.patch`)를 풉니다. 틀리면 이유를 남기고 false 입니다. */
            [[nodiscard]] static bool parseVersion( string_view text, const utf8* pName, const utf8* pElement, string_view sourceName, ModVersion& outVersion )
            {
                if ( ModVersion::parse( text, outVersion ) == false )
                {
                    SW_LOG_ERROR_ID( "ModManifest.BadVersion", "Mod manifest '%#': '%#' of <%#> is '%#', not major.minor.patch (each 0 to 65535)", sourceName, pName, pElement, text );
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
        StringBuilder<constant::buffer::kMax256> resultBuilder;
        resultBuilder.appendFormat( "%#.%#.%#", _major, _minor, _patch );
        return string( resultBuilder.view() );
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
        StringBuilder<constant::buffer::kMax256> resultBuilder;
        resultBuilder.appendFormat( "%#/%#/", kModDomainRoot, id );
        return string( resultBuilder.view() );
    }

    string ModManifest::makeLayerRoot( string_view id )
    {
        StringBuilder<constant::buffer::kMax256> resultBuilder;
        resultBuilder.appendFormat( "%#/%#/%#", kModDomainRoot, id, kLayerFolder );
        return string( resultBuilder.view() );
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

        // 속성은 리플렉션 필수 칸이다 — 빠진 칸(XMLFields.RequiredFieldMissing) · 숫자가 아닌 글(XMLFields.ValueUnreadable) · 모르는 속성을 이름과 함께 알린다.
        ModManifestRootDef rootDef;
        if ( XMLFields::readProperties( root, rootDef, sourceName ) == false )
            return false;
        ModManifest manifest;
        if ( Internal::checkID( rootDef._id, kRootElement, sourceName ) == false || Internal::parseVersion( rootDef._version, "version", kRootElement, sourceName, manifest._version ) == false )
            return false;
        manifest._id                = rootDef._id;
        manifest._minContentVersion = rootDef._minContentVersion;
        manifest._maxContentVersion = rootDef._maxContentVersion;
        manifest._priority          = rootDef._priority;
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
        ModDependencyDef def;
        if ( XMLFields::readProperties( node, def, sourceName ) == false || Internal::checkID( def._id, "Dependency", sourceName ) == false )
            return false;
        ModDependency dependency;
        if ( Internal::parseVersion( def._minVersion, "minVersion", "Dependency", sourceName, dependency._minVersion ) == false )
            return false;
        dependency._id        = def._id;
        dependency._bOptional = def._bOptional;
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
        ModConflictDef def;
        if ( XMLFields::readProperties( node, def, sourceName ) == false || ModManifestInternal::checkID( def._id, "Conflict", sourceName ) == false )
            return false;
        string conflictID = def._id;
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
        ModPathDef def;
        if ( XMLFields::readProperties( node, def, sourceName ) == false || ModManifestInternal::checkOverridePath( def._path, sourceName ) == false )
            return false;
        const string_view path = def._path;
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
        ModPathDef def;
        if ( XMLFields::readProperties( node, def, sourceName ) == false || ModManifestInternal::checkOverridePath( def._path, sourceName ) == false )
            return false;
        const string_view path = def._path;
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
