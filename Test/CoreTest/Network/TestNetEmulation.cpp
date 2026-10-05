#include "pch.h"

#include "Core/Network/Transport/NetEmulation.h"
#include "Core/Network/Transport/NetTransport.h"

#include "TestFramework/TestFramework.h"

// 네트워크 흉내 — 지연 · 흔들림, 손실 · 중복 비율, 일부러 늦춘 패킷이 앞지르기를 당함, 대역폭 줄과 큐 넘침, 연결별 덮어쓰기, 꺼지면 그대로.
// 전역 변수(-gv_netEmu*)로 조건을 만드는 것은 EngineTest 의 NetClientServerTest 가 본다(전역 변수 표가 엔진에 있다).

using namespace sw;

namespace
{
    /** @brief 완벽한 루프백 위에 흉내를 씌운 보내는 쪽 · 받는 쪽입니다. */
    struct NetEmulationTestLink
    {
        LoopbackNetwork       _network{ 3u };
        LoopbackTransport*    _pSenderEndpoint{ _network.createEndpoint( 7000 ) };
        LoopbackTransport*    _pReceiver{ _network.createEndpoint( 7001 ) };
        NetEmulationTransport _sender{ _pSenderEndpoint, 17u };

        /** @brief 시간을 넘기고 받은 패킷의 첫 바이트(일련번호)를 모읍니다. */
        void advance( float64 time, vector<int32>& outListReceived )
        {
            _sender.update( time );
            _network.advance( time );
            NetAddress    from{};
            vector<uint8> buffer;
            while ( _pReceiver->receive( from, buffer ) )
                outListReceived.push_back( buffer.empty() ? -1 : static_cast<int32>( buffer[0] ) | ( buffer.size() > 1 ? static_cast<int32>( buffer[1] ) << 8 : 0 ) );
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
        SW_ASSERT_TRUE( link.sendNumbered( number ) );
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
        inversionCount += listReceived[index] < listReceived[index - 1] ? 1 : 0;
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
        SW_ASSERT_TRUE( link.sendNumbered( number, 1000 ) );
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
