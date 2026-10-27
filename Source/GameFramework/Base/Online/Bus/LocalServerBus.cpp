#include "pch.h"

#include "GameFramework/Base/Online/Bus/LocalServerBus.h"

#include "Core/Container/VectorUtil.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "LocalServerBus" );
} // namespace sw

namespace sw
{
    LocalServerBusHub::LocalServerBusHub()
        : _mutex{}
        , _mapInbox{}
        , _inboxIDAllocator{}
    {
    }

    Subscription LocalServerBusHub::registerInbox( uint64& outInboxID )
    {
        {
            ScopedLock<mutex> lock{ _mutex };
            outInboxID            = _inboxIDAllocator.allocate();
            _mapInbox[outInboxID] = Inbox{};
        }
        return issueSubscription( outInboxID );
    }

    void LocalServerBusHub::releaseSubscription( uint64 id )
    {
        ScopedLock<mutex> lock{ _mutex };
        _mapInbox.erase( id );
    }

    void LocalServerBusHub::subscribe( uint64 inboxID, string_view topic )
    {
        ScopedLock<mutex> lock{ _mutex };
        const auto        inboxIt = _mapInbox.find( inboxID );
        if ( inboxIt == _mapInbox.end() )
            return;
        vector<string>& listTopic = inboxIt->second._listTopic;
        if ( VectorUtil::contains( listTopic, topic ) == false )
            listTopic.push_back( string{ topic } );
    }

    void LocalServerBusHub::unsubscribe( uint64 inboxID, string_view topic )
    {
        ScopedLock<mutex> lock{ _mutex };
        const auto        inboxIt = _mapInbox.find( inboxID );
        if ( inboxIt == _mapInbox.end() )
            return;
        vector<string>& listTopic = inboxIt->second._listTopic;
        VectorUtil::removeIf( listTopic, [&topic]( const string& subscribed )
        { return subscribed == topic; } );
    }

    int32 LocalServerBusHub::deliver( const ServerBusMessage& message )
    {
        ScopedLock<mutex> lock{ _mutex };
        int32             receiverCount = 0;
        for ( auto& [inboxID, inbox] : _mapInbox )
        {
            (void)inboxID;
            if ( VectorUtil::contains( inbox._listTopic, message.getTopic() ) == false )
                continue;
            inbox._listMessage.push_back( message );
            ++receiverCount;
        }
        return receiverCount;
    }

    int32 LocalServerBusHub::takeMessages( uint64 inboxID, vector<ServerBusMessage>& outListMessage )
    {
        ScopedLock<mutex> lock{ _mutex };
        const auto        inboxIt = _mapInbox.find( inboxID );
        if ( inboxIt == _mapInbox.end() )
            return 0;
        vector<ServerBusMessage>& listMessage = inboxIt->second._listMessage;
        const int32               takenCount  = static_cast<int32>( listMessage.size() );
        for ( ServerBusMessage& message : listMessage )
        {
            outListMessage.push_back( std::move( message ) );
        }
        listMessage.clear();
        return takenCount;
    }
} // namespace sw

namespace sw
{
    LocalServerBus::LocalServerBus( LocalServerBusHub* pHub, uint64 serverID )
        : _pHub{ pHub }
        , _serverID{ serverID }
        , _inboxID{ 0 }
        , _inboxSubscription{ pHub->registerInbox( _inboxID ) }
        , _nextSequence{ 1 }
    {
    }

    LocalServerBus::~LocalServerBus() = default; // 받은편지함은 구독이 지운다

    void LocalServerBus::publish( string_view topic, const uint8* pData, int32 size )
    {
        const bool bValid = isValidTopic( topic ) && 0 <= size && size <= kMaxMessageSize && ( size == 0 || pData != nullptr );
        if ( bValid == false )
        {
            SW_LOG_WARNING( "Dropped a bus message: topic '%#' size %#", topic, size );
            return;
        }
        const ServerBusMessage message( vector<uint8>( pData, pData + size ), string{ topic }, _serverID, _nextSequence++ );
        (void)_pHub->deliver( message );
    }

    void LocalServerBus::subscribe( string_view topic )
    {
        if ( isValidTopic( topic ) == false )
        {
            SW_LOG_WARNING( "Ignored a bus subscription to an invalid topic '%#'", topic );
            return;
        }
        _pHub->subscribe( _inboxID, topic );
    }

    void LocalServerBus::unsubscribe( string_view topic )
    {
        _pHub->unsubscribe( _inboxID, topic );
    }

    int32 LocalServerBus::pollMessages( vector<ServerBusMessage>& outListMessage )
    {
        return _pHub->takeMessages( _inboxID, outListMessage );
    }
} // namespace sw
