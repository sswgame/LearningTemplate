#include "pch.h"

#include "GameFramework/Kits/Online/Economy/OfferCatalog.h"

#include "Core/String/StringUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXml.h"
#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"

namespace sw
{
    SW_LOG_CALLER( "OfferCatalog" );

    namespace
    {
        struct OfferCatalogInternal
        {
            [[nodiscard]] static bool readAmount( const XmlNode& node, int64& outAmount )
            {
                return StringUtil::parseInt64( node.getAttributeText( "amount" ), outAmount ) && 1 <= outAmount && outAmount <= LedgerConstant::kMaxAmount;
            }

            /** @brief 비었으면 0 으로 두고 true, 숫자가 아니면 false 입니다. */
            [[nodiscard]] static bool readOptionalTime( const XmlNode& node, const utf8* pName, int64& outTimeMs )
            {
                const string_view text = node.getAttributeText( pName );
                return text.empty() || StringUtil::parseInt64( text, outTimeMs );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool OfferCatalog::addOffer( const OfferDef& def )
    {
        const bool bPriceXorProduct = def._listPrice.empty() != def._listProduct.empty();
        const bool bShapeOk         = def._listGrant.empty() == false && bPriceXorProduct && def._maxCountPerPurchase >= 1 && def._limitPerAccount >= 0;
        if ( bShapeOk == false || LedgerUtil::isValidAssetId( def._id ) == false || findOffer( def._id ) != nullptr )
            return false;
        for ( const OfferGrant& grant : def._listGrant )
        {
            if ( LedgerUtil::isValidAssetId( grant._assetId ) == false )
                return false;
        }
        for ( const OfferPrice& price : def._listPrice )
        {
            if ( LedgerUtil::isValidAssetId( price._currencyId ) == false )
                return false;
        }
        _listOffer.push_back( def );
        return true;
    }

    const OfferDef* OfferCatalog::findOffer( string_view id ) const
    {
        for ( const OfferDef& def : _listOffer )
        {
            if ( def._id == id )
                return &def;
        }
        return nullptr;
    }

    const OfferDef* OfferCatalog::findOfferByProduct( string_view storeName, string_view productId ) const
    {
        for ( const OfferDef& def : _listOffer )
        {
            for ( const OfferProduct& product : def._listProduct )
            {
                if ( product._storeName == storeName && product._productId == productId )
                    return &def;
            }
        }
        return nullptr;
    }

    uint32 OfferCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild( "Offer" ); node; node = node.findNextSibling( "Offer" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            OfferDef def;
            def._id                  = pId;
            def._limitPerAccount     = node.getAttributeInt( "limit", 0 );
            def._maxCountPerPurchase = node.getAttributeInt( "maxCount", 1 );
            bool bOk                 = OfferCatalogInternal::readOptionalTime( node, "startMs", def._startMs ) && OfferCatalogInternal::readOptionalTime( node, "endMs", def._endMs );
            for ( XmlNode priceNode = node.findChild( "Price" ); bOk && priceNode; priceNode = priceNode.findNextSibling( "Price" ) )
            {
                OfferPrice& price = def._listPrice.emplace_back();
                price._currencyId = string( priceNode.getAttributeText( "currency" ) );
                bOk               = OfferCatalogInternal::readAmount( priceNode, price._amount );
            }
            for ( XmlNode grantNode = node.findChild( "Grant" ); bOk && grantNode; grantNode = grantNode.findNextSibling( "Grant" ) )
            {
                OfferGrant& grant = def._listGrant.emplace_back();
                grant._assetId    = string( grantNode.getAttributeText( "asset" ) );
                bOk               = OfferCatalogInternal::readAmount( grantNode, grant._amount );
            }
            for ( XmlNode productNode = node.findChild( "Product" ); bOk && productNode; productNode = productNode.findNextSibling( "Product" ) )
            {
                OfferProduct& product = def._listProduct.emplace_back();
                product._storeName    = string( productNode.getAttributeText( "store" ) );
                product._productId    = string( productNode.getAttributeText( "id" ) );
                bOk                   = product._storeName.empty() == false && product._productId.empty() == false;
            }
            if ( bOk == false || addOffer( def ) == false )
            {
                SW_LOG_WARNING( "%#: offer '%#' is malformed (needs grants and either prices or products, amounts 1..1e15, lowercase ids) - skipped", sourceName, pId );
                continue;
            }
            ++loadedCount;
        }
        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <Offer> entries", sourceName );
        return loadedCount;
    }
} // namespace sw
