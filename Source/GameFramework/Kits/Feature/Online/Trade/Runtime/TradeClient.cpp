#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Trade/Runtime/TradeClient.h"

#include "Core/Foundation/Constants.h"

#include "GameFramework/Kits/Feature/Online/Trade/Runtime/TradeSessionRecord.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Online/Service/OnlineClientRegistry.h"

namespace sw
{
    TradeClient::TradeClient()
        : _listReply{}
        , _listUpdate{}
        , _snapshot{}
        , _accountID{ kInvalidAccountID }
    {
    }

    uint64 TradeClient::invite( string_view peerDisplayName, const NetIdempotencyKey& key )
    {
        TradeInviteRequest request;
        request._peerDisplayName = string( peerDisplayName );
        return send<TradeMethod::Invite>( request, key );
    }

    uint64 TradeClient::respond( uint64 tradeID, bool bAccept, const NetIdempotencyKey& key )
    {
        TradeRespondRequest request;
        request._tradeID = tradeID;
        request._bAccept = bAccept ? SW_TRUE : SW_FALSE;
        return send<TradeMethod::Respond>( request, key );
    }

    uint64 TradeClient::setOffer( uint64 tradeID, const vector<TradeLeg>& listLeg, const NetIdempotencyKey& key )
    {
        TradeOfferRequest request;
        request._listLeg = listLeg;
        request._tradeID = tradeID;
        return send<TradeMethod::SetOffer>( request, key );
    }

    uint64 TradeClient::lock( uint64 tradeID, const NetIdempotencyKey& key )
    {
        return send<TradeMethod::Lock>( TradeIDRequest{ tradeID }, key );
    }

    uint64 TradeClient::confirm( uint64 tradeID, const NetIdempotencyKey& key )
    {
        const optional<int32> sideIndex    = _snapshot._tradeID == tradeID ? _snapshot.findSideIndex( _accountID ) : optional<int32>{};
        const uint32          ownRevision  = sideIndex.has_value() ? _snapshot._arrSide[*sideIndex]._offerRevision : 0;
        const uint32          peerRevision = sideIndex.has_value() ? _snapshot._arrSide[1 - *sideIndex]._offerRevision : 0;
        return confirmSeen( tradeID, ownRevision, peerRevision, key );
    }

    uint64 TradeClient::confirmSeen( uint64 tradeID, uint32 seenOwnRevision, uint32 seenPeerRevision, const NetIdempotencyKey& key )
    {
        TradeConfirmRequest request;
        request._tradeID          = tradeID;
        request._seenOwnRevision  = seenOwnRevision;
        request._seenPeerRevision = seenPeerRevision;
        return send<TradeMethod::Confirm>( request, key );
    }

    uint64 TradeClient::cancel( uint64 tradeID, const NetIdempotencyKey& key )
    {
        return send<TradeMethod::Cancel>( TradeIDRequest{ tradeID }, key );
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

    void TradeClient::onPush( TradeMethod::Invited, const TradeNotice& body )
    {
        recordUpdate( TradeMethod::Invited::kKind, body );
    }

    void TradeClient::onPush( TradeMethod::Update, const TradeNotice& body )
    {
        recordUpdate( TradeMethod::Update::kKind, body );
    }

    void TradeClient::onPush( TradeMethod::Closed, const TradeNotice& body )
    {
        recordUpdate( TradeMethod::Closed::kKind, body );
    }

    void TradeClient::recordUpdate( uint16 kind, const TradeNotice& body )
    {
        TradeClientUpdate update;
        update._kind        = kind;
        update._snapshot    = body._snapshot;
        update._listBalance = body._listBalance;
        observe( update._snapshot );
        _listUpdate.push_back( std::move( update ) );
    }

    template <typename TMethod>
    uint64 TradeClient::send( const typename TMethod::Request& request, const NetIdempotencyKey& key )
    {
        return call<TMethod>( request, OnlineReplyDelegate<TMethod>::template create<&TradeClient::onReply>( this ), key );
    }

    void TradeClient::onReply( const OnlineResult<TradeReply>& result )
    {
        TradeClientReply reply;
        reply._requestID      = result._requestID;
        reply._method         = result._method;
        reply._idempotencyKey = result._idempotencyKey;
        reply._errorCode      = result._errorCode;
        reply._result         = result._reply._result; // 공통 범주면 기본값이고 `_errorCode` 가 이유다
        if ( result._errorCode == OnlineError::kOk )
        {
            reply._snapshot    = result._reply._snapshot;
            reply._listBalance = result._reply._listBalance;
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
    TradeOutcome TradeClient::makeLegs( const Inventory& inventory, const vector<int32>& listSlot, AssetIDFromItem pAssetIDFromItem, vector<TradeLeg>& outListLeg )
    {
        outListLeg.clear();
        if ( pAssetIDFromItem == nullptr )
            return TradeOutcome::fail( OnlineError::kInvalidRequest );
        for ( size_t slotIndex = 0; slotIndex < listSlot.size(); ++slotIndex )
        {
            const int32 slot = listSlot[slotIndex];
            for ( size_t previousIndex = 0; previousIndex < slotIndex; ++previousIndex )
            {
                if ( listSlot[previousIndex] == slot )
                    return TradeOutcome::fail( OnlineError::kInvalidRequest ); // 같은 슬롯을 두 번 지정했다
            }
            if ( slot < 0 || slot >= inventory.getSlotCount() )
                return TradeOutcome::fail( OnlineError::kInvalidRequest );
            const InventorySlot& stack = inventory.getSlot( slot );
            if ( stack.isEmpty() )
                return TradeOutcome::fail( OnlineError::kInvalidRequest );
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
        return static_cast<int32>( outListLeg.size() ) > TradeConstant::kMaxLegsPerSide ? TradeOutcome{ TradeResult::TooManyLegs } : TradeOutcome{ TradeResult::Ok };
    }
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_CLIENT( TradeClient );
} // namespace sw
