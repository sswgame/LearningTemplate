#include "pch.h"

#include "Core/Network/NetHost.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/NetTransport.h"

#include <chrono>
#include <cstring>
#include <mutex>
#include <random>
#include <thread>

namespace sw
{
    namespace
    {
        struct NetHostInternal
        {
            static constexpr int32 kTypeBits   = 3;
            static constexpr int32 kHeaderSize = 8; ///< 프로토콜 id 4 + 체크섬 4

            /** @brief FNV-1a 32 — 프로토콜 id 를 먼저 섞어 다른 게임의 패킷은 체크섬부터 틀린다. */
            static uint32 computeChecksum( uint32 protocolId, const uint8* pData, int32 size )
            {
                uint32 hash = 2166136261u;
                for ( int32 shift = 0; shift < 32; shift += 8 )
                    hash = ( hash ^ ( ( protocolId >> shift ) & 0xFFu ) ) * 16777619u;
                for ( int32 index = 0; index < size; ++index )
                    hash = ( hash ^ pData[index] ) * 16777619u;
                return hash;
            }

            static void writeUint32( vector<uint8>& outBytes, size_t offset, uint32 value )
            {
                for ( int32 index = 0; index < 4; ++index )
                    outBytes[offset + static_cast<size_t>( index )] = static_cast<uint8>( value >> ( index * 8 ) );
            }

            static uint64 makeAddressKey( const NetAddress& address ) { return ( static_cast<uint64>( address._ipv4 ) << 16 ) | address._port; }

            /** @brief splitmix64 의 마무리 섞기입니다. */
            static uint64 mix64( uint64 value )
            {
                value = ( value ^ ( value >> 30 ) ) * 0xBF58476D1CE4E5B9ull;
                value = ( value ^ ( value >> 27 ) ) * 0x94D049BB133111EBull;
                return value ^ ( value >> 31 );
            }

            /** @brief 운영체제 난수 64 비트 — 도전 값이 다른 실행 · 다른 호스트와 겹치지 않고 미리 알 수 없게. */
            static uint64 makeRandomSeed()
            {
                std::random_device randomDevice;
                const uint64       seed = ( static_cast<uint64>( randomDevice() ) << 32 ) | static_cast<uint64>( randomDevice() );
                return seed != 0 ? seed : 0x9E3779B97F4A7C15ull;
            }

            static uint32 readUint32( const uint8* pData )
            {
                return static_cast<uint32>( pData[0] ) | ( static_cast<uint32>( pData[1] ) << 8 ) | ( static_cast<uint32>( pData[2] ) << 16 ) |
                       ( static_cast<uint32>( pData[3] ) << 24 );
            }

            /** @brief 거절 패킷이 싣는 이유입니다. 모르는 값은 Rejected 로 읽는다(다른 판이 이유를 늘려도 클라이언트는 끊긴다). */
            static NetDisconnectReason readDeniedReason( uint64 value )
            {
                switch ( static_cast<NetDisconnectReason>( value & 0xFFu ) )
                {
                    case NetDisconnectReason::ServerFull:
                        return NetDisconnectReason::ServerFull;
                    case NetDisconnectReason::VersionMismatch:
                        return NetDisconnectReason::VersionMismatch;
                    case NetDisconnectReason::None:
                    case NetDisconnectReason::Requested:
                    case NetDisconnectReason::Remote:
                    case NetDisconnectReason::Timeout:
                    case NetDisconnectReason::Rejected:
                        break;
                }
                return NetDisconnectReason::Rejected;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    NetHost::NetHost()
        : _mutex{}
        , _listSlot{}
        , _listEvent{}
        , _pendingBatch{}
        , _packetWriter{}
        , _mapSlotByAddress{}
        , _connectPromise{}
        , _listFinished{}
        , _listReceived{}
        , _flushBatch{}
        , _listDeliver{}
        , _listDrainScratch{}
        , _settings{}
        , _pTransport{ nullptr }
        , _saltState{ 0 }
        , _rejectedPacketCount{ 0 }
        , _mismatchLogCount{ 0 }
        , _challengeSecret{ 0 }
        , _protocolId{ 0 }
        , _updateDepth{ 0 }
        , _clientIndex{ -1 }
        , _receiveCursor{ 0 }
        , _bServer{ SW_FALSE }
        , _bConnectPending{ SW_FALSE }
    {
    }

    void NetHost::initialize( INetTransport* pTransport, const NetHostSettings& settings )
    {
        vector<FinishedConnect> listFinished;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            finishConnect( NetDisconnectReason::Requested );
            _pTransport      = pTransport;
            _settings        = settings;
            _saltState       = settings._saltSeed != 0 ? settings._saltSeed : NetHostInternal::makeRandomSeed();
            _challengeSecret = nextSalt();
            _listSlot.clear();
            _mapSlotByAddress.clear();
            _packetWriter.reserve( kNetMaxPacketSize );
            _pendingBatch.clear();
            _pendingBatch._bytes.reserve( static_cast<size_t>( kNetMaxPacketSize ) * 4 );
            _listEvent.clear();
            _rejectedPacketCount = 0;
            _mismatchLogCount    = 0;
            _protocolId          = NetProtocol::makeProtocolId( settings._gameId, settings._wireVersion );
            _clientIndex         = -1;
            _bServer             = SW_FALSE;
            listFinished.swap( _listFinished );
        }
        deliverFinished( listFinished );
    }

    NetHost::PacketType NetHost::peekPacketType( const uint8* pData, int32 size )
    {
        if ( pData == nullptr || size <= NetHostInternal::kHeaderSize )
            return PacketType::Count;
        BitReader    reader( pData + NetHostInternal::kHeaderSize, size - NetHostInternal::kHeaderSize );
        const uint32 type = reader.readBits( NetHostInternal::kTypeBits );
        return type < static_cast<uint32>( PacketType::Count ) ? static_cast<PacketType>( type ) : PacketType::Count;
    }

    int64 NetHost::computeChallengeWindow( float64 time ) { return static_cast<int64>( MathUtil::floor( time / kChallengeWindowSeconds ) ); }

    uint64 NetHost::makeChallengeToken( const NetAddress& address, uint64 clientSalt, int64 window ) const
    {
        uint64 value = NetHostInternal::mix64( _challengeSecret ^ NetHostInternal::makeAddressKey( address ) );
        value        = NetHostInternal::mix64( value ^ clientSalt );
        value        = NetHostInternal::mix64( value ^ static_cast<uint64>( window ) ^ ( _challengeSecret << 1 ) );
        return value | 1u; // 0 은 "아직 도전을 못 받았다" 의 뜻
    }

    uint64 NetHost::nextSalt()
    {
        // splitmix64
        _saltState += 0x9E3779B97F4A7C15ull;
        uint64 value = _saltState;
        value        = ( value ^ ( value >> 30 ) ) * 0xBF58476D1CE4E5B9ull;
        value        = ( value ^ ( value >> 27 ) ) * 0x94D049BB133111EBull;
        return value ^ ( value >> 31 );
    }

    bool NetHost::listen()
    {
        std::scoped_lock<mutex> lock{ _mutex };
        if ( _pTransport == nullptr )
            return false;
        _bServer = SW_TRUE;
        _listSlot.assign( static_cast<size_t>( MathUtil::max( 1, _settings._maxConnections ) ), Slot{} );
        _mapSlotByAddress.clear();
        _mapSlotByAddress.reserve( _listSlot.size() * 2 );
        return true;
    }

    bool NetHost::connect( const NetAddress& serverAddress )
    {
        vector<FinishedConnect> listFinished;
        bool                    bStarted = false;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            finishConnect( NetDisconnectReason::Requested ); // 앞의 비동기 연결은 끝난다
            bStarted = startConnect( serverAddress );
            listFinished.swap( _listFinished );
        }
        deliverFinished( listFinished );
        return bStarted;
    }

    TaskFuture<NetConnectResult> NetHost::connectAsync( const NetAddress& serverAddress )
    {
        vector<FinishedConnect>      listFinished;
        TaskFuture<NetConnectResult> future;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            finishConnect( NetDisconnectReason::Requested );
            _connectPromise  = TaskPromise<NetConnectResult>{};
            future           = _connectPromise.getFuture();
            _bConnectPending = SW_TRUE;
            if ( startConnect( serverAddress ) == false )
                finishConnect( NetDisconnectReason::Rejected );
            listFinished.swap( _listFinished );
        }
        deliverFinished( listFinished );
        return future;
    }

    bool NetHost::startConnect( const NetAddress& serverAddress )
    {
        if ( _pTransport == nullptr || serverAddress.isValid() == false )
            return false;
        _bServer = SW_FALSE;
        _listSlot.assign( 1, Slot{} );
        _mapSlotByAddress.clear();
        bindAddress( 0, serverAddress );
        Slot& slot             = _listSlot[0];
        slot._address          = serverAddress;
        slot._clientSalt       = nextSalt();
        slot._state            = NetConnectionState::Connecting;
        slot._connectStartTime = -1.0; // 첫 update 에서 시각을 잡는다
        slot._lastSendTime     = -1.0;
        return true;
    }

    int32 NetHost::findSlotByAddress( const NetAddress& address ) const
    {
        const auto iter = _mapSlotByAddress.find( NetHostInternal::makeAddressKey( address ) );
        return iter != _mapSlotByAddress.end() ? iter->second : -1;
    }

    void NetHost::bindAddress( int32 slotIndex, const NetAddress& address )
    {
        _mapSlotByAddress[NetHostInternal::makeAddressKey( address )] = slotIndex;
    }

    void NetHost::unbindAddress( int32 slotIndex )
    {
        const auto iter = _mapSlotByAddress.find( NetHostInternal::makeAddressKey( _listSlot[static_cast<size_t>( slotIndex )]._address ) );
        if ( iter != _mapSlotByAddress.end() && iter->second == slotIndex )
            _mapSlotByAddress.erase( iter );
    }

    int32 NetHost::findFreeSlot() const
    {
        for ( size_t index = 0; index < _listSlot.size(); ++index )
        {
            if ( _listSlot[index]._state == NetConnectionState::Disconnected )
                return static_cast<int32>( index );
        }
        return -1;
    }

    void NetHost::sendFramed( const NetAddress& to, uint32 headerId )
    {
        // 보낼 묶음의 바이트 뒤에 바로 짓는다(헤더 + 몸) — 보내기는 잠금을 푼 뒤 `sendBatch` 가.
        const vector<uint8>& body     = _packetWriter.getBytes();
        const int32          bodySize = _packetWriter.getByteCount();
        const size_t         offset   = _pendingBatch._bytes.size();
        _pendingBatch._bytes.resize( offset + static_cast<size_t>( NetHostInternal::kHeaderSize + bodySize ) );
        uint8* pPacket = _pendingBatch._bytes.data() + offset;
        if ( bodySize > 0 )
            std::memcpy( pPacket + NetHostInternal::kHeaderSize, body.data(), static_cast<size_t>( bodySize ) );
        NetHostInternal::writeUint32( _pendingBatch._bytes, offset, headerId );
        NetHostInternal::writeUint32( _pendingBatch._bytes, offset + 4, NetHostInternal::computeChecksum( headerId, pPacket + NetHostInternal::kHeaderSize, bodySize ) );
        _pendingBatch._listDatagram.push_back( OutgoingDatagram{ to, static_cast<int32>( offset ), NetHostInternal::kHeaderSize + bodySize } );
    }

    void NetHost::sendBatch( const OutgoingBatch& batch )
    {
        for ( const OutgoingDatagram& datagram : batch._listDatagram )
            (void)_pTransport->send( datagram._to, batch._bytes.data() + datagram._offset, datagram._size );
    }

    void NetHost::takePending( OutgoingBatch& outBatch, vector<FinishedConnect>& outListFinished )
    {
        outBatch.clear();
        outBatch._listDatagram.swap( _pendingBatch._listDatagram );
        outBatch._bytes.swap( _pendingBatch._bytes );
        outListFinished.clear();
        outListFinished.swap( _listFinished );
    }

    void NetHost::deliverFinished( vector<FinishedConnect>& listFinished )
    {
        for ( FinishedConnect& finished : listFinished )
            finished._promise.setValue( finished._result );
        listFinished.clear();
    }

    void NetHost::pushEvent( const NetHostEvent& event )
    {
        _listEvent.push_back( event );
        if ( _bServer == SW_FALSE )
            finishConnect( event._kind == NetHostEvent::Kind::Connected ? NetDisconnectReason::None : event._reason );
    }

    void NetHost::finishConnect( NetDisconnectReason reason )
    {
        if ( _bConnectPending == SW_FALSE )
            return;
        _bConnectPending = SW_FALSE;
        FinishedConnect finished{
            std::move( _connectPromise ), NetConnectResult{ reason, reason == NetDisconnectReason::None ? _clientIndex : -1 }
        };
        _listFinished.push_back( std::move( finished ) );
    }

    void NetHost::sendControl( const NetAddress& to, PacketType type, uint64 valueA, uint64 valueB )
    {
        BitWriter& writer = _packetWriter;
        writer.clear();
        writer.writeBits( static_cast<uint32>( type ), NetHostInternal::kTypeBits );
        writer.writeBits( static_cast<uint32>( valueA ), 32 );
        writer.writeBits( static_cast<uint32>( valueA >> 32 ), 32 );
        writer.writeBits( static_cast<uint32>( valueB ), 32 );
        writer.writeBits( static_cast<uint32>( valueB >> 32 ), 32 );
        sendFramed( to, isHandshakeFramed( type ) ? NetProtocol::kHandshakeId : _protocolId );
    }

    void NetHost::sendDenied( const NetAddress& to, NetDisconnectReason reason, uint64 clientSalt )
    {
        sendControl( to, PacketType::Denied, static_cast<uint64>( reason ) | ( static_cast<uint64>( _protocolId ) << 32 ), clientSalt );
    }

    void NetHost::sendPayload( float64 time, Slot& slot )
    {
        BitWriter& writer = _packetWriter;
        writer.clear();
        writer.writeBits( static_cast<uint32>( PacketType::Payload ), NetHostInternal::kTypeBits );
        // 연결 값(두 소금의 섞음) — 같은 주소의 옛 연결 · 위조 패킷을 거른다.
        const uint64 token = slot._clientSalt ^ slot._serverSalt;
        writer.writeBits( static_cast<uint32>( token ), 32 );
        slot._connection.writePacket( time, writer, kNetMaxPacketSize - NetHostInternal::kHeaderSize, _settings._keepAliveInterval );
        sendFramed( slot._address, _protocolId );
        slot._lastSendTime = time;
    }

    bool NetHost::waitForReceive( float64 timeoutSeconds )
    {
        INetTransport* pTransport = nullptr;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            pTransport = _pTransport;
        }
        if ( pTransport != nullptr )
            return pTransport->waitForReceive( timeoutSeconds );
        std::this_thread::sleep_for( std::chrono::duration<float64>( timeoutSeconds ) );
        return false;
    }

    void NetHost::update( float64 time )
    {
        const uint32 depth = _updateDepth.fetch_add( 1, std::memory_order_acq_rel );
        SW_ASSERT( depth == 0 && "NetHost::update called from two threads at once" );
        (void)depth;
        INetTransport* pTransport = nullptr;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            pTransport = _pTransport;
        }
        if ( pTransport != nullptr )
        {
            // 1) 받기 — 잠금 밖. 받은 버퍼는 다시 쓴다.
            pTransport->update( time );
            int32 receivedCount = 0;
            while ( receivedCount < kMaxDatagramPerUpdate )
            {
                if ( receivedCount == static_cast<int32>( _listReceived.size() ) )
                    _listReceived.emplace_back();
                ReceivedDatagram& datagram = _listReceived[static_cast<size_t>( receivedCount )];
                if ( pTransport->receive( datagram._from, datagram._buffer ) == false )
                    break;
                ++receivedCount;
            }
            // 2) 처리 · 타임아웃 · 보낼 패킷 — 잠금 안.
            {
                std::scoped_lock<mutex> lock{ _mutex };
                for ( int32 index = 0; index < receivedCount; ++index )
                {
                    const ReceivedDatagram& datagram = _listReceived[static_cast<size_t>( index )];
                    handlePacket( time, datagram._from, datagram._buffer.data(), static_cast<int32>( datagram._buffer.size() ) );
                }
                updateSlots( time );
                takePending( _flushBatch, _listDeliver );
            }
            // 3) 보내기 · 비동기 연결 결과 — 잠금 밖.
            sendBatch( _flushBatch );
            deliverFinished( _listDeliver );
        }
        _updateDepth.fetch_sub( 1, std::memory_order_acq_rel );
    }

    void NetHost::updateSlots( float64 time )
    {
        for ( size_t index = 0; index < _listSlot.size(); ++index )
        {
            Slot& slot = _listSlot[index];
            switch ( slot._state )
            {
                case NetConnectionState::Connecting:
                {
                    if ( slot._connectStartTime < 0.0 )
                        slot._connectStartTime = time;
                    if ( time - slot._connectStartTime > _settings._connectTimeout )
                    {
                        closeSlot( static_cast<int32>( index ), NetDisconnectReason::Timeout, false );
                        break;
                    }
                    if ( slot._lastSendTime < 0.0 || time - slot._lastSendTime >= _settings._connectRetryInterval )
                    {
                        // 도전을 받았으면 응답을, 아니면 요청을 되풀이한다.
                        if ( slot._serverSalt != 0 )
                            sendControl( slot._address, PacketType::ChallengeResponse, slot._clientSalt, slot._serverSalt );
                        else
                            sendControl( slot._address, PacketType::ConnectRequest, slot._clientSalt,
                                         ( static_cast<uint64>( _settings._gameId ) << 32 ) | _protocolId );
                        slot._lastSendTime = time;
                    }
                    break;
                }
                case NetConnectionState::Connected:
                {
                    if ( time - slot._lastReceiveTime > _settings._timeout )
                    {
                        closeSlot( static_cast<int32>( index ), NetDisconnectReason::Timeout, false );
                        break;
                    }
                    // 보낼 것이 있으면 `_sendInterval` 마다, 없으면 `_keepAliveInterval` 마다만(유지 · RTT · 상대의 확인용).
                    const float64 sinceSend = slot._lastSendTime < 0.0 ? 1.0e9 : time - slot._lastSendTime;
                    const bool    bDue      = sinceSend >= _settings._sendInterval &&
                                      ( sinceSend >= _settings._keepAliveInterval || slot._connection.hasDataToSend( time ) );
                    if ( bDue )
                        sendPayload( time, slot );
                    break;
                }
                case NetConnectionState::Disconnected:
                case NetConnectionState::Disconnecting:
                {
                    break;
                }
            }
        }
    }

    void NetHost::handlePacket( float64 time, const NetAddress& from, const uint8* pData, int32 size )
    {
        const uint32 headerId = size > NetHostInternal::kHeaderSize ? NetHostInternal::readUint32( pData ) : 0u;
        if ( size <= NetHostInternal::kHeaderSize || ( headerId != _protocolId && headerId != NetProtocol::kHandshakeId ) ||
             NetHostInternal::readUint32( pData + 4 ) !=
                 NetHostInternal::computeChecksum( headerId, pData + NetHostInternal::kHeaderSize, size - NetHostInternal::kHeaderSize ) )
        {
            ++_rejectedPacketCount; // 다른 판 · 다른 게임 · 깨진 패킷
            return;
        }
        BitReader        reader( pData + NetHostInternal::kHeaderSize, size - NetHostInternal::kHeaderSize );
        const PacketType type = static_cast<PacketType>( reader.readBits( NetHostInternal::kTypeBits ) );
        if ( type >= PacketType::Count || isHandshakeFramed( type ) != ( headerId == NetProtocol::kHandshakeId ) )
        {
            ++_rejectedPacketCount;
            return;
        }
        const int32 slotIndex = findSlotByAddress( from );
        if ( type == PacketType::Payload )
        {
            const uint32 token = reader.readBits( 32 );
            if ( isValidSlot( slotIndex ) == false || _listSlot[static_cast<size_t>( slotIndex )]._state != NetConnectionState::Connected )
            {
                // 서버는 수락했지만 클라이언트가 Accepted 를 잃었다 — 데이터 패킷으로는 연결로 치지 않는다(수락에만 서버가 준 번호가 있다).
                // 응답을 바로 다시 보내면 서버가 수락을 다시 보낸다. 이 패킷의 신뢰 메시지는 확인하지 않았으니 서버가 다시 보낸다.
                Slot* pSlot = isValidSlot( slotIndex ) ? &_listSlot[static_cast<size_t>( slotIndex )] : nullptr;
                if ( _bServer == SW_FALSE && pSlot != nullptr && pSlot->_state == NetConnectionState::Connecting && pSlot->_serverSalt != 0 &&
                     token == static_cast<uint32>( pSlot->_clientSalt ^ pSlot->_serverSalt ) )
                {
                    sendControl( pSlot->_address, PacketType::ChallengeResponse, pSlot->_clientSalt, pSlot->_serverSalt );
                    pSlot->_lastSendTime = time;
                    return;
                }
                ++_rejectedPacketCount;
                return;
            }
            Slot& slot = _listSlot[static_cast<size_t>( slotIndex )];
            if ( token != static_cast<uint32>( slot._clientSalt ^ slot._serverSalt ) )
            {
                ++_rejectedPacketCount;
                return;
            }
            if ( slot._connection.readPacket( time, reader ) )
                slot._lastReceiveTime = time;
            return;
        }

        const uint64 valueA = static_cast<uint64>( reader.readBits( 32 ) ) | ( static_cast<uint64>( reader.readBits( 32 ) ) << 32 );
        const uint64 valueB = static_cast<uint64>( reader.readBits( 32 ) ) | ( static_cast<uint64>( reader.readBits( 32 ) ) << 32 );
        if ( reader.hasOverflowed() )
        {
            ++_rejectedPacketCount;
            return;
        }
        switch ( type )
        {
            case PacketType::ConnectRequest:
            {
                if ( _bServer == SW_FALSE )
                    return;
                const uint32 requestProtocolId = static_cast<uint32>( valueB );
                if ( requestProtocolId != _protocolId )
                {
                    const uint32              requestGameId = static_cast<uint32>( valueB >> 32 );
                    const NetDisconnectReason reason        = requestGameId == _settings._gameId ? NetDisconnectReason::VersionMismatch : NetDisconnectReason::Rejected;
                    ++_mismatchLogCount;
                    if ( ( _mismatchLogCount & ( _mismatchLogCount - 1 ) ) == 0 )
                        SW_LOG_WARNING( "NetHost: refused a connection from %# (%#) — it speaks protocol 0x%08x (game 0x%08x), this server 0x%08x (game 0x%08x); %# such requests so far",
                                        from.toString().c_str(), toString( reason ), requestProtocolId, requestGameId, _protocolId, _settings._gameId,
                                        _mismatchLogCount );
                    sendDenied( from, reason, valueA );
                    return;
                }
                if ( isValidSlot( slotIndex ) )
                {
                    // 이 주소는 이미 연결돼 있다 — 같은 요청의 늦은 재전송이면 수락을 다시, 다른 소금(새로 시작한 클라이언트)이면 옛 연결이 끝날 때까지 답하지 않는다.
                    const Slot& slot = _listSlot[static_cast<size_t>( slotIndex )];
                    if ( slot._clientSalt == valueA )
                        sendControl( from, PacketType::Accepted, static_cast<uint64>( slotIndex ), slot._serverSalt );
                    return;
                }
                if ( findFreeSlot() < 0 )
                {
                    sendDenied( from, NetDisconnectReason::ServerFull, valueA );
                    return;
                }
                // 자리를 잡지 않는다 — 도전 값만 돌려준다. 그 값은 이 주소로 간 패킷에만 있으므로, 되돌려 준 응답이 와야 주소가 진짜다.
                sendControl( from, PacketType::Challenge, valueA, makeChallengeToken( from, valueA, computeChallengeWindow( time ) ) );
                return;
            }
            case PacketType::Challenge:
            {
                if ( _bServer || isValidSlot( slotIndex ) == false )
                    return;
                Slot& slot = _listSlot[static_cast<size_t>( slotIndex )];
                if ( slot._state != NetConnectionState::Connecting || valueA != slot._clientSalt )
                    return;
                slot._serverSalt = valueB;
                sendControl( from, PacketType::ChallengeResponse, slot._clientSalt, slot._serverSalt );
                slot._lastSendTime = time;
                return;
            }
            case PacketType::ChallengeResponse:
            {
                if ( _bServer == SW_FALSE )
                    return;
                if ( isValidSlot( slotIndex ) )
                {
                    // 이미 연결됐다 — 수락을 잃은 클라이언트의 되풀이다.
                    Slot& slot = _listSlot[static_cast<size_t>( slotIndex )];
                    if ( slot._clientSalt == valueA && slot._serverSalt == valueB )
                    {
                        slot._lastReceiveTime = time;
                        sendControl( from, PacketType::Accepted, static_cast<uint64>( slotIndex ), slot._serverSalt );
                    }
                    return;
                }
                // 도전 값을 다시 만들어 맞춰 본다 — 이번 칸이나 바로 앞 칸에 만든 것만(5~10 초).
                const int64 window = computeChallengeWindow( time );
                if ( valueB != makeChallengeToken( from, valueA, window ) && valueB != makeChallengeToken( from, valueA, window - 1 ) )
                {
                    ++_rejectedPacketCount; // 위조 · 만료된 응답
                    return;
                }
                const int32 freeIndex = findFreeSlot();
                if ( freeIndex < 0 )
                {
                    sendDenied( from, NetDisconnectReason::ServerFull, valueA );
                    return;
                }
                Slot& slot    = _listSlot[static_cast<size_t>( freeIndex )];
                slot          = Slot{};
                slot._address = from;
                bindAddress( freeIndex, from );
                slot._clientSalt      = valueA;
                slot._serverSalt      = valueB;
                slot._state           = NetConnectionState::Connected;
                slot._lastReceiveTime = time;
                slot._lastSendTime    = -1.0;
                slot._connection.reset();
                pushEvent( NetHostEvent{ freeIndex, NetDisconnectReason::None, NetHostEvent::Kind::Connected } );
                sendControl( from, PacketType::Accepted, static_cast<uint64>( freeIndex ), slot._serverSalt );
                return;
            }
            case PacketType::Accepted:
            {
                if ( _bServer || isValidSlot( slotIndex ) == false )
                    return;
                Slot& slot = _listSlot[static_cast<size_t>( slotIndex )];
                if ( slot._state != NetConnectionState::Connecting || valueB != slot._serverSalt )
                    return;
                slot._state           = NetConnectionState::Connected;
                slot._lastReceiveTime = time;
                slot._lastSendTime    = -1.0;
                slot._connection.reset();
                _clientIndex = static_cast<int32>( valueA );
                pushEvent( NetHostEvent{ 0, NetDisconnectReason::None, NetHostEvent::Kind::Connected } );
                return;
            }
            case PacketType::Denied:
            {
                if ( _bServer || isValidSlot( slotIndex ) == false )
                    return;
                const Slot& slot = _listSlot[static_cast<size_t>( slotIndex )];
                if ( slot._state != NetConnectionState::Connecting || valueB != slot._clientSalt )
                    return; // 이 요청에 대한 거절이 아니다(옛 요청 · 위조)
                const NetDisconnectReason reason = NetHostInternal::readDeniedReason( valueA );
                if ( reason != NetDisconnectReason::ServerFull )
                    SW_LOG_WARNING( "NetHost: server %# refused the connection (%#) — it speaks protocol 0x%08x, this client 0x%08x (game 0x%08x)",
                                    from.toString().c_str(), toString( reason ), static_cast<uint32>( valueA >> 32 ), _protocolId, _settings._gameId );
                closeSlot( slotIndex, reason, false );
                return;
            }
            case PacketType::Disconnect:
            {
                if ( isValidSlot( slotIndex ) == false )
                    return;
                const Slot& slot = _listSlot[static_cast<size_t>( slotIndex )];
                if ( valueA == ( slot._clientSalt ^ slot._serverSalt ) )
                    closeSlot( slotIndex, NetDisconnectReason::Remote, false );
                return;
            }
            case PacketType::Payload:
            case PacketType::Count:
            {
                return;
            }
        }
    }

    void NetHost::closeSlot( int32 slotIndex, NetDisconnectReason reason, bool bNotifyRemote )
    {
        Slot& slot = _listSlot[static_cast<size_t>( slotIndex )];
        if ( slot._state == NetConnectionState::Disconnected )
            return;
        if ( bNotifyRemote && slot._state == NetConnectionState::Connected )
        {
            // 끊김 알림은 잃을 수 있으니 몇 번 보낸다(못 받아도 저쪽은 타임아웃으로 안다).
            for ( int32 repeat = 0; repeat < 3; ++repeat )
                sendControl( slot._address, PacketType::Disconnect, slot._clientSalt ^ slot._serverSalt, 0 );
        }
        const bool bWasVisible = slot._state == NetConnectionState::Connected || _bServer == SW_FALSE;
        unbindAddress( slotIndex );
        slot._state = NetConnectionState::Disconnected;
        slot._connection.reset();
        if ( bWasVisible )
            pushEvent( NetHostEvent{ slotIndex, reason, NetHostEvent::Kind::Disconnected } );
        if ( _bServer == SW_FALSE )
            _clientIndex = -1;
    }

    bool NetHost::sendMessage( int32 connectionId, NetChannelType channel, const uint8* pData, int32 size )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return sendMessageLocked( connectionId, channel, pData, size );
    }

    bool NetHost::sendMessageLocked( int32 connectionId, NetChannelType channel, const uint8* pData, int32 size )
    {
        if ( isValidSlot( connectionId ) == false || _listSlot[static_cast<size_t>( connectionId )]._state != NetConnectionState::Connected )
            return false;
        return _listSlot[static_cast<size_t>( connectionId )]._connection.sendMessage( channel, pData, size );
    }

    int32 NetHost::broadcast( NetChannelType channel, const uint8* pData, int32 size, int32 exceptId )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        int32                   sentCount = 0;
        for ( size_t index = 0; index < _listSlot.size(); ++index )
        {
            if ( static_cast<int32>( index ) != exceptId && sendMessageLocked( static_cast<int32>( index ), channel, pData, size ) )
                ++sentCount;
        }
        return sentCount;
    }

    bool NetHost::receiveMessage( int32& outConnectionId, NetChannelType& outChannel, vector<uint8>& outBuffer )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const int32             slotCount = static_cast<int32>( _listSlot.size() );
        for ( int32 step = 0; step < slotCount; ++step )
        {
            const int32 index = ( _receiveCursor + step ) % slotCount;
            Slot&       slot  = _listSlot[static_cast<size_t>( index )];
            if ( slot._state != NetConnectionState::Connected )
                continue;
            for ( int32 channel = 0; channel < static_cast<int32>( NetChannelType::Count ); ++channel )
            {
                if ( slot._connection.receiveMessage( static_cast<NetChannelType>( channel ), outBuffer ) )
                {
                    outConnectionId = index;
                    outChannel      = static_cast<NetChannelType>( channel );
                    _receiveCursor  = index; // 같은 연결부터 이어서 비운다
                    return true;
                }
            }
        }
        _receiveCursor = slotCount > 0 ? ( _receiveCursor + 1 ) % slotCount : 0;
        return false;
    }

    void NetHost::disconnect( int32 connectionId )
    {
        // 끊김 알림은 바로 보낸다(곧 호스트를 없앨 수 있다) — 이 스레드의 사본으로, 잠금 밖에서.
        OutgoingBatch           batch;
        vector<FinishedConnect> listFinished;
        INetTransport*          pTransport = nullptr;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            if ( isValidSlot( connectionId ) )
                closeSlot( connectionId, NetDisconnectReason::Requested, true );
            takePending( batch, listFinished );
            pTransport = _pTransport;
        }
        if ( pTransport != nullptr )
        {
            for ( const OutgoingDatagram& datagram : batch._listDatagram )
                (void)pTransport->send( datagram._to, batch._bytes.data() + datagram._offset, datagram._size );
        }
        deliverFinished( listFinished );
    }

    void NetHost::disconnectAll()
    {
        OutgoingBatch           batch;
        vector<FinishedConnect> listFinished;
        INetTransport*          pTransport = nullptr;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            for ( size_t index = 0; index < _listSlot.size(); ++index )
                closeSlot( static_cast<int32>( index ), NetDisconnectReason::Requested, true );
            takePending( batch, listFinished );
            pTransport = _pTransport;
        }
        if ( pTransport != nullptr )
        {
            for ( const OutgoingDatagram& datagram : batch._listDatagram )
                (void)pTransport->send( datagram._to, batch._bytes.data() + datagram._offset, datagram._size );
        }
        deliverFinished( listFinished );
    }

    void NetHost::drainEvents( vector<NetHostEvent>& outListEvent )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        outListEvent.insert( outListEvent.end(), _listEvent.begin(), _listEvent.end() );
        _listEvent.clear();
    }

    void NetHost::drainInbound( NetInbound& outInbound )
    {
        outInbound.clear();
        std::scoped_lock<mutex> lock{ _mutex };
        outInbound._listEvent.swap( _listEvent );
        const int32 slotCount = static_cast<int32>( _listSlot.size() );
        for ( int32 index = 0; index < slotCount; ++index )
        {
            Slot& slot = _listSlot[static_cast<size_t>( index )];
            if ( slot._state != NetConnectionState::Connected )
                continue;
            for ( int32 channel = 0; channel < static_cast<int32>( NetChannelType::Count ); ++channel )
            {
                while ( slot._connection.receiveMessage( static_cast<NetChannelType>( channel ), _listDrainScratch ) )
                {
                    NetInboundMessage message;
                    message._connectionId = index;
                    message._offset       = static_cast<int32>( outInbound._bytes.size() );
                    message._size         = static_cast<int32>( _listDrainScratch.size() );
                    message._channel      = static_cast<NetChannelType>( channel );
                    outInbound._bytes.insert( outInbound._bytes.end(), _listDrainScratch.begin(), _listDrainScratch.end() );
                    outInbound._listMessage.push_back( message );
                }
            }
        }
    }

    bool NetHost::isServer() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _bServer != SW_FALSE;
    }

    int32 NetHost::getClientIndex() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _clientIndex;
    }

    uint64 NetHost::getRejectedPacketCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _rejectedPacketCount;
    }

    uint32 NetHost::getProtocolId() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _protocolId;
    }

    bool NetHost::getConnectionStats( int32 connectionId, NetConnectionStats& outStats ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        if ( isValidSlot( connectionId ) == false || _listSlot[static_cast<size_t>( connectionId )]._state != NetConnectionState::Connected )
            return false;
        outStats = _listSlot[static_cast<size_t>( connectionId )]._connection.getStats();
        return true;
    }

    NetConnectionState NetHost::getConnectionState( int32 connectionId ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return isValidSlot( connectionId ) ? _listSlot[static_cast<size_t>( connectionId )]._state : NetConnectionState::Disconnected;
    }

    const NetConnection* NetHost::findConnection( int32 connectionId ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return isValidSlot( connectionId ) && _listSlot[static_cast<size_t>( connectionId )]._state == NetConnectionState::Connected ? &_listSlot[static_cast<size_t>( connectionId )]._connection : nullptr;
    }

    NetAddress NetHost::getConnectionAddress( int32 connectionId ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return isValidSlot( connectionId ) ? _listSlot[static_cast<size_t>( connectionId )]._address : NetAddress{};
    }

    int32 NetHost::getConnectedCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        int32                   count = 0;
        for ( const Slot& slot : _listSlot )
            count += slot._state == NetConnectionState::Connected ? 1 : 0;
        return count;
    }

    void NetHost::collectConnected( vector<int32>& outListConnection ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        outListConnection.clear();
        for ( size_t index = 0; index < _listSlot.size(); ++index )
        {
            if ( _listSlot[index]._state == NetConnectionState::Connected )
                outListConnection.push_back( static_cast<int32>( index ) );
        }
    }
} // namespace sw
