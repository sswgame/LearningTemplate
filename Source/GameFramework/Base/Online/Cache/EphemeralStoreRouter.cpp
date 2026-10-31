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
        _subscriptions.detachAll(); // 남은 구독은 무효로 — 뒤늦은 해제가 내린 앞을 부르지 않는다
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

        unordered_map<uint64, PendingReply> mapPending;
        mapPending = std::move( _mapRequestToReply );
        _mapRequestToReply.clear();
        _pStore = nullptr;
        for ( auto& [requestID, pending] : mapPending )
        {
            EphemeralReply reply;
            reply._requestID = requestID;
            reply._result    = EphemeralResult::Unavailable;
            pending._onReply( reply );
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
            const ReplyDelegate onReply = replyIt->second._onReply;
            _mapRequestToReply.erase( replyIt );
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

    void EphemeralStoreRouter::submit( const EphemeralRequest& request )
    {
        if ( _pStore == nullptr )
            ::sw::internal::reportMissingRequiredService( SW_FUNCTION_SIGNATURE );
        (void)_pStore->submit( request ); // 답은 pump 가 주인 없는 id 로 보고 버린다
    }

    uint64 EphemeralStoreRouter::submit( const EphemeralRequest& request, ReplyDelegate onReply, const Subscription& owner )
    {
        if ( _pStore == nullptr )
            ::sw::internal::reportMissingRequiredService( SW_FUNCTION_SIGNATURE );
        const uint64 ownerID = _subscriptions.findIssued( owner );
        if ( ownerID == 0 || onReply.isBound() == false )
        {
            SW_LOG_ERROR_ID( "EphemeralStoreRouter.SubmitRefused", "EphemeralStoreRouter: submit %# - nothing is submitted", ownerID == 0 ? "without a live reply owner of this router" : "with an empty delegate" );
            return 0;
        }
        const uint64 requestID = _pStore->submit( request );
        // 메모리 앞은 맡기는 시점에 답을 큐에 넣지만 꺼내는 것은 다음 pump 다 — 등록이 늦지 않다.
        _mapRequestToReply.emplace( requestID, PendingReply{ onReply, ownerID } );
        return requestID;
    }

    Subscription EphemeralStoreRouter::subscribe( string_view channel, MessageDelegate onMessage )
    {
        const bool bCannotSubscribe = _pStore == nullptr || onMessage.isBound() == false;
        if ( bCannotSubscribe )
        {
            SW_LOG_ERROR_ID( "EphemeralStoreRouter.SubscribeRefused", "EphemeralStoreRouter: subscribe('%#') %# - nothing is subscribed", channel, _pStore == nullptr ? "before initialize" : "with an empty delegate" );
            return Subscription{};
        }
        if ( hasChannelSubscriber( channel ) == false )
            _pStore->subscribe( channel );
        ChannelSubscriber& subscriber = _listSubscriber.emplace_back();
        subscriber._channel           = string( channel );
        subscriber._onMessage         = onMessage;
        subscriber._subscriptionID    = _subscriptionIDAllocator.allocate();
        return _subscriptions.issue( subscriber._subscriptionID );
    }

    void EphemeralStoreRouter::releaseSubscription( uint64 id )
    {
        // 주인 몫이었으면 그 답들을 버린다(지우기는 마지막 원소를 이 자리로 옮기므로 같은 자리를 다시 본다).
        for ( auto replyIt = _mapRequestToReply.begin(); replyIt != _mapRequestToReply.end(); )
        {
            if ( replyIt->second._ownerID == id )
                replyIt = _mapRequestToReply.erase( replyIt );
            else
                ++replyIt;
        }
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
