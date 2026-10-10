#include "pch.h"

#include "GameFramework/Base/Online/Bus/LocalServerBus.h"

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
        , _nextInboxID{ 1 }
    {
    }

    uint64 LocalServerBusHub::registerInbox()
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const uint64            inboxID = _nextInboxID++;
        _mapInbox[inboxID]              = Inbox{};
        return inboxID;
    }

    void LocalServerBusHub::unregisterInbox( uint64 inboxID )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _mapInbox.erase( inboxID );
    }

    void LocalServerBusHub::subscribe( uint64 inboxID, string_view topic )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const auto              inboxIt = _mapInbox.find( inboxID );
        if ( inboxIt == _mapInbox.end() )
            return;
        vector<string>& listTopic = inboxIt->second._listTopic;
        if ( std::find( listTopic.begin(), listTopic.end(), topic ) == listTopic.end() )
            listTopic.push_back( string{ topic } );
    }

    void LocalServerBusHub::unsubscribe( uint64 inboxID, string_view topic )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const auto              inboxIt = _mapInbox.find( inboxID );
        if ( inboxIt == _mapInbox.end() )
            return;
        vector<string>& listTopic = inboxIt->second._listTopic;
        listTopic.erase( std::remove( listTopic.begin(), listTopic.end(), topic ), listTopic.end() );
    }

    int32 LocalServerBusHub::deliver( const ServerBusMessage& message )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        int32                   receiverCount = 0;
        for ( auto& [inboxID, inbox] : _mapInbox )
        {
            (void)inboxID;
            if ( std::find( inbox._listTopic.begin(), inbox._listTopic.end(), message._topic ) == inbox._listTopic.end() )
                continue;
            inbox._listMessage.push_back( message );
            ++receiverCount;
        }
        return receiverCount;
    }

    int32 LocalServerBusHub::takeMessages( uint64 inboxID, vector<ServerBusMessage>& outListMessage )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const auto              inboxIt = _mapInbox.find( inboxID );
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
        , _inboxID{ pHub->registerInbox() }
        , _nextSequence{ 1 }
    {
    }

    LocalServerBus::~LocalServerBus() { _pHub->unregisterInbox( _inboxID ); }

    void LocalServerBus::publish( string_view topic, const uint8* pData, int32 size )
    {
        const bool bValid = isValidTopic( topic ) && 0 <= size && size <= kMaxMessageSize && ( size == 0 || pData != nullptr );
        if ( bValid == false )
        {
            SW_LOG_WARNING( "Dropped a bus message: topic '%#' size %#", topic, size );
            return;
        }
        ServerBusMessage message;
        message._bytes.assign( pData, pData + size );
        message._topic          = string{ topic };
        message._originServerID = _serverID;
        message._sequence       = _nextSequence++;
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

    void LocalServerBus::unsubscribe( string_view topic ) { _pHub->unsubscribe( _inboxID, topic ); }

    int32 LocalServerBus::pollMessages( vector<ServerBusMessage>& outListMessage ) { return _pHub->takeMessages( _inboxID, outListMessage ); }
} // namespace sw
