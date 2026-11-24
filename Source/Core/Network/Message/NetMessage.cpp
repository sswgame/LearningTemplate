#include "pch.h"

#include "Core/Network/Message/NetMessage.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Network/Connection/NetEndpoint.h"

namespace sw
{
    NetMessageWriter::NetMessageWriter()
        : _writer{}
        , _messageChannel{ NetChannelKind::ReliableOrdered }
        , _state{ State::Raw }
    {
        _writer.reserve( NetConnection::kMaxSingleMessageSize );
    }

    BitWriter& NetMessageWriter::begin( uint8 kind )
    {
        _writer.clear();
        _writer.writeBits( kind, 8 );
        _state = State::Raw;
        return _writer;
    }

    BitWriter& NetMessageWriter::beginDescribed( uint8 kind, NetChannelKind channel )
    {
        BitWriter& writer = begin( kind );
        _messageChannel   = channel;
        _state            = State::Typed;
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

    bool NetMessageWriter::sendMessage( INetPeers& peers, int32 connectionID ) const
    {
        return allowsDescriptorChannel() && peers.sendMessage( connectionID, _messageChannel, _writer.getBytes().data(), _writer.getByteCount() );
    }

    bool NetMessageWriter::trySendMessage( INetPeers& peers, int32 connectionID ) const
    {
        return allowsDescriptorChannel() && peers.trySendMessage( connectionID, _messageChannel, _writer.getBytes().data(), _writer.getByteCount() );
    }

    int32 NetMessageWriter::broadcastMessage( INetPeers& peers, int32 exceptID ) const
    {
        return allowsDescriptorChannel() ? peers.broadcast( _messageChannel, _writer.getBytes().data(), _writer.getByteCount(), exceptID ) : 0;
    }

    int32 NetMessageWriter::sendMessageToPeers( INetPeers& peers ) const
    {
        if ( allowsDescriptorChannel() == false )
            return 0;
        if ( peers.isServer() )
            return peers.broadcast( _messageChannel, _writer.getBytes().data(), _writer.getByteCount(), constant::invalid_index::kInt32 );
        return peers.sendMessage( 0, _messageChannel, _writer.getBytes().data(), _writer.getByteCount() ) ? 1 : 0;
    }

    bool NetMessageWriter::sendNotifiedMessage( INetPeers& peers, int32 connectionID, uint32 tag ) const
    {
        if ( allowsDescriptorChannel() == false )
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
        return NetQueuedMessage{ _writer.getBytes(), _messageChannel };
    }

    optional<NetChannelKind> NetMessageWriter::getMessageChannel() const
    {
        if ( _state == State::Typed )
            return _messageChannel;
        return nullopt;
    }

    bool INetMessageHandler::isMessageKindHandled( uint8 kind ) const
    {
        const uint8 rangeBase = getMessageRangeBase();
        if ( NetMessageRange::isInRange( kind, rangeBase ) == false )
            return false;
        return ( ( getMessageKindMask() >> ( kind - rangeBase ) ) & 1u ) != 0;
    }

    NetHandleResult INetMessageHandler::handleMessage( int32 connectionID, const uint8* pData, int32 size, NetChannelKind channel )
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
        , _arrNotificationHandler{}
        , _arrKindDirection{}
        , _kindStats{}
        , _inbound{}
        , _malformedCount{}
        , _unclaimedNotificationCount{ 0 }
        , _mapPeerWireVersion{}
        , _listRefusedConnection{}
        , _sessionSerial{ 0 }
        , _bWireVersionsChanged{ SW_FALSE }
    {
    }

    Subscription NetMessageRouter::registerHandler( INetMessageHandler* pHandler )
    {
        if ( pHandler == nullptr )
        {
            SW_LOG_ERROR_ID( "Net.RouterNullHandler", "NetMessageRouter: registerHandler got a null handler" );
            return Subscription{};
        }
        // 이미 단 처리기를 다시 달면 자기가 맡은 종류와 겹쳐 아래에서 거절된다. 종류를 맡지 않는(사건만 받는) 처리기는 등록마다 항목 하나 · 구독 하나다.
        const uint8  rangeBase    = pHandler->getMessageRangeBase();
        const uint16 kindMask     = pHandler->getMessageKindMask();
        const uint16 notifiedMask = pHandler->getNotifiedKindMask();
        // 방향을 적지 않은 종류는 받지 않는다 — 어느 역할이 받아도 되는지 모르면 반대 방향을 거를 수 없다(폴백 없음).
        const uint16 missingDirectionMask = static_cast<uint16>( kindMask & ~pHandler->getMessageDirectionMask().getDeclaredMask() );
        if ( missingDirectionMask != 0 )
        {
            SW_LOG_ERROR_ID( "Net.RouterKindWithoutDirection", "NetMessageRouter: range 0x%02x kinds (mask 0x%04x) have no declared direction - the handler is refused (declare each message ToServer, ToClient or Both)",
                             static_cast<int32>( rangeBase ), static_cast<int32>( missingDirectionMask ) );
            return Subscription{};
        }
        // 한 영역은 표 하나다 — 영역을 나눠 맡는 처리기(락스텝 · 롤백)는 같은 표의 판을 낸다. 다르면 상대와 견줄 판이 둘이 된다.
        const uint32 wireVersion     = pHandler->getWireVersion();
        const uint32 existingVersion = findWireVersion( rangeBase );
        if ( wireVersion != INetMessageHandler::kNoMessageTable && existingVersion != INetMessageHandler::kNoMessageTable && wireVersion != existingVersion )
        {
            SW_LOG_ERROR_ID( "Net.RouterTableVersionConflict", "NetMessageRouter: range 0x%02x already uses message table version %# - a handler with version %# is refused (one range is one table)",
                             static_cast<int32>( rangeBase ), existingVersion, wireVersion );
            return Subscription{};
        }
        for ( int32 offset = 0; offset < NetMessageRange::kSize; ++offset )
        {
            const int32 kind        = static_cast<int32>( rangeBase ) + offset;
            const bool  bRouterKind = kind == NetMessageRange::kRouterWireVersionKind && ( ( ( kindMask | notifiedMask ) >> offset ) & 1u ) != 0;
            if ( bRouterKind )
            {
                SW_LOG_ERROR_ID( "Net.RouterKindReserved", "NetMessageRouter: message kind 0x%02x belongs to the router (table version notice) - the new handler is refused", kind );
                return Subscription{};
            }
            const bool bClaimed = ( ( kindMask >> offset ) & 1u ) != 0 && kind < kKindCount && _arrKindHandler[kind] != nullptr;
            if ( bClaimed )
            {
                SW_LOG_ERROR_ID( "Net.RouterKindClaimed", "NetMessageRouter: message kind 0x%02x is already claimed - the new handler is refused (give it its own range or a disjoint kind mask)", kind );
                return Subscription{};
            }
            const bool bNotificationClaimed = ( ( notifiedMask >> offset ) & 1u ) != 0 && kind < kKindCount && _arrNotificationHandler[kind] != nullptr;
            if ( bNotificationClaimed )
            {
                SW_LOG_ERROR_ID( "Net.RouterNotificationKindClaimed", "NetMessageRouter: delivery notices of message kind 0x%02x are already claimed - the new handler is refused", kind );
                return Subscription{};
            }
        }
        _listHandler.push_back( pHandler );
        rebuildKindTable();
        _bWireVersionsChanged = SW_TRUE;
        return _subscriptions.issue( pHandler );
    }

    void NetMessageRouter::closeSession()
    {
        _subscriptions.detachAll();
        _listHandler.clear();
        rebuildKindTable();
        _mapPeerWireVersion.clear();
        _listRefusedConnection.clear();
        ++_sessionSerial;
        _bWireVersionsChanged = SW_FALSE;
    }

    void NetMessageRouter::releaseHandler( INetMessageHandler* pHandler )
    {
        VectorUtil::removeFirst( _listHandler, pHandler ); // 주소로만 찾는다 — 역참조하지 않는다
        rebuildKindTable();
    }

    uint32 NetMessageRouter::findWireVersion( uint8 rangeBase ) const
    {
        for ( const INetMessageHandler* pHandler : _listHandler )
        {
            const uint32 wireVersion = pHandler->getWireVersion();
            if ( pHandler->getMessageRangeBase() == rangeBase && wireVersion != INetMessageHandler::kNoMessageTable )
                return wireVersion;
        }
        return INetMessageHandler::kNoMessageTable;
    }

    void NetMessageRouter::sendWireVersions( INetPeers& peers, int32 connectionID )
    {
        vector<PeerWireVersion> listOwn;
        for ( const INetMessageHandler* pHandler : _listHandler )
        {
            const uint32 wireVersion = pHandler->getWireVersion();
            const uint8  rangeBase   = pHandler->getMessageRangeBase();
            const bool   bListed     = VectorUtil::containsIf( listOwn, [rangeBase]( const PeerWireVersion& entry )
                  { return entry._rangeBase == rangeBase; } );
            if ( wireVersion != INetMessageHandler::kNoMessageTable && bListed == false )
                listOwn.push_back( PeerWireVersion{ wireVersion, rangeBase } );
        }
        if ( listOwn.empty() )
            return;
        NetMessageWriter messageWriter;
        BitWriter&       writer = messageWriter.begin( NetMessageRange::kRouterWireVersionKind );
        writer.writeVarUint( listOwn.size() );
        for ( const PeerWireVersion& entry : listOwn )
        {
            writer.writeBits( entry._rangeBase, 8 );
            writer.writeUint32( entry._wireVersion );
        }
        if ( connectionID == constant::invalid_index::kInt32 )
            (void)messageWriter.sendToPeers( peers, NetChannelKind::ReliableOrdered ); // 연결이 없으면 보낼 곳이 없다 — 연결이 열릴 때 다시 보낸다
        else
            (void)messageWriter.send( peers, connectionID, NetChannelKind::ReliableOrdered ); // 닫히는 중이면 보낼 곳이 없다
    }

    void NetMessageRouter::receiveWireVersions( INetPeers& peers, int32 connectionID, const uint8* pData, int32 size )
    {
        // 영역은 모두 해서 kKindCount / kSize 개다 — 그보다 많은 알림은 깨진 것이다(상대가 보낸 수를 믿고 할당하지 않는다).
        constexpr uint64        kMaxRangeCount = static_cast<uint64>( kKindCount / NetMessageRange::kSize );
        BitReader               body( pData + 1, size - 1 );
        const uint64            count = body.readVarUint();
        vector<PeerWireVersion> listPeer;
        for ( uint64 index = 0; body.hasOverflowed() == false && index < count && count <= kMaxRangeCount; ++index )
        {
            PeerWireVersion entry;
            entry._rangeBase   = static_cast<uint8>( body.readBits( 8 ) );
            entry._wireVersion = body.readUint32();
            listPeer.push_back( entry );
        }
        if ( body.hasOverflowed() || count > kMaxRangeCount )
        {
            _malformedCount.add();
            SW_LOG_WARNING_ID( "Net.RouterTableVersionMalformed", "NetMessageRouter: connection %# sent a broken message table version notice - disconnecting it", connectionID );
            peers.disconnect( connectionID );
            _listRefusedConnection.push_back( connectionID );
            return;
        }
        _mapPeerWireVersion[connectionID] = std::move( listPeer );
        (void)disconnectOnWireVersionMismatch( peers, connectionID );
    }

    bool NetMessageRouter::disconnectOnWireVersionMismatch( INetPeers& peers, int32 connectionID )
    {
        const auto iter = _mapPeerWireVersion.find( connectionID );
        if ( iter == _mapPeerWireVersion.end() )
            return false;
        for ( const PeerWireVersion& peer : iter->second )
        {
            const uint32 ownVersion = findWireVersion( peer._rangeBase );
            if ( ownVersion == INetMessageHandler::kNoMessageTable || ownVersion == peer._wireVersion )
                continue;
            SW_LOG_ERROR_ID( "Net.MessageTableVersionMismatch",
                             "NetMessageRouter: connection %# uses message table version %# for range 0x%02x but this build uses %# - disconnecting it (the two builds disagree on that kit's messages)",
                             connectionID, peer._wireVersion, static_cast<int32>( peer._rangeBase ), ownVersion );
            peers.disconnect( connectionID );
            _listRefusedConnection.push_back( connectionID );
            _mapPeerWireVersion.erase( iter );
            return true;
        }
        return false;
    }

    void NetMessageRouter::rebuildKindTable()
    {
        for ( int32 kind = 0; kind < kKindCount; ++kind )
        {
            _arrKindHandler[kind]         = nullptr;
            _arrNotificationHandler[kind] = nullptr;
            _arrKindDirection[kind]       = 0;
        }
        for ( INetMessageHandler* pHandler : _listHandler )
        {
            const uint8                   rangeBase     = pHandler->getMessageRangeBase();
            const uint16                  kindMask      = pHandler->getMessageKindMask();
            const uint16                  notifiedMask  = pHandler->getNotifiedKindMask();
            const NetMessageDirectionMask directionMask = pHandler->getMessageDirectionMask();
            for ( int32 offset = 0; offset < NetMessageRange::kSize; ++offset )
            {
                const int32 kind = static_cast<int32>( rangeBase ) + offset;
                if ( kind >= kKindCount )
                    break;
                const bool bNotificationKind = ( ( notifiedMask >> offset ) & 1u ) != 0;
                if ( bNotificationKind && _arrNotificationHandler[kind] == nullptr )
                    _arrNotificationHandler[kind] = pHandler; // 겹침은 등록 때 거절했다
                if ( ( ( kindMask >> offset ) & 1u ) == 0 )
                    continue;
                if ( _arrKindHandler[kind] != nullptr )
                {
                    SW_LOG_ERROR_ID( "Net.RouterKindClaimedTwice", "NetMessageRouter: message kind 0x%02x is claimed by two handlers - the first one keeps it", kind );
                    continue;
                }
                _arrKindHandler[kind]   = pHandler;
                _arrKindDirection[kind] = static_cast<uint8>( ( ( ( directionMask._toServerMask >> offset ) & 1u ) != 0 ? kDirectionToServerBit : 0u ) |
                                                              ( ( ( directionMask._toClientMask >> offset ) & 1u ) != 0 ? kDirectionToClientBit : 0u ) );
            }
        }
    }

    void NetMessageRouter::dispatchNotification( const NetDeliveryNotification& notification )
    {
        INetMessageHandler* pHandler = _arrNotificationHandler[notification._kind];
        if ( pHandler != nullptr )
        {
            pHandler->onDeliveryNotification( notification );
            return;
        }
        if ( _unclaimedNotificationCount == 0 )
            SW_LOG_ERROR_ID( "Net.RouterUnclaimedNotification", "NetMessageRouter: a delivery notice of message kind 0x%02x has no handler - the sender did not register (getNotifiedKindMask); further ones are only counted",
                             static_cast<int32>( notification._kind ) );
        ++_unclaimedNotificationCount;
    }

    bool NetMessageRouter::isInboundAllowed( uint8 kind, NetPeerRole role ) const
    {
        const uint8 roleBit = role == NetPeerRole::Server ? kDirectionToServerBit : kDirectionToClientBit;
        return ( _arrKindDirection[kind] & roleBit ) != 0;
    }

    void NetMessageRouter::refuseWrongDirection( INetPeers& peers, int32 connectionID, uint8 kind, NetPeerRole role )
    {
        _malformedCount.add();
        SW_LOG_WARNING_ID( "Net.RouterWrongDirection", "NetMessageRouter: connection %# sent message kind 0x%02x that does not travel toward the %# - disconnecting it", connectionID,
                           static_cast<int32>( kind ), role == NetPeerRole::Server ? "server" : "client" );
        peers.disconnect( connectionID );
        _listRefusedConnection.push_back( connectionID );
    }

    NetHandleResult NetMessageRouter::dispatch( NetPeerRole role, const NetMessageContext& context, const uint8* pData, int32 size )
    {
        if ( pData == nullptr || size <= 0 )
            return NetHandleResult::NotMine;
        INetMessageHandler* pHandler = _arrKindHandler[pData[0]];
        if ( pHandler == nullptr )
            return NetHandleResult::NotMine;
        if ( isInboundAllowed( pData[0], role ) == false )
        {
            _malformedCount.add();
            return NetHandleResult::Malformed; // 반대 방향 종류다 — 처리기는 자기 역할이 받는 종류만 본다
        }
        BitReader             body( pData + 1, size - 1 );
        const NetHandleResult result = pHandler->handleNetMessage( NetMessageContext{ pData, size, context._connectionID, context._channel, pData[0] }, body );
        if ( result == NetHandleResult::Malformed )
            _malformedCount.add();
        return result;
    }

    void NetMessageRouter::dispatchEvent( const NetEndpointEvent& event )
    {
        for ( INetMessageHandler* pHandler : _listHandler )
        {
            if ( event._kind == NetEndpointEvent::Kind::Connected )
                pHandler->onConnectionOpened( event._connectionID );
            else
                pHandler->onConnectionClosed( event._connectionID, event._reason );
        }
    }

    int32 NetMessageRouter::poll( INetPeers& peers, vector<NetReceivedMessage>* pOutUnhandled, vector<NetEndpointEvent>* pOutListEvent )
    {
        peers.pollInbound( _inbound );
        _listRefusedConnection.clear();
        const NetPeerRole role = peers.isServer() ? NetPeerRole::Server : NetPeerRole::Client;
        for ( const NetEndpointEvent& event : _inbound._listEvent )
        {
            dispatchEvent( event );
            if ( event._kind == NetEndpointEvent::Kind::Connected )
            {
                _mapPeerWireVersion.erase( event._connectionID ); // 같은 연결 ID 를 다음 연결이 다시 쓴다
                sendWireVersions( peers, event._connectionID );
            }
            else
            {
                _mapPeerWireVersion.erase( event._connectionID );
            }
        }
        // 등록이 바뀌었으면 알리고, 이미 들고 있는 상대 판과 견준다 — 이 쪽이 늦게 등록한 영역도 메시지를 넘기기 전에 걸러진다.
        if ( _bWireVersionsChanged != SW_FALSE )
        {
            _bWireVersionsChanged = SW_FALSE;
            sendWireVersions( peers, constant::invalid_index::kInt32 );
            vector<int32> listConnection;
            for ( const auto& entry : _mapPeerWireVersion )
            {
                listConnection.push_back( entry.first );
            }
            for ( const int32 connectionID : listConnection )
            {
                (void)disconnectOnWireVersionMismatch( peers, connectionID );
            }
        }
        if ( pOutListEvent != nullptr )
            pOutListEvent->insert( pOutListEvent->end(), _inbound._listEvent.begin(), _inbound._listEvent.end() );
        for ( const NetDeliveryNotification& notification : _inbound._listDeliveryNotification )
        {
            dispatchNotification( notification );
        }
        for ( const NetInboundMessage& message : _inbound._listMessage )
        {
            const uint8* pData = _inbound._bytes.data() + message._offset;
            _kindStats.add( pData, message._size );
            if ( VectorUtil::contains( _listRefusedConnection, message._connectionID ) )
                continue; // 판이 달라 끊은 연결이다 — 다른 표로 읽지 않는다
            if ( message._size > 0 && pData[0] == NetMessageRange::kRouterWireVersionKind )
            {
                receiveWireVersions( peers, message._connectionID, pData, message._size );
                continue;
            }
            const bool bWrongDirection = message._size > 0 && _arrKindHandler[pData[0]] != nullptr && isInboundAllowed( pData[0], role ) == false;
            if ( bWrongDirection )
            {
                refuseWrongDirection( peers, message._connectionID, pData[0], role );
                continue;
            }
            if ( dispatch( role, NetMessageContext{ pData, message._size, message._connectionID, message._channel, message._size > 0 ? pData[0] : uint8{ 0 } }, pData, message._size ) !=
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
