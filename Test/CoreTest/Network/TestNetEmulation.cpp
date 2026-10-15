#include "pch.h"

#include "Core/Network/Transport/NetEmulation.h"
#include "Core/Network/Transport/NetTransport.h"

#include "TestFramework/TestFramework.h"

// 네트워크 흉내 — 지연 · 흔들림, 손실 · 중복 비율, 일부러 늦춘 패킷이 앞지르기를 당함, 대역폭 줄과 큐 넘침, 연결별 덮어쓰기, 깨짐(한 바이트), 꺼지면 그대로.
// 전역 변수(-gv_netEmu*)로 조건을 만드는 것은 EngineTest 의 NetClientServerTest 가 본다(전역 변수 표가 엔진에 있다).

using namespace sw;

namespace
{
    /** @brief 완벽한 루프백 위에 흉내를 씌운 보내는 쪽 · 받는 쪽입니다. */
    struct NetEmulationTestLink
    {
        LoopbackNetwork       _network;
        LoopbackTransport*    _pSenderEndpoint{ _network.createEndpoint( 7000 ) };
        LoopbackTransport*    _pReceiver{ _network.createEndpoint( 7001 ) };
        NetEmulationTransport _sender{ _pSenderEndpoint, 17u };

        /** @brief 시간을 넘기고 받은 패킷의 첫 바이트(일련번호)를 모읍니다. */
        void advance( float64 time, vector<int32>& outListReceived )
        {
            _sender.update( time );
            _network.deliverInFlight();
            NetAddress    from{};
            vector<uint8> buffer;
            while ( _pReceiver->receive( from, buffer ) )
            {
                outListReceived.push_back( buffer.empty() ? -1 : static_cast<int32>( buffer[0] ) | ( buffer.size() > 1 ? static_cast<int32>( buffer[1] ) << 8 : 0 ) );
            }
        }

        bool sendNumbered( int32 number, int32 size = 2 )
        {
            vector<uint8> buffer( static_cast<size_t>( size ), 0 );
            buffer[0] = static_cast<uint8>( number & 0xFF );
            if ( size > 1 )
                buffer[1] = static_cast<uint8>( ( number >> 8 ) & 0xFF );
            return _sender.send( NetAddress::makeLoopback( 7001 ), buffer.data(), size );
        }
    };

    /** @brief @p buffer 에서 @p fill 과 다른 바이트 수입니다. */
    int32 countChangedBytes( const vector<uint8>& buffer, uint8 fill )
    {
        int32 count = 0;
        for ( const uint8 byte : buffer )
        {
            count += byte != fill ? 1 : 0;
        }
        return count;
    }
} // namespace

/**
 * @brief [NetEmulationTest] 조건이 꺼져 있으면 바로 넘기고, 지연이 있으면 그 시간이 지나야 닿는다(흔들림 범위 안)
 */
SW_TEST_CASE( NetEmulationTest, LatencyHoldsPacketsUntilTheirTime )
{
    NetEmulationTestLink link;
    vector<int32>        listReceived;
    SW_ASSERT_TRUE( link.sendNumbered( 1 ) );
    link.advance( 0.0, listReceived );
    SW_ASSERT_EQUAL( size_t( 1 ), listReceived.size() ); // 꺼져 있으면 그대로

    NetEmulationConditions conditions;
    conditions._latency = 0.1;
    conditions._jitter  = 0.02;
    link._sender.setDefaultConditions( conditions );
    listReceived.clear();
    for ( int32 number = 0; number < 20; ++number )
    {
        SW_ASSERT_TRUE( link.sendNumbered( number ) );
    }
    link.advance( 0.079, listReceived );
    SW_EXPECT_TRUE( listReceived.empty() ); // 지연 − 흔들림 전에는 아무것도 없다
    link.advance( 0.121, listReceived );
    SW_EXPECT_EQUAL( size_t( 20 ), listReceived.size() );
    SW_EXPECT_EQUAL( size_t( 0 ), link._sender.getQueuedCount() );
}

/**
 * @brief [NetEmulationTest] 손실 · 중복은 정한 비율 근처로 일어나고(씨앗 고정), 순서를 뒤바꾼 패킷은 뒤 패킷에게 앞지르기를 당한다
 */
SW_TEST_CASE( NetEmulationTest, LossDuplicationAndReorderingFollowTheRates )
{
    NetEmulationTestLink   link;
    NetEmulationConditions conditions;
    conditions._latency       = 0.01;
    conditions._lossRate      = 0.2f;
    conditions._duplicateRate = 0.1f;
    conditions._reorderRate   = 0.1f;
    conditions._reorderDelay  = 0.05;
    link._sender.setDefaultConditions( conditions );

    vector<int32> listReceived;
    float64       time = 0.0;
    for ( int32 number = 0; number < 2000; ++number )
    {
        SW_ASSERT_TRUE( link.sendNumbered( number ) );
        time += 0.001;
        link.advance( time, listReceived );
    }
    link.advance( time + 1.0, listReceived );

    const NetEmulationStats stats = link._sender.getStats();
    SW_EXPECT_TRUE( 300u <= stats._droppedCount && stats._droppedCount <= 500u );       // 20 % ± 5 %
    SW_EXPECT_TRUE( 110u <= stats._duplicatedCount && stats._duplicatedCount <= 210u ); // 남은 1600 의 10 %
    SW_EXPECT_TRUE( stats._reorderedCount > 100u );
    SW_EXPECT_EQUAL( size_t( stats._sentCount ), listReceived.size() );
    int32 inversionCount = 0;
    for ( size_t index = 1; index < listReceived.size(); ++index )
    {
        inversionCount += listReceived[index] < listReceived[index - 1] ? 1 : 0;
    }
    SW_EXPECT_TRUE( inversionCount > 50 ); // 늦춘 패킷이 뒤에 닿는다
}

/**
 * @brief [NetEmulationTest] 대역폭 상한은 큰 패킷 줄을 회선 속도대로 흘리고 큐가 넘치면 버린다 · 연결 하나만 덮어쓸 수 있다
 */
SW_TEST_CASE( NetEmulationTest, BandwidthQueuesPerLinkAndOverridesOneConnection )
{
    NetEmulationTestLink   link;
    NetEmulationConditions slow;
    slow._bandwidthBytesPerSecond = 5000;
    slow._maxQueuedBytes          = 8000;
    link._sender.setConditions( NetAddress::makeLoopback( 7001 ), slow );
    SW_EXPECT_EQUAL( 5000, link._sender.findConditions( NetAddress::makeLoopback( 7001 ) )._bandwidthBytesPerSecond );
    SW_EXPECT_EQUAL( 0, link._sender.findConditions( NetAddress::makeLoopback( 7999 ) )._bandwidthBytesPerSecond );

    for ( int32 number = 0; number < 10; ++number )
    {
        SW_ASSERT_TRUE( link.sendNumbered( number, 1000 ) );
    }
    // 8000 바이트가 큐 상한 — 여덟이 줄을 서고 둘은 버린다.
    SW_EXPECT_EQUAL( uint64( 2 ), link._sender.getStats()._queueDropCount );
    vector<int32> listReceived;
    link.advance( 0.95, listReceived );
    SW_EXPECT_EQUAL( size_t( 4 ), listReceived.size() ); // 1000 바이트 / 5000 B/s = 0.2 s 마다 하나
    link.advance( 1.61, listReceived );
    SW_EXPECT_EQUAL( size_t( 8 ), listReceived.size() );

    // 덮어쓰기를 풀면 기본(꺼짐)이다.
    link._sender.clearConditions( NetAddress::makeLoopback( 7001 ) );
    SW_EXPECT_FALSE( link._sender.findConditions( NetAddress::makeLoopback( 7001 ) ).isActive() );
}

/**
 * @brief [NetEmulationTest] 깨짐은 정한 비율 근처로 패킷마다 한 바이트를 뒤집어 넘기고(버리지 않는다) 그 수를 센다 — 받는 쪽 체크섬 테스트가 이것을 쓴다
 */
SW_TEST_CASE( NetEmulationTest, CorruptionFlipsOneBytePerPacket )
{
    constexpr int32        kPacketCount = 2000;
    constexpr int32        kPacketSize  = 16;
    constexpr uint8        kFill        = 0x11;
    NetEmulationTestLink   link;
    NetEmulationConditions conditions;
    conditions._latency     = 0.01;
    conditions._corruptRate = 0.1f;
    SW_EXPECT_TRUE( conditions.isActive() );
    link._sender.setDefaultConditions( conditions );

    const vector<uint8> packet( static_cast<size_t>( kPacketSize ), kFill );
    vector<uint8>       received;
    NetAddress          from{};
    int32               receivedCount  = 0;
    int32               corruptedCount = 0;
    int32               manyByteCount  = 0;
    float64             time           = 0.0;
    for ( int32 frame = 0; frame < kPacketCount + 20; ++frame )
    {
        if ( frame < kPacketCount )
            SW_ASSERT_TRUE( link._sender.send( NetAddress::makeLoopback( 7001 ), packet.data(), kPacketSize ) );
        time += 0.001;
        link._sender.update( time );
        while ( link._pReceiver->receive( from, received ) )
        {
            const int32 changedCount = countChangedBytes( received, kFill );
            ++receivedCount;
            corruptedCount += changedCount == 1 ? 1 : 0;
            manyByteCount += changedCount > 1 ? 1 : 0;
        }
    }
    const NetEmulationStats stats = link._sender.getStats();
    SW_EXPECT_EQUAL( kPacketCount, receivedCount ); // 깨진 패킷도 버리지 않는다
    SW_EXPECT_EQUAL( uint64( 0 ), stats._droppedCount );
    SW_EXPECT_TRUE( 150u <= stats._corruptedCount && stats._corruptedCount <= 250u ); // 10 % ± 2.5 %
    SW_EXPECT_EQUAL( static_cast<int32>( stats._corruptedCount ), corruptedCount );
    SW_EXPECT_EQUAL( 0, manyByteCount ); // 한 패킷에 한 바이트만
}

/**
 * @brief [NetEmulationTest] 키-값 덮어쓰기는 틀리면 아무것도 바꾸지 않는다(회선 조절 S4 의 개발 명령 · 에디터 패널이 쓴다). 이름 있는 프리셋은 Engine 의 표다(`NetEmulationPresetTableTest`)
 */
SW_TEST_CASE( NetEmulationTest, OverridesMakeConditions )
{
    NetEmulationConditions overridden;
    string                 error;
    SW_ASSERT_TRUE( NetEmulationConditions::parseOverrides( "latencyMs=80  jitterMs=20 lossPercent=5 bandwidthKBps=256", overridden, error ) );
    SW_EXPECT_NEAR_EQUAL( overridden._latency, 0.08, 1.0e-9 );
    SW_EXPECT_NEAR_EQUAL( overridden._jitter, 0.02, 1.0e-9 );
    SW_EXPECT_NEAR_EQUAL( overridden._lossRate, 0.05f, 1.0e-6f );
    SW_EXPECT_EQUAL( overridden._bandwidthBytesPerSecond, 256 * 1024 );
    const NetEmulationConditions before = overridden;
    SW_EXPECT_FALSE( NetEmulationConditions::parseOverrides( "latencyMs=10 pingMs=5", overridden, error ) );
    SW_EXPECT_TRUE( error.find( "pingMs" ) != string::npos );
    SW_EXPECT_NEAR_EQUAL( overridden._latency, before._latency, 1.0e-12 ); // 앞의 latencyMs 도 적용하지 않았다
    SW_EXPECT_FALSE( NetEmulationConditions::parseOverrides( "lossPercent=-1", overridden, error ) );
}

/**
 * @brief [NetEmulationTest] 덮어쓰기 글을 만들어 다시 읽으면 같은 조건이다 — 에디터 회선 조절이 보내는 글과 개발 명령이 읽는 글이 한 정의를 쓴다
 */
SW_TEST_CASE( NetEmulationTest, OverrideTextRoundTrips )
{
    NetEmulationConditions lte;
    lte._latency                 = 0.035;
    lte._jitter                  = 0.015;
    lte._lossRate                = 0.01f;
    lte._reorderRate             = 0.005f;
    lte._bandwidthBytesPerSecond = 1024 * 1024;
    NetEmulationConditions lossy;
    lossy._latency       = 0.08;
    lossy._lossRate      = 0.05f;
    lossy._duplicateRate = 0.01f;
    for ( const NetEmulationConditions& preset : { NetEmulationConditions{}, lte, lossy } )
    {
        const string           text = preset.makeOverrideText();
        NetEmulationConditions parsed;
        parsed._latency = 9.0; // 글이 모든 키를 써서 앞 상태를 덮는지 본다
        string error;
        SW_ASSERT_TRUE_MSG( NetEmulationConditions::parseOverrides( text, parsed, error ), error.c_str() );
        SW_EXPECT_NEAR_EQUAL( parsed._latency, preset._latency, 1.0e-6 );
        SW_EXPECT_NEAR_EQUAL( parsed._jitter, preset._jitter, 1.0e-6 );
        SW_EXPECT_NEAR_EQUAL( parsed._lossRate, preset._lossRate, 1.0e-5f );
        SW_EXPECT_NEAR_EQUAL( parsed._duplicateRate, preset._duplicateRate, 1.0e-5f );
        SW_EXPECT_NEAR_EQUAL( parsed._reorderRate, preset._reorderRate, 1.0e-5f );
        SW_EXPECT_EQUAL( parsed._bandwidthBytesPerSecond, preset._bandwidthBytesPerSecond );
    }
    SW_EXPECT_EQUAL( lte.makeOverrideText(), string( "latencyMs=35 jitterMs=15 lossPercent=1 duplicatePercent=0 reorderPercent=0.5 bandwidthKBps=1024" ) );
}

/**
 * @brief [NetEmulationTest] 보고 줄은 로그 꼬리표 · 명령 메아리 뒤에 있어도 읽히고, 머리가 없거나 값이 숫자가 아니면 읽지 않는다
 */
SW_TEST_CASE( NetEmulationTest, ReportLineRoundTripsThroughALogLine )
{
    NetEmulationReport report;
    report._connectionCount     = 2;
    report._sentPacketCount     = 1200;
    report._droppedPacketCount  = 360;
    report._resentMessageCount  = 41;
    report._measuredLossPercent = 29.5f;
    report._rttMilliseconds     = 12.25f;
    const string line           = "[12:00:01.234] [Info] [DevConsole] > net.emulation stats : " + report.makeLine();

    NetEmulationReport parsed;
    SW_ASSERT_TRUE( NetEmulationReport::tryParseLine( line, parsed ) );
    SW_EXPECT_EQUAL( parsed._connectionCount, 2 );
    SW_EXPECT_EQUAL( parsed._sentPacketCount, uint64{ 1200 } );
    SW_EXPECT_EQUAL( parsed._droppedPacketCount, uint64{ 360 } );
    SW_EXPECT_EQUAL( parsed._resentMessageCount, uint64{ 41 } );
    SW_EXPECT_NEAR_EQUAL( parsed._measuredLossPercent, 29.5f, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( parsed._rttMilliseconds, 12.25f, 1.0e-4f );

    NetEmulationReport untouched;
    untouched._resentMessageCount = 7;
    SW_EXPECT_FALSE( NetEmulationReport::tryParseLine( "net session: Client, connected 1", untouched ) );
    SW_EXPECT_FALSE( NetEmulationReport::tryParseLine( string( NetEmulationReport::kLinePrefix ) + " resent=many", untouched ) );
    SW_EXPECT_EQUAL( untouched._resentMessageCount, uint64{ 7 } );
}

/**
 * @brief [NetEmulationTest] 끊김(스파이크)은 그 시간 동안 보내는 것을 모두 버리고, 끝나면 다시 그대로 보낸다
 */
SW_TEST_CASE( NetEmulationTest, OutageDropsEverythingUntilItEnds )
{
    NetEmulationTestLink link;
    vector<int32>        listReceived;
    link._sender.update( 1.0 );
    link._sender.startOutage( 0.5 );
    SW_EXPECT_TRUE( link._sender.isInOutage() );
    for ( int32 number = 0; number < 10; ++number )
    {
        SW_ASSERT_TRUE( link.sendNumbered( number ) ); // 보내는 쪽은 모른다
    }
    link.advance( 1.2, listReceived );
    SW_EXPECT_TRUE( listReceived.empty() );
    link.advance( 1.5, listReceived ); // 끊김이 끝났다
    SW_EXPECT_FALSE( link._sender.isInOutage() );
    SW_ASSERT_TRUE( link.sendNumbered( 42 ) );
    link.advance( 1.5, listReceived );
    SW_ASSERT_EQUAL( size_t( 1 ), listReceived.size() );
    SW_EXPECT_EQUAL( 42, listReceived[0] );
    SW_EXPECT_EQUAL( uint64( 10 ), link._sender.getStats()._outageDroppedCount );
}

/**
 * @brief [NetEmulationTest] 받는 쪽 조건은 받은 패킷을 그 지연만큼 붙잡았다 넘기고, 손실 비율만큼 버린다(올림 · 내림을 따로 나쁘게 한다)
 */
SW_TEST_CASE( NetEmulationTest, IncomingConditionsDelayAndDropReceivedPackets )
{
    NetEmulationTestLink   link;
    NetEmulationTransport  receiver{ link._pReceiver, 29u };
    NetEmulationConditions incoming;
    incoming._latency = 0.1;
    receiver.setIncomingConditions( incoming );
    for ( int32 number = 0; number < 5; ++number )
    {
        SW_ASSERT_TRUE( link.sendNumbered( number ) );
    }
    link._sender.update( 0.0 );
    link._network.deliverInFlight();
    NetAddress    from{};
    vector<uint8> buffer;
    receiver.update( 0.0 );
    SW_EXPECT_FALSE( receiver.receive( from, buffer ) ); // 받았지만 붙잡고 있다
    receiver.update( 0.099 );
    SW_EXPECT_FALSE( receiver.receive( from, buffer ) );
    receiver.update( 0.1 );
    int32 receivedCount = 0;
    int32 lastNumber    = -1;
    while ( receiver.receive( from, buffer ) )
    {
        SW_EXPECT_TRUE( buffer[0] > lastNumber || lastNumber < 0 ); // 흔들림이 없으면 도착 순서 그대로
        lastNumber = buffer[0];
        ++receivedCount;
    }
    SW_EXPECT_EQUAL( 5, receivedCount );

    incoming           = NetEmulationConditions{};
    incoming._lossRate = 0.5f;
    receiver.setIncomingConditions( incoming );
    constexpr int32 kPacketCount = 1000;
    for ( int32 number = 0; number < kPacketCount; ++number )
    {
        SW_ASSERT_TRUE( link.sendNumbered( number ) );
    }
    link._sender.update( 1.0 );
    link._network.deliverInFlight();
    receiver.update( 1.0 );
    receivedCount = 0;
    while ( receiver.receive( from, buffer ) )
    {
        ++receivedCount;
    }
    SW_EXPECT_TRUE( 400 <= receivedCount && receivedCount <= 600 ); // 50 % ± 10 %
    SW_EXPECT_EQUAL( static_cast<uint64>( kPacketCount - receivedCount ), receiver.getStats()._incomingDroppedCount );
}
