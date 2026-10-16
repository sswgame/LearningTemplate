#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Trade/Shared/TradeClient.h"

#include "Core/Network/BitStream.h"

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
        options._idempotencyKey     = key.isValid() ? key : NetIdempotencyKey::makeRandom();
        const uint64      requestID = _nextRequestID++;
        const PendingCall pendingCall{ options._idempotencyKey, requestID, method };
        (void)_callTable.send( *_pClient, method, body, options, OnlineResponseDelegate::create<&TradeClient::onResponse>( this ), pendingCall ); // 응답은 pendingCall._requestID 로 알린다
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
