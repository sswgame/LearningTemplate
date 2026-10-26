#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Economy/Shared/Catalog/OfferCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"
#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"

namespace sw
{
    SW_LOG_CALLER( "OfferCatalog" );

    namespace
    {
        struct OfferCatalogInternal
        {
            /** @brief 자산 id 가 원장 형식이고 양이 1 ~ 원장 다리 하나(`LedgerConstant::kMaxAmount`)이면 true 입니다. */
            static bool isValidLeg( const string& assetID, int64 amount )
            {
                const bool bAmountOk = 1 <= amount && amount <= LedgerConstant::kMaxAmount;
                return bAmountOk && LedgerUtil::isValidAssetID( assetID );
            }

            /** @brief 원소 @p parent 의 @p pElement 자식을 모두 스키마로 읽어 @p outListDef 뒤에 붙입니다. 하나라도 틀리면 false 입니다. */
            template <typename TDef>
            [[nodiscard]] static bool readChildren( const XMLNode& parent, const utf8* pElement, string_view sourceName, vector<TDef>& outListDef )
            {
                bool bValid = true;
                for ( XMLNode node = parent.findChild( pElement ); node; node = node.findNextSibling( pElement ) )
                {
                    TDef def;
                    if ( GameDataXML::readFields( node, def, sourceName ) )
                        outListDef.push_back( def );
                    else
                        bValid = false;
                }
                return bValid;
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
        const bool bInvalidOffer    = bShapeOk == false || LedgerUtil::isValidAssetID( def._id ) == false || findOffer( def._id ) != nullptr;
        if ( bInvalidOffer )
            return false;
        // 양의 상한(원장 다리 하나)은 float32 로 정확히 적을 수 없어 `PROPERTY( Max )` 대신 여기서 원장 상수로 본다.
        for ( const OfferGrant& grant : def._listGrant )
        {
            if ( OfferCatalogInternal::isValidLeg( grant._assetID, grant._amount ) == false )
                return false;
        }
        for ( const OfferPrice& price : def._listPrice )
        {
            if ( OfferCatalogInternal::isValidLeg( price._currencyID, price._amount ) == false )
                return false;
        }
        _listOffer.push_back( def );
        return true;
    }

    const OfferDef* OfferCatalog::findOffer( string_view id ) const
    {
        return VectorUtil::findBy<&OfferDef::_id>( _listOffer, id );
    }

    const OfferDef* OfferCatalog::findOfferByProduct( string_view storeName, string_view productID ) const
    {
        for ( const OfferDef& def : _listOffer )
        {
            for ( const OfferProduct& product : def._listProduct )
            {
                if ( product._storeName == storeName && product._productID == productID )
                    return &def;
            }
        }
        return nullptr;
    }

    uint32 OfferCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            if ( StringUtil::equals( node.getName(), "Offer" ) == false )
                SW_LOG_ERROR( "%#: unknown element <%#> - skipped", sourceName, node.getName() );
            else if ( loadOffer( node, sourceName ) )
                ++loadedCount;
        }
        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <Offer> entries", sourceName );
        return loadedCount;
    }

    bool OfferCatalog::loadOffer( const XMLNode& node, string_view sourceName )
    {
        using Internal = OfferCatalogInternal;
        OfferDef def;
        bool     bValid = GameDataXML::readFields( node, def, sourceName );
        bValid          = Internal::readChildren( node, "Price", sourceName, def._listPrice ) && bValid;
        bValid          = Internal::readChildren( node, "Grant", sourceName, def._listGrant ) && bValid;
        bValid          = Internal::readChildren( node, "Product", sourceName, def._listProduct ) && bValid;
        if ( bValid == false )
            return false;
        if ( addOffer( def ) == false )
        {
            SW_LOG_ERROR( "%#: offer '%#' is malformed (needs grants and either prices or products, amounts 1..1e15, lowercase ids, a unique id) - skipped", sourceName, def._id );
            return false;
        }
        return true;
    }
} // namespace sw
