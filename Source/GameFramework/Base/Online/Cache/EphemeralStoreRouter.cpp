#include "pch.h"

#include "GameFramework/Base/Online/Cache/EphemeralStoreRouter.h"

#include "Core/Container/VectorUtil.h"

namespace sw
{
    EphemeralStoreRouter::EphemeralStoreRouter()
        : _listReplyScratch{}
        , _listMessageScratch{}
        , _listDispatchScratch{}
        , _listSubscriber{}
        , _mapRequestToReply{}
        , _pStore{ nullptr }
        , _subscriptionIDAllocator{}
    {
    }

    EphemeralStoreRouter::~EphemeralStoreRouter()
    {
        shutdown();
    }

    void EphemeralStoreRouter::initialize( IEphemeralStore* pStore )
    {
        SW_ASSERT( pStore != nullptr );
        _pStore = pStore;
    }

    void EphemeralStoreRouter::shutdown()
    {
        if ( _pStore == nullptr )
            return;
        detachAllSubscriptions(); // 남은 구독은 무효로 — 뒤늦은 해제가 내린 앞을 부르지 않는다
        vector<string> listChannel;
        for ( const ChannelSubscriber& subscriber : _listSubscriber )
        {
            listChannel.push_back( subscriber._channel );
        }
        VectorUtil::sortUnique( listChannel ); // 채널마다 한 번 푼다
        for ( const string& channel : listChannel )
        {
            _pStore->unsubscribe( channel );
        }
        _listSubscriber.clear();

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
            for ( const ChannelSubscriber& subscriber : _listSubscriber )
            {
                if ( subscriber._channel == message._channel )
                    _listDispatchScratch.push_back( subscriber._onMessage );
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
        if ( _pStore == nullptr )
            ::sw::internal::reportMissingRequiredService( SW_FUNCTION_SIGNATURE );
        const uint64 requestID = _pStore->submit( request );
        // 메모리 앞은 맡기는 시점에 답을 큐에 넣지만 꺼내는 것은 다음 pump 다 — 등록이 늦지 않다.
        _mapRequestToReply.emplace( requestID, onReply );
        return requestID;
    }

    Subscription EphemeralStoreRouter::subscribe( string_view channel, MessageDelegate onMessage )
    {
        const bool bCannotSubscribe = _pStore == nullptr || onMessage.isBound() == false;
        if ( bCannotSubscribe )
        {
            SW_LOG_ERROR( "EphemeralStoreRouter: subscribe('%#') %# - nothing is subscribed", channel, _pStore == nullptr ? "before initialize" : "with an empty delegate" );
            return Subscription{};
        }
        if ( hasChannelSubscriber( channel ) == false )
            _pStore->subscribe( channel );
        ChannelSubscriber& subscriber = _listSubscriber.emplace_back();
        subscriber._channel           = string( channel );
        subscriber._onMessage         = onMessage;
        subscriber._subscriptionID    = _subscriptionIDAllocator.allocate();
        return issueSubscription( subscriber._subscriptionID );
    }

    void EphemeralStoreRouter::releaseSubscription( uint64 id )
    {
        const std::optional<size_t> index = VectorUtil::findIndexBy<&ChannelSubscriber::_subscriptionID>( _listSubscriber, id );
        if ( index.has_value() == false )
            return;
        const string channel = std::move( _listSubscriber[*index]._channel );
        _listSubscriber.erase( _listSubscriber.begin() + static_cast<ptrdiff_t>( *index ) );
        if ( _pStore != nullptr && hasChannelSubscriber( channel ) == false )
            _pStore->unsubscribe( channel );
    }

    bool EphemeralStoreRouter::hasChannelSubscriber( string_view channel ) const
    {
        return VectorUtil::containsBy<&ChannelSubscriber::_channel>( _listSubscriber, channel );
    }
} // namespace sw
