#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Economy/Runtime/Catalog/CurrencyCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

namespace sw
{
    SW_LOG_CALLER( "CurrencyCatalog" );

    namespace
    {
        /** @brief 재원이 모두 정의된 비가상 화폐이면 true 입니다. */
        bool hasValidFunding( const CurrencyCatalog& catalog, const CurrencyDef& def )
        {
            for ( const string& funding : def._listFundingAsset )
            {
                const CurrencyDef* pFunding = catalog.findCurrency( funding );
                if ( pFunding == nullptr || pFunding->isVirtual() )
                    return false;
            }
            return true;
        }
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
        return VectorUtil::findBy<&CurrencyDef::_id>( _listCurrency, id );
    }

    int64 CurrencyCatalog::getBalanceCap( string_view assetID ) const
    {
        const CurrencyDef* pDef = findCurrency( assetID );
        return pDef != nullptr ? pDef->_cap : 0;
    }

    uint32 CurrencyCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            if ( StringUtil::equals( node.getName(), "Currency" ) == false )
                SW_LOG_ERROR( "%#: unknown element <%#> - skipped", sourceName, node.getName() );
            else if ( loadCurrency( node, sourceName ) )
                ++loadedCount;
        }
        // 가상 화폐의 재원은 정의된 비가상 화폐여야 한다. 적힌 순서와 상관없이 모두 읽은 뒤에 확인한다.
        for ( size_t defIndex = 0; defIndex < _listCurrency.size(); )
        {
            if ( hasValidFunding( *this, _listCurrency[defIndex] ) == false )
            {
                SW_LOG_ERROR( "%#: virtual currency '%#' funds from an unknown or virtual currency - skipped", sourceName, _listCurrency[defIndex]._id );
                _listCurrency.erase( _listCurrency.begin() + static_cast<ptrdiff_t>( defIndex ) );
                loadedCount = loadedCount > 0 ? loadedCount - 1 : 0;
                continue;
            }
            ++defIndex;
        }
        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <Currency> entries", sourceName );
        return loadedCount;
    }

    bool CurrencyCatalog::loadCurrency( const XMLNode& node, string_view sourceName )
    {
        CurrencyDef def;
        bool        bValid = GameDataXML::readFields( node, def, sourceName );
        if ( bValid && LedgerUtil::isValidAssetID( def._id ) == false )
        {
            SW_LOG_ERROR( "%#: currency id '%#' must be [0-9a-z_.-]", sourceName, def._id );
            bValid = false;
        }
        for ( XMLNode fundingNode = node.findChild( "Funding" ); fundingNode; fundingNode = fundingNode.findNextSibling( "Funding" ) )
        {
            CurrencyFundingDef funding;
            if ( GameDataXML::readFields( fundingNode, funding, sourceName ) )
                def._listFundingAsset.push_back( funding._asset );
            else
                bValid = false;
        }
        if ( bValid )
            addCurrency( def );
        return bValid;
    }
} // namespace sw
