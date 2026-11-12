#include "pch.h"

#include "GameFramework/Kits/Feature/Content/ModSupport/ModSupportSettings.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"

#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    bool ModSupportSettings::parseXMLText( string_view text, string_view sourceName, ModSupportSettings& outSettings )
    {
        // 데이터 문서 길로 읽는다 — 조건(`_when`)을 푼다.
        XMLDocument doc;
        if ( doc.parseData( text, sourceName ) == false )
        {
            SW_LOG_ERROR_ID( "ModSupportSettings.NotXML", "Mod support settings '%#' could not be parsed: %#", sourceName, doc.getLastError() );
            return false;
        }
        return parseDocument( doc, sourceName, outSettings );
    }

    bool ModSupportSettings::parseDocument( const XMLDocument& doc, string_view sourceName, ModSupportSettings& outSettings )
    {
        const XMLNode root = doc.getRoot( kRootElement );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR_ID( "ModSupportSettings.WrongRoot", "Mod support settings '%#' has no <%#> root", sourceName, kRootElement );
            return false;
        }
        ModSupportSettings settings;
        // 한계는 리플렉션 칸이다 — 빠진 칸(XMLFields.RequiredFieldMissing) · 0 인 한계(XMLFields.ValueOutOfRange) · 숫자가 아닌 글을 모두 이름과 함께 알린다.
        if ( XMLFields::readProperties( root, settings, sourceName ) == false )
            return false;
        using Element                          = XMLCatalogElement<ModSupportSettings>;
        static constexpr Element kArrElement[] = {
            Element::of<&ModSupportSettings::readOverridable>( "Overridable" ).uncounted(), // 없으면 덮어쓰기를 받지 않는 정책이다(빈 목록이 맞다)
        };
        if ( XMLCatalogReadResult::readInDocumentOrder( settings, XMLCatalogRoot{ root }, sourceName, kArrElement ).isClean() == false )
            return false;
        outSettings = std::move( settings );
        return true;
    }

    bool ModSupportSettings::readOverridable( const XMLNode& node, string_view sourceName )
    {
        const string_view prefix = node.getAttributeText( "prefix" );
        // 접두는 정규형 폴더(끝 `/`)만 받는다 — `game/empty/data` 가 `game/empty/database/…` 까지 열지 않게.
        const string_view folder  = prefix.empty() ? prefix : prefix.substr( 0, prefix.size() - 1 );
        const bool        bFolder = prefix.size() > 1 && StringUtil::endsWith( prefix, "/" ) && AssetPaths::normalizeAssetID( folder ) == folder;
        if ( bFolder == false || StringUtil::startsWith( prefix, "mods/" ) )
        {
            SW_LOG_ERROR_ID( "ModSupportSettings.BadPrefix", "Mod support settings '%#': overridable prefix '%#' must be a normal-form resource folder ending in '/' outside 'mods/'", sourceName,
                             prefix );
            return false;
        }
        _listOverridablePrefix.emplace_back( prefix );
        return true;
    }

    bool ModSupportSettings::loadFromAsset( const AssetID& assetID, ModSupportSettings& outSettings )
    {
        string text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "ModSupportSettings.NotFound", "Mod support settings '%#' could not be read", assetID.view() );
            return false;
        }
        // 리소스 id 로 합친다 — 조건 · 덮어쓰기 층 · 다시 읽기 기록 · 접속 대조를 받는다(글 입구 `parseXMLText` 는 id 가 없다).
        XMLDocument doc;
        if ( doc.parseResourceText( text, assetID ) == false )
        {
            SW_LOG_ERROR_ID( "ModSupportSettings.NotXML", "Mod support settings '%#' could not be parsed: %#", assetID.view(), doc.getLastError() );
            return false;
        }
        return parseDocument( doc, assetID.view(), outSettings );
    }

    bool ModSupportSettings::isOverridable( string_view assetID ) const
    {
        for ( const string& prefix : _listOverridablePrefix )
        {
            if ( StringUtil::startsWith( assetID, prefix ) )
                return true;
        }
        return false;
    }

    /** @brief 모드 정책 데이터 종류입니다 — 게임 팩마다 `game/<팩>/data/modsupport.xml`(`GamePackDataUtil::tryMakeKindPath`). 데이터 검사가 이 로더로 읽는다. */
    SW_ASSET_TYPE( ModSupportSettings, AssetTypeDef::makeDataFile( ModSupportSettings::kDataKind, AssetPathRule{ "game/", {}, "/data/modsupport.xml" },
                                                                   AssetTypeLoader::with<ModSupportSettings, &ModSupportSettings::loadFromAsset>() ) );
} // namespace sw
