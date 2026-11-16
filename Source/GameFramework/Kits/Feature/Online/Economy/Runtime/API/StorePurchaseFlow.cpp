#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Economy/Runtime/API/StorePurchaseFlow.h"

#include "GameFramework/Kits/Feature/Online/Economy/Runtime/Protocol/ReceiptBuyerToken.h"

namespace sw
{
    StorePurchaseFlow::StorePurchaseFlow( IStoreBillingBridge& bridge, EconomyClient& client, const ReportDelegate& onReport )
        : _listPending{}
        , _onReport{ onReport }
        , _bridge{ bridge }
        , _client{ client }
    {
    }

    bool StorePurchaseFlow::begin( uint64 accountID, string_view productID )
    {
        if ( accountID == 0 || productID.empty() )
        {
            SW_LOG_WARNING_ID( "StorePurchaseFlow.CannotBegin", "A store purchase needs a signed-in account and a product - without the account token the store takes the money and the server refuses the grant" );
            return false;
        }
        StoreBillingRequest request;
        request._productID  = string( productID );
        request._buyerToken = ReceiptBuyerToken::make( accountID );
        return _bridge.beginPurchase( request );
    }

    void StorePurchaseFlow::update()
    {
        vector<StoreBillingResult> listResult;
        _bridge.pollResults( listResult );
        for ( const StoreBillingResult& result : listResult )
        {
            if ( result._status == StoreBillingStatus::Purchased )
            {
                EconomyRedeemRequest request;
                request._storeName = _bridge.getStoreName();
                request._payload   = result._receiptPayload;
                redeem( request, NetIdempotencyKey::makeRandom() );
                continue;
            }
            StorePurchaseReport finished;
            finished._billing     = result._status;
            finished._failureText = result._failureText;
            notifyReport( finished );
        }
    }

    void StorePurchaseFlow::retryRedeem( const StorePurchaseReport& finished )
    {
        if ( finished._bRetryable == SW_TRUE )
            redeem( finished._redeem, finished._key );
    }

    void StorePurchaseFlow::redeem( const EconomyRedeemRequest& request, const NetIdempotencyKey& key )
    {
        _listPending.push_back( PendingRedeem{ request, key } ); // 응답이 요청 함수 안에서 올 수 있다(보내지 못한 요청) — 먼저 적어 둔다
        (void)_client.call<EconomyMethod::RedeemReceipt>( request, OnlineReplyDelegate<EconomyMethod::RedeemReceipt>::create<&StorePurchaseFlow::onRedeemReply>( this ), key );
    }

    void StorePurchaseFlow::onRedeemReply( const OnlineResult<EconomyRedeemReply>& reply )
    {
        for ( size_t index = 0; index < _listPending.size(); ++index )
        {
            if ( ( _listPending[index]._key == reply._idempotencyKey ) == false )
                continue;
            StorePurchaseReport finished;
            finished._redeem  = std::move( _listPending[index]._request );
            finished._key     = reply._idempotencyKey;
            finished._billing = StoreBillingStatus::Purchased;
            finished._result  = reply._errorCode != OnlineError::kOk ? EconomyServiceResult::fail( reply._errorCode ) : EconomyServiceResult{ reply._reply._result };
            _listPending.erase( _listPending.begin() + static_cast<ptrdiff_t>( index ) );

            const EconomyServiceResult result = finished._result;
            // 공통 범주의 재시도(장애 · 충돌 · 도배 제한)와 업무 결과의 재시도(스토어 대기)를 함께 본다
            const bool bRetryable = reply.isRetryable() || ( reply._errorCode == OnlineError::kOk && reply._reply.isRetryable() );
            finished._bRetryable  = bRetryable ? SW_TRUE : SW_FALSE;
            if ( result == EconomyResult::BuyerTokenMissing )
                SW_LOG_WARNING_ID( "StorePurchaseFlow.BuyerTokenMissing", "The store answer has no account token - the '%#' billing bridge must pass StoreBillingRequest::_buyerToken to the purchase (obfuscatedAccountId / appAccountToken)", _bridge.getStoreName() );
            if ( result == EconomyResult::Ok || result == EconomyResult::ReceiptRefunded ) // 지급됐거나 돌려줄 것이 없다. 그 밖의 거절은 마치지 않아 맞는 계정이 다시 낼 수 있다
                _bridge.finishPurchase( finished._redeem._payload );
            notifyReport( finished );
            return;
        }
    }

    void StorePurchaseFlow::notifyReport( const StorePurchaseReport& finished )
    {
        if ( _onReport.isBound() )
            _onReport( finished );
    }
} // namespace sw
