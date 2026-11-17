#include "pch.h"

#include "Core/Network/Transport/NetTransport.h"
#include "Core/Network/Transport/PlatformSocketUtil.h"
#include "Core/Network/Transport/UDPNetTransport.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/MonotonicClock.h"

// 전송 — 실제 UDP 소켓(포트 0 · 시한 있는 대기), 주소 풀기, 루프백 망의 배달 순서.

using namespace sw;

namespace
{
    /** @brief @p tryReceive 가 참이 될 때까지 소켓이 읽을 것을 가질 때마다 다시 부릅니다(최대 5 초 — 고정 시간을 자지 않는다). */
    template <typename FTryReceive>
    bool receiveUDPTestDatagramWithin( INetTransport& transport, FTryReceive&& tryReceive )
    {
        const Deadline deadline = Deadline::afterMilliseconds( 5000 );
        while ( tryReceive() == false )
        {
            if ( deadline.isExpired() )
                return false;
            (void)transport.waitForReceive( 0.05 ); // 데이터그램이 오면 바로 깬다 — 결과는 다음 tryReceive 가 본다
        }
        return true;
    }
} // namespace

SW_TEST_CASE( NetTransportTest, UDPTransportSendsDatagramsOverLocalhost )
{
    UDPNetTransport server;
    UDPNetTransport client;
    SW_REQUIRE_TRUE( server.open( 0, true ) );
    SW_REQUIRE_TRUE( client.open( 0, true ) );
    const NetAddress serverAddress = server.getLocalAddress();
    SW_EXPECT_TRUE( serverAddress._port != 0 );
    const uint8 arrData[5] = { 1, 2, 3, 4, 5 };
    SW_REQUIRE_TRUE( client.send( serverAddress, arrData, 5 ) );
    NetAddress    from{};
    vector<uint8> buffer;
    SW_REQUIRE_TRUE( receiveUDPTestDatagramWithin( server, [&]()
    { return server.receive( from, buffer ); } ) );
    SW_EXPECT_EQUAL( 5, static_cast<int32>( buffer.size() ) );
    SW_EXPECT_EQUAL( 5, buffer[4] );
    SW_EXPECT_EQUAL( static_cast<int32>( client.getLocalAddress()._port ), static_cast<int32>( from._port ) );
    SW_EXPECT_FALSE( server.receive( from, buffer ) );

    // 닫힌 포트로 보낸 뒤에도 받기가 계속된다 — Windows 는 돌아온 ICMP "포트 닿지 않음" 을 다음 recvfrom 의 WSAECONNRESET 으로 알린다.
    NetAddress closedAddress{};
    {
        UDPNetTransport closed;
        SW_REQUIRE_TRUE( closed.open( 0, true ) );
        closedAddress = closed.getLocalAddress();
    }
    SW_REQUIRE_TRUE( client.send( closedAddress, arrData, 5 ) );
    (void)client.waitForReceive( 0.02 ); // 돌아올 ICMP 를 잠깐 기다린다(오면 바로 깬다) — 와도 안 와도 아래 받기는 성공해야 한다
    SW_REQUIRE_TRUE( server.send( client.getLocalAddress(), arrData, 3 ) );
    SW_REQUIRE_TRUE( receiveUDPTestDatagramWithin( client, [&]()
    { return client.receive( from, buffer ); } ) );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( buffer.size() ) );
    SW_EXPECT_EQUAL( static_cast<int32>( serverAddress._port ), static_cast<int32>( from._port ) );

    // 일괄 보내기 · 받기 — 크기가 맞지 않는 데이터그램은 손실로 세고, 나머지는 보낸 순서대로 도착한다.
    NetDatagramView arrView[3];
    const uint8     arrBig[kNetMaxPacketSize + 1]{};
    arrView[0] = NetDatagramView{ serverAddress, arrData, 5 };
    arrView[1] = NetDatagramView{ serverAddress, arrBig, kNetMaxPacketSize + 1 }; // 한도 초과 — 손실
    arrView[2] = NetDatagramView{ serverAddress, arrData, 3 };
    SW_EXPECT_EQUAL( 2, client.sendBatch( arrView, 3 ) );
    NetReceivedDatagram arrReceived[8];
    int32               batchReceived = 0;
    (void)receiveUDPTestDatagramWithin( server, [&]()
    {
        batchReceived += server.receiveBatch( arrReceived + batchReceived, 8 - batchReceived );
        return batchReceived >= 2;
    } ); // 결과는 아래 단언이 본다
    SW_REQUIRE_EQUAL( 2, batchReceived );
    SW_EXPECT_EQUAL( 5, static_cast<int32>( arrReceived[0]._buffer.size() ) );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( arrReceived[1]._buffer.size() ) );
    SW_EXPECT_EQUAL( static_cast<int32>( client.getLocalAddress()._port ), static_cast<int32>( arrReceived[1]._from._port ) );
    SW_EXPECT_EQUAL( 0, server.receiveBatch( arrReceived, 8 ) );

    NetAddress parsed{};
    SW_EXPECT_TRUE( NetAddress::parse( "10.0.0.7:7777", 1, parsed ) );
    SW_EXPECT_TRUE( parsed == NetAddress::make( 10, 0, 0, 7, 7777 ) );
    SW_EXPECT_TRUE( parsed.toString() == "10.0.0.7:7777" );
    SW_EXPECT_TRUE( NetAddress::make( 203, 0, 113, 77, 7777 ).toMaskedString() == "203.0.113.x" ); // 로그용 — 마지막 옥텟과 포트를 가린다
    SW_EXPECT_TRUE( NetAddress::parse( "192.168.1.2", 3000, parsed ) && parsed._port == 3000 );
    SW_EXPECT_FALSE( NetAddress::parse( "300.1.1.1:5", 1, parsed ) );
    SW_EXPECT_FALSE( NetAddress::parse( "1.2.3", 1, parsed ) );
}

SW_TEST_CASE( NetTransportTest, ResolveAddressReadsNumbersAndAsksTheResolverForNames )
{
    // 점 넷 숫자는 해석기를 거치지 않고, 이름은 운영체제 해석기(hosts 파일 포함)로 푼다. 풀리지 않거나 포트가 깨지면 false — 다른 주소로 대신하지 않는다.
    NetAddress resolved{};
    SW_EXPECT_TRUE( PlatformSocketUtil::resolveAddress( "10.0.0.7:7777", 1, resolved ) );
    SW_EXPECT_TRUE( resolved == NetAddress::make( 10, 0, 0, 7, 7777 ) );
    SW_EXPECT_TRUE( PlatformSocketUtil::resolveAddress( "localhost:6380", 1, resolved ) );
    SW_EXPECT_TRUE( resolved == NetAddress::makeLoopback( 6380 ) );
    SW_EXPECT_TRUE( PlatformSocketUtil::resolveAddress( "localhost", 6379, resolved ) && resolved._port == 6379 );
    SW_EXPECT_FALSE( PlatformSocketUtil::resolveAddress( "localhost:70000", 1, resolved ) );
    SW_EXPECT_FALSE( PlatformSocketUtil::resolveAddress( ":7777", 1, resolved ) );
    SW_EXPECT_FALSE( PlatformSocketUtil::resolveAddress( "no-such-host.invalid:7777", 1, resolved ) );
}

/**
 * @brief [NetTransportTest] 루프백 망은 회선을 나쁘게 하지 않는다 — 보낸 순서대로 다음 `update` 에 배달하고, 받을 엔드포인트가 없는 패킷은 버린 수로 센다
 */
SW_TEST_CASE( NetTransportTest, LoopbackDeliversInSendOrderOnNextUpdate )
{
    LoopbackNetwork    network;
    LoopbackTransport* pSender   = network.createEndpoint( 7000 );
    LoopbackTransport* pReceiver = network.createEndpoint( 7001 );
    SW_REQUIRE_NOT_NULL( pSender );
    SW_REQUIRE_NOT_NULL( pReceiver );
    SW_EXPECT_NULL( network.createEndpoint( 7001 ) ); // 한 포트에 엔드포인트 하나
    for ( uint8 number = 0; number < 5; ++number )
    {
        SW_REQUIRE_TRUE( pSender->send( NetAddress::makeLoopback( 7001 ), &number, 1 ) );
    }
    const uint8 orphan = 9;
    SW_REQUIRE_TRUE( pSender->send( NetAddress::makeLoopback( 7999 ), &orphan, 1 ) ); // 받을 엔드포인트가 없다

    NetAddress    from{};
    vector<uint8> buffer;
    SW_EXPECT_FALSE( pReceiver->receive( from, buffer ) ); // 다음 update 까지는 날아가는 중
    pReceiver->update( 0.0 );                              // 어느 엔드포인트의 update 든 망 전체를 배달한다
    for ( uint8 number = 0; number < 5; ++number )
    {
        SW_REQUIRE_TRUE( pReceiver->receive( from, buffer ) );
        SW_EXPECT_TRUE( from == NetAddress::makeLoopback( 7000 ) );
        SW_EXPECT_EQUAL( static_cast<int32>( number ), static_cast<int32>( buffer[0] ) );
    }
    SW_EXPECT_FALSE( pReceiver->receive( from, buffer ) );
    SW_EXPECT_EQUAL( uint64{ 5 }, network.getDeliveredCount() );
    SW_EXPECT_EQUAL( uint64{ 1 }, network.getDroppedCount() );
}

/**
 * @brief [NetTransportTest] IPv6 루프백(`::1`)으로 실제 UDP 데이터그램이 오가고, 받은 주소는 IPv6 그대로다(감사 N-14)
 * @details IPv6 를 끈 기계에서는 소켓이 열리지 않아 진다 — IPv4 로 대신 열지 않는다(`PlatformSocketUtil::openUDPSocket`).
 */
SW_TEST_CASE( NetTransportTest, UDPTransportSendsDatagramsOverIpv6Loopback )
{
    UDPNetTransport server;
    UDPNetTransport client;
    SW_REQUIRE_TRUE( server.open( 0, true, UDPPortSharing::Exclusive, NetAddressFamily::Ipv6 ) );
    SW_REQUIRE_TRUE( client.open( 0, true, UDPPortSharing::Exclusive, NetAddressFamily::Ipv6 ) );
    const NetAddress serverAddress = server.getLocalAddress();
    SW_EXPECT_TRUE( serverAddress.isIpv6() && serverAddress.isLoopback() && serverAddress._port != 0 );
    const uint8 arrData[4] = { 6, 7, 8, 9 };
    SW_REQUIRE_TRUE( client.send( serverAddress, arrData, 4 ) );
    NetAddress    from{};
    vector<uint8> buffer;
    SW_REQUIRE_TRUE( receiveUDPTestDatagramWithin( server, [&]()
    { return server.receive( from, buffer ); } ) );
    SW_EXPECT_EQUAL( 4, static_cast<int32>( buffer.size() ) );
    SW_EXPECT_TRUE( from == client.getLocalAddress() ); // ::1 과 보낸 포트

    // IPv4 소켓은 IPv6 상대에게 보내지 못한다 — 다른 주소군으로 바꿔 보내지 않는다.
    UDPNetTransport ipv4Only;
    SW_REQUIRE_TRUE( ipv4Only.open( 0, true ) );
    SW_EXPECT_FALSE( ipv4Only.send( serverAddress, arrData, 4 ) );
}

/**
 * @brief [NetTransportTest] IPv6 주소 글은 RFC 4291 로 읽고 RFC 5952 로 쓴다. IPv4 사상 주소는 IPv4 로 되돌린다. 깨진 글은 false 다(감사 N-14)
 */
SW_TEST_CASE( NetTransportTest, Ipv6AddressTextRoundTrips )
{
    NetAddress parsed{};
    SW_REQUIRE_TRUE( NetAddress::parse( "[::1]:7777", 1, parsed ) );
    SW_EXPECT_TRUE( parsed == NetAddress::makeIpv6Loopback( 7777 ) );
    SW_EXPECT_TRUE( parsed.toString() == "[::1]:7777" );
    SW_REQUIRE_TRUE( NetAddress::parse( "2001:DB8:0:0:1:0:0:1", 3000, parsed ) ); // 대괄호가 없으면 포트도 없다
    SW_EXPECT_EQUAL( 3000, static_cast<int32>( parsed._port ) );
    SW_EXPECT_TRUE( parsed.toString() == "[2001:db8::1:0:0:1]:3000" ); // 같은 길이의 0 구간은 앞의 것을 줄인다
    SW_EXPECT_TRUE( parsed.toMaskedString() == "2001:db8::x" );        // 앞 48 비트 — 포트와 사람을 가린다
    SW_REQUIRE_TRUE( NetAddress::parse( "[::]:5", 1, parsed ) );
    SW_EXPECT_TRUE( parsed.isAnyInterface() && parsed.isIpv6() );
    SW_REQUIRE_TRUE( NetAddress::parse( "[fd12:3456::7]:9", 1, parsed ) );
    SW_EXPECT_TRUE( parsed.isPrivate() && parsed.isLoopbackOrPrivate() );
    SW_REQUIRE_TRUE( NetAddress::parse( "[::ffff:c000:280]:80", 1, parsed ) ); // 192.0.2.128 의 사상 주소
    SW_EXPECT_TRUE( parsed == NetAddress::make( 192, 0, 2, 128, 80 ) );
    SW_EXPECT_TRUE( NetAddress::make( 192, 0, 2, 128, 80 ).toIpv4Mapped().toUnmapped() == NetAddress::make( 192, 0, 2, 128, 80 ) );

    SW_EXPECT_FALSE( NetAddress::parse( "[::1", 1, parsed ) );
    SW_EXPECT_FALSE( NetAddress::parse( "[::1]7777", 1, parsed ) );
    SW_EXPECT_FALSE( NetAddress::parse( "[::1]:0", 1, parsed ) );
    SW_EXPECT_FALSE( NetAddress::parse( "1::2::3", 1, parsed ) );
    SW_EXPECT_FALSE( NetAddress::parse( ":::", 1, parsed ) );
    SW_EXPECT_FALSE( NetAddress::parse( "12345::1", 1, parsed ) );
    SW_EXPECT_FALSE( NetAddress::parse( "1:2:3:4:5:6:7:8:9", 1, parsed ) );
    SW_EXPECT_FALSE( NetAddress::parse( "1:2:3:4:5:6:7::8", 1, parsed ) ); // `::` 는 묶음 하나 이상이다
    SW_EXPECT_FALSE( NetAddress::parse( "1.2.3.4.5", 1, parsed ) );
    SW_EXPECT_TRUE( NetAddress::makeLoopback( 1 ) != NetAddress::makeIpv6Loopback( 1 ) );
}
