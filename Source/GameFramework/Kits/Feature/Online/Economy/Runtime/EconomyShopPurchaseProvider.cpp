#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Economy/Runtime/EconomyShopPurchaseProvider.h"

#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "GameFramework/Kits/Feature/Online/Economy/Runtime/EconomyClient.h"
#include "GameFramework/Kits/Feature/Online/Economy/Runtime/OfferCatalog.h"

namespace sw
{
    /** @brief 공급자와 답 사이에 나누는 상태입니다. 공급자가 먼저 사라지면 답의 약한 참조가 끊긴다. */
    struct EconomyShopPurchaseProvider::PurchaseState
    {
        EconomyClient*        _pClient{ nullptr };
        const OfferCatalog*   _pOffers{ nullptr };
        ShopPurchaseDelegate  _onFinished{};
        string                _offerID{};
        vector<LedgerBalance> _listBefore{}; ///< 보내기 전 사본의 잔액(상품의 자산마다)
        string                _receiptID{};
        int64                 _total{ 0 };
        int32                 _count{ 0 };
        uint32                _finishedCount{ 0 };
        uint8                 _bPending{ SW_FALSE };
    };

    namespace
    {
        struct EconomyShopPurchaseProviderInternal
        {
            /** @brief @p listBalance 에서 @p assetID 의 잔액을 찾습니다. */
            static const LedgerBalance* findBalance( const vector<LedgerBalance>& listBalance, string_view assetID )
            {
                for ( const LedgerBalance& balance : listBalance )
                {
                    if ( balance._assetID == assetID )
                        return &balance;
                }
                return nullptr;
            }

            /** @brief @p amount × @p count 가 원장 범위 안이면 @p outProduct 에 씁니다. */
            static bool multiplyInRange( int64 amount, int32 count, int64& outProduct )
            {
                if ( amount < 0 || count < 1 || amount > LedgerConstant::kMaxAmount / static_cast<int64>( count ) )
                    return false;
                outProduct = amount * static_cast<int64>( count );
                return true;
            }

            /** @brief 상품이 움직이는 자산(값 화폐 · 지급 자산)과 그 기대 변화(값은 음수)입니다. */
            static bool collectExpectedChanges( const OfferDef& def, int32 count, vector<LedgerBalance>& outListChange )
            {
                outListChange.clear();
                for ( const OfferPrice& price : def._listPrice )
                {
                    int64 total = 0;
                    if ( multiplyInRange( price._amount, count, total ) == false )
                        return false;
                    outListChange.push_back( LedgerBalance{ price._currencyID, -total, 0 } );
                }
                for ( const OfferGrant& grant : def._listGrant )
                {
                    int64 total = 0;
                    if ( multiplyInRange( grant._amount, count, total ) == false )
                        return false;
                    LedgerBalance* pSame = nullptr;
                    for ( LedgerBalance& change : outListChange )
                    {
                        pSame = change._assetID == grant._assetID ? &change : pSame;
                    }
                    if ( pSame != nullptr )
                        pSame->_amount += total; // 같은 자산을 내고 받는 상품(교환)은 순변화를 본다
                    else
                        outListChange.push_back( LedgerBalance{ grant._assetID, total, 0 } );
                }
                return true;
            }

            static ShopPurchaseStatus toStatus( EconomyResult result )
            {
                switch ( result )
                {
                    case EconomyResult::Ok:
                    {
                        return ShopPurchaseStatus::Succeeded;
                    }
                    case EconomyResult::InsufficientFunds:
                    {
                        return ShopPurchaseStatus::NotEnoughMoney;
                    }
                    case EconomyResult::LimitReached:
                    {
                        return ShopPurchaseStatus::LimitReached;
                    }
                    default:
                    {
                        break;
                    }
                }
                return ShopPurchaseStatus::Rejected;
            }

            static string makeReceiptID( const NetIdempotencyKey& key )
            {
                fixed_string<constant::buffer::kMax64> text{};
                formatstring( text.data(), text.capacity(), "%016llx%016llx", static_cast<unsigned long long>( key._high ), static_cast<unsigned long long>( key._low ) );
                return string( text.c_str() );
            }
        };
    } // namespace

    EconomyShopPurchaseProvider::EconomyShopPurchaseProvider( EconomyClient& client, const OfferCatalog& offers )
        : _state{ make_shared<PurchaseState>() }
    {
        _state->_pClient = &client;
        _state->_pOffers = &offers;
    }

    EconomyShopPurchaseProvider::~EconomyShopPurchaseProvider() = default;

    bool EconomyShopPurchaseProvider::makeOffer( const OfferDef& def, ShopOffer& outOffer )
    {
        const bool bOneVirtualPrice = def.isRealMoney() == false && def._listPrice.size() == 1 && def._listGrant.empty() == false;
        if ( bOneVirtualPrice == false || def._listPrice[0]._amount < 1 || def._listPrice[0]._amount > LedgerConstant::kMaxAmount || def._maxCountPerPurchase < 1 )
            return false;
        outOffer              = ShopOffer{};
        outOffer._offerID     = def._id;
        outOffer._displayName = def._id; // 상품 id 가 현지화 키다(표에 없으면 id 그대로 보인다)
        outOffer._currency    = hashed_string( def._listPrice[0]._currencyID );
        outOffer._unitPrice   = def._listPrice[0]._amount;
        outOffer._maxCount    = def._maxCountPerPurchase;
        outOffer._stock       = ShopOffer::kUnlimitedStock;
        return true;
    }

    void EconomyShopPurchaseProvider::collectOffers( vector<ShopOffer>& outListOffer ) const
    {
        outListOffer.clear();
        for ( const OfferDef& def : _state->_pOffers->getOffers() )
        {
            ShopOffer offer{};
            if ( makeOffer( def, offer ) )
                outListOffer.push_back( std::move( offer ) );
        }
    }

    int64 EconomyShopPurchaseProvider::getBalance( const hashed_string& currency ) const
    {
        return _state->_pClient->getBalance( currency.c_str() );
    }

    uint32 EconomyShopPurchaseProvider::getRevision() const
    {
        return _state->_pClient->getRevision() + _state->_finishedCount;
    }

    bool EconomyShopPurchaseProvider::isPurchasePending() const
    {
        return _state->_bPending == SW_TRUE;
    }

    void EconomyShopPurchaseProvider::beginPurchase( const ShopOffer& offer, int32 count, const ShopPurchaseDelegate& onFinished )
    {
        using Internal = EconomyShopPurchaseProviderInternal;
        ShopPurchaseReceipt refused{};
        refused._offerID = offer._offerID;
        refused._count   = count;
        if ( _state->_bPending == SW_TRUE )
        {
            refused._status = ShopPurchaseStatus::Busy;
            if ( onFinished.isBound() )
                onFinished( refused );
            return;
        }
        // 화면이 넘긴 줄이 아니라 카탈로그의 정의로 다시 셈한다 — 줄의 값 · 최대 수를 믿지 않는다.
        const OfferDef*       pDef = _state->_pOffers->findOffer( offer._offerID );
        ShopOffer             checked{};
        int64                 total = 0;
        vector<LedgerBalance> listChange;
        const bool            bValid = pDef != nullptr && makeOffer( *pDef, checked ) && checked.computeTotal( count, total ) && Internal::collectExpectedChanges( *pDef, count, listChange );
        if ( bValid == false )
        {
            refused._status = ShopPurchaseStatus::Rejected;
            if ( onFinished.isBound() )
                onFinished( refused );
            return;
        }
        _state->_listBefore.clear();
        for ( const LedgerBalance& change : listChange )
        {
            _state->_listBefore.push_back( LedgerBalance{ change._assetID, _state->_pClient->getBalance( change._assetID ), 0 } );
        }
        _state->_onFinished = onFinished;
        _state->_offerID    = pDef->_id;
        _state->_count      = count;
        _state->_total      = total;
        _state->_receiptID.clear();
        _state->_bPending = SW_TRUE;

        EconomyPurchaseRequest request{};
        request._offerID                   = pDef->_id;
        request._count                     = count;
        const weak_ptr<PurchaseState> weak = _state;
        // 멱등 키는 call 이 만든다 — 같은 구매를 다시 낼 때만 그 키를 넘긴다. 콜백은 정확히 한 번(보내지 못하면 이 안에서 바로) 온다.
        (void)_state->_pClient->call<EconomyMethod::Purchase>( request, SW_DELEGATE_LAMBDA( OnlineReplyDelegate<EconomyMethod::Purchase>, [weak]( const OnlineResult<EconomyPurchaseReply>& result )
        { onReply( weak, result ); } ) );
    }

    ShopPurchaseStatus EconomyShopPurchaseProvider::evaluateReply( const OfferDef& def, int32 count, const vector<LedgerBalance>& listBefore, const OnlineResult<EconomyPurchaseReply>& result )
    {
        using Internal = EconomyShopPurchaseProviderInternal;
        if ( result._errorCode != OnlineError::kOk )
            return ShopPurchaseStatus::ServerError;
        const ShopPurchaseStatus status = Internal::toStatus( result._reply._result );
        if ( status != ShopPurchaseStatus::Succeeded )
            return status;
        vector<LedgerBalance> listChange;
        if ( Internal::collectExpectedChanges( def, count, listChange ) == false )
            return ShopPurchaseStatus::Unverified;
        for ( const LedgerBalance& after : result._reply._listBalance )
        {
            const LedgerBalance* pChange  = Internal::findBalance( listChange, after._assetID );
            const bool           bInRange = after._amount >= -LedgerConstant::kMaxAmount && after._amount <= LedgerConstant::kMaxAmount;
            if ( pChange == nullptr || bInRange == false )
                return ShopPurchaseStatus::Unverified; // 이 상품과 무관한 자산 · 범위 밖 잔액
            if ( result._reply._bReplayed == SW_TRUE )
                continue; // 같은 키의 지난 답 — 그 사이 다른 변화가 있어 차이로 대조할 수 없다
            const LedgerBalance* pBefore = Internal::findBalance( listBefore, after._assetID );
            if ( pBefore == nullptr || after._amount - pBefore->_amount != pChange->_amount )
                return ShopPurchaseStatus::Unverified; // 값 · 수량만큼 움직이지 않았다
        }
        // 값 화폐가 답에 없으면 서버가 돈을 받았는지 모른다 — 대조할 수 없다.
        if ( def._listPrice.empty() || Internal::findBalance( result._reply._listBalance, def._listPrice[0]._currencyID ) == nullptr )
            return ShopPurchaseStatus::Unverified;
        return ShopPurchaseStatus::Succeeded;
    }

    void EconomyShopPurchaseProvider::onReply( const weak_ptr<PurchaseState>& weakState, const OnlineResult<EconomyPurchaseReply>& result )
    {
        using Internal                        = EconomyShopPurchaseProviderInternal;
        const shared_ptr<PurchaseState> state = weakState.lock();
        if ( state == nullptr )
            return; // 공급자가 먼저 사라졌다 — 정본은 서버, 다음 지갑 받기가 맞춘다
        ShopPurchaseReceipt receipt{};
        receipt._offerID     = state->_offerID;
        receipt._count       = state->_count;
        receipt._receiptID   = Internal::makeReceiptID( result._idempotencyKey );
        const OfferDef* pDef = state->_pOffers->findOffer( state->_offerID );
        receipt._status      = pDef != nullptr ? evaluateReply( *pDef, state->_count, state->_listBefore, result ) : ShopPurchaseStatus::Unverified;
        receipt._paid        = receipt._status == ShopPurchaseStatus::Succeeded ? state->_total : 0;
        if ( receipt._status == ShopPurchaseStatus::Unverified )
        {
            SW_LOG_WARNING_ID( "EconomyShop.PurchaseReplyMismatch", "Purchase reply for '%#' x%# did not match the offer - refreshing the wallet from the server", state->_offerID.c_str(), state->_count );
            (void)state->_pClient->call<EconomyMethod::GetWallet>( SW_DELEGATE_LAMBDA( OnlineReplyDelegate<EconomyMethod::GetWallet>, []( const OnlineResult<EconomyWalletReply>& ) {} ) ); // 스냅샷은 클라이언트 훅(onCallSucceeded)이 캐시에 넣는다
        }
        state->_bPending = SW_FALSE;
        ++state->_finishedCount;
        const ShopPurchaseDelegate onFinished = state->_onFinished;
        state->_onFinished                    = ShopPurchaseDelegate{};
        if ( onFinished.isBound() )
            onFinished( receipt );
    }
} // namespace sw
