#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Trade/Shared/TradeClient.h"

#include "Core/Network/BitStream.h"

namespace sw
{
    TradeClient::TradeClient()
        : _mapClientIDToCall{}
        , _listReply{}
        , _listUpdate{}
        , _snapshot{}
        , _sendingCall{}
        , _pClient{ nullptr }
        , _accountID{ kInvalidAccountID }
        , _nextRequestID{ 1 }
        , _bSending{ SW_FALSE }
    {
    }

    void TradeClient::initialize( OnlineServiceClient* pClient ) { _pClient = pClient; }

    uint64 TradeClient::invite( string_view peerDisplayName, const NetIdempotencyKey& key )
    {
        BitWriter body;
        body.writeBlob( reinterpret_cast<const uint8*>( peerDisplayName.data() ), static_cast<int32>( peerDisplayName.size() ) );
        return send( TradeMethod::kInvite, body, key );
    }

    uint64 TradeClient::respond( uint64 tradeID, bool bAccept, const NetIdempotencyKey& key )
    {
        BitWriter body;
        body.writeVarUint( tradeID );
        body.writeBool( bAccept );
        return send( TradeMethod::kRespond, body, key );
    }

    uint64 TradeClient::setOffer( uint64 tradeID, const vector<TradeLeg>& listLeg, const NetIdempotencyKey& key )
    {
        BitWriter body;
        body.writeVarUint( tradeID );
        TradeWire::writeLegs( body, listLeg );
        return send( TradeMethod::kSetOffer, body, key );
    }

    uint64 TradeClient::lock( uint64 tradeID, const NetIdempotencyKey& key )
    {
        BitWriter body;
        body.writeVarUint( tradeID );
        return send( TradeMethod::kLock, body, key );
    }

    uint64 TradeClient::confirm( uint64 tradeID, const NetIdempotencyKey& key )
    {
        const int32  sideIndex    = _snapshot._tradeID == tradeID ? _snapshot.findSideIndex( _accountID ) : -1;
        const uint32 ownRevision  = sideIndex >= 0 ? _snapshot._arrSide[sideIndex]._offerRevision : 0;
        const uint32 peerRevision = sideIndex >= 0 ? _snapshot._arrSide[1 - sideIndex]._offerRevision : 0;
        return confirmSeen( tradeID, ownRevision, peerRevision, key );
    }

    uint64 TradeClient::confirmSeen( uint64 tradeID, uint32 seenOwnRevision, uint32 seenPeerRevision, const NetIdempotencyKey& key )
    {
        BitWriter body;
        body.writeVarUint( tradeID );
        body.writeVarUint( seenOwnRevision );
        body.writeVarUint( seenPeerRevision );
        return send( TradeMethod::kConfirm, body, key );
    }

    uint64 TradeClient::cancel( uint64 tradeID, const NetIdempotencyKey& key )
    {
        BitWriter body;
        body.writeVarUint( tradeID );
        return send( TradeMethod::kCancel, body, key );
    }

    int32 TradeClient::pollReplies( vector<TradeClientReply>& outListReply )
    {
        const int32 count = static_cast<int32>( _listReply.size() );
        for ( TradeClientReply& reply : _listReply )
        {
            outListReply.push_back( std::move( reply ) );
        }
        _listReply.clear();
        return count;
    }

    int32 TradeClient::pollUpdates( vector<TradeClientUpdate>& outListUpdate )
    {
        const int32 count = static_cast<int32>( _listUpdate.size() );
        for ( TradeClientUpdate& update : _listUpdate )
        {
            outListUpdate.push_back( std::move( update ) );
        }
        _listUpdate.clear();
        return count;
    }

    void TradeClient::onServicePush( uint16 kind, BitReader& body )
    {
        TradeClientUpdate update;
        update._kind       = kind;
        TradeResult result = TradeResult::Ok;
        if ( TradeReplyWire::readReply( body, result, update._snapshot, update._listBalance ) == false )
            return;
        observe( update._snapshot );
        _listUpdate.push_back( std::move( update ) );
    }

    uint64 TradeClient::send( uint16 method, const BitWriter& body, const NetIdempotencyKey& key )
    {
        NetRequestOptions options;
        options._idempotencyKey = key.isValid() ? key : NetIdempotencyKey::makeRandom();
        const uint64 requestID  = _nextRequestID++;
        _sendingCall            = PendingCall{ options._idempotencyKey, requestID, method };
        _bSending               = SW_TRUE;
        const uint64 clientID   = _pClient->sendRequest( method, body, options, OnlineResponseDelegate::create<&TradeClient::onResponse>( this ) );
        _bSending               = SW_FALSE;
        if ( _sendingCall._requestID != 0 )
            _mapClientIDToCall[clientID] = _sendingCall;
        _sendingCall = PendingCall{};
        return requestID;
    }

    void TradeClient::onResponse( const OnlineResponse& response )
    {
        PendingCall call;
        const auto  callIt = _mapClientIDToCall.find( response._requestID );
        if ( callIt != _mapClientIDToCall.end() )
        {
            call = callIt->second;
            _mapClientIDToCall.erase( callIt );
        }
        else if ( _bSending == SW_TRUE && _sendingCall._requestID != 0 )
        {
            call                    = _sendingCall;
            _sendingCall._requestID = 0;
        }
        else
        {
            return;
        }
        TradeClientReply reply;
        reply._requestID      = call._requestID;
        reply._method         = call._method;
        reply._idempotencyKey = call._key;
        reply._errorCode      = response._errorCode;
        if ( response.isOk() == false )
        {
            reply._result = response._errorCode == OnlineError::kInvalidRequest ? TradeResult::Invalid : TradeResult::Unavailable;
        }
        else
        {
            BitReader body( response._pBody, response._bodySize );
            if ( TradeReplyWire::readReply( body, reply._result, reply._snapshot, reply._listBalance ) == false )
                reply._result = TradeResult::Invalid;
            else
                observe( reply._snapshot );
        }
        _listReply.push_back( std::move( reply ) );
    }

    void TradeClient::observe( const TradeSnapshot& snapshot )
    {
        if ( snapshot._tradeID == 0 )
            return;
        const bool bNewer = snapshot._tradeID != _snapshot._tradeID || snapshot._updatedMs >= _snapshot._updatedMs;
        if ( bNewer )
            _snapshot = snapshot;
    }
} // namespace sw
