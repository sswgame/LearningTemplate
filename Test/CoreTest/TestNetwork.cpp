#include "pch.h"

#include "Core/Network/BitStream.h"
#include "Core/Network/NetConnection.h"
#include "Core/Network/NetHost.h"
#include "Core/Network/NetMessage.h"
#include "Core/Network/NetTransport.h"
#include "Core/Network/SequenceBuffer.h"
#include "Core/Network/UdpNetTransport.h"

#include "TestFramework/TestFramework.h"

// 네트워크 공통 계층 — 비트 스트림, 시퀀스 감김, 신뢰성(재전송 · 순서 · 중복 · 옛것 버리기 · RTT), 핸드셰이크(도전 · 가득 참 · 다른 프로토콜),
// 나쁜 망(지연 · 흔들림 · 손실 · 중복 · 깨짐)에서의 신뢰 순서, 끊기 · 타임아웃, 실제 UDP 소켓, 한가할 때의 유지 패킷, 메시지 라우터, 많은 연결.

using namespace sw;

namespace
{
    struct NetTestPair
    {
        LoopbackNetwork _network{ 77u };
        NetHost         _server;
        NetHost         _client;
        float64         _time{ 0.0 };

        explicit NetTestPair( const NetHostSettings& settings = NetHostSettings{} )
        {
            _server.initialize( _network.createEndpoint( 4000 ), settings );
            _client.initialize( _network.createEndpoint( 5000 ), settings );
        }

        void run( float64 seconds, float64 step = 1.0 / 60.0 )
        {
            for ( float64 elapsed = 0.0; elapsed < seconds; elapsed += step )
            {
                _time += step;
                _server.update( _time );
                _client.update( _time );
            }
        }
    };

    vector<uint8> makeMessage( int32 value, int32 size = 4 )
    {
        vector<uint8> buffer( static_cast<size_t>( size ), 0 );
        for ( int32 index = 0; index < 4 && index < size; ++index )
            buffer[static_cast<size_t>( index )] = static_cast<uint8>( value >> ( index * 8 ) );
        return buffer;
    }

    /** @brief 시험용 처리기 — 맡은 영역에서 @p acceptedKind 만 받아들이고 받은 수를 셉니다. */
    class CountingHandler : public INetMessageHandler
    {
    public:
        CountingHandler( uint8 rangeBase, uint8 acceptedKind )
            : _rangeBase{ rangeBase }
            , _acceptedKind{ acceptedKind }
            , _handledCount{ 0 }
            , _lastConnectionId{ -1 }
        {
        }

        uint8 getMessageRangeBase() const override { return _rangeBase; }
        bool  handleNetMessage( int32 connectionId, const uint8* pData, int32 size ) override
        {
            if ( size <= 0 || pData[0] != _acceptedKind )
                return false;
            ++_handledCount;
            _lastConnectionId = connectionId;
            return true;
        }

        int32 getHandledCount() const { return _handledCount; }
        int32 getLastConnectionId() const { return _lastConnectionId; }

    private:
        uint8 _rangeBase;
        uint8 _acceptedKind;
        int32 _handledCount;
        int32 _lastConnectionId;
    };

    /** @brief 시험용 의사 난수(xorshift) — 시드가 같으면 같은 수열입니다. */
    uint32 nextRandom( uint32& inOutState )
    {
        inOutState ^= inOutState << 13;
        inOutState ^= inOutState >> 17;
        inOutState ^= inOutState << 5;
        return inOutState;
    }

    int32 readMessageValue( const vector<uint8>& buffer )
    {
        int32 value = 0;
        for ( int32 index = 0; index < 4 && index < static_cast<int32>( buffer.size() ); ++index )
            value |= static_cast<int32>( buffer[static_cast<size_t>( index )] ) << ( index * 8 );
        return value;
    }
} // namespace

SW_TEST_CASE( NetworkTest, BitStreamPacksRangesFloatsVarIntsAndDetectsOverflow )
{
    BitWriter writer;
    writer.writeBool( true );
    writer.writeInt( 73, 0, 100 );  // 7 비트
    writer.writeInt( -5, -10, 10 ); // 5 비트
    writer.writeInt( 500, 0, 100 ); // 잘린다
    writer.writeQuantizedFloat( 12.345f, -100.0f, 100.0f, 0.01f );
    writer.writeFloat( 3.25f );
    writer.writeVarUint( 300 );
    writer.writeVarInt( -70000 );
    writer.alignToByte();
    const uint8 arrRaw[3] = { 1, 2, 250 };
    writer.writeBytes( arrRaw, 3 );
    SW_EXPECT_EQUAL( 7, BitMath::computeBitsRequired( 0, 100 ) );
    SW_EXPECT_EQUAL( 5, BitMath::computeBitsRequired( -10, 10 ) );

    BitReader reader( writer.getBytes().data(), writer.getByteCount() );
    SW_EXPECT_TRUE( reader.readBool() );
    SW_EXPECT_EQUAL( 73, reader.readInt( 0, 100 ) );
    SW_EXPECT_EQUAL( -5, reader.readInt( -10, 10 ) );
    SW_EXPECT_EQUAL( 100, reader.readInt( 0, 100 ) );
    SW_EXPECT_NEAR_EQUAL( 12.345f, reader.readQuantizedFloat( -100.0f, 100.0f, 0.01f ), 0.006f );
    SW_EXPECT_NEAR_EQUAL( 3.25f, reader.readFloat(), 1.0e-6f );
    SW_EXPECT_EQUAL( 300, static_cast<int32>( reader.readVarUint() ) );
    SW_EXPECT_EQUAL( -70000, static_cast<int32>( reader.readVarInt() ) );
    reader.alignToByte();
    uint8 arrRead[3] = {};
    SW_EXPECT_TRUE( reader.readBytes( arrRead, 3 ) );
    SW_EXPECT_EQUAL( 250, arrRead[2] );
    SW_EXPECT_FALSE( reader.hasOverflowed() );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( reader.readBits( 32 ) ) );
    SW_EXPECT_TRUE( reader.hasOverflowed() );

    // 끝나지 않는 가변 정수.
    const uint8 arrBroken[2] = { 0xFF, 0xFF };
    BitReader   broken( arrBroken, 2 );
    (void)broken.readVarUint();
    SW_EXPECT_TRUE( broken.hasOverflowed() );
}

SW_TEST_CASE( NetworkTest, SequencesWrapAndSequenceBufferForgetsStaleEntries )
{
    SW_EXPECT_TRUE( NetSequence::isGreater( 1, 65535 ) );
    SW_EXPECT_TRUE( NetSequence::isLess( 65530, 3 ) );
    SW_EXPECT_FALSE( NetSequence::isGreater( 100, 200 ) );
    SW_EXPECT_EQUAL( 4, NetSequence::computeDifference( 2, 65534 ) );
    SW_EXPECT_EQUAL( -4, NetSequence::computeDifference( 65534, 2 ) );

    SequenceBuffer<int32> buffer( 8 );
    *buffer.insert( 65534 ) = 1;
    *buffer.insert( 3 )     = 2; // 감겨서 앞섰다 — 사이(65535 · 0 · 1 · 2)는 비운다
    SW_EXPECT_TRUE( buffer.find( 65534 ) != nullptr && *buffer.find( 65534 ) == 1 );
    SW_EXPECT_TRUE( buffer.exists( 0 ) == false );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( buffer.getNewest() ) );
    SW_EXPECT_NULL( buffer.insert( 65000 ) ); // 너무 낡았다
    *buffer.insert( 20 ) = 3;
    SW_EXPECT_FALSE( buffer.exists( 65534 ) ); // 크기만큼 지나 덮였다
}

SW_TEST_CASE( NetworkTest, ConnectionsResendReliableMessagesInOrderAndDropStaleSequenced )
{
    NetConnection sender;
    NetConnection receiver;
    for ( int32 index = 0; index < 40; ++index )
    {
        const vector<uint8> buffer = makeMessage( index, 40 );
        SW_ASSERT_TRUE( sender.sendMessage( NetChannelType::ReliableOrdered, buffer.data(), static_cast<int32>( buffer.size() ) ) );
    }
    const vector<uint8> tooBig( static_cast<size_t>( NetConnection::kMaxMessageSize + 1 ), 0 );
    SW_EXPECT_FALSE( sender.sendMessage( NetChannelType::ReliableOrdered, tooBig.data(), static_cast<int32>( tooBig.size() ) ) );

    // 세 패킷 중 하나를 잃고, 받는 쪽은 매번 확인을 돌려준다.
    vector<int32> listReceived;
    float64       time = 0.0;
    for ( int32 round = 0; round < 200 && static_cast<int32>( listReceived.size() ) < 40; ++round )
    {
        time += 0.05;
        BitWriter writer;
        sender.writePacket( time, writer, 300 );
        if ( round % 3 != 1 )
        {
            BitReader reader( writer.getBytes().data(), writer.getByteCount() );
            SW_EXPECT_TRUE( receiver.readPacket( time + 0.02, reader ) );
            // 같은 패킷이 두 번 오면 두 번째는 버린다.
            BitReader duplicate( writer.getBytes().data(), writer.getByteCount() );
            SW_EXPECT_FALSE( receiver.readPacket( time + 0.03, duplicate ) );
        }
        BitWriter ackWriter;
        receiver.writePacket( time + 0.02, ackWriter, 300 );
        BitReader ackReader( ackWriter.getBytes().data(), ackWriter.getByteCount() );
        (void)sender.readPacket( time + 0.04, ackReader );
        vector<uint8> buffer;
        while ( receiver.receiveMessage( NetChannelType::ReliableOrdered, buffer ) )
            listReceived.push_back( readMessageValue( buffer ) );
    }
    SW_ASSERT_TRUE( listReceived.size() == 40 );
    for ( int32 index = 0; index < 40; ++index )
        SW_EXPECT_EQUAL( index, listReceived[static_cast<size_t>( index )] );
    SW_EXPECT_TRUE( sender.getStats()._resentMessageCount > 0 );
    SW_EXPECT_EQUAL( 0, sender.getPendingReliableCount() );
    SW_EXPECT_NEAR_EQUAL( 0.04f, sender.getStats()._rtt, 0.01f );

    // 순서만 — 한 패킷에는 가장 새 것 하나, 늦게 온 옛 패킷은 버린다.
    NetConnection fresh;
    NetConnection late;
    for ( int32 index = 0; index < 3; ++index )
    {
        const vector<uint8> buffer = makeMessage( index );
        SW_ASSERT_TRUE( fresh.sendMessage( NetChannelType::UnreliableSequenced, buffer.data(), 4 ) );
    }
    BitWriter firstPacket;
    fresh.writePacket( 0.0, firstPacket, 300 );
    const vector<uint8> newer = makeMessage( 9 );
    SW_ASSERT_TRUE( fresh.sendMessage( NetChannelType::UnreliableSequenced, newer.data(), 4 ) );
    BitWriter secondPacket;
    fresh.writePacket( 0.1, secondPacket, 300 );
    BitReader secondReader( secondPacket.getBytes().data(), secondPacket.getByteCount() );
    BitReader firstReader( firstPacket.getBytes().data(), firstPacket.getByteCount() );
    SW_EXPECT_TRUE( late.readPacket( 0.2, secondReader ) );
    SW_EXPECT_TRUE( late.readPacket( 0.3, firstReader ) ); // 패킷은 받지만 그 안의 옛 순서 메시지는 버린다
    vector<uint8> buffer;
    SW_ASSERT_TRUE( late.receiveMessage( NetChannelType::UnreliableSequenced, buffer ) );
    SW_EXPECT_EQUAL( 9, readMessageValue( buffer ) );
    SW_EXPECT_FALSE( late.receiveMessage( NetChannelType::UnreliableSequenced, buffer ) );
}

SW_TEST_CASE( NetworkTest, HostsHandshakeExchangeAndSurviveBadNetworks )
{
    NetHostSettings settings;
    settings._maxConnections = 2;
    NetTestPair pair( settings );
    SW_ASSERT_TRUE( pair._server.listen() );
    SW_ASSERT_TRUE( pair._client.connect( NetAddress::makeLoopback( 4000 ) ) );
    SW_EXPECT_TRUE( pair._client.getConnectionState( 0 ) == NetConnectionState::Connecting );
    pair.run( 0.3 );
    SW_ASSERT_TRUE( pair._client.getConnectionState( 0 ) == NetConnectionState::Connected );
    SW_EXPECT_EQUAL( 1, pair._server.getConnectedCount() );
    SW_EXPECT_EQUAL( 0, pair._client.getClientIndex() );
    vector<NetHostEvent> listEvent;
    pair._server.drainEvents( listEvent );
    SW_EXPECT_TRUE( listEvent.size() == 1 && listEvent[0]._kind == NetHostEvent::Kind::Connected );

    // 나쁜 망 — 지연 80 ms ± 30, 손실 20 %, 중복 10 %, 깨짐 5 %. 신뢰 메시지 200 개가 순서대로 한 번씩.
    LoopbackConditions conditions;
    conditions._latency       = 0.08f;
    conditions._jitter        = 0.03f;
    conditions._lossRate      = 0.2f;
    conditions._duplicateRate = 0.1f;
    conditions._corruptRate   = 0.05f;
    pair._network.setConditions( conditions );
    for ( int32 index = 0; index < 200; ++index )
    {
        const vector<uint8> buffer = makeMessage( index, 24 );
        SW_ASSERT_TRUE( pair._client.sendMessage( 0, NetChannelType::ReliableOrdered, buffer ) );
    }
    vector<int32> listReceived;
    for ( int32 frame = 0; frame < 60 * 20 && listReceived.size() < 200; ++frame )
    {
        pair.run( 1.0 / 60.0 );
        int32          connectionId = -1;
        NetChannelType channel      = NetChannelType::Unreliable;
        vector<uint8>  buffer;
        while ( pair._server.receiveMessage( connectionId, channel, buffer ) )
        {
            SW_EXPECT_EQUAL( 0, connectionId );
            listReceived.push_back( readMessageValue( buffer ) );
        }
    }
    SW_ASSERT_TRUE( listReceived.size() == 200 );
    for ( int32 index = 0; index < 200; ++index )
        SW_EXPECT_EQUAL( index, listReceived[static_cast<size_t>( index )] );
    // RTT · 손실률 · 깨짐은 패킷이 쌓여야 보인다(메시지 200 개는 패킷 스무 개 남짓에 다 실렸다) — 6 초 동안 매 프레임 입력 같은 비신뢰 메시지를 보낸다.
    const NetConnectionStats& stats = pair._client.findConnection( 0 )->getStats();
    for ( int32 frame = 0; frame < 60 * 6; ++frame )
    {
        const vector<uint8> input = makeMessage( frame, 8 );
        SW_ASSERT_TRUE( pair._client.sendMessage( 0, NetChannelType::Unreliable, input ) );
        pair.run( 1.0 / 60.0 );
    }
    SW_EXPECT_TRUE( pair._server.getRejectedPacketCount() > 0 );              // 깨진 패킷을 체크섬이 걸렀다
    SW_EXPECT_TRUE( stats._rtt > 0.12f && stats._rtt < 0.3f );                // 지연 80 ms 왕복 + 흔들림
    SW_EXPECT_TRUE( stats._packetLoss > 0.12f && stats._packetLoss < 0.45f ); // 보낸 쪽 손실 20 % + 깨짐 5 %
    SW_EXPECT_EQUAL( 1, pair._server.getConnectedCount() );                   // 나쁜 망에서도 끊기지 않았다

    // 서버 → 클라이언트 방송.
    pair._network.setConditions( LoopbackConditions{} );
    const vector<uint8> hello = makeMessage( 4242 );
    SW_EXPECT_EQUAL( 1, pair._server.broadcast( NetChannelType::ReliableOrdered, hello.data(), 4 ) );
    pair.run( 0.2 );
    int32          connectionId = -1;
    NetChannelType channel      = NetChannelType::Unreliable;
    vector<uint8>  buffer;
    SW_ASSERT_TRUE( pair._client.receiveMessage( connectionId, channel, buffer ) );
    SW_EXPECT_EQUAL( 4242, readMessageValue( buffer ) );
    SW_EXPECT_TRUE( channel == NetChannelType::ReliableOrdered );
}

SW_TEST_CASE( NetworkTest, HostsRejectFullServersForeignProtocolsAndDetectDisconnects )
{
    NetHostSettings settings;
    settings._maxConnections = 1;
    settings._timeout        = 1.0f;
    LoopbackNetwork network( 5u );
    NetHost         server;
    NetHost         first;
    NetHost         second;
    NetHost         foreign;
    server.initialize( network.createEndpoint( 4000 ), settings );
    first.initialize( network.createEndpoint( 5001 ), settings );
    second.initialize( network.createEndpoint( 5002 ), settings );
    NetHostSettings otherGame = settings;
    otherGame._protocolId     = 0xDEADBEEFu;
    foreign.initialize( network.createEndpoint( 5003 ), otherGame );
    SW_EXPECT_TRUE( network.createEndpoint( 4000 ) == nullptr );
    SW_ASSERT_TRUE( server.listen() );
    SW_ASSERT_TRUE( first.connect( NetAddress::makeLoopback( 4000 ) ) );
    float64    time   = 0.0;
    const auto runAll = [&]( float64 seconds, bool bServer, bool bFirst )
    {
        for ( float64 elapsed = 0.0; elapsed < seconds; elapsed += 1.0 / 60.0 )
        {
            time += 1.0 / 60.0;
            if ( bServer )
                server.update( time );
            if ( bFirst )
                first.update( time );
            second.update( time );
            foreign.update( time );
        }
    };
    runAll( 0.3, true, true );
    SW_ASSERT_TRUE( first.getConnectionState( 0 ) == NetConnectionState::Connected );

    // 가득 찬 서버 · 다른 게임.
    SW_ASSERT_TRUE( second.connect( NetAddress::makeLoopback( 4000 ) ) );
    SW_ASSERT_TRUE( foreign.connect( NetAddress::makeLoopback( 4000 ) ) );
    runAll( 0.5, true, true );
    vector<NetHostEvent> listEvent;
    second.drainEvents( listEvent );
    SW_ASSERT_TRUE( listEvent.size() == 1 );
    SW_EXPECT_TRUE( listEvent[0]._reason == NetDisconnectReason::ServerFull );
    SW_EXPECT_TRUE( foreign.getConnectionState( 0 ) == NetConnectionState::Connecting ); // 서버는 남의 패킷에 답하지 않는다
    SW_EXPECT_TRUE( server.getRejectedPacketCount() > 0 );
    runAll( 5.0, true, true );
    listEvent.clear();
    foreign.drainEvents( listEvent );
    SW_EXPECT_TRUE( listEvent.size() == 1 && listEvent[0]._reason == NetDisconnectReason::Timeout );

    // 끊기 — 클라이언트가 끊으면 서버가 알고, 자리가 빈다.
    first.disconnect( 0 );
    runAll( 0.2, true, true );
    listEvent.clear();
    server.drainEvents( listEvent );
    SW_ASSERT_TRUE( listEvent.empty() == false );
    SW_EXPECT_TRUE( listEvent.back()._kind == NetHostEvent::Kind::Disconnected && listEvent.back()._reason == NetDisconnectReason::Remote );
    SW_EXPECT_EQUAL( 0, server.getConnectedCount() );

    // 타임아웃 — 서버가 멈추면 클라이언트는 1 초 뒤 끊긴 것으로 안다.
    SW_ASSERT_TRUE( first.connect( NetAddress::makeLoopback( 4000 ) ) );
    runAll( 0.3, true, true );
    SW_ASSERT_TRUE( first.getConnectionState( 0 ) == NetConnectionState::Connected );
    first.drainEvents( listEvent );
    listEvent.clear();
    runAll( 1.5, false, true );
    first.drainEvents( listEvent );
    SW_ASSERT_TRUE( listEvent.empty() == false );
    SW_EXPECT_TRUE( listEvent.back()._reason == NetDisconnectReason::Timeout );
}

SW_TEST_CASE( NetworkTest, UdpTransportSendsDatagramsOverLocalhost )
{
    UdpNetTransport server;
    UdpNetTransport client;
    SW_ASSERT_TRUE( server.open( 0 ) );
    SW_ASSERT_TRUE( client.open( 0 ) );
    const NetAddress serverAddress = server.getLocalAddress();
    SW_EXPECT_TRUE( serverAddress._port != 0 );
    const uint8 arrData[5] = { 1, 2, 3, 4, 5 };
    SW_ASSERT_TRUE( client.send( serverAddress, arrData, 5 ) );
    NetAddress    from{};
    vector<uint8> buffer;
    bool          bReceived = false;
    for ( int32 attempt = 0; attempt < 200 && bReceived == false; ++attempt )
    {
        bReceived = server.receive( from, buffer );
        if ( bReceived == false )
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
    SW_ASSERT_TRUE( bReceived );
    SW_EXPECT_EQUAL( 5, static_cast<int32>( buffer.size() ) );
    SW_EXPECT_EQUAL( 5, buffer[4] );
    SW_EXPECT_EQUAL( static_cast<int32>( client.getLocalAddress()._port ), static_cast<int32>( from._port ) );
    SW_EXPECT_FALSE( server.receive( from, buffer ) );

    NetAddress parsed{};
    SW_EXPECT_TRUE( NetAddress::parse( "10.0.0.7:7777", 1, parsed ) );
    SW_EXPECT_TRUE( parsed == NetAddress::make( 10, 0, 0, 7, 7777 ) );
    SW_EXPECT_TRUE( parsed.toString() == "10.0.0.7:7777" );
    SW_EXPECT_TRUE( NetAddress::parse( "192.168.1.2", 3000, parsed ) && parsed._port == 3000 );
    SW_EXPECT_FALSE( NetAddress::parse( "300.1.1.1:5", 1, parsed ) );
    SW_EXPECT_FALSE( NetAddress::parse( "1.2.3", 1, parsed ) );
}

SW_TEST_CASE( NetworkTest, BitStreamKeepsWireLayoutAndRoundTripsRandomFields )
{
    // 선 위의 비트 배치는 바꾸지 않는다 — 바이트 단위로 빨라진 쓰기가 옛 비트 단위 쓰기와 같은 바이트를 낸다.
    BitWriter   writer;
    const uint8 arrRaw[5] = { 1, 2, 3, 0xFE, 0x80 };
    writer.writeBits( 5, 3 );
    writer.writeBool( true );
    writer.writeBits( 0xABCDE, 20 );
    writer.writeInt( -7, -100, 100 );
    writer.writeFloat( 3.25f );
    writer.writeQuantizedFloat( 1.5f, -10.0f, 10.0f, 0.01f );
    writer.writeVarUint( 300 );
    writer.writeVarInt( -12345 );
    writer.writeBytes( arrRaw, 5 ); // 바이트 경계가 아닌 곳
    writer.writeBits( 0xFFFFFFFFu, 32 );
    writer.alignToByte();
    writer.writeBytes( arrRaw, 3 ); // 바이트 경계
    const uint8 arrExpected[] = { 0xED, 0xCD, 0xAB, 0x5D, 0x00, 0x00, 0x50, 0x40, 0x7E, 0x64, 0x15, 0x88, 0x07, 0x0E,
                                  0x08, 0x10, 0x18, 0xF0, 0x07, 0xFC, 0xFF, 0xFF, 0xFF, 0x07, 0x01, 0x02, 0x03 };
    SW_ASSERT_EQUAL( static_cast<int32>( sizeof( arrExpected ) ), writer.getByteCount() );
    SW_EXPECT_EQUAL( 216, writer.getBitCount() );
    for ( size_t index = 0; index < sizeof( arrExpected ); ++index )
        SW_EXPECT_EQUAL( static_cast<int32>( arrExpected[index] ), static_cast<int32>( writer.getBytes()[index] ) );

    // 무작위 — 폭 1..32 비트와 경계를 가리지 않는 바이트 덩어리를 섞어 쓰고 그대로 읽는다. 쓰기를 비워도 다시 쓸 수 있다.
    uint32 state = 0x2545F491u;
    for ( int32 round = 0; round < 500; ++round )
    {
        writer.clear();
        uint32      arrValue[48]{};
        int32       arrWidth[48]{};
        uint8       arrBlob[48][6]{};
        const int32 fieldCount = 1 + static_cast<int32>( nextRandom( state ) % 48 );
        for ( int32 field = 0; field < fieldCount; ++field )
        {
            if ( nextRandom( state ) % 4 == 0 )
            {
                arrWidth[field] = -static_cast<int32>( nextRandom( state ) % 7 ); // 0..6 바이트
                for ( int32 byteIndex = 0; byteIndex < -arrWidth[field]; ++byteIndex )
                    arrBlob[field][byteIndex] = static_cast<uint8>( nextRandom( state ) );
                writer.writeBytes( arrBlob[field], -arrWidth[field] );
                continue;
            }
            arrWidth[field] = 1 + static_cast<int32>( nextRandom( state ) % 32 );
            arrValue[field] = nextRandom( state ) & ( arrWidth[field] == 32 ? 0xFFFFFFFFu : ( 1u << arrWidth[field] ) - 1u );
            writer.writeBits( arrValue[field], arrWidth[field] );
        }
        BitReader reader( writer.getBytes().data(), writer.getByteCount() );
        for ( int32 field = 0; field < fieldCount; ++field )
        {
            if ( arrWidth[field] <= 0 )
            {
                uint8 arrRead[6]{};
                SW_ASSERT_TRUE( reader.readBytes( arrRead, -arrWidth[field] ) );
                for ( int32 byteIndex = 0; byteIndex < -arrWidth[field]; ++byteIndex )
                    SW_ASSERT_EQUAL( static_cast<int32>( arrBlob[field][byteIndex] ), static_cast<int32>( arrRead[byteIndex] ) );
                continue;
            }
            SW_ASSERT_TRUE( reader.readBits( arrWidth[field] ) == arrValue[field] );
        }
        SW_ASSERT_FALSE( reader.hasOverflowed() );
        SW_EXPECT_TRUE( reader.getBitsRemaining() < 8 );
    }
}

SW_TEST_CASE( NetworkTest, IdleConnectionsSendKeepAlivesInsteadOfEveryInterval )
{
    NetTestPair pair;
    SW_ASSERT_TRUE( pair._server.listen() );
    SW_ASSERT_TRUE( pair._client.connect( NetAddress::makeLoopback( 4000 ) ) );
    pair.run( 0.5 );
    SW_ASSERT_TRUE( pair._client.getConnectionState( 0 ) == NetConnectionState::Connected );

    // 4 초 동안 아무것도 보내지 않는다 — 보낼 간격(1/30 초)마다가 아니라 유지 간격(0.25 초)마다 유지 패킷과 그 확인만 오간다.
    const NetConnectionStats& clientStats = pair._client.findConnection( 0 )->getStats();
    const NetConnectionStats& serverStats = pair._server.findConnection( 0 )->getStats();
    const uint64              clientSent  = clientStats._sentPacketCount;
    const uint64              serverSent  = serverStats._sentPacketCount;
    pair.run( 4.0 );
    const uint64 clientIdleSent = clientStats._sentPacketCount - clientSent;
    const uint64 serverIdleSent = serverStats._sentPacketCount - serverSent;
    SW_EXPECT_TRUE( clientIdleSent >= 8 && clientIdleSent <= 40 ); // 30 Hz 였다면 120
    SW_EXPECT_TRUE( serverIdleSent >= 8 && serverIdleSent <= 40 );
    SW_EXPECT_TRUE( clientStats._rtt > 0.0f && clientStats._rtt < 0.1f ); // 유지 패킷도 RTT 를 잰다(지연 없는 망)
    SW_EXPECT_TRUE( clientStats._packetLoss < 0.01f );                    // 확인만 담은 답은 확인을 바라지 않으니 잃은 것으로 세지 않는다
    SW_EXPECT_EQUAL( 1, pair._server.getConnectedCount() );

    // 보낼 것이 생기면 바로 — 다음 유지 시각을 기다리지 않는다. 메시지 길이 칸(11 비트)의 끝 1024 바이트까지 실린다.
    const vector<uint8> largest = makeMessage( 777, NetConnection::kMaxMessageSize );
    SW_ASSERT_TRUE( pair._client.sendMessage( 0, NetChannelType::ReliableOrdered, largest ) );
    SW_EXPECT_FALSE( pair._client.sendMessage( 0, NetChannelType::ReliableOrdered, makeMessage( 0, NetConnection::kMaxMessageSize + 1 ) ) );
    pair.run( 2.0 / 30.0 );
    int32          connectionId = -1;
    NetChannelType channel      = NetChannelType::Unreliable;
    vector<uint8>  buffer;
    SW_ASSERT_TRUE( pair._server.receiveMessage( connectionId, channel, buffer ) );
    SW_EXPECT_EQUAL( NetConnection::kMaxMessageSize, static_cast<int32>( buffer.size() ) );
    SW_EXPECT_EQUAL( 777, readMessageValue( buffer ) );
}

SW_TEST_CASE( NetworkTest, MessageRouterDispatchesByRangeAndKeepsUnhandled )
{
    NetTestPair pair;
    SW_ASSERT_TRUE( pair._server.listen() );
    SW_ASSERT_TRUE( pair._client.connect( NetAddress::makeLoopback( 4000 ) ) );
    pair.run( 0.3 );
    SW_ASSERT_TRUE( pair._client.getConnectionState( 0 ) == NetConnectionState::Connected );

    // 한 영역에 처리기 둘 — 등록 순서대로 묻고 처음 받아들인 쪽에서 멈춘다.
    CountingHandler  lockstep( NetMessageRange::kLockstep, 0x21 );
    CountingHandler  gameFirst( NetMessageRange::kGame, 0x81 );
    CountingHandler  gameSecond( NetMessageRange::kGame, 0x82 );
    NetMessageRouter router;
    router.addHandler( &lockstep );
    router.addHandler( &gameFirst );
    router.addHandler( &gameSecond );

    NetMessageWriter messageWriter;
    const uint8      arrKind[] = { 0x21, 0x81, 0x82, 0x82, 0x83, 0x51, 0x22 };
    for ( const uint8 kind : arrKind )
    {
        messageWriter.begin( kind ).writeVarUint( kind );
        SW_ASSERT_TRUE( messageWriter.send( pair._client, 0, NetChannelType::ReliableOrdered ) );
    }
    pair.run( 0.2 );
    vector<NetReceivedMessage> listUnhandled;
    SW_EXPECT_EQUAL( 7, router.pump( pair._server, &listUnhandled ) );
    SW_EXPECT_EQUAL( 1, lockstep.getHandledCount() );
    SW_EXPECT_EQUAL( 1, gameFirst.getHandledCount() );
    SW_EXPECT_EQUAL( 2, gameSecond.getHandledCount() );
    SW_EXPECT_EQUAL( 0, gameSecond.getLastConnectionId() );
    // 거절(0x83 · 0x22)과 처리기 없는 영역(0x51)은 받은 순서대로 남는다.
    SW_ASSERT_EQUAL( 3, static_cast<int32>( listUnhandled.size() ) );
    SW_EXPECT_EQUAL( 0x83, static_cast<int32>( listUnhandled[0]._buffer[0] ) );
    SW_EXPECT_EQUAL( 0x51, static_cast<int32>( listUnhandled[1]._buffer[0] ) );
    SW_EXPECT_EQUAL( 0x22, static_cast<int32>( listUnhandled[2]._buffer[0] ) );
    SW_EXPECT_EQUAL( 0, listUnhandled[0]._connectionId );

    // 뺀 처리기는 더 묻지 않는다. 빈 메시지는 누구에게도 가지 않는다.
    router.removeHandler( &gameFirst );
    SW_EXPECT_FALSE( router.dispatch( 0, messageWriter.begin( 0x81 ).getBytes().data(), 1 ) );
    SW_EXPECT_FALSE( router.dispatch( 0, nullptr, 0 ) );
    SW_EXPECT_EQUAL( 1, gameFirst.getHandledCount() );
}

SW_TEST_CASE( NetworkTest, ServerTellsManyClientsApartByAddress )
{
    constexpr int32 kClientCount = 40;
    NetHostSettings settings;
    settings._maxConnections = 48;
    LoopbackNetwork network( 11u );
    NetHost         server;
    server.initialize( network.createEndpoint( 4000 ), settings );
    SW_ASSERT_TRUE( server.listen() );
    NetHost arrClient[kClientCount];
    for ( int32 index = 0; index < kClientCount; ++index )
    {
        arrClient[index].initialize( network.createEndpoint( static_cast<uint16>( 6000 + index ) ), settings );
        SW_ASSERT_TRUE( arrClient[index].connect( NetAddress::makeLoopback( 4000 ) ) );
    }
    float64    time   = 0.0;
    const auto runAll = [&]( float64 seconds )
    {
        for ( float64 elapsed = 0.0; elapsed < seconds; elapsed += 1.0 / 60.0 )
        {
            time += 1.0 / 60.0;
            server.update( time );
            for ( NetHost& client : arrClient )
                client.update( time );
        }
    };
    runAll( 0.5 );
    SW_ASSERT_EQUAL( kClientCount, server.getConnectedCount() );

    // 클라이언트마다 자기 번호를 보낸다 — 서버의 연결 id 가 그 클라이언트의 주소와 맞아야 한다.
    for ( int32 index = 0; index < kClientCount; ++index )
        SW_ASSERT_TRUE( arrClient[index].sendMessage( 0, NetChannelType::ReliableOrdered, makeMessage( index ) ) );
    runAll( 0.2 );
    int32          connectionId = -1;
    NetChannelType channel      = NetChannelType::Unreliable;
    vector<uint8>  buffer;
    int32          receivedCount = 0;
    while ( server.receiveMessage( connectionId, channel, buffer ) )
    {
        const int32 clientIndex = readMessageValue( buffer );
        SW_EXPECT_EQUAL( static_cast<int32>( 6000 + clientIndex ), static_cast<int32>( server.getConnectionAddress( connectionId )._port ) );
        ++receivedCount;
    }
    SW_EXPECT_EQUAL( kClientCount, receivedCount );

    // 하나가 끊고 다시 붙어도 주소로 바른 자리를 찾는다.
    arrClient[7].disconnect( 0 );
    runAll( 0.2 );
    SW_EXPECT_EQUAL( kClientCount - 1, server.getConnectedCount() );
    SW_ASSERT_TRUE( arrClient[7].connect( NetAddress::makeLoopback( 4000 ) ) );
    runAll( 0.5 );
    SW_ASSERT_EQUAL( kClientCount, server.getConnectedCount() );
    SW_ASSERT_TRUE( arrClient[7].sendMessage( 0, NetChannelType::ReliableOrdered, makeMessage( 7 ) ) );
    runAll( 0.2 );
    SW_ASSERT_TRUE( server.receiveMessage( connectionId, channel, buffer ) );
    SW_EXPECT_EQUAL( 6007, static_cast<int32>( server.getConnectionAddress( connectionId )._port ) );
}
