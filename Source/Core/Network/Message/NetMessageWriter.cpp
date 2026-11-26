#include "pch.h"

#include "Core/Network/Message/NetMessageWriter.h"

namespace sw
{
    NetMessageWriter::NetMessageWriter()
        : _writer{}
        , _messageChannel{ NetChannelKind::ReliableOrdered }
        , _state{ State::Raw }
        , _bWorldScoped{ SW_FALSE }
    {
        _writer.reserve( NetConnection::kMaxSingleMessageSize );
    }

    BitWriter& NetMessageWriter::begin( uint8 kind )
    {
        _writer.clear();
        _writer.writeBits( kind, 8 );
        _state        = State::Raw;
        _bWorldScoped = SW_FALSE;
        return _writer;
    }

    BitWriter& NetMessageWriter::beginDescribed( uint8 kind, NetChannelKind channel, bool bWorldScoped )
    {
        BitWriter& writer = begin( kind );
        if ( bWorldScoped )
        {
            _bWorldScoped = SW_TRUE;
            writer.writeBits( 0, 8 ); // 보낼 때 적는다 — 쓰는 쪽이 월드를 고르지 않는다
        }
        _messageChannel = channel;
        _state          = State::Typed;
        return writer;
    }

    bool NetMessageWriter::allowsCallerChannel() const
    {
        if ( _state == State::Raw )
            return true;
        // 서술자가 있는 메시지를 다른 채널로 보내려 했다(또는 쓰다 만 바이트) — 호출부 결함이다.
        SW_LOG_ERROR_ID( "Net.MessageChannelChosenByCaller", "NetMessageWriter: message kind 0x%02x has a descriptor channel - send it with sendMessage, not with a caller-chosen channel",
                         _writer.getByteCount() > 0 ? static_cast<int32>( _writer.getBytes()[0] ) : constant::invalid_index::kInt32 );
        return false;
    }

    bool NetMessageWriter::allowsDescriptorChannel() const
    {
        if ( _state == State::Typed )
            return true;
        SW_LOG_ERROR_ID( "Net.MessageNotTyped", "NetMessageWriter: message kind 0x%02x was not written by writeMessage (hand body or failed write) - it is not sent",
                         _writer.getByteCount() > 0 ? static_cast<int32>( _writer.getBytes()[0] ) : constant::invalid_index::kInt32 );
        return false;
    }

    bool NetMessageWriter::prepareDescribed( const INetPeers& peers )
    {
        if ( allowsDescriptorChannel() == false )
            return false;
        if ( _bWorldScoped == SW_TRUE )
            _writer.overwriteByte( 1, peers.getWorldGeneration() );
        return true;
    }

    bool NetMessageWriter::prepareDescribed( const INetPeers& peers, int32 connectionID )
    {
        return prepareDescribed( peers ) && ( _bWorldScoped == SW_FALSE || peers.isInWorld( connectionID ) );
    }

    int32 NetMessageWriter::broadcastDescribed( INetPeers& peers, int32 exceptID )
    {
        if ( _bWorldScoped == SW_FALSE )
            return peers.broadcast( _messageChannel, _writer.getBytes().data(), _writer.getByteCount(), exceptID );
        vector<int32> listConnection;
        peers.collectInWorld( listConnection );
        int32 sentCount = 0;
        for ( const int32 connectionID : listConnection )
        {
            if ( connectionID != exceptID && peers.sendMessage( connectionID, _messageChannel, _writer.getBytes().data(), _writer.getByteCount() ) )
                ++sentCount;
        }
        return sentCount;
    }

    bool NetMessageWriter::send( INetPeers& peers, int32 connectionID, NetChannelKind channel ) const
    {
        return allowsCallerChannel() && peers.sendMessage( connectionID, channel, _writer.getBytes().data(), _writer.getByteCount() );
    }

    int32 NetMessageWriter::broadcast( INetPeers& peers, NetChannelKind channel, int32 exceptID ) const
    {
        return allowsCallerChannel() ? peers.broadcast( channel, _writer.getBytes().data(), _writer.getByteCount(), exceptID ) : 0;
    }

    int32 NetMessageWriter::sendToPeers( INetPeers& peers, NetChannelKind channel ) const
    {
        if ( allowsCallerChannel() == false )
            return 0;
        if ( peers.isServer() )
            return peers.broadcast( channel, _writer.getBytes().data(), _writer.getByteCount(), constant::invalid_index::kInt32 );
        return peers.sendMessage( 0, channel, _writer.getBytes().data(), _writer.getByteCount() ) ? 1 : 0;
    }

    bool NetMessageWriter::sendMessage( INetPeers& peers, int32 connectionID )
    {
        return prepareDescribed( peers, connectionID ) && peers.sendMessage( connectionID, _messageChannel, _writer.getBytes().data(), _writer.getByteCount() );
    }

    bool NetMessageWriter::trySendMessage( INetPeers& peers, int32 connectionID )
    {
        return prepareDescribed( peers, connectionID ) && peers.trySendMessage( connectionID, _messageChannel, _writer.getBytes().data(), _writer.getByteCount() );
    }

    int32 NetMessageWriter::broadcastMessage( INetPeers& peers, int32 exceptID )
    {
        return prepareDescribed( peers ) ? broadcastDescribed( peers, exceptID ) : 0;
    }

    int32 NetMessageWriter::sendMessageToPeers( INetPeers& peers )
    {
        if ( peers.isServer() )
            return broadcastMessage( peers );
        return sendMessage( peers, 0 ) ? 1 : 0;
    }

    bool NetMessageWriter::sendNotifiedMessage( INetPeers& peers, int32 connectionID, uint32 tag )
    {
        if ( prepareDescribed( peers, connectionID ) == false )
            return false;
        if ( _messageChannel != NetChannelKind::Unreliable )
        {
            SW_LOG_ERROR_ID( "Net.MessageNotifiedNeedsUnreliable", "NetMessageWriter: message kind 0x%02x is not an unreliable message - delivery notices only ride unreliable messages",
                             static_cast<int32>( _writer.getBytes()[0] ) );
            return false;
        }
        return peers.sendNotifiedMessage( connectionID, _writer.getBytes().data(), _writer.getByteCount(), tag );
    }

    optional<NetQueuedMessage> NetMessageWriter::makeQueuedMessage() const
    {
        if ( allowsDescriptorChannel() == false )
            return nullopt;
        if ( _bWorldScoped == SW_TRUE )
        {
            SW_LOG_ERROR_ID( "Net.MessageWorldScopedQueued", "NetMessageWriter: message kind 0x%02x belongs to a world and cannot be queued for later - it is not sent", static_cast<int32>( _writer.getBytes()[0] ) );
            return nullopt;
        }
        return NetQueuedMessage{ _writer.getBytes(), _messageChannel };
    }

    optional<NetChannelKind> NetMessageWriter::getMessageChannel() const
    {
        if ( _state == State::Typed )
            return _messageChannel;
        return nullopt;
    }
} // namespace sw
