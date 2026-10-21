#include "pch.h"

#include "Core/Network/Message/NetMessage.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Network/Connection/NetHost.h"

namespace sw
{
    NetMessageWriter::NetMessageWriter()
        : _writer{}
    {
        _writer.reserve( NetConnection::kMaxSingleMessageSize );
    }

    BitWriter& NetMessageWriter::begin( uint8 kind )
    {
        _writer.clear();
        _writer.writeBits( kind, 8 );
        return _writer;
    }

    bool NetMessageWriter::send( INetPeers& peers, int32 connectionID, NetChannelType channel ) const
    {
        return peers.sendMessage( connectionID, channel, _writer.getBytes().data(), _writer.getByteCount() );
    }

    int32 NetMessageWriter::broadcast( INetPeers& peers, NetChannelType channel, int32 exceptID ) const
    {
        return peers.broadcast( channel, _writer.getBytes().data(), _writer.getByteCount(), exceptID );
    }

    int32 NetMessageWriter::sendToPeers( INetPeers& peers, NetChannelType channel ) const
    {
        if ( peers.isServer() )
            return broadcast( peers, channel );
        return send( peers, 0, channel ) ? 1 : 0;
    }

    bool INetMessageHandler::isMessageKindHandled( uint8 kind ) const
    {
        const uint8 rangeBase = getMessageRangeBase();
        if ( NetMessageRange::isInRange( kind, rangeBase ) == false )
            return false;
        return ( ( getMessageKindMask() >> ( kind - rangeBase ) ) & 1u ) != 0;
    }

    NetHandleResult INetMessageHandler::handleMessage( int32 connectionID, const uint8* pData, int32 size, NetChannelType channel )
    {
        const bool bMessageNotHandled = pData == nullptr || size <= 0 || isMessageKindHandled( pData[0] ) == false;
        if ( bMessageNotHandled )
            return NetHandleResult::NotMine;
        BitReader body( pData + 1, size - 1 );
        return handleNetMessage( NetMessageContext{ pData, size, connectionID, channel, pData[0] }, body );
    }

    NetMessageRouter::NetMessageRouter()
        : _listHandler{}
        , _arrKindHandler{}
        , _kindTally{}
        , _inbound{}
        , _malformedCount{ 0 }
    {
    }

    bool NetMessageRouter::registerHandler( INetMessageHandler* pHandler )
    {
        if ( pHandler == nullptr )
            return false;
        if ( VectorUtil::contains( _listHandler, pHandler ) )
            return true;
        const uint8  rangeBase = pHandler->getMessageRangeBase();
        const uint16 kindMask  = pHandler->getMessageKindMask();
        for ( int32 offset = 0; offset < NetMessageRange::kSize; ++offset )
        {
            const int32 kind     = static_cast<int32>( rangeBase ) + offset;
            const bool  bClaimed = ( ( kindMask >> offset ) & 1u ) != 0 && kind < kKindCount && _arrKindHandler[kind] != nullptr;
            if ( bClaimed )
            {
                SW_LOG_ERROR( "NetMessageRouter: message kind 0x%02x is already claimed - the new handler is refused (give it its own range or a disjoint kind mask)", kind );
                return false;
            }
        }
        _listHandler.push_back( pHandler );
        rebuildKindTable();
        return true;
    }

    void NetMessageRouter::unregisterHandler( INetMessageHandler* pHandler )
    {
        VectorUtil::removeIf( _listHandler, [pHandler]( const INetMessageHandler* pRegistered )
        { return pRegistered == pHandler; } );
        rebuildKindTable();
    }

    void NetMessageRouter::rebuildKindTable()
    {
        for ( INetMessageHandler*& pSlot : _arrKindHandler )
        {
            pSlot = nullptr;
        }
        for ( INetMessageHandler* pHandler : _listHandler )
        {
            const uint8  rangeBase = pHandler->getMessageRangeBase();
            const uint16 kindMask  = pHandler->getMessageKindMask();
            for ( int32 offset = 0; offset < NetMessageRange::kSize; ++offset )
            {
                if ( ( ( kindMask >> offset ) & 1u ) == 0 )
                    continue;
                const int32 kind = static_cast<int32>( rangeBase ) + offset;
                if ( kind >= kKindCount )
                    break;
                if ( _arrKindHandler[kind] != nullptr )
                {
                    SW_LOG_ERROR( "NetMessageRouter: message kind 0x%02x is claimed by two handlers - the first one keeps it", kind );
                    continue;
                }
                _arrKindHandler[kind] = pHandler;
            }
        }
    }

    NetHandleResult NetMessageRouter::dispatch( const NetMessageContext& context, const uint8* pData, int32 size )
    {
        if ( pData == nullptr || size <= 0 )
            return NetHandleResult::NotMine;
        INetMessageHandler* pHandler = _arrKindHandler[pData[0]];
        if ( pHandler == nullptr )
            return NetHandleResult::NotMine;
        BitReader             body( pData + 1, size - 1 );
        const NetHandleResult result = pHandler->handleNetMessage( NetMessageContext{ pData, size, context._connectionID, context._channel, pData[0] }, body );
        if ( result == NetHandleResult::Malformed )
            ++_malformedCount;
        return result;
    }

    void NetMessageRouter::dispatchEvent( const NetHostEvent& event )
    {
        for ( INetMessageHandler* pHandler : _listHandler )
        {
            if ( event._kind == NetHostEvent::Kind::Connected )
                pHandler->onConnectionOpened( event._connectionID );
            else
                pHandler->onConnectionClosed( event._connectionID, event._reason );
        }
    }

    int32 NetMessageRouter::pump( INetPeers& peers, vector<NetReceivedMessage>* pOutUnhandled, vector<NetHostEvent>* pOutListEvent )
    {
        peers.drainInbound( _inbound );
        for ( const NetHostEvent& event : _inbound._listEvent )
        {
            dispatchEvent( event );
        }
        if ( pOutListEvent != nullptr )
            pOutListEvent->insert( pOutListEvent->end(), _inbound._listEvent.begin(), _inbound._listEvent.end() );
        for ( const NetInboundMessage& message : _inbound._listMessage )
        {
            const uint8* pData = _inbound._bytes.data() + message._offset;
            _kindTally.add( pData, message._size );
            if ( dispatch( NetMessageContext{ pData, message._size, message._connectionID, message._channel, message._size > 0 ? pData[0] : uint8{ 0 } }, pData, message._size ) !=
                     NetHandleResult::NotMine ||
                 pOutUnhandled == nullptr )
                continue;
            NetReceivedMessage unhandled;
            unhandled._buffer.assign( pData, pData + message._size );
            unhandled._connectionID = message._connectionID;
            unhandled._channel      = message._channel;
            pOutUnhandled->push_back( std::move( unhandled ) );
        }
        return static_cast<int32>( _inbound._listMessage.size() );
    }

    int32 NetMessageRouter::relayToOtherPeers( INetPeers& peers, const NetMessageContext& context )
    {
        const bool bCannotRelay = peers.isServer() == false || context._pMessage == nullptr || context._messageSize <= 0;
        if ( bCannotRelay )
            return 0;
        return peers.broadcast( context._channel, context._pMessage, context._messageSize, context._connectionID );
    }
} // namespace sw
