#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/StoreReceiptValidator.h"

#include "Core/Foundation/Constants.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include "GameFramework/Runtime/Online/HTTP/HTTPClient.h"

namespace sw
{
    namespace
    {
        struct StoreReceiptValidatorInternal
        {
            static constexpr size_t kMaxNotifiedRefundCount = 100000; ///< 겹친 조회에서 같은 거래를 두 번 알리지 않으려고 기억하는 상한

            static void reject( ReceiptValidationResult& inoutResult, const utf8* pText )
            {
                inoutResult._status      = ReceiptStatus::Invalid;
                inoutResult._failureText = pText;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ReceiptPollWindow::ReceiptPollWindow( int64 intervalMs, int64 lookbackMs, int64 maxAgeMs )
        : _intervalMs{ intervalMs }
        , _lookbackMs{ lookbackMs }
        , _maxAgeMs{ maxAgeMs }
        , _nextMs{ 0 }
        , _lastMs{ 0 }
        , _startMs{ 0 }
        , _cursorMs{ 0 }
        , _bInFlight{ SW_FALSE }
    {
    }

    bool ReceiptPollWindow::tryBegin( int64 nowMs )
    {
        if ( _bInFlight == SW_TRUE || nowMs < _nextMs )
            return false;
        _bInFlight = SW_TRUE;
        _nextMs    = nowMs + _intervalMs;
        if ( _cursorMs != 0 )
            return true; // 잘린 조회를 잇는다 — 끝 시각은 처음 정한 그대로다
        _startMs = nowMs;
        if ( _lastMs == 0 )
            _lastMs = nowMs;
        return true;
    }

    int64 ReceiptPollWindow::getFromMs() const
    {
        if ( _cursorMs != 0 )
            return _cursorMs;
        const int64 fromMs = _lastMs - _lookbackMs;
        if ( _maxAgeMs <= 0 )
            return fromMs;
        const int64 oldestMs = _startMs - _maxAgeMs;
        return fromMs < oldestMs ? oldestMs : fromMs; // 스토어가 받지 않는 시작점은 그 조회를 영원히 400 으로 만든다
    }

    int64 ReceiptPollWindow::getLostMs() const
    {
        if ( _cursorMs != 0 || _maxAgeMs <= 0 )
            return 0;
        const int64 lostMs = ( _startMs - _maxAgeMs ) - ( _lastMs - _lookbackMs );
        return lostMs > 0 ? lostMs : 0;
    }

    void ReceiptPollWindow::finishRead()
    {
        _lastMs    = _startMs;
        _cursorMs  = 0;
        _bInFlight = SW_FALSE;
    }

    void ReceiptPollWindow::finishFailed()
    {
        _bInFlight = SW_FALSE;
    }

    void ReceiptPollWindow::finishIncomplete()
    {
        _cursorMs  = 0;
        _bInFlight = SW_FALSE;
    }

    void ReceiptPollWindow::continueFrom( int64 cursorMs )
    {
        _cursorMs  = cursorMs;
        _nextMs    = 0;
        _bInFlight = SW_FALSE;
    }
} // namespace sw

namespace sw
{
    StoreReceiptValidator::StoreReceiptValidator( ReceiptBuyerRule buyerRule, ReceiptPollWindow refundWindow, HTTPClient* pHTTPClient )
        : _buyerRule{ std::move( buyerRule ) }
        , _refundWindow{ refundWindow }
        , _mapRequestToCall{}
        , _listWaiting{}
        , _listDone{}
        , _listRefund{}
        , _listDeliveryDone{}
        , _uniqueSetNotifiedRefund{}
        , _pHTTPClient{ pHTTPClient }
        , _bRefundIncomplete{ SW_FALSE }
        , _bStopped{ SW_FALSE }
        , _bCursorRestored{ SW_FALSE }
        , _bCursorMoved{ SW_FALSE }
    {
    }

    StoreReceiptValidator::~StoreReceiptValidator() = default;

    void StoreReceiptValidator::restoreRefundCursor( int64 lastReadMs )
    {
        _refundWindow.restore( lastReadMs );
        _bCursorRestored = SW_TRUE;
    }

    bool StoreReceiptValidator::takeRefundCursor( int64& outLastReadMs )
    {
        if ( _bCursorMoved == SW_FALSE )
            return false;
        _bCursorMoved = SW_FALSE;
        outLastReadMs = _refundWindow.getLastReadMs();
        return true;
    }

    ReceiptResponseKind StoreReceiptValidator::classifyResponse( const HTTPClientResponse& response )
    {
        if ( response.isTransientFailure() )
            return ReceiptResponseKind::Unreachable;
        if ( response._statusCode == HTTPConstant::kStatusUnauthorized || response._statusCode == HTTPConstant::kStatusForbidden )
            return ReceiptResponseKind::CredentialRejected;
        return response.isSuccess() ? ReceiptResponseKind::Answered : ReceiptResponseKind::Refused;
    }

    void StoreReceiptValidator::applyOrderFacts( const ReceiptAsk& ask, const ReceiptOrderFacts& facts, ReceiptValidationResult& inoutResult )
    {
        using Internal              = StoreReceiptValidatorInternal;
        inoutResult._status         = facts._status;
        inoutResult._failureText    = facts._failureText;
        inoutResult._storeBuyerID   = facts._storeBuyerID;
        inoutResult._purchaseTimeMs = facts._purchaseTimeMs;
        inoutResult._bSandbox       = facts._bSandbox;
        if ( facts._productID.empty() == false )
            inoutResult._productID = facts._productID;
        if ( inoutResult._transactionID.empty() )
            inoutResult._transactionID = facts._transactionID;
        const bool bDecided = facts._status == ReceiptStatus::Valid || facts._status == ReceiptStatus::Refunded;
        if ( bDecided == false )
            return; // 거절이거나 아직 정해지지 않았다 — 지급으로 이어지지 않으니 대조할 것이 없다
        if ( facts._transactionID.empty() )
        {
            Internal::reject( inoutResult, "the store answer has no transaction id" ); // 분개 키를 만들 수 없다
            return;
        }
        const bool bOtherTransaction = ask._transactionID.empty() == false && facts._transactionID != ask._transactionID;
        const bool bOtherKey         = ask._askedKey.empty() == false && facts._answeredKey.empty() == false && facts._answeredKey != ask._askedKey;
        if ( bOtherTransaction || bOtherKey )
        {
            Internal::reject( inoutResult, "the store answered for another order" ); // 묻지 않은 주문의 답으로 지급하지 않는다
            return;
        }
        if ( ask._productID.empty() == false && facts._productID.empty() == false && facts._productID != ask._productID )
        {
            Internal::reject( inoutResult, "the store answered for another product" );
            return;
        }
        if ( facts._status == ReceiptStatus::Valid && inoutResult._productID.empty() )
            Internal::reject( inoutResult, "the store order has no product" ); // 상품이 없는 주문은 지급할 것이 없다
    }

    void StoreReceiptValidator::addRefund( string_view transactionID, int64 refundedMs )
    {
        const string text( transactionID );
        if ( text.empty() || _uniqueSetNotifiedRefund.count( text ) != 0 )
            return;
        if ( _uniqueSetNotifiedRefund.size() >= StoreReceiptValidatorInternal::kMaxNotifiedRefundCount )
            _uniqueSetNotifiedRefund.clear(); // 상한 — 겹친 알림은 서비스가 거래 id 로 한 번만 처리한다
        _uniqueSetNotifiedRefund.insert( text );
        _listRefund.push_back( ReceiptRefundNotification{ getStoreName(), text, refundedMs } );
    }

    void StoreReceiptValidator::removeRefund( string_view transactionID )
    {
        _uniqueSetNotifiedRefund.erase( string( transactionID ) );
    }

    ReceiptValidationResult StoreReceiptValidator::makeResult( const ReceiptCall& call ) const
    {
        ReceiptValidationResult result;
        result._storeName     = call._request._storeName;
        result._ticket        = call._request._ticket;
        result._accountID     = call._request._accountID;
        result._transactionID = call._ask._transactionID;
        result._productID     = call._ask._productID;
        result._buyerRule     = _buyerRule;
        return result;
    }

    void StoreReceiptValidator::completeRetry( const ReceiptCall& call, string failureText )
    {
        ReceiptValidationResult result = makeResult( call );
        result._status                 = ReceiptStatus::Retry;
        result._failureText            = std::move( failureText );
        _listDone.push_back( std::move( result ) );
    }

    void StoreReceiptValidator::submitValidation( const ReceiptValidationRequest& request, UnixMilliseconds now )
    {
        ReceiptCall call;
        call._kind    = ReceiptCall::Kind::Validate;
        call._request = request;
        if ( const expected<void> read = readPayload( request._payload, call._ask ); read.has_value() == false )
        {
            // 요청에 실을 수 없는 영수증은 스토어에 묻지 않고 거절한다(다른 인자를 끼워 넣는 길을 막는다).
            ReceiptValidationResult result = makeResult( call );
            result._transactionID.clear();
            result._productID.clear();
            result._status      = ReceiptStatus::Invalid;
            result._failureText = string( read.error().message() );
            _listDone.push_back( std::move( result ) );
            return;
        }
        dispatch( std::move( call ), now );
    }

    void StoreReceiptValidator::dispatch( ReceiptCall call, UnixMilliseconds now )
    {
        switch ( prepareAuth( now ) )
        {
            case AuthState::Ready:
            {
                send( std::move( call ), now );
                break;
            }
            case AuthState::Waiting:
            {
                _listWaiting.push_back( std::move( call ) );
                break;
            }
            case AuthState::Failed:
            {
                _listWaiting.push_back( std::move( call ) );
                flushWaiting( false, now );
                break;
            }
        }
    }

    void StoreReceiptValidator::send( ReceiptCall call, UnixMilliseconds now )
    {
        const HTTPClientRequest request = makeHTTPRequest( call, now );
        const uint64            id      = _pHTTPClient->submitRequest( request );
        _mapRequestToCall[id]           = std::move( call );
    }

    void StoreReceiptValidator::failCall( const ReceiptCall& call )
    {
        // 자격을 받지 못했다 — 검증은 같은 영수증으로 다시 시도하게 하고, 지급 알림은 저장소의 레코드로 다시 보내고, 조회는 다음 주기에 맡긴다.
        switch ( call._kind )
        {
            case ReceiptCall::Kind::Validate:
            {
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "%# credentials are not available", getStoreName() );
                completeRetry( call, string( textBuilder.view() ) );
                break;
            }
            case ReceiptCall::Kind::ConfirmDelivery:
            {
                completeDelivery( call, ReceiptDeliveryStatus::Retry );
                break;
            }
            case ReceiptCall::Kind::RefundPage:
            {
                _refundWindow.finishFailed();
                break;
            }
        }
    }

    void StoreReceiptValidator::flushWaiting( bool bGranted, UnixMilliseconds now )
    {
        vector<ReceiptCall> listWaiting;
        listWaiting.swap( _listWaiting );
        for ( ReceiptCall& call : listWaiting )
        {
            if ( bGranted )
                send( std::move( call ), now );
            else
                failCall( call );
        }
    }

    void StoreReceiptValidator::finishValidation( const ReceiptCall& call, const HTTPClientResponse& response, UnixMilliseconds now )
    {
        switch ( classifyResponse( response ) )
        {
            case ReceiptResponseKind::Unreachable:
            {
                if ( response._bTransportFailed == SW_TRUE )
                {
                    completeRetry( call, response._failureText );
                    return;
                }
                StringBuilder<constant::buffer::kMax256> reason;
                reason.appendFormat( "%# answered status %#", getStoreName(), response._statusCode );
                completeRetry( call, string( reason.view() ) );
                return;
            }
            case ReceiptResponseKind::CredentialRejected:
            {
                onCredentialRejected( response );
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "%# rejected our credentials (status %#)", getStoreName(), response._statusCode );
                completeRetry( call, string( textBuilder.view() ) );
                return;
            }
            case ReceiptResponseKind::Refused:
            {
                ReceiptValidationResult result = makeResult( call );
                result._status                 = ReceiptStatus::Invalid; // 모르는 영수증이면 스토어가 4xx 로 답한다
                StringBuilder<constant::buffer::kMax256> failureTextBuilder;
                failureTextBuilder.appendFormat( "%# does not know this purchase (status %#)", getStoreName(), response._statusCode );
                result._failureText = string( failureTextBuilder.view() );
                _listDone.push_back( std::move( result ) );
                return;
            }
            case ReceiptResponseKind::Answered:
            {
                break;
            }
        }
        JSONDocument document;
        if ( document.tryParse( response.getBodyText() ) == false )
        {
            // 2xx 인데 읽을 수 없다 — 영수증이 없다는 답이 아니다(프록시 · 잘린 몸). Invalid 면 결제한 사람의 영수증을 버린다.
            StringBuilder<constant::buffer::kMax256> textBuilder;
            textBuilder.appendFormat( "%# answered a body that is not JSON", getStoreName() );
            completeRetry( call, string( textBuilder.view() ) );
            return;
        }
        ReceiptOrderFacts facts;
        parseOrder( document.getRoot(), call, now, facts );
        ReceiptValidationResult result = makeResult( call );
        applyOrderFacts( call._ask, facts, result );
        if ( result._status == ReceiptStatus::Valid && facts._bNeedsDeliveryConfirm == SW_TRUE )
        {
            // 여기서 알리지 않는다 — 구매자 대조 · 지급이 남았다. 지급이 진 영수증을 알리면 스토어가 환불하지 않아 돈만 받은 구매가 남는다.
            result._deliveryKey        = call._request._payload;
            result._deliveryDeadlineMs = facts._deliveryDeadlineMs;
        }
        _listDone.push_back( std::move( result ) );
    }

    void StoreReceiptValidator::confirmDelivery( const ReceiptDeliveryTask& task, UnixMilliseconds now )
    {
        ReceiptCall call;
        call._kind               = ReceiptCall::Kind::ConfirmDelivery;
        call._recordKey          = task._recordKey;
        call._request._storeName = task._storeName;
        call._request._payload   = task._deliveryKey;
        if ( const expected<void> read = readPayload( task._deliveryKey, call._ask ); read.has_value() == false )
        {
            SW_LOG_ERROR_ID( "StoreReceiptValidator.DeliveryKeyUnreadable", "A %# delivery record has an unreadable key (%#) - it is dropped, the store refunds that purchase when its deadline passes", getStoreName(),
                             read.error().message() );
            completeDelivery( call, ReceiptDeliveryStatus::Refused );
            return;
        }
        if ( _bStopped == SW_TRUE )
        {
            completeDelivery( call, ReceiptDeliveryStatus::Retry );
            return;
        }
        dispatch( std::move( call ), now );
    }

    void StoreReceiptValidator::finishDelivery( const ReceiptCall& call, const HTTPClientResponse& response )
    {
        switch ( classifyResponse( response ) )
        {
            case ReceiptResponseKind::Answered:
            {
                completeDelivery( call, ReceiptDeliveryStatus::Confirmed );
                break;
            }
            case ReceiptResponseKind::Refused:
            {
                // 이미 확인했거나 소비했거나 없어진 구매다. 다시 보내도 같고, 정말 확인되지 않았으면 스토어가 환불하고 환불 조회가 회수한다.
                SW_LOG_WARNING_ID( "StoreReceiptValidator.DeliveryRefused", "%# refused a delivery confirmation (status %#) - it is not sent again", getStoreName(), response._statusCode );
                completeDelivery( call, ReceiptDeliveryStatus::Refused );
                break;
            }
            case ReceiptResponseKind::CredentialRejected:
            {
                onCredentialRejected( response );
                completeDelivery( call, ReceiptDeliveryStatus::Retry );
                break;
            }
            case ReceiptResponseKind::Unreachable:
            {
                completeDelivery( call, ReceiptDeliveryStatus::Retry );
                break;
            }
        }
    }

    void StoreReceiptValidator::completeDelivery( const ReceiptCall& call, ReceiptDeliveryStatus status )
    {
        _listDeliveryDone.push_back( ReceiptDeliveryResult{ getStoreName(), call._recordKey, status } );
    }

    void StoreReceiptValidator::pollDeliveryResults( vector<ReceiptDeliveryResult>& outListServiceResult )
    {
        for ( ReceiptDeliveryResult& serviceResult : _listDeliveryDone )
        {
            outListServiceResult.push_back( std::move( serviceResult ) );
        }
        _listDeliveryDone.clear();
    }

    void StoreReceiptValidator::finishRefundPage( const ReceiptCall& call, const HTTPClientResponse& response, UnixMilliseconds now )
    {
        if ( classifyResponse( response ) == ReceiptResponseKind::CredentialRejected )
            onCredentialRejected( response );
        JSONDocument document;
        if ( response.isSuccess() == false || document.tryParse( response.getBodyText() ) == false )
        {
            SW_LOG_WARNING_ID( "StoreReceiptValidator.RefundQueryFailed", "The %# refund query failed (status %#) - the next poll reads from the last successful one", getStoreName(), response._statusCode );
            _refundWindow.finishFailed(); // 시작점을 그대로 둔다 — 다음 조회가 이번 구간까지 다시 읽는다
            return;
        }
        ReceiptRefundPage page;
        parseRefundPage( document.getRoot(), now, page );
        if ( page._bIncomplete == SW_TRUE )
            _bRefundIncomplete = SW_TRUE;
        if ( page._nextPageToken.empty() == false )
        {
            const bool bLooping = page._nextPageToken == call._pageToken || call._page + 1 >= kMaxRefundPages;
            if ( bLooping )
            {
                SW_LOG_WARNING_ID( "StoreReceiptValidator.RefundQueryTooManyPages", "The %# refund query did not end after %# pages - the next poll reads the same range again", getStoreName(), call._page + 1 );
                _refundWindow.finishFailed();
                return;
            }
            ReceiptCall next = call;
            next._page       = call._page + 1;
            next._pageToken  = page._nextPageToken;
            dispatch( std::move( next ), now );
            return;
        }
        if ( page._bTruncated == SW_TRUE )
        {
            if ( page._cursorMs > _refundWindow.getFromMs() )
            {
                _refundWindow.continueFrom( page._cursorMs ); // 다음 틱에 이어 읽는다. 읽지 못한 것이 있었으면 끝난 뒤에 구간을 묶는다
                return;
            }
            // 이을 시각이 없다 — 구간을 옮기면 잘린 뒤쪽의 환불을 영영 놓친다. 앞쪽은 이미 알렸다.
            SW_LOG_WARNING_ID( "StoreReceiptValidator.RefundQueryTruncated", "The %# refund query hit its limit without a way to continue - the next poll reads the same range again", getStoreName() );
            _refundWindow.finishFailed();
            return;
        }
        if ( _bRefundIncomplete == SW_TRUE )
        {
            _bRefundIncomplete = SW_FALSE;
            _refundWindow.finishIncomplete(); // 끝까지 갔지만 읽지 못한 항목이 있다 — 그 항목을 지나치지 않게 처음부터 다시 읽는다
            return;
        }
        _refundWindow.finishRead();
        _bCursorMoved = SW_TRUE;
    }

    void StoreReceiptValidator::update( UnixMilliseconds now )
    {
        // 서버 루프의 시각(유닉스 밀리초) 하나로 HTTP 시한 · 자격 · 조회 주기 · 조회 구간을 모두 잰다.
        _pHTTPClient->update();
        if ( _bStopped == SW_FALSE && _bCursorRestored == SW_TRUE && _refundWindow.tryBegin( now.count() ) )
        {
            const int64 lostMs = _refundWindow.getLostMs();
            if ( lostMs > 0 )
                SW_LOG_WARNING_ID( "StoreReceiptValidator.RefundGapBeyondStoreLimit", "The %# refund query cannot reach far enough back: %# minutes older than the store keeps were never read (polling stopped or failed for too long) - refunds in that span are missed, check them by hand", getStoreName(), lostMs / ( constant::time::kSecondsPerMinute * constant::time::kMillisecondsPerSecond ) );
            ReceiptCall call;
            call._kind = ReceiptCall::Kind::RefundPage;
            dispatch( std::move( call ), now );
        }
        vector<HTTPClientResponse> listResponse;
        (void)_pHTTPClient->pollResponses( listResponse );
        for ( const HTTPClientResponse& response : listResponse )
        {
            if ( consumeAuthResponse( response, now ) )
                continue;
            const auto callIt = _mapRequestToCall.find( response._requestID );
            if ( callIt == _mapRequestToCall.end() )
                continue;
            const ReceiptCall call = std::move( callIt->second );
            _mapRequestToCall.erase( callIt );
            switch ( call._kind )
            {
                case ReceiptCall::Kind::Validate:
                {
                    finishValidation( call, response, now );
                    break;
                }
                case ReceiptCall::Kind::ConfirmDelivery:
                {
                    finishDelivery( call, response );
                    break;
                }
                case ReceiptCall::Kind::RefundPage:
                {
                    finishRefundPage( call, response, now );
                    break;
                }
            }
        }
    }

    void StoreReceiptValidator::pollCompletions( vector<ReceiptValidationResult>& outListResult )
    {
        for ( ReceiptValidationResult& result : _listDone )
        {
            outListResult.push_back( std::move( result ) );
        }
        _listDone.clear();
    }

    void StoreReceiptValidator::pollRefunds( vector<ReceiptRefundNotification>& outListNotice )
    {
        for ( ReceiptRefundNotification& notice : _listRefund )
        {
            outListNotice.push_back( std::move( notice ) );
        }
        _listRefund.clear();
    }

    void StoreReceiptValidator::shutdown()
    {
        // 날아가던 검증은 같은 영수증으로 다시 시도하게 Retry 로 끝낸다. 지급 알림(저장소에 남아 있다) · 조회는 다음 기동의 일이다.
        StringBuilder<constant::buffer::kMax256> textBuilder;
        textBuilder.appendFormat( "%# validation was interrupted by shutdown", getStoreName() );
        const string text{ textBuilder.view() };
        for ( const auto& [requestID, call] : _mapRequestToCall )
        {
            if ( call._kind == ReceiptCall::Kind::Validate )
                completeRetry( call, text );
        }
        for ( const ReceiptCall& call : _listWaiting )
        {
            if ( call._kind == ReceiptCall::Kind::Validate )
                completeRetry( call, text );
        }
        _mapRequestToCall.clear();
        _listWaiting.clear();
        _bStopped = SW_TRUE;
    }
} // namespace sw
