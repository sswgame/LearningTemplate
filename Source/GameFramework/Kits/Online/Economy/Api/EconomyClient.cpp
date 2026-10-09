#include "pch.h"

#include "GameFramework/Kits/Online/Economy/Api/EconomyClient.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Kits/Online/Economy/Api/EconomyMirror.h"

namespace sw
{
    EconomyClient::EconomyClient()
        : _mapClientIdToCall{}
        , _listBalance{}
        , _sendingCall{}
        , _pClient{ nullptr }
        , _nextRequestId{ 1 }
        , _revision{ 0 }
        , _bSending{ SW_FALSE }
    {
    }

    void EconomyClient::initialize( OnlineServiceClient* pClient ) { _pClient = pClient; }

    uint64 EconomyClient::requestWallet( ReplyDelegate onReply ) { return send( EconomyMethod::kGetWallet, BitWriter{}, NetIdempotencyKey{}, onReply ); }

    uint64 EconomyClient::requestHistory( const EconomyHistoryRequest& request, ReplyDelegate onReply )
    {
        BitWriter body;
        EconomyProtocol::writeHistoryRequest( body, request );
        return send( EconomyMethod::kGetHistory, body, NetIdempotencyKey{}, onReply );
    }

    uint64 EconomyClient::requestPurchase( const EconomyPurchaseRequest& request, const NetIdempotencyKey& key, ReplyDelegate onReply )
    {
        BitWriter body;
        EconomyProtocol::writePurchaseRequest( body, request );
        return send( EconomyMethod::kPurchase, body, key.isValid() ? key : NetIdempotencyKey::makeRandom(), onReply );
    }

    uint64 EconomyClient::requestRedeem( const EconomyRedeemRequest& request, const NetIdempotencyKey& key, ReplyDelegate onReply )
    {
        BitWriter body;
        EconomyProtocol::writeRedeemRequest( body, request );
        return send( EconomyMethod::kRedeemReceipt, body, key.isValid() ? key : NetIdempotencyKey::makeRandom(), onReply );
    }

    void EconomyClient::applyBalances( const vector<LedgerBalance>& listBalance, bool bSnapshot )
    {
        if ( bSnapshot )
        {
            _listBalance = listBalance;
        }
        else
        {
            for ( const LedgerBalance& balance : listBalance )
            {
                LedgerBalance* pCached = nullptr;
                for ( LedgerBalance& cached : _listBalance )
                {
                    if ( cached._assetId == balance._assetId )
                        pCached = &cached;
                }
                if ( pCached != nullptr )
                    pCached->_amount = balance._amount;
                else
                    _listBalance.push_back( balance );
            }
        }
        ++_revision;
    }

    void EconomyClient::applyLedgerBalances( Wallet& inoutWallet ) const { EconomyMirror::applyToWallet( _listBalance, true, inoutWallet ); }

    int64 EconomyClient::getBalance( string_view assetId ) const
    {
        for ( const LedgerBalance& balance : _listBalance )
        {
            if ( balance._assetId == assetId )
                return balance._amount;
        }
        return 0;
    }

    void EconomyClient::onServicePush( uint16 kind, BitReader& body )
    {
        (void)kind; // 경제 서비스는 아직 알림을 보내지 않는다(잔액은 응답에 실린다)
        (void)body;
    }

    uint64 EconomyClient::send( uint16 method, const BitWriter& body, const NetIdempotencyKey& key, ReplyDelegate onReply )
    {
        const uint64 requestId = _nextRequestId++;
        _sendingCall           = PendingCall{ onReply, key, requestId, method };
        if ( _pClient == nullptr )
        {
            EconomyClientReply reply;
            reply._requestId      = requestId;
            reply._method         = method;
            reply._idempotencyKey = key;
            reply._errorCode      = OnlineError::kUnavailable;
            reply._reply._result  = EconomyResult::Unavailable;
            _sendingCall          = PendingCall{};
            if ( onReply.isBound() )
                onReply( reply );
            return requestId;
        }
        NetRequestOptions options;
        options._idempotencyKey = key;
        _bSending               = SW_TRUE;
        const uint64 clientId   = _pClient->sendRequest( method, body, options, OnlineResponseDelegate::create<&EconomyClient::onResponse>( this ) );
        _bSending               = SW_FALSE;
        if ( _sendingCall._requestId != 0 )
            _mapClientIdToCall[clientId] = _sendingCall; // 그 자리에서 끝나지 않았다 — 응답을 기다린다
        _sendingCall = PendingCall{};
        return requestId;
    }

    void EconomyClient::onResponse( const OnlineResponse& response )
    {
        PendingCall call;
        const auto  callIt = _mapClientIdToCall.find( response._requestId );
        if ( callIt != _mapClientIdToCall.end() )
        {
            call = callIt->second;
            _mapClientIdToCall.erase( callIt );
        }
        else if ( _bSending == SW_TRUE && _sendingCall._requestId != 0 )
        {
            call                    = _sendingCall;
            _sendingCall._requestId = 0;
        }
        else
        {
            return;
        }
        EconomyClientReply reply;
        reply._requestId      = call._requestId;
        reply._method         = call._method;
        reply._idempotencyKey = call._key;
        reply._errorCode      = response._errorCode;
        if ( response.isOk() == false )
        {
            reply._reply._result = EconomyProtocol::fromErrorCode( response._errorCode );
        }
        else
        {
            BitReader body( response._pBody, response._bodySize );
            if ( EconomyProtocol::readReply( body, reply._reply ) == false )
            {
                reply._reply         = EconomyReply{};
                reply._reply._result = EconomyResult::Unavailable;
            }
            else if ( reply._reply._result == EconomyResult::Ok )
            {
                const bool bSnapshot = call._method == EconomyMethod::kGetWallet;
                if ( bSnapshot || reply._reply._listBalance.empty() == false )
                    applyBalances( reply._reply._listBalance, bSnapshot );
            }
        }
        if ( call._onReply.isBound() )
            call._onReply( reply );
    }
} // namespace sw
