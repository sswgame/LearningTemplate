#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Economy/Runtime/Catalog/CurrencyCatalog.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
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
    CurrencyCatalog::CurrencyCatalog()
        : _listCurrency{}
        , _ledgerPolicy{ *this }
    {
    }

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

    bool CurrencyCatalog::isTransferable( string_view assetID ) const
    {
        const CurrencyDef* pDef = findCurrency( assetID );
        return pDef == nullptr || pDef->_bPaid == false;
    }

    uint32 CurrencyCatalog::loadRoot( const XMLCatalogRoot& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readProperties 가 남긴다).
        using Element                          = XMLCatalogElement<CurrencyCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&CurrencyCatalog::loadCurrency>( "Currency" ),
        };
        uint32 loadedCount = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement )._loadedCount;
        // 가상 화폐의 재원은 정의된 비가상 화폐여야 한다. 적힌 순서와 상관없이 모두 읽은 뒤에 확인한다.
        for ( size_t defIndex = 0; defIndex < _listCurrency.size(); )
        {
            if ( hasValidFunding( *this, _listCurrency[defIndex] ) == false )
            {
                SW_LOG_ERROR_ID( "CurrencyCatalog.VirtualCurrencyFundsUnknown", "%#: virtual currency '%#' funds from an unknown or virtual currency - skipped", sourceName, _listCurrency[defIndex]._id );
                _listCurrency.erase( _listCurrency.begin() + static_cast<ptrdiff_t>( defIndex ) );
                loadedCount = loadedCount > 0 ? loadedCount - 1 : 0;
                continue;
            }
            ++defIndex;
        }
        return loadedCount;
    }

    bool CurrencyCatalog::loadCurrency( const XMLNode& node, string_view sourceName )
    {
        CurrencyDef def;
        bool        bValid = XMLFields::readProperties( node, def, sourceName );
        if ( bValid && LedgerUtil::isValidAssetID( def._id ) == false )
        {
            SW_LOG_ERROR_ID( "CurrencyCatalog.CurrencyIdMustZ", "%#: currency id '%#' must be [0-9a-z_.-]", sourceName, def._id );
            bValid = false;
        }
        for ( XMLNode fundingNode = node.findChild( "Funding" ); fundingNode; fundingNode = fundingNode.findNextSibling( "Funding" ) )
        {
            CurrencyFundingDef funding;
            if ( XMLFields::readProperties( fundingNode, funding, sourceName ) )
                def._listFundingAsset.push_back( funding._asset );
            else
                bValid = false;
        }
        if ( bValid )
            addCurrency( def );
        return bValid;
    }
} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<CurrencyCatalog>::acquire`). 상한 정책이 이 객체를 참조로 들어 옮길 수 없으므로 다시 읽으면 떼어 낸다. 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( CurrencyCatalog, CurrencyCatalog, SW_KIT_API, CurrencyCatalog::kAssetType.withLoader( AssetTypeLoader::of<CurrencyCatalog>( MemoryTag::Game, SharedAssetReloadMode::Detach ) ) );
} // namespace sw
