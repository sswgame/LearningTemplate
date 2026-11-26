#include "pch.h"

#include "Core/Network/Message/NetMessage.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Message/NetMessageTable.h"
#include "Core/Time/MonotonicClock.h"

#include <limits>

namespace sw
{
    namespace
    {
        struct NetMessageInternal
        {
            static int64 nowMilliseconds() { return MonotonicClock::nowNanoseconds() / constant::time::kNanosecondsPerMillisecond; }
        };
    } // namespace

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
        const int32 headSize = isWorldScoped() ? 2 : 1; // 종류 · 월드 세대(직접 전달은 세대를 보지 않는다)
        if ( size < headSize || connectionID < 0 )
            return NetHandleResult::Malformed;
        BitReader body( pData + headSize, size - headSize );
        return handleNetMessage( NetMessageContext{ pData, size, connectionID, channel, pData[0] }, body );
    }

    NetMessageRouter::NetMessageRouter()
        : _listHandler{}
        , _listNewHandler{}
        , _arrKindHandler{}
        , _arrNotificationHandler{}
        , _arrKindDirection{}
        , _arrKindMaxPerPoll{}
        , _arrKindLimitIndex{}
        , _listPollCount{}
        , _limitedKindCount{ 0 }
        , _kindStats{}
        , _inbound{}
        , _malformedCount{}
        , _unclaimedNotificationCount{ 0 }
        , _mapPeerWireVersion{}
        , _mapPresenceCount{}
        , _listRefusedConnection{}
        , _listHeld{}
        , _pClock{ &NetMessageInternal::nowMilliseconds }
        , _sessionSerial{ 0 }
        , _worldClock{}
        , _bindSerial{ 0 }
        , _sentClock{ 0 }
        , _worldSceneID{ 0 }
        , _localSceneID{}
        , _announcedGeneration{ 0 }
        , _worldGeneration{ 0 }
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
        // 서버로 갈 수 있는 종류는 상한이 있어야 한다 — 없으면 한 클라이언트가 틱 하나에 시킬 수 있는 일이 무한이다(폴백 없음).
        const uint16 toServerMask = static_cast<uint16>( kindMask & pHandler->getMessageDirectionMask()._toServerMask );
        for ( int32 offset = 0; offset < NetMessageRange::kSize; ++offset )
        {
            if ( ( ( toServerMask >> offset ) & 1u ) == 0 || pHandler->getMaxPerPoll( static_cast<uint8>( rangeBase + offset ) ) != 0 )
                continue;
            SW_LOG_ERROR_ID( "Net.RouterKindWithoutLimit", "NetMessageRouter: message kind 0x%02x travels to the server without a request limit - the handler is refused (declare it with NetLimitedMessageDef)",
                             static_cast<int32>( rangeBase ) + offset );
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
            const bool  bRouterKind = NetMessageRange::isRouterKind( kind ) && ( ( ( kindMask | notifiedMask ) >> offset ) & 1u ) != 0;
            if ( bRouterKind )
            {
                SW_LOG_ERROR_ID( "Net.RouterKindReserved", "NetMessageRouter: message kind 0x%02x belongs to the router - the new handler is refused", kind );
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
        _listNewHandler.push_back( pHandler );
        rebuildKindTable();
        _bWireVersionsChanged = SW_TRUE;
        return _subscriptions.issue( pHandler );
    }

    void NetMessageRouter::closeSession()
    {
        _subscriptions.detachAll();
        _listHandler.clear();
        _listNewHandler.clear();
        rebuildKindTable();
        _mapPeerWireVersion.clear();
        _mapPresenceCount.clear();
        _listRefusedConnection.clear();
        _listHeld.clear();
        ++_sessionSerial;
        _worldSceneID         = 0;
        _localSceneID         = nullopt;
        _announcedGeneration  = 0;
        _worldGeneration      = 0;
        _sentClock            = 0;
        _bWireVersionsChanged = SW_FALSE;
        _worldClock.reset( NetClockSide::Replica );
    }

    void NetMessageRouter::releaseHandler( INetMessageHandler* pHandler )
    {
        VectorUtil::removeFirst( _listHandler, pHandler ); // 주소로만 찾는다 — 역참조하지 않는다
        VectorUtil::removeFirst( _listNewHandler, pHandler );
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
            peers.disconnect( connectionID, NetLinger::none() );
            _listRefusedConnection.push_back( connectionID );
            return;
        }
        _mapPeerWireVersion[connectionID] = std::move( listPeer );
        (void)disconnectOnWireVersionMismatch( peers, connectionID );
    }

    NetMessageRouter::WorldOrder NetMessageRouter::compareWorld( uint8 generation, uint8 localGeneration )
    {
        if ( generation == localGeneration )
            return WorldOrder::Same;
        if ( localGeneration == 0 )
            return WorldOrder::Later; // 이쪽에는 아직 월드가 없다
        if ( generation == 0 )
            return WorldOrder::Earlier; // 보낸 쪽이 월드를 모른다
        constexpr int32 kCycle = std::numeric_limits<uint8>::max();
        const int32     ahead  = ( static_cast<int32>( generation ) - static_cast<int32>( localGeneration ) + kCycle ) % kCycle;
        return ahead <= kCycle / 2 ? WorldOrder::Later : WorldOrder::Earlier;
    }

    bool NetMessageRouter::isFromOtherWorld( const INetMessageHandler& handler, const uint8* pData, int32 size ) const
    {
        return handler.isWorldScoped() && size > 1 && pData[1] != _worldGeneration;
    }

    bool NetMessageRouter::isLaterWorld( uint8 generation ) const
    {
        return compareWorld( generation, _worldGeneration ) == WorldOrder::Later;
    }

    void NetMessageRouter::setWorldGeneration( INetPeers& peers, uint8 generation )
    {
        _worldGeneration = generation;
        _sentClock       = 0;
        _worldClock.reset( peers.isServer() ? NetClockSide::Authority : NetClockSide::Replica );
        peers.setWorldGeneration( generation ); // 이 뒤에 보내는 월드 범위 메시지가 싣는다
    }

    void NetMessageRouter::openWorld( INetPeers& peers, uint64 sceneID )
    {
        SW_ASSERT( peers.isServer() ); // 세대는 서버가 정한다 — 클라이언트는 받기만 한다
        ++_bindSerial;
        vector<int32> listConnection;
        peers.collectInWorld( listConnection );
        for ( const int32 connectionID : listConnection )
        {
            removeFromWorld( peers, connectionID ); // 앞 월드다 — 클라이언트가 새 월드에 들어오면 다시 알린다
        }
        _worldSceneID        = sceneID;
        _announcedGeneration = _announcedGeneration == std::numeric_limits<uint8>::max() ? uint8{ 1 } : static_cast<uint8>( _announcedGeneration + 1u );
        setWorldGeneration( peers, _announcedGeneration );
        sendWorldGeneration( peers, constant::invalid_index::kInt32 );
    }

    void NetMessageRouter::setLocalScene( INetPeers& peers, uint64 sceneID )
    {
        SW_ASSERT( peers.isServer() == false );
        ++_bindSerial;
        _localSceneID = sceneID;
        if ( _worldGeneration != 0 && _localSceneID == _worldSceneID )
            sendPresence( peers ); // 같은 씬을 이쪽만 다시 세웠다 — 서버가 상태를 처음부터 보낸다
        updateClientWorld( peers );
    }

    void NetMessageRouter::updateClientWorld( INetPeers& peers )
    {
        const uint8 generation = _localSceneID == _worldSceneID ? _announcedGeneration : uint8{ 0 }; // 아직 씬을 세우지 않았으면(빈 값) 어느 월드에도 들지 않는다
        if ( generation == _worldGeneration )
            return;
        setWorldGeneration( peers, generation );
        sendPresence( peers );
        if ( generation == 0 )
            return;
        for ( size_t handlerIndex = 0; handlerIndex < _listHandler.size(); ++handlerIndex )
        {
            _listHandler[handlerIndex]->onWorldOpened( generation );
        }
    }

    void NetMessageRouter::announceEnteredPeers( INetPeers& peers )
    {
        if ( _listNewHandler.empty() )
            return;
        vector<INetMessageHandler*> listHandler;
        listHandler.swap( _listNewHandler );
        vector<int32> listConnection;
        peers.collectInWorld( listConnection );
        for ( INetMessageHandler* pHandler : listHandler )
        {
            for ( const int32 connectionID : listConnection )
            {
                if ( VectorUtil::contains( _listHandler, pHandler ) ) // 앞 처리기가 알림 안에서 구독을 풀 수 있다
                    pHandler->onPeerEnteredWorld( connectionID );
            }
        }
    }

    void NetMessageRouter::removeFromWorld( INetPeers& peers, int32 connectionID )
    {
        if ( peers.isServer() == false || peers.isInWorld( connectionID ) == false )
            return;
        peers.setInWorld( connectionID, false );
        for ( size_t handlerIndex = 0; handlerIndex < _listHandler.size(); ++handlerIndex )
        {
            _listHandler[handlerIndex]->onPeerLeftWorld( connectionID );
        }
    }

    void NetMessageRouter::sendPresence( INetPeers& peers )
    {
        if ( peers.getConnectedCount() == 0 )
            return; // 붙으면 서버의 알림을 받고 다시 정한다
        NetMessageWriter messageWriter;
        messageWriter.begin( NetMessageRange::kRouterPresenceKind ).writeBits( _worldGeneration, 8 );
        (void)messageWriter.send( peers, 0, NetChannelKind::ReliableOrdered ); // 닫히는 중이면 알릴 곳이 없다
    }

    void NetMessageRouter::receivePresence( INetPeers& peers, int32 connectionID, const uint8* pData, int32 size )
    {
        if ( peers.isServer() == false )
        {
            refuseWrongDirection( peers, connectionID, pData[0], NetPeerRole::Client );
            return;
        }
        BitReader   body( pData + 1, size - 1 );
        const uint8 generation = static_cast<uint8>( body.readBits( 8 ) );
        if ( body.hasOverflowed() )
        {
            refuseMalformed( peers, connectionID, pData[0] );
            return;
        }
        PresenceCount& presence = _mapPresenceCount[connectionID];
        const int64    now      = _pClock();
        if ( now - presence._windowStart >= kPresenceWindowMilliseconds )
            presence = PresenceCount{ now, 0 };
        if ( ++presence._count > kMaxPresencePerWindow )
        {
            refuseMalformed( peers, connectionID, pData[0] );
            return;
        }
        removeFromWorld( peers, connectionID ); // 다시 들어온 것도 처음부터다
        if ( generation == 0 || generation != _worldGeneration )
            return; // 나갔거나, 서버가 그 사이 새 월드를 열었다(새 세대의 알림을 받고 다시 들어온다)
        peers.setInWorld( connectionID, true );
        for ( size_t handlerIndex = 0; handlerIndex < _listHandler.size(); ++handlerIndex )
        {
            _listHandler[handlerIndex]->onPeerEnteredWorld( connectionID );
        }
    }

    void NetMessageRouter::sendWorldGeneration( INetPeers& peers, int32 connectionID )
    {
        NetMessageWriter messageWriter;
        BitWriter&       body = messageWriter.begin( NetMessageRange::kRouterWorldKind );
        body.writeBits( _announcedGeneration, 8 );
        body.writeVarUint( _worldSceneID );
        if ( connectionID == constant::invalid_index::kInt32 )
            (void)messageWriter.sendToPeers( peers, NetChannelKind::ReliableOrdered ); // 연결이 없으면 보낼 곳이 없다 — 연결이 열릴 때 보낸다
        else
            (void)messageWriter.send( peers, connectionID, NetChannelKind::ReliableOrdered ); // 닫히는 중이면 보낼 곳이 없다
    }

    void NetMessageRouter::receiveWorldGeneration( INetPeers& peers, int32 connectionID, const uint8* pData, int32 size )
    {
        if ( peers.isServer() )
        {
            refuseWrongDirection( peers, connectionID, pData[0], NetPeerRole::Server );
            return;
        }
        BitReader    body( pData + 1, size - 1 );
        const uint8  generation = static_cast<uint8>( body.readBits( 8 ) );
        const uint64 sceneID    = body.readVarUint();
        if ( body.hasOverflowed() || generation == 0 )
        {
            refuseMalformed( peers, connectionID, pData[0] );
            return;
        }
        _announcedGeneration = generation;
        _worldSceneID        = sceneID;
        updateClientWorld( peers );
    }

    void NetMessageRouter::refuseMalformed( INetPeers& peers, int32 connectionID, uint8 kind )
    {
        _malformedCount.add();
        SW_LOG_WARNING_ID( "Net.RouterMessageMalformed", "NetMessageRouter: connection %# sent a broken router message of kind 0x%02x - disconnecting it", connectionID, static_cast<int32>( kind ) );
        peers.disconnect( connectionID, NetLinger::none() );
        _listRefusedConnection.push_back( connectionID );
    }

    void NetMessageRouter::advanceWorldClock( INetPeers& peers, float32 deltaTime )
    {
        _worldClock.advance( deltaTime );
        const uint32 sendIndex = _worldClock.getWholeTick( kClockSendTicks );
        const bool   bSend     = peers.isServer() && _worldGeneration != 0 && sendIndex != _sentClock;
        if ( bSend == false )
            return;
        _sentClock = sendIndex;
        if ( peers.getConnectedCount() == 0 )
            return;
        NetMessageWriter messageWriter;
        BitWriter&       body = messageWriter.begin( NetMessageRange::kRouterClockKind );
        body.writeBits( _worldGeneration, 8 );
        body.writeVarUint( _worldClock.getWholeTick() );
        (void)messageWriter.broadcast( peers, NetChannelKind::Unreliable ); // 잃어도 다음 것이 더 새 시각이다
    }

    void NetMessageRouter::receiveWorldClock( INetPeers& peers, int32 connectionID, const uint8* pData, int32 size )
    {
        if ( peers.isServer() )
        {
            refuseWrongDirection( peers, connectionID, pData[0], NetPeerRole::Server );
            return;
        }
        BitReader    body( pData + 1, size - 1 );
        const uint8  generation = static_cast<uint8>( body.readBits( 8 ) );
        const uint32 serverTick = body.readVarUint32();
        if ( body.hasOverflowed() || generation == 0 )
        {
            refuseMalformed( peers, connectionID, pData[0] );
            return;
        }
        if ( generation == _worldGeneration )
            _worldClock.observe( serverTick ); // 다른 월드의 시각은 이 수열이 아니다
    }

    void NetMessageRouter::setClock( ClockFunction pClock )
    {
        SW_ASSERT( pClock != nullptr );
        _pClock = pClock;
    }

    bool NetMessageRouter::isBlockedByHeld( int32 connectionID, uint8 rangeBase, uint64 key ) const
    {
        for ( const HeldMessage& held : _listHeld )
        {
            const bool bSameStream = held._connectionID == connectionID && held._rangeBase == rangeBase;
            if ( bSameStream && ( held._bMissingHandler == SW_TRUE || held._key == key ) )
                return true;
        }
        return false;
    }

    bool NetMessageRouter::isBlockedByMissingHandler( int32 connectionID, uint8 rangeBase ) const
    {
        for ( const HeldMessage& held : _listHeld )
        {
            if ( held._connectionID == connectionID && held._rangeBase == rangeBase && held._bMissingHandler == SW_TRUE )
                return true;
        }
        return false;
    }

    void NetMessageRouter::holdMessage( INetPeers& peers, const NetInboundMessage& message, const uint8* pData, uint64 key, int64 holdMilliseconds, bool bMissingHandler )
    {
        HeldMessage held;
        held._buffer.assign( pData, pData + message._size );
        held._key             = key;
        held._deadline        = _pClock() + holdMilliseconds;
        held._connectionID    = message._connectionID;
        held._channel         = message._channel;
        held._bMissingHandler = bMissingHandler ? SW_TRUE : SW_FALSE;
        held._rangeBase       = static_cast<uint8>( pData[0] - pData[0] % NetMessageRange::kSize );
        int32 heldCount       = 0;
        int32 heldBytes       = message._size;
        for ( const HeldMessage& other : _listHeld )
        {
            if ( other._connectionID != message._connectionID )
                continue;
            ++heldCount;
            heldBytes += static_cast<int32>( other._buffer.size() );
        }
        if ( heldCount >= kMaxHeldMessages || heldBytes > kMaxHeldBytes )
            reportDeliveryFailed( peers, held, NetDeliveryFailure::Overflow );
        else
            _listHeld.push_back( std::move( held ) );
    }

    void NetMessageRouter::reportDeliveryFailed( INetPeers& peers, const HeldMessage& held, NetDeliveryFailure reason )
    {
        _deliveryFailedCount.add();
        if ( held._bMissingHandler == SW_TRUE )
        {
            SW_LOG_WARNING_ONCE_ID( "Net.RouterNoHandlerInTime", "NetMessageRouter: a reliable message of kind 0x%02x from connection %# found no handler in time - dropped (further ones are only counted)",
                                    static_cast<int32>( held._buffer[0] ), held._connectionID );
            return; // 처리기를 달지 않는 쪽(키트가 없는 씬)일 수 있다 — 보류를 선언했는지 모른다
        }
        SW_LOG_WARNING_ID( "Net.RouterDeliveryFailed", "NetMessageRouter: a held message of kind 0x%02x (key %#) from connection %# was not delivered (reason %#)", static_cast<int32>( held._buffer[0] ), held._key,
                           held._connectionID, static_cast<int32>( reason ) );
        NetMessageWriter messageWriter;
        BitWriter&       writer = messageWriter.begin( NetMessageRange::kRouterDeliveryFailedKind );
        writer.writeBits( held._buffer[0], 8 );
        writer.writeBits( static_cast<uint32>( reason ), 2 );
        writer.writeVarUint( held._key );
        (void)messageWriter.send( peers, held._connectionID, NetChannelKind::ReliableOrdered ); // 닫히는 중이면 알릴 곳이 없다
    }

    void NetMessageRouter::receiveDeliveryFailed( INetPeers& peers, int32 connectionID, const uint8* pData, int32 size )
    {
        BitReader    body( pData + 1, size - 1 );
        const uint8  kind   = static_cast<uint8>( body.readBits( 8 ) );
        const uint32 reason = body.readBits( 2 );
        const uint64 key    = body.readVarUint();
        if ( body.hasOverflowed() || reason > static_cast<uint32>( NetDeliveryFailure::WorldChanged ) )
        {
            _malformedCount.add();
            SW_LOG_WARNING_ID( "Net.RouterDeliveryFailedMalformed", "NetMessageRouter: connection %# sent a broken undelivered notice - disconnecting it", connectionID );
            peers.disconnect( connectionID, NetLinger::none() );
            _listRefusedConnection.push_back( connectionID );
            return;
        }
        for ( size_t handlerIndex = 0; handlerIndex < _listHandler.size(); ++handlerIndex )
        {
            INetMessageHandler* const pHandler = _listHandler[handlerIndex];
            if ( NetMessageRange::isInRange( kind, pHandler->getMessageRangeBase() ) )
                pHandler->onMessageDeliveryFailed( connectionID, kind, key, static_cast<NetDeliveryFailure>( reason ) );
        }
    }

    NetHandleResult NetMessageRouter::deliverOrHold( INetPeers& peers, NetPeerRole role, const NetInboundMessage& message, const uint8* pData, bool bHoldMissingHandler )
    {
        const NetMessageContext   context{ pData, message._size, message._connectionID, message._channel, message._size > 0 ? pData[0] : uint8{ 0 } };
        const bool                bReliable = message._channel == NetChannelKind::ReliableOrdered || message._channel == NetChannelKind::ReliableUnordered;
        INetMessageHandler* const pHandler  = message._size > 0 ? _arrKindHandler[pData[0]] : nullptr;
        if ( pHandler == nullptr )
        {
            // 엔진 영역(부하 메시지 등)은 처리기 없이 버리는 종류가 있다. 비신뢰는 늦으면 쓸모가 없다.
            const bool bHold = bHoldMissingHandler && bReliable && message._size > 0 && pData[0] >= NetMessageRange::kFramework;
            if ( bHold == false )
                return NetHandleResult::NotMine;
            holdMessage( peers, message, pData, 0, kMissingHandlerHoldMilliseconds, true );
            return NetHandleResult::NotReady;
        }
        const uint8 rangeBase = pHandler->getMessageRangeBase();
        if ( isFromOtherWorld( *pHandler, pData, message._size ) )
        {
            _otherWorldCount.add(); // 월드가 바뀌는 사이 날아오던 것이다 — 번호가 같은 다른 것에 닿지 않는다. 들어오면 서버가 처음부터 보낸다
            return NetHandleResult::Handled;
        }
        if ( bReliable && isBlockedByMissingHandler( message._connectionID, rangeBase ) )
        {
            holdMessage( peers, message, pData, 0, kMissingHandlerHoldMilliseconds, true ); // 처리기를 기다리던 앞 메시지를 앞지르지 않는다
            return NetHandleResult::NotReady;
        }
        const int32  headSize         = pHandler->isWorldScoped() ? 2 : 1;
        const uint32 holdMilliseconds = bReliable ? pHandler->getHoldLimit( pData[0] ) : 0u;
        const uint64 key              = holdMilliseconds != 0u && message._size >= headSize ? pHandler->readMessageHoldKey( pData[0], BitReader( pData + headSize, message._size - headSize ) ) : 0u;
        if ( holdMilliseconds != 0u && isBlockedByHeld( message._connectionID, rangeBase, key ) )
        {
            holdMessage( peers, message, pData, key, static_cast<int64>( holdMilliseconds ), false ); // 같은 대상의 앞 메시지가 기다린다 — 앞지르지 않는다
            return NetHandleResult::NotReady;
        }
        const NetHandleResult result = dispatch( role, context, pData, message._size );
        if ( result != NetHandleResult::NotReady )
            return result;
        if ( holdMilliseconds != 0u )
            holdMessage( peers, message, pData, key, static_cast<int64>( holdMilliseconds ), false );
        else if ( bReliable )
            SW_LOG_ERROR_ID( "Net.RouterHoldNotDeclared", "NetMessageRouter: the handler of kind 0x%02x is not ready but its message does not declare a hold (NetHeldMessageDef) - dropped", static_cast<int32>( pData[0] ) );
        return result;
    }

    void NetMessageRouter::retryHeldMessages( INetPeers& peers, NetPeerRole role )
    {
        if ( _listHeld.empty() )
            return;
        // 줄을 떼어 앞에서부터 다시 준다 — 아직이면 `deliverOrHold` 가 새 시한 없이 다시 세워야 하므로 시한은 여기서 지킨다.
        vector<HeldMessage> listRetry;
        listRetry.swap( _listHeld );
        const int64 now = _pClock();
        for ( HeldMessage& held : listRetry )
        {
            INetMessageHandler* const pHandler = _arrKindHandler[held._buffer[0]];
            const bool                bExpired = now >= held._deadline;
            NetHandleResult           result   = NetHandleResult::NotReady;
            if ( pHandler != nullptr )
            {
                const int32 size     = static_cast<int32>( held._buffer.size() );
                const int32 headSize = pHandler->isWorldScoped() ? 2 : 1;
                if ( isFromOtherWorld( *pHandler, held._buffer.data(), size ) )
                {
                    reportDeliveryFailed( peers, held, NetDeliveryFailure::WorldChanged ); // 기다리는 사이 월드가 바뀌었다
                    continue;
                }
                if ( held._bMissingHandler == SW_TRUE )
                {
                    if ( isInboundAllowed( held._buffer[0], role ) == false )
                    {
                        refuseWrongDirection( peers, held._connectionID, held._buffer[0], role );
                        continue;
                    }
                    const bool bEarlierWaits = isBlockedByMissingHandler( held._connectionID, held._rangeBase );
                    if ( bEarlierWaits == false )
                        held._bMissingHandler = SW_FALSE;
                    if ( bEarlierWaits == false && size >= headSize && pHandler->getHoldLimit( held._buffer[0] ) != 0u )
                        held._key = pHandler->readMessageHoldKey( held._buffer[0], BitReader( held._buffer.data() + headSize, size - headSize ) );
                }
                const bool bBlocked = held._bMissingHandler == SW_TRUE || VectorUtil::contains( _listRefusedConnection, held._connectionID ) ||
                                      ( pHandler->getHoldLimit( held._buffer[0] ) != 0u && isBlockedByHeld( held._connectionID, held._rangeBase, held._key ) );
                if ( bBlocked == false )
                    result = dispatch( role, NetMessageContext{ held._buffer.data(), size, held._connectionID, held._channel, held._buffer[0] }, held._buffer.data(), size );
            }
            if ( result != NetHandleResult::NotReady )
                continue;
            if ( bExpired )
                reportDeliveryFailed( peers, held, NetDeliveryFailure::TimedOut );
            else
                _listHeld.push_back( std::move( held ) );
        }
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
            peers.disconnect( connectionID, NetLinger::none() );
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
            _arrKindMaxPerPoll[kind]      = 0;
            _arrKindLimitIndex[kind]      = 0;
        }
        _limitedKindCount = 0;
        _listPollCount.clear();
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
                const uint32 maxPerPoll = pHandler->getMaxPerPoll( static_cast<uint8>( kind ) );
                if ( maxPerPoll == 0 )
                    continue;
                _arrKindMaxPerPoll[kind] = static_cast<uint16>( MathUtil::min( maxPerPoll, uint32{ 0xFFFF } ) );
                _arrKindLimitIndex[kind] = static_cast<uint8>( _limitedKindCount++ );
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
        peers.disconnect( connectionID, NetLinger::none() );
        _listRefusedConnection.push_back( connectionID );
    }

    // 신뢰 메시지를 넘기면 연결을 끊는다 — 정상 클라이언트는 확인받지 못한 신뢰 메시지를 창까지만 보내므로 상한이 창 이상이면 닿지 않는다
    static_assert( NetRequestLimit::kCommand >= static_cast<uint32>( NetConnection::kReliableWindow ) && NetRequestLimit::kPerTick >= NetRequestLimit::kCommand &&
                   NetRequestLimit::kBurst >= NetRequestLimit::kPerTick );

    bool NetMessageRouter::countOverLimit( int32 connectionID, uint8 kind )
    {
        if ( _arrKindMaxPerPoll[kind] == 0 || connectionID < 0 )
            return false;
        const size_t index = static_cast<size_t>( connectionID ) * static_cast<size_t>( _limitedKindCount ) + _arrKindLimitIndex[kind];
        if ( index >= _listPollCount.size() )
            _listPollCount.resize( ( static_cast<size_t>( connectionID ) + 1 ) * static_cast<size_t>( _limitedKindCount ), uint16{ 0 } );
        if ( _listPollCount[index] >= _arrKindMaxPerPoll[kind] )
            return true;
        ++_listPollCount[index];
        return false;
    }

    void NetMessageRouter::refuseOverLimit( INetPeers& peers, int32 connectionID, uint8 kind, NetChannelKind channel )
    {
        _overLimitCount.add();
        const bool bReliable = channel == NetChannelKind::ReliableOrdered || channel == NetChannelKind::ReliableUnordered;
        if ( bReliable == false )
            return; // 잃어도 되는 것이다 — 로그도 남기지 않는다(요청으로 로그를 채울 수 있다)
        SW_LOG_WARNING_ID( "Net.RouterRequestLimit", "NetMessageRouter: connection %# sent more than %# reliable messages of kind 0x%02x in one poll - disconnecting it", connectionID,
                           static_cast<int32>( _arrKindMaxPerPoll[kind] ), static_cast<int32>( kind ) );
        peers.disconnect( connectionID, NetLinger::none() );
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
        const int32 headSize = pHandler->isWorldScoped() ? 2 : 1; // 종류 · 월드 세대
        if ( size < headSize || context._connectionID < 0 )
        {
            _malformedCount.add();
            return NetHandleResult::Malformed;
        }
        BitReader             body( pData + headSize, size - headSize );
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
        std::fill( _listPollCount.begin(), _listPollCount.end(), uint16{ 0 } );
        const NetPeerRole role = peers.isServer() ? NetPeerRole::Server : NetPeerRole::Client;
        announceEnteredPeers( peers );
        for ( const NetEndpointEvent& event : _inbound._listEvent )
        {
            _mapPresenceCount.erase( event._connectionID );
            removeFromWorld( peers, event._connectionID ); // 새 연결도 닫힌 연결도 월드에 없다
            if ( role == NetPeerRole::Client )
            {
                // 앞 서버의 세대 · 씬을 이어 보지 않는다(새 서버는 처음부터 센다).
                _announcedGeneration = 0;
                _worldSceneID        = 0;
                setWorldGeneration( peers, 0 );
            }
            // 월드 세대는 처리기가 열림 사건에서 보내는 첫 메시지보다 먼저 닿는다(같은 신뢰 순서 채널).
            const bool bTellWorld = event._kind == NetEndpointEvent::Kind::Connected && role == NetPeerRole::Server && _announcedGeneration != 0;
            if ( bTellWorld )
                sendWorldGeneration( peers, event._connectionID );
            dispatchEvent( event );
            if ( event._kind == NetEndpointEvent::Kind::Connected )
            {
                _mapPeerWireVersion.erase( event._connectionID ); // 같은 연결 ID 를 다음 연결이 다시 쓴다
                sendWireVersions( peers, event._connectionID );
            }
            else
            {
                _mapPeerWireVersion.erase( event._connectionID );
                VectorUtil::removeIf( _listHeld, [&event]( const HeldMessage& held )
                { return held._connectionID == event._connectionID; } ); // 같은 번호의 다음 연결이 물려받지 않는다
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
        retryHeldMessages( peers, role ); // 앞서 온 것이 새 메시지보다 먼저다
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
            if ( message._size > 0 && pData[0] == NetMessageRange::kRouterWorldKind )
            {
                receiveWorldGeneration( peers, message._connectionID, pData, message._size );
                continue;
            }
            if ( message._size > 0 && pData[0] == NetMessageRange::kRouterPresenceKind )
            {
                receivePresence( peers, message._connectionID, pData, message._size );
                continue;
            }
            if ( message._size > 0 && pData[0] == NetMessageRange::kRouterClockKind )
            {
                receiveWorldClock( peers, message._connectionID, pData, message._size );
                continue;
            }
            if ( message._size > 0 && pData[0] == NetMessageRange::kRouterDeliveryFailedKind )
            {
                receiveDeliveryFailed( peers, message._connectionID, pData, message._size );
                continue;
            }
            const bool bWrongDirection = message._size > 0 && _arrKindHandler[pData[0]] != nullptr && isInboundAllowed( pData[0], role ) == false;
            if ( bWrongDirection )
            {
                refuseWrongDirection( peers, message._connectionID, pData[0], role );
                continue;
            }
            const bool bOverLimit = role == NetPeerRole::Server && message._size > 0 && countOverLimit( message._connectionID, pData[0] );
            if ( bOverLimit )
            {
                refuseOverLimit( peers, message._connectionID, pData[0], message._channel );
                continue;
            }
            // 처리기 없는 메시지를 부르는 쪽이 받아 가면(테스트 · 하니스) 보류하지 않는다.
            if ( deliverOrHold( peers, role, message, pData, pOutUnhandled == nullptr ) != NetHandleResult::NotMine || pOutUnhandled == nullptr )
                continue;
            NetReceivedMessage unhandled;
            unhandled._buffer.assign( pData, pData + message._size );
            unhandled._connectionID = message._connectionID;
            unhandled._channel      = message._channel;
            pOutUnhandled->push_back( std::move( unhandled ) );
        }
        for ( size_t handlerIndex = 0; handlerIndex < _listHandler.size(); ++handlerIndex ) // 처리기가 그 안에서 구독을 풀 수 있다
        {
            _listHandler[handlerIndex]->onPollFinished();
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
