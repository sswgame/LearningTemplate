#include "pch.h"

#include "Core/Network/NetHost.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/NetTransport.h"

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

            static uint32 readUint32( const uint8* pData )
            {
                return static_cast<uint32>( pData[0] ) | ( static_cast<uint32>( pData[1] ) << 8 ) | ( static_cast<uint32>( pData[2] ) << 16 ) |
                       ( static_cast<uint32>( pData[3] ) << 24 );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    NetHost::NetHost()
        : _listSlot{}
        , _listEvent{}
        , _receiveBuffer{}
        , _settings{}
        , _pTransport{ nullptr }
        , _saltState{ 0 }
        , _rejectedPacketCount{ 0 }
        , _clientIndex{ -1 }
        , _receiveCursor{ 0 }
        , _bServer{ SW_FALSE }
    {
    }

    void NetHost::initialize( INetTransport* pTransport, const NetHostSettings& settings )
    {
        _pTransport = pTransport;
        _settings   = settings;
        _saltState  = settings._saltSeed != 0 ? settings._saltSeed : 0x9E3779B97F4A7C15ull;
        _listSlot.clear();
        _listEvent.clear();
        _rejectedPacketCount = 0;
        _clientIndex         = -1;
        _bServer             = SW_FALSE;
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
        if ( _pTransport == nullptr )
            return false;
        _bServer = SW_TRUE;
        _listSlot.assign( static_cast<size_t>( MathUtil::max( 1, _settings._maxConnections ) ), Slot{} );
        return true;
    }

    bool NetHost::connect( const NetAddress& serverAddress )
    {
        if ( _pTransport == nullptr || serverAddress.isValid() == false )
            return false;
        _bServer = SW_FALSE;
        _listSlot.assign( 1, Slot{} );
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
        for ( size_t index = 0; index < _listSlot.size(); ++index )
        {
            if ( _listSlot[index]._state != NetConnectionState::Disconnected && _listSlot[index]._address == address )
                return static_cast<int32>( index );
        }
        return -1;
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

    void NetHost::sendControl( const NetAddress& to, PacketType type, uint64 valueA, uint64 valueB )
    {
        BitWriter writer;
        writer.writeBits( static_cast<uint32>( type ), NetHostInternal::kTypeBits );
        writer.writeBits( static_cast<uint32>( valueA ), 32 );
        writer.writeBits( static_cast<uint32>( valueA >> 32 ), 32 );
        writer.writeBits( static_cast<uint32>( valueB ), 32 );
        writer.writeBits( static_cast<uint32>( valueB >> 32 ), 32 );
        vector<uint8> buffer( static_cast<size_t>( NetHostInternal::kHeaderSize ), 0 );
        buffer.insert( buffer.end(), writer.getBytes().begin(), writer.getBytes().end() );
        NetHostInternal::writeUint32( buffer, 0, _settings._protocolId );
        NetHostInternal::writeUint32( buffer, 4,
                                      NetHostInternal::computeChecksum( _settings._protocolId, buffer.data() + NetHostInternal::kHeaderSize,
                                                                        static_cast<int32>( buffer.size() ) - NetHostInternal::kHeaderSize ) );
        (void)_pTransport->send( to, buffer.data(), static_cast<int32>( buffer.size() ) );
    }

    void NetHost::sendPayload( float64 time, Slot& slot )
    {
        BitWriter writer;
        writer.writeBits( static_cast<uint32>( PacketType::Payload ), NetHostInternal::kTypeBits );
        // 연결 값(두 소금의 섞음) — 같은 주소의 옛 연결 · 위조 패킷을 거른다.
        const uint64 token = slot._clientSalt ^ slot._serverSalt;
        writer.writeBits( static_cast<uint32>( token ), 32 );
        slot._connection.writePacket( time, writer, kNetMaxPacketSize - NetHostInternal::kHeaderSize );
        vector<uint8> buffer( static_cast<size_t>( NetHostInternal::kHeaderSize ), 0 );
        buffer.insert( buffer.end(), writer.getBytes().begin(), writer.getBytes().end() );
        NetHostInternal::writeUint32( buffer, 0, _settings._protocolId );
        NetHostInternal::writeUint32( buffer, 4,
                                      NetHostInternal::computeChecksum( _settings._protocolId, buffer.data() + NetHostInternal::kHeaderSize,
                                                                        static_cast<int32>( buffer.size() ) - NetHostInternal::kHeaderSize ) );
        (void)_pTransport->send( slot._address, buffer.data(), static_cast<int32>( buffer.size() ) );
        slot._lastSendTime = time;
    }

    void NetHost::update( float64 time )
    {
        if ( _pTransport == nullptr )
            return;
        _pTransport->update( time );
        receivePackets( time );
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
                    if ( _bServer )
                        break; // 서버는 클라이언트의 되풀이를 기다린다
                    if ( slot._lastSendTime < 0.0 || time - slot._lastSendTime >= _settings._connectRetryInterval )
                    {
                        // 도전을 받았으면 응답을, 아니면 요청을 되풀이한다.
                        if ( slot._serverSalt != 0 )
                            sendControl( slot._address, PacketType::ChallengeResponse, slot._clientSalt ^ slot._serverSalt, 0 );
                        else
                            sendControl( slot._address, PacketType::ConnectRequest, slot._clientSalt, _settings._protocolId );
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
                    const bool bDue = slot._lastSendTime < 0.0 || time - slot._lastSendTime >= _settings._sendInterval;
                    if ( bDue )
                        sendPayload( time, slot );
                    break;
                }
                case NetConnectionState::Disconnected:
                case NetConnectionState::Disconnecting:
                    break;
            }
        }
    }

    void NetHost::receivePackets( float64 time )
    {
        NetAddress from{};
        while ( _pTransport->receive( from, _receiveBuffer ) )
            handlePacket( time, from, _receiveBuffer );
    }

    void NetHost::handlePacket( float64 time, const NetAddress& from, const vector<uint8>& buffer )
    {
        const int32 size = static_cast<int32>( buffer.size() );
        if ( size <= NetHostInternal::kHeaderSize || NetHostInternal::readUint32( buffer.data() ) != _settings._protocolId ||
             NetHostInternal::readUint32( buffer.data() + 4 ) !=
                 NetHostInternal::computeChecksum( _settings._protocolId, buffer.data() + NetHostInternal::kHeaderSize, size - NetHostInternal::kHeaderSize ) )
        {
            ++_rejectedPacketCount; // 다른 게임 · 깨진 패킷
            return;
        }
        BitReader        reader( buffer.data() + NetHostInternal::kHeaderSize, size - NetHostInternal::kHeaderSize );
        const PacketType type = static_cast<PacketType>( reader.readBits( NetHostInternal::kTypeBits ) );
        if ( type >= PacketType::Count )
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
                // 서버가 수락했지만 클라이언트가 Accepted 를 잃었다 — 첫 데이터 패킷이 수락을 대신한다(아래 클라이언트 처리).
                if ( _bServer == SW_FALSE && isValidSlot( slotIndex ) && _listSlot[0]._state == NetConnectionState::Connecting &&
                     token == static_cast<uint32>( _listSlot[0]._clientSalt ^ _listSlot[0]._serverSalt ) && _listSlot[0]._serverSalt != 0 )
                {
                    _listSlot[0]._state           = NetConnectionState::Connected;
                    _listSlot[0]._lastReceiveTime = time;
                    _listEvent.push_back( NetHostEvent{ 0, NetDisconnectReason::None, NetHostEvent::Kind::Connected } );
                }
                else
                {
                    ++_rejectedPacketCount;
                    return;
                }
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
                if ( static_cast<uint32>( valueB ) != _settings._protocolId )
                {
                    sendControl( from, PacketType::Denied, static_cast<uint64>( NetDisconnectReason::Rejected ), 0 );
                    return;
                }
                if ( isValidSlot( slotIndex ) )
                {
                    Slot& slot = _listSlot[static_cast<size_t>( slotIndex )];
                    // 같은 요청의 재전송 — 이미 수락했으면 수락을, 아니면 도전을 다시.
                    if ( slot._clientSalt == valueA )
                        sendControl( from, slot._state == NetConnectionState::Connected ? PacketType::Accepted : PacketType::Challenge,
                                     slot._state == NetConnectionState::Connected ? static_cast<uint64>( slotIndex ) : slot._clientSalt, slot._serverSalt );
                    return;
                }
                const int32 freeIndex = findFreeSlot();
                if ( freeIndex < 0 )
                {
                    sendControl( from, PacketType::Denied, static_cast<uint64>( NetDisconnectReason::ServerFull ), 0 );
                    return;
                }
                Slot& slot              = _listSlot[static_cast<size_t>( freeIndex )];
                slot                    = Slot{};
                slot._address           = from;
                slot._clientSalt        = valueA;
                slot._serverSalt        = nextSalt();
                slot._state             = NetConnectionState::Connecting;
                slot._bPendingChallenge = SW_TRUE;
                slot._connectStartTime  = time;
                slot._lastSendTime      = time; // 서버의 Connecting 은 다시 보내지 않는다 — 클라이언트가 되풀이한다
                sendControl( from, PacketType::Challenge, slot._clientSalt, slot._serverSalt );
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
                sendControl( from, PacketType::ChallengeResponse, slot._clientSalt ^ slot._serverSalt, 0 );
                slot._lastSendTime = time;
                return;
            }
            case PacketType::ChallengeResponse:
            {
                if ( _bServer == SW_FALSE || isValidSlot( slotIndex ) == false )
                    return;
                Slot& slot = _listSlot[static_cast<size_t>( slotIndex )];
                if ( valueA != ( slot._clientSalt ^ slot._serverSalt ) )
                    return;
                if ( slot._state == NetConnectionState::Connecting )
                {
                    slot._state             = NetConnectionState::Connected;
                    slot._bPendingChallenge = SW_FALSE;
                    slot._lastReceiveTime   = time;
                    slot._lastSendTime      = -1.0;
                    slot._connection.reset();
                    _listEvent.push_back( NetHostEvent{ slotIndex, NetDisconnectReason::None, NetHostEvent::Kind::Connected } );
                }
                sendControl( from, PacketType::Accepted, static_cast<uint64>( slotIndex ), slot._serverSalt );
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
                _listEvent.push_back( NetHostEvent{ 0, NetDisconnectReason::None, NetHostEvent::Kind::Connected } );
                return;
            }
            case PacketType::Denied:
            {
                if ( _bServer || isValidSlot( slotIndex ) == false || _listSlot[static_cast<size_t>( slotIndex )]._state != NetConnectionState::Connecting )
                    return;
                const NetDisconnectReason reason = valueA == static_cast<uint64>( NetDisconnectReason::ServerFull ) ? NetDisconnectReason::ServerFull
                                                                                                                    : NetDisconnectReason::Rejected;
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
                return;
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
        slot._state            = NetConnectionState::Disconnected;
        slot._connection.reset();
        if ( bWasVisible )
            _listEvent.push_back( NetHostEvent{ slotIndex, reason, NetHostEvent::Kind::Disconnected } );
        if ( _bServer == SW_FALSE )
            _clientIndex = -1;
    }

    bool NetHost::sendMessage( int32 connectionId, NetChannelType channel, const uint8* pData, int32 size )
    {
        if ( isValidSlot( connectionId ) == false || _listSlot[static_cast<size_t>( connectionId )]._state != NetConnectionState::Connected )
            return false;
        return _listSlot[static_cast<size_t>( connectionId )]._connection.sendMessage( channel, pData, size );
    }

    int32 NetHost::broadcast( NetChannelType channel, const uint8* pData, int32 size, int32 exceptId )
    {
        int32 sentCount = 0;
        for ( size_t index = 0; index < _listSlot.size(); ++index )
        {
            if ( static_cast<int32>( index ) != exceptId && sendMessage( static_cast<int32>( index ), channel, pData, size ) )
                ++sentCount;
        }
        return sentCount;
    }

    bool NetHost::receiveMessage( int32& outConnectionId, NetChannelType& outChannel, vector<uint8>& outBuffer )
    {
        const int32 slotCount = static_cast<int32>( _listSlot.size() );
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
        if ( isValidSlot( connectionId ) )
            closeSlot( connectionId, NetDisconnectReason::Requested, true );
    }

    void NetHost::disconnectAll()
    {
        for ( size_t index = 0; index < _listSlot.size(); ++index )
            closeSlot( static_cast<int32>( index ), NetDisconnectReason::Requested, true );
    }

    void NetHost::drainEvents( vector<NetHostEvent>& outListEvent )
    {
        outListEvent.insert( outListEvent.end(), _listEvent.begin(), _listEvent.end() );
        _listEvent.clear();
    }

    NetConnectionState NetHost::getConnectionState( int32 connectionId ) const
    {
        return isValidSlot( connectionId ) ? _listSlot[static_cast<size_t>( connectionId )]._state : NetConnectionState::Disconnected;
    }

    const NetConnection* NetHost::findConnection( int32 connectionId ) const
    {
        return getConnectionState( connectionId ) == NetConnectionState::Connected ? &_listSlot[static_cast<size_t>( connectionId )]._connection : nullptr;
    }

    NetAddress NetHost::getConnectionAddress( int32 connectionId ) const
    {
        return isValidSlot( connectionId ) ? _listSlot[static_cast<size_t>( connectionId )]._address : NetAddress{};
    }

    int32 NetHost::getConnectedCount() const
    {
        int32 count = 0;
        for ( const Slot& slot : _listSlot )
            count += slot._state == NetConnectionState::Connected ? 1 : 0;
        return count;
    }

    void NetHost::collectConnected( vector<int32>& outListConnection ) const
    {
        outListConnection.clear();
        for ( size_t index = 0; index < _listSlot.size(); ++index )
        {
            if ( _listSlot[index]._state == NetConnectionState::Connected )
                outListConnection.push_back( static_cast<int32>( index ) );
        }
    }
} // namespace sw
