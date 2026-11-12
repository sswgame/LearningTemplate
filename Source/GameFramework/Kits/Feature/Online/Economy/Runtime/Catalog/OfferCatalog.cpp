#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Economy/Runtime/Catalog/OfferCatalog.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"
#include "GameFramework/Runtime/Online/Ledger/LedgerTypes.h"

namespace sw
{
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
                    if ( XMLFields::readProperties( node, def, sourceName ) )
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

    uint32 OfferCatalog::loadRoot( const XMLCatalogRoot& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readProperties 가 남긴다).
        using Element                          = XMLCatalogElement<OfferCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&OfferCatalog::loadOffer>( "Offer" ),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );
        return result._loadedCount;
    }

    bool OfferCatalog::loadOffer( const XMLNode& node, string_view sourceName )
    {
        using Internal = OfferCatalogInternal;
        OfferDef def;
        bool     bValid = XMLFields::readProperties( node, def, sourceName );
        bValid          = Internal::readChildren( node, "Price", sourceName, def._listPrice ) && bValid;
        bValid          = Internal::readChildren( node, "Grant", sourceName, def._listGrant ) && bValid;
        bValid          = Internal::readChildren( node, "Product", sourceName, def._listProduct ) && bValid;
        if ( bValid == false )
            return false;
        if ( addOffer( def ) == false )
        {
            SW_LOG_ERROR_ID( "OfferCatalog.OfferMalformedNeedsGrants", "%#: offer '%#' is malformed (needs grants and either prices or products, amounts 1..1e15, lowercase ids, a unique id) - skipped", sourceName, def._id );
            return false;
        }
        return true;
    }
} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<OfferCatalog>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( OfferCatalog, OfferCatalog, SW_KIT_API, OfferCatalog::kAssetType.withLoader( AssetTypeLoader::of<OfferCatalog>( MemoryTag::Game ) ) );
} // namespace sw
