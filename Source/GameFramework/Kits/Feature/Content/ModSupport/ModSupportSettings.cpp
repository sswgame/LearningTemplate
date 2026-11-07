#include "pch.h"

#include "GameFramework/Kits/Feature/Content/ModSupport/ModSupportSettings.h"

#include "Core/Container/StringUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    namespace
    {
        struct ModSupportSettingsInternal
        {
            /** @brief 필수 양수 속성을 읽습니다. 없거나 숫자가 아니거나 0 이면 이유를 남기고 false 입니다(0 인 한계는 모든 모드를 막는 오타다). */
            [[nodiscard]] static bool readLimit( const XMLNode& node, const utf8* pName, string_view sourceName, uint64& outValue )
            {
                const utf8* pValue = node.findAttribute( pName );
                if ( pValue == nullptr )
                {
                    SW_LOG_ERROR_ID( "ModSupportSettings.MissingField", "Mod support settings '%#': <%#> needs the '%#' attribute", sourceName, node.getName(), pName );
                    return false;
                }
                if ( StringUtil::parseUint64( pValue, outValue ) == false || outValue == 0 )
                {
                    SW_LOG_ERROR_ID( "ModSupportSettings.BadLimit", "Mod support settings '%#': '%#' is '%#', not a whole number above 0", sourceName, pName, pValue );
                    return false;
                }
                return true;
            }

            [[nodiscard]] static bool readLimit32( const XMLNode& node, const utf8* pName, string_view sourceName, uint32& outValue )
            {
                uint64 value = 0;
                if ( readLimit( node, pName, sourceName, value ) == false )
                    return false;
                if ( value > 0xFFFFFFFFull )
                {
                    SW_LOG_ERROR_ID( "ModSupportSettings.BadLimit", "Mod support settings '%#': '%#' (%#) does not fit 32 bits", sourceName, pName, value );
                    return false;
                }
                outValue = static_cast<uint32>( value );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool ModSupportSettings::parseXMLText( string_view text, string_view sourceName, ModSupportSettings& outSettings )
    {
        using Internal = ModSupportSettingsInternal;
        XMLDocument doc;
        if ( doc.parse( text, sourceName ) == false )
        {
            SW_LOG_ERROR_ID( "ModSupportSettings.NotXML", "Mod support settings '%#' could not be parsed: %#", sourceName, doc.getLastError() );
            return false;
        }
        const XMLNode root = doc.getRoot( kRootElement );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR_ID( "ModSupportSettings.WrongRoot", "Mod support settings '%#' has no <%#> root", sourceName, kRootElement );
            return false;
        }
        ModSupportSettings settings;
        if ( Internal::readLimit32( root, "contentVersion", sourceName, settings._contentVersion ) == false || Internal::readLimit32( root, "maxModCount", sourceName, settings._maxModCount ) == false ||
             Internal::readLimit32( root, "maxManifestBytes", sourceName, settings._maxManifestBytes ) == false ||
             Internal::readLimit( root, "maxPackBytes", sourceName, settings._maxPackBytes ) == false || Internal::readLimit32( root, "maxEntryCount", sourceName, settings._maxEntryCount ) == false ||
             Internal::readLimit32( root, "maxEntryBytes", sourceName, settings._maxEntryBytes ) == false )
            return false;
        using Element                          = XMLCatalogElement<ModSupportSettings>;
        static constexpr Element kArrElement[] = {
            Element::of<&ModSupportSettings::readOverridable>( "Overridable" ).uncounted(), // 없으면 덮어쓰기를 받지 않는 정책이다(빈 목록이 맞다)
        };
        if ( XMLCatalogReader::readInDocumentOrder( settings, root, sourceName, kArrElement ).isClean() == false )
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
        return parseXMLText( text, assetID.view(), outSettings );
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
