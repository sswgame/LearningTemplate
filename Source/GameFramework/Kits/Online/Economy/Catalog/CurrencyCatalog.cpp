#include "pch.h"

#include "GameFramework/Kits/Online/Economy/Catalog/CurrencyCatalog.h"

#include "Core/String/StringUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXml.h"

namespace sw
{
    SW_LOG_CALLER( "CurrencyCatalog" );

    namespace
    {
        struct CurrencyCatalogInternal
        {
            /** @brief 재원이 모두 정의된 비가상 화폐인가입니다. */
            static bool hasValidFunding( const CurrencyCatalog& catalog, const CurrencyDef& def )
            {
                for ( const string& funding : def._listFundingAsset )
                {
                    const CurrencyDef* pFunding = catalog.findCurrency( funding );
                    if ( pFunding == nullptr || pFunding->isVirtual() )
                        return false;
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void CurrencyCatalog::addCurrency( const CurrencyDef& def )
    {
        for ( CurrencyDef& existing : _listCurrency )
        {
            if ( existing._id == def._id )
            {
                existing = def;
                return;
            }
        }
        _listCurrency.push_back( def );
    }

    const CurrencyDef* CurrencyCatalog::findCurrency( string_view id ) const
    {
        for ( const CurrencyDef& def : _listCurrency )
        {
            if ( def._id == id )
                return &def;
        }
        return nullptr;
    }

    int64 CurrencyCatalog::getBalanceCap( string_view assetId ) const
    {
        const CurrencyDef* pDef = findCurrency( assetId );
        return pDef != nullptr ? pDef->_cap : 0;
    }

    uint32 CurrencyCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        for ( XmlNode node = root.findChild( "Currency" ); node; node = node.findNextSibling( "Currency" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            if ( LedgerUtil::isValidAssetId( pId ) == false )
            {
                SW_LOG_WARNING( "%#: currency id '%#' must be [0-9a-z_.-] - skipped", sourceName, pId );
                continue;
            }
            CurrencyDef def;
            def._id                   = pId;
            def._bPaid                = node.getAttributeBool( "paid", false ) ? SW_TRUE : SW_FALSE;
            const string_view capText = node.getAttributeText( "cap" );
            const bool        bCapBad = capText.empty() == false && ( StringUtil::parseInt64( capText, def._cap ) == false || def._cap < 0 );
            if ( bCapBad )
            {
                SW_LOG_WARNING( "%#: currency '%#' has an invalid cap '%#' - no cap", sourceName, pId, capText );
                def._cap = 0;
            }
            for ( XmlNode fundingNode = node.findChild( "Funding" ); fundingNode; fundingNode = fundingNode.findNextSibling( "Funding" ) )
            {
                def._listFundingAsset.push_back( string( fundingNode.getAttributeText( "asset" ) ) );
            }
            addCurrency( def );
        }
        // 가상 화폐의 재원은 정의된 비가상 화폐여야 한다 — 모두 읽은 뒤에 본다(적힌 순서와 무관하게).
        uint32 loadedCount = 0;
        for ( size_t defIndex = 0; defIndex < _listCurrency.size(); )
        {
            if ( CurrencyCatalogInternal::hasValidFunding( *this, _listCurrency[defIndex] ) == false )
            {
                SW_LOG_WARNING( "%#: virtual currency '%#' funds from an unknown or virtual currency - skipped", sourceName, _listCurrency[defIndex]._id );
                _listCurrency.erase( _listCurrency.begin() + static_cast<ptrdiff_t>( defIndex ) );
                continue;
            }
            ++loadedCount;
            ++defIndex;
        }
        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <Currency> entries", sourceName );
        return loadedCount;
    }
} // namespace sw
