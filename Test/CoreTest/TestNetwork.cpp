#include "pch.h"

#include "Core/Network/BitStream.h"
#include "Core/Network/NetConnection.h"
#include "Core/Network/NetHost.h"
#include "Core/Network/NetTransport.h"
#include "Core/Network/SequenceBuffer.h"
#include "Core/Network/UdpNetTransport.h"

#include "TestFramework/TestFramework.h"

// 네트워크 공통 계층 — 비트 스트림, 시퀀스 감김, 신뢰성(재전송 · 순서 · 중복 · 옛것 버리기 · RTT), 핸드셰이크(도전 · 가득 참 · 다른 프로토콜),
// 나쁜 망(지연 · 흔들림 · 손실 · 중복 · 깨짐)에서의 신뢰 순서, 끊기 · 타임아웃, 실제 UDP 소켓.

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
    SW_EXPECT_TRUE( pair._server.getRejectedPacketCount() > 0 ); // 깨진 패킷을 체크섬이 걸렀다
    const NetConnectionStats& stats = pair._client.findConnection( 0 )->getStats();
    SW_EXPECT_TRUE( stats._rtt > 0.12f && stats._rtt < 0.3f );
    pair.run( 6.0 );                                                          // 손실률은 패킷이 쌓여야 보인다(메시지 200 개는 패킷 스무 개 남짓에 다 실렸다)
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
