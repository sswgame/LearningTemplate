#include "pch.h"

#include "Core/Network/NetMessage.h"

#include "Core/Network/NetHost.h"

#include <algorithm>

namespace sw
{
    NetMessageWriter::NetMessageWriter()
        : _writer{}
    {
        _writer.reserve( NetConnection::kMaxMessageSize );
    }

    BitWriter& NetMessageWriter::begin( uint8 kind )
    {
        _writer.clear();
        _writer.writeBits( kind, 8 );
        return _writer;
    }

    bool NetMessageWriter::send( NetHost& host, int32 connectionId, NetChannelType channel ) const
    {
        return host.sendMessage( connectionId, channel, _writer.getBytes().data(), _writer.getByteCount() );
    }

    int32 NetMessageWriter::broadcast( NetHost& host, NetChannelType channel, int32 exceptId ) const
    {
        return host.broadcast( channel, _writer.getBytes().data(), _writer.getByteCount(), exceptId );
    }

    NetMessageRouter::NetMessageRouter()
        : _arrHandler{}
        , _receiveBuffer{}
    {
    }

    void NetMessageRouter::addHandler( INetMessageHandler* pHandler )
    {
        if ( pHandler == nullptr )
            return;
        vector<INetMessageHandler*>& listHandler = _arrHandler[pHandler->getMessageRangeBase() >> 4];
        if ( std::find( listHandler.begin(), listHandler.end(), pHandler ) == listHandler.end() )
            listHandler.push_back( pHandler );
    }

    void NetMessageRouter::removeHandler( INetMessageHandler* pHandler )
    {
        for ( vector<INetMessageHandler*>& listHandler : _arrHandler )
            listHandler.erase( std::remove( listHandler.begin(), listHandler.end(), pHandler ), listHandler.end() );
    }

    bool NetMessageRouter::dispatch( int32 connectionId, const uint8* pData, int32 size )
    {
        if ( pData == nullptr || size <= 0 )
            return false;
        for ( INetMessageHandler* pHandler : _arrHandler[pData[0] >> 4] )
        {
            if ( pHandler->handleNetMessage( connectionId, pData, size ) )
                return true;
        }
        return false;
    }

    int32 NetMessageRouter::pump( NetHost& host, vector<NetReceivedMessage>* pOutUnhandled )
    {
        int32          count        = 0;
        int32          connectionId = -1;
        NetChannelType channel      = NetChannelType::ReliableOrdered;
        while ( host.receiveMessage( connectionId, channel, _receiveBuffer ) )
        {
            ++count;
            if ( dispatch( connectionId, _receiveBuffer.data(), static_cast<int32>( _receiveBuffer.size() ) ) || pOutUnhandled == nullptr )
                continue;
            NetReceivedMessage message;
            message._buffer       = _receiveBuffer;
            message._connectionId = connectionId;
            message._channel      = channel;
            pOutUnhandled->push_back( std::move( message ) );
        }
        return count;
    }
} // namespace sw
