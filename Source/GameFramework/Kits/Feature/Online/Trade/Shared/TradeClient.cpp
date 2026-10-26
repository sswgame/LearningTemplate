#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Trade/Shared/TradeClient.h"

#include "Core/Serialization/BitStream.h"

#include "GameFramework/Base/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Base/Online/Service/OnlineReplyUtil.h"
#include "GameFramework/Kits/Feature/Online/Trade/Shared/TradeProtocol.h"

namespace sw
{
    TradeClient::TradeClient()
        : _callTable{}
        , _listReply{}
        , _listUpdate{}
        , _snapshot{}
        , _pClient{ nullptr }
        , _accountID{ kInvalidAccountID }
        , _nextRequestID{ 1 }
    {
    }

    void TradeClient::initialize( OnlineServiceClient* pClient )
    {
        _pClient = pClient;
    }

    uint64 TradeClient::invite( string_view peerDisplayName, const NetIdempotencyKey& key )
    {
        TradeInviteRequest request;
        request._peerDisplayName = string( peerDisplayName );
        return send( TradeMethod::kInvite, request, key );
    }

    uint64 TradeClient::respond( uint64 tradeID, bool bAccept, const NetIdempotencyKey& key )
    {
        TradeRespondRequest request;
        request._tradeID = tradeID;
        request._bAccept = bAccept ? SW_TRUE : SW_FALSE;
        return send( TradeMethod::kRespond, request, key );
    }

    uint64 TradeClient::setOffer( uint64 tradeID, const vector<TradeLeg>& listLeg, const NetIdempotencyKey& key )
    {
        TradeOfferRequest request;
        request._listLeg = listLeg;
        request._tradeID = tradeID;
        return send( TradeMethod::kSetOffer, request, key );
    }

    uint64 TradeClient::lock( uint64 tradeID, const NetIdempotencyKey& key )
    {
        return send( TradeMethod::kLock, TradeIDRequest{ tradeID }, key );
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
        TradeConfirmRequest request;
        request._tradeID          = tradeID;
        request._seenOwnRevision  = seenOwnRevision;
        request._seenPeerRevision = seenPeerRevision;
        return send( TradeMethod::kConfirm, request, key );
    }

    uint64 TradeClient::cancel( uint64 tradeID, const NetIdempotencyKey& key )
    {
        return send( TradeMethod::kCancel, TradeIDRequest{ tradeID }, key );
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
        TradeReply push;
        if ( TradeProtocol::MessageTypes::read( body, push ) != FixedReadResult::Ok )
            return;
        TradeClientUpdate update;
        update._kind        = kind;
        update._snapshot    = std::move( push._snapshot );
        update._listBalance = std::move( push._listBalance );
        observe( update._snapshot );
        _listUpdate.push_back( std::move( update ) );
    }

    template <typename TRequest>
    uint64 TradeClient::send( uint16 method, const TRequest& request, const NetIdempotencyKey& key )
    {
        NetRequestOptions options;
        options._idempotencyKey     = key.isValid() ? key : NetIdempotencyKey::makeRandom();
        const uint64      requestID = _nextRequestID++;
        const PendingCall pendingCall{ options._idempotencyKey, requestID, method };
        // 범위 밖 값이라 쓰지 못하면 보내지 않고 kInvalidRequest 응답으로 끝난다. 그 응답도 onResponse 를 지난다
        (void)_callTable.sendMessage<TradeProtocol::MessageTypes>( *_pClient, method, request, options, OnlineResponseDelegate::create<&TradeClient::onResponse>( this ),
                                                                   pendingCall ); // 응답은 pendingCall._requestID 로 알린다
        return requestID;
    }

    void TradeClient::onResponse( const OnlineResponse& response )
    {
        PendingCall call;
        if ( _callTable.take( response._requestID, call ) == false )
            return;
        TradeClientReply reply;
        reply._requestID      = call._requestID;
        reply._method         = call._method;
        reply._idempotencyKey = call._key;
        reply._errorCode      = response._errorCode;
        if ( response.isOk() == false )
        {
            OnlineReplyUtil::markError<TradeProtocol>( response._errorCode, reply ); // 공통 오류(해시 불일치)는 키트 표보다 먼저
        }
        else
        {
            BitReader  body( response._pBody, response._bodySize );
            TradeReply received;
            if ( TradeProtocol::MessageTypes::read( body, received ) != FixedReadResult::Ok )
            {
                reply._result = TradeResult::Invalid;
            }
            else
            {
                reply._result      = received._result;
                reply._snapshot    = std::move( received._snapshot );
                reply._listBalance = std::move( received._listBalance );
                observe( reply._snapshot );
            }
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

namespace sw
{
    TradeResult TradeClient::makeLegs( const Inventory& inventory, const vector<int32>& listSlot, AssetIDFromItem pAssetIDFromItem, vector<TradeLeg>& outListLeg )
    {
        outListLeg.clear();
        if ( pAssetIDFromItem == nullptr )
            return TradeResult::Invalid;
        for ( size_t slotIndex = 0; slotIndex < listSlot.size(); ++slotIndex )
        {
            const int32 slot = listSlot[slotIndex];
            for ( size_t previousIndex = 0; previousIndex < slotIndex; ++previousIndex )
            {
                if ( listSlot[previousIndex] == slot )
                    return TradeResult::Invalid; // 같은 슬롯을 두 번 지정했다
            }
            if ( slot < 0 || slot >= inventory.getSlotCount() )
                return TradeResult::Invalid;
            const InventorySlot& stack = inventory.getSlot( slot );
            if ( stack.isEmpty() )
                return TradeResult::Invalid;
            if ( stack.hasInstanceState() || stack._durability > 0.0f )
                return TradeResult::NotTradable;
            string assetID;
            if ( pAssetIDFromItem( stack._itemID, assetID ) == false )
                return TradeResult::NotTradable;
            TradeLeg* pLeg = nullptr;
            for ( TradeLeg& leg : outListLeg )
            {
                if ( leg._assetID == assetID )
                    pLeg = &leg;
            }
            if ( pLeg == nullptr )
            {
                pLeg           = &outListLeg.emplace_back();
                pLeg->_assetID = assetID;
            }
            pLeg->_amount += stack._count;
        }
        return static_cast<int32>( outListLeg.size() ) > TradeConstant::kMaxLegsPerSide ? TradeResult::TooManyLegs : TradeResult::Ok;
    }
} // namespace sw
