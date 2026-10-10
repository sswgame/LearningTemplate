#include "pch.h"

#include "GameFramework/Base/Online/Cache/EphemeralStoreRouter.h"

namespace sw
{
    EphemeralStoreRouter::EphemeralStoreRouter()
        : _listReplyScratch{}
        , _listMessageScratch{}
        , _listDispatchScratch{}
        , _listSubscription{}
        , _mapRequestToReply{}
        , _pStore{ nullptr }
        , _nextSubscriptionID{ 1 }
    {
    }

    EphemeralStoreRouter::~EphemeralStoreRouter() { shutdown(); }

    void EphemeralStoreRouter::initialize( IEphemeralStore* pStore )
    {
        SW_ASSERT( pStore != nullptr );
        _pStore = pStore;
    }

    void EphemeralStoreRouter::shutdown()
    {
        if ( _pStore == nullptr )
            return;
        while ( _listSubscription.empty() == false ) // 채널마다 한 번 푼다
        {
            const string channel = _listSubscription.back()._channel;
            _pStore->unsubscribe( channel );
            for ( size_t index = _listSubscription.size(); index > 0; --index )
            {
                if ( _listSubscription[index - 1]._channel == channel )
                    _listSubscription.erase( _listSubscription.begin() + static_cast<ptrdiff_t>( index - 1 ) );
            }
        }

        unordered_map<uint64, ReplyDelegate> mapPending;
        mapPending = std::move( _mapRequestToReply );
        _mapRequestToReply.clear();
        _pStore = nullptr;
        for ( auto& [requestID, onReply] : mapPending )
        {
            if ( onReply.isBound() == false )
                continue;
            EphemeralReply reply;
            reply._requestID = requestID;
            reply._result    = EphemeralResult::Unavailable;
            onReply( reply );
        }
    }

    int32 EphemeralStoreRouter::pump()
    {
        if ( _pStore == nullptr )
            return 0;
        int32 dispatchCount = 0;

        _listReplyScratch.clear();
        (void)_pStore->pollReplies( _listReplyScratch );
        for ( const EphemeralReply& reply : _listReplyScratch )
        {
            const auto replyIt = _mapRequestToReply.find( reply._requestID );
            if ( replyIt == _mapRequestToReply.end() )
                continue;
            const ReplyDelegate onReply = replyIt->second;
            _mapRequestToReply.erase( replyIt );
            if ( onReply.isBound() )
                onReply( reply );
            ++dispatchCount;
        }

        _listMessageScratch.clear();
        (void)_pStore->pollMessages( _listMessageScratch );
        for ( const EphemeralMessage& message : _listMessageScratch )
        {
            _listDispatchScratch.clear();
            for ( const Subscription& subscription : _listSubscription )
            {
                if ( subscription._channel == message._channel )
                    _listDispatchScratch.push_back( subscription._onMessage );
            }
            for ( const MessageDelegate& onMessage : _listDispatchScratch )
            {
                onMessage( message );
                ++dispatchCount;
            }
        }
        return dispatchCount;
    }

    uint64 EphemeralStoreRouter::submit( const EphemeralRequest& request, ReplyDelegate onReply )
    {
        SW_ASSERT( _pStore != nullptr );
        const uint64 requestID = _pStore->submit( request );
        // 메모리 앞은 맡기는 자리에서 답을 큐에 넣지만 꺼내는 것은 다음 pump 다 — 등록이 늦지 않다.
        _mapRequestToReply.emplace( requestID, onReply );
        return requestID;
    }

    uint64 EphemeralStoreRouter::subscribe( string_view channel, MessageDelegate onMessage )
    {
        if ( _pStore == nullptr || onMessage.isBound() == false )
            return 0;
        if ( hasChannelSubscriber( channel ) == false )
            _pStore->subscribe( channel );
        Subscription& subscription   = _listSubscription.emplace_back();
        subscription._channel        = string( channel );
        subscription._onMessage      = onMessage;
        subscription._subscriptionID = _nextSubscriptionID++;
        return subscription._subscriptionID;
    }

    void EphemeralStoreRouter::unsubscribe( uint64 subscriptionID )
    {
        for ( size_t index = 0; index < _listSubscription.size(); ++index )
        {
            if ( _listSubscription[index]._subscriptionID != subscriptionID )
                continue;
            const string channel = std::move( _listSubscription[index]._channel );
            _listSubscription.erase( _listSubscription.begin() + static_cast<ptrdiff_t>( index ) );
            if ( _pStore != nullptr && hasChannelSubscriber( channel ) == false )
                _pStore->unsubscribe( channel );
            return;
        }
    }

    bool EphemeralStoreRouter::hasChannelSubscriber( string_view channel ) const
    {
        for ( const Subscription& subscription : _listSubscription )
        {
            if ( subscription._channel == channel )
                return true;
        }
        return false;
    }
} // namespace sw
