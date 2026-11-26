#include "pch.h"

#include "GameFramework/Runtime/Online/Cache/Server/CacheStoreRouter.h"

#include "Core/Algorithm/VectorUtil.h"

namespace sw
{
    CacheStoreRouter::CacheStoreRouter()
        : _listReplyScratch{}
        , _listMessageScratch{}
        , _listDispatchScratch{}
        , _listSubscriber{}
        , _replies{}
        , _pStore{ nullptr }
        , _subscriptionIDAllocator{}
    {
    }

    CacheStoreRouter::~CacheStoreRouter()
    {
        shutdown();
    }

    void CacheStoreRouter::initialize( ICacheStore& store )
    {
        _pStore = &store;
    }

    void CacheStoreRouter::shutdown()
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

        _pStore = nullptr;
        _replies.answerAll( CacheReply::makeUnavailable() );
    }

    int32 CacheStoreRouter::poll()
    {
        if ( _pStore == nullptr )
            return 0;
        int32 dispatchCount = 0;

        _listReplyScratch.clear();
        (void)_pStore->pollReplies( _listReplyScratch );
        for ( const CacheReply& reply : _listReplyScratch )
        {
            if ( _replies.answer( reply._requestID, reply ) )
                ++dispatchCount; // 모르는 번호는 주인이 몫을 놓았거나 답을 보지 않는 쓰기다
        }

        _listMessageScratch.clear();
        (void)_pStore->pollMessages( _listMessageScratch );
        for ( const CacheMessage& message : _listMessageScratch )
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

    void CacheStoreRouter::submit( const CacheRequest& request )
    {
        if ( _pStore == nullptr )
            ::sw::internal::reportMissingRequiredSubsystem( SW_FUNCTION_SIGNATURE );
        (void)_pStore->submit( request ); // 답은 poll 가 주인 없는 id 로 보고 버린다
    }

    uint64 CacheStoreRouter::submitOwned( const CacheRequest& request, const Subscription& owner, uint64& outOwnerID )
    {
        if ( _pStore == nullptr )
            ::sw::internal::reportMissingRequiredSubsystem( SW_FUNCTION_SIGNATURE );
        outOwnerID = _subscriptions.findIssued( owner );
        if ( outOwnerID != 0 )
            return _pStore->submit( request );
        SW_LOG_ERROR_ID( "CacheStoreRouter.SubmitRefused", "CacheStoreRouter: submit without a live reply owner of this router - nothing is submitted" );
        return 0;
    }

    Subscription CacheStoreRouter::subscribe( string_view channel, MessageDelegate onMessage )
    {
        const bool bCannotSubscribe = _pStore == nullptr || onMessage.isBound() == false;
        if ( bCannotSubscribe )
        {
            SW_LOG_ERROR_ID( "CacheStoreRouter.SubscribeRefused", "CacheStoreRouter: subscribe('%#') %# - nothing is subscribed", channel, _pStore == nullptr ? "before initialize" : "with an empty delegate" );
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

    void CacheStoreRouter::releaseSubscription( uint64 id )
    {
        _replies.discardOwnedBy( id ); // 주인 몫이었으면 그 답들을 버린다
        const optional<size_t> index = VectorUtil::findIndexBy<&ChannelSubscriber::_subscriptionID>( _listSubscriber, id );
        if ( index.has_value() == false )
            return;
        const string channel = std::move( _listSubscriber[*index]._channel );
        _listSubscriber.erase( _listSubscriber.begin() + static_cast<ptrdiff_t>( *index ) );
        if ( _pStore != nullptr && hasChannelSubscriber( channel ) == false )
            _pStore->unsubscribe( channel );
    }

    bool CacheStoreRouter::hasChannelSubscriber( string_view channel ) const
    {
        return VectorUtil::containsBy<&ChannelSubscriber::_channel>( _listSubscriber, channel );
    }
} // namespace sw
