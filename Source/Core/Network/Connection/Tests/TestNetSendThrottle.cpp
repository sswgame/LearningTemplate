#include "pch.h"

#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Connection/NetSendThrottle.h"
#include "Core/Network/Tests/LoopbackCluster.h"
#include "Core/Network/Tests/NetTestUtil.h"
#include "Core/Network/Transport/NetEmulation.h"
#include "Core/TestFramework/TestFramework.h"

// 송신 속도 조절(AIMD, 감사 N-11) — 계수 검사, 손실 · 줄 지연에 곱셈 감소와 하한, 맑은 회선에 덧셈 증가와 상한,
// 손실 20 % 회선에서 고정 버킷보다 적은 재전송과 하한 밑으로 내려가지 않는 처리량, 손실이 0 으로 돌아오면 상한까지 회복.

using namespace sw;
using test::NetTestUtil;

namespace
{
    struct NetSendThrottleTestInternal
    {
        static constexpr int32   kMaxBytesPerSecond = 100000;
        static constexpr int32   kMinBytesPerSecond = 20000;
        static constexpr int32   kMessageSize       = 1000; // 조각나지 않는 신뢰 메시지 하나 — 패킷 하나에 하나
        static constexpr float64 kStep              = 1.0 / 60.0;

        static NetSendThrottleConfig makeConfig()
        {
            NetSendThrottleConfig config;
            config._minBytesPerSecond      = kMinBytesPerSecond;
            config._increaseBytesPerSecond = 5000;
            config._decreaseFactor         = 0.5;
            config._lossThreshold          = 0.1;
            config._maxQueueDelaySeconds   = 0.1;
            config._adjustIntervalSeconds  = 0.25;
            return config;
        }

        static NetConnectionStats makeStats( float32 rtt, float32 packetLoss )
        {
            NetConnectionStats stats;
            stats._rtt        = rtt;
            stats._packetLoss = packetLoss;
            return stats;
        }

        /** @brief 회선 한 구간을 돈 결과입니다. */
        struct RunResult
        {
            int64   _deliveredBytes{ 0 };
            float32 _lowestSendRate{ static_cast<float32>( kMaxBytesPerSecond ) };
            float32 _lastSendRate{ 0.0f };
        };

        /** @brief 클라이언트가 신뢰 창을 늘 채워 두고(`trySendMessage`) @p seconds 동안 손 시각으로 돈다 — 서버가 받은 바이트와 클라이언트 송신 속도의 최저 · 마지막 값. */
        static RunResult runSaturated( test::LoopbackCluster& cluster, float64 seconds )
        {
            const vector<uint8>   payload = NetTestUtil::makePatternMessage( 7u, kMessageSize );
            RunResult             result;
            vector<vector<uint8>> listReceived;
            for ( float64 elapsed = 0.0; elapsed < seconds; elapsed += kStep )
            {
                int32 queuedCount = 0;
                while ( queuedCount < 8 && cluster.getClient( 0 ).trySendMessage( 0, NetChannelKind::ReliableUnordered, payload ) )
                {
                    ++queuedCount; // 창이 차면 false — 늘 꽉 채워 둔다
                }
                cluster.step( kStep );
                listReceived.clear();
                NetTestUtil::pollServerMessages( cluster, listReceived );
                for ( const vector<uint8>& message : listReceived )
                {
                    result._deliveredBytes += static_cast<int64>( message.size() );
                }
                NetConnectionStats stats;
                if ( cluster.getClient( 0 ).getConnectionStats( 0, stats ) )
                {
                    result._lowestSendRate = stats._sendBytesPerSecond < result._lowestSendRate ? stats._sendBytesPerSecond : result._lowestSendRate;
                    result._lastSendRate   = stats._sendBytesPerSecond;
                }
            }
            return result;
        }

        static NetEmulationConditions makeLossyLine( float32 lossRate )
        {
            NetEmulationConditions conditions;
            conditions._latency  = 0.03;
            conditions._lossRate = lossRate;
            return conditions;
        }

        static uint64 getClientResentCount( test::LoopbackCluster& cluster )
        {
            const NetConnection* pConnection = cluster.getClient( 0 ).findConnection( 0 );
            return pConnection != nullptr ? pConnection->getStats()._resentMessageCount : 0;
        }
    };
} // namespace

/** @brief [NetSendThrottleTest] 계수 검사 — 하한은 패킷 하나 이상 · 상한 이하, 감소 계수 · 손실 문턱은 (0, 1), 나머지는 0 보다 커야 한다. 틀린 칸 이름이 오류에 실린다 */
SW_TEST_CASE( NetSendThrottleTest, ValidateRejectsUnusableSettings )
{
    using Internal                     = NetSendThrottleTestInternal;
    const NetSendThrottleConfig config = Internal::makeConfig();
    SW_EXPECT_TRUE( NetSendThrottle::validate( config, Internal::kMaxBytesPerSecond ).has_value() );

    NetSendThrottleConfig belowPacket = config;
    belowPacket._minBytesPerSecond    = kNetMaxPacketSize - 1;
    const expected<void> belowResult  = NetSendThrottle::validate( belowPacket, Internal::kMaxBytesPerSecond );
    SW_REQUIRE_FALSE( belowResult.has_value() );
    SW_EXPECT_TRUE( belowResult.error().detail() == "_minBytesPerSecond" );
    SW_EXPECT_FALSE( NetSendThrottle::validate( config, Internal::kMinBytesPerSecond - 1 ).has_value() ); // 하한이 상한보다 크다

    NetSendThrottleConfig noDecrease = config;
    noDecrease._decreaseFactor       = 1.0;
    SW_EXPECT_FALSE( NetSendThrottle::validate( noDecrease, Internal::kMaxBytesPerSecond ).has_value() );
    NetSendThrottleConfig noIncrease   = config;
    noIncrease._increaseBytesPerSecond = 0;
    SW_EXPECT_FALSE( NetSendThrottle::validate( noIncrease, Internal::kMaxBytesPerSecond ).has_value() );
    NetSendThrottleConfig noThreshold = config;
    noThreshold._lossThreshold        = 0.0;
    SW_EXPECT_FALSE( NetSendThrottle::validate( noThreshold, Internal::kMaxBytesPerSecond ).has_value() );
    NetSendThrottleConfig noQueueDelay = config;
    noQueueDelay._maxQueueDelaySeconds = 0.0;
    SW_EXPECT_FALSE( NetSendThrottle::validate( noQueueDelay, Internal::kMaxBytesPerSecond ).has_value() );
    NetSendThrottleConfig noInterval  = config;
    noInterval._adjustIntervalSeconds = 0.0;
    SW_EXPECT_FALSE( NetSendThrottle::validate( noInterval, Internal::kMaxBytesPerSecond ).has_value() );
}

/** @brief [NetSendThrottleTest] 상한에서 시작해 손실마다 곱셈 감소로 하한까지 내려가고, 맑으면 조절마다 덧셈 증가로 상한까지 오른다. 조절 간격 안에서는 바꾸지 않는다 */
SW_TEST_CASE( NetSendThrottleTest, LossHalvesToTheFloorAndClearLineAddsBackToTheCeiling )
{
    using Internal                     = NetSendThrottleTestInternal;
    const NetSendThrottleConfig config = Internal::makeConfig();
    NetSendThrottle             throttle;
    SW_EXPECT_NEAR( 100000.0, throttle.getBytesPerSecond( Internal::kMaxBytesPerSecond ), 1.0e-6 );

    throttle.update( 0.0, config, Internal::kMaxBytesPerSecond, Internal::makeStats( 0.06f, 0.2f ) ); // 첫 조절은 상한에서 시작만 한다
    SW_EXPECT_NEAR( 100000.0, throttle.getBytesPerSecond( Internal::kMaxBytesPerSecond ), 1.0e-6 );
    throttle.update( 0.1, config, Internal::kMaxBytesPerSecond, Internal::makeStats( 0.06f, 0.2f ) ); // 간격(0.25 초) 안
    SW_EXPECT_NEAR( 100000.0, throttle.getBytesPerSecond( Internal::kMaxBytesPerSecond ), 1.0e-6 );
    throttle.update( 0.25, config, Internal::kMaxBytesPerSecond, Internal::makeStats( 0.06f, 0.2f ) );
    SW_EXPECT_NEAR( 50000.0, throttle.getBytesPerSecond( Internal::kMaxBytesPerSecond ), 1.0e-6 );
    throttle.update( 0.5, config, Internal::kMaxBytesPerSecond, Internal::makeStats( 0.06f, 0.2f ) );
    SW_EXPECT_NEAR( 25000.0, throttle.getBytesPerSecond( Internal::kMaxBytesPerSecond ), 1.0e-6 );
    throttle.update( 0.75, config, Internal::kMaxBytesPerSecond, Internal::makeStats( 0.06f, 0.2f ) );
    SW_EXPECT_NEAR( 20000.0, throttle.getBytesPerSecond( Internal::kMaxBytesPerSecond ), 1.0e-6 ); // 12500 이 아니라 하한
    throttle.update( 1.0, config, Internal::kMaxBytesPerSecond, Internal::makeStats( 0.06f, 0.2f ) );
    SW_EXPECT_NEAR( 20000.0, throttle.getBytesPerSecond( Internal::kMaxBytesPerSecond ), 1.0e-6 );

    float64 time = 1.0;
    for ( int32 step = 0; step < 15; ++step )
    {
        time += 0.25;
        throttle.update( time, config, Internal::kMaxBytesPerSecond, Internal::makeStats( 0.06f, 0.0f ) );
    }
    SW_EXPECT_NEAR( 95000.0, throttle.getBytesPerSecond( Internal::kMaxBytesPerSecond ), 1.0e-6 ); // 20000 + 15 × 5000
    for ( int32 step = 0; step < 4; ++step )
    {
        time += 0.25;
        throttle.update( time, config, Internal::kMaxBytesPerSecond, Internal::makeStats( 0.06f, 0.0f ) );
    }
    SW_EXPECT_NEAR( 100000.0, throttle.getBytesPerSecond( Internal::kMaxBytesPerSecond ), 1.0e-6 ); // 상한에서 멈춘다
}

/** @brief [NetSendThrottleTest] 손실이 없어도 RTT 가 가장 작은 RTT 보다 줄 지연 문턱(0.1 초) 넘게 길어지면 막힘으로 보고 줄인다 — 회선 줄이 차는 것을 손실 전에 본다 */
SW_TEST_CASE( NetSendThrottleTest, QueueingDelayCountsAsCongestion )
{
    using Internal                     = NetSendThrottleTestInternal;
    const NetSendThrottleConfig config = Internal::makeConfig();
    NetSendThrottle             throttle;
    throttle.update( 0.0, config, Internal::kMaxBytesPerSecond, Internal::makeStats( 0.05f, 0.0f ) );
    throttle.update( 0.25, config, Internal::kMaxBytesPerSecond, Internal::makeStats( 0.12f, 0.0f ) ); // 0.07 초 늦음 — 문턱 안
    SW_EXPECT_NEAR( 100000.0, throttle.getBytesPerSecond( Internal::kMaxBytesPerSecond ), 1.0e-6 );
    throttle.update( 0.5, config, Internal::kMaxBytesPerSecond, Internal::makeStats( 0.2f, 0.0f ) ); // 0.15 초 늦음
    SW_EXPECT_NEAR( 50000.0, throttle.getBytesPerSecond( Internal::kMaxBytesPerSecond ), 1.0e-6 );
}

/** @brief [NetSendThrottleTest] 경로가 길어진 연결(50 → 250 ms)은 최솟값 창이 지나면 다시 상한까지 오른다 — 연결 처음의 최솟값을 끝까지 들면 하한에 묶인다 */
SW_TEST_CASE( NetSendThrottleTest, LongerPathStopsCountingAsCongestionAfterTheWindow )
{
    using Internal                     = NetSendThrottleTestInternal;
    const NetSendThrottleConfig config = Internal::makeConfig();
    NetSendThrottle             throttle;
    float64                     time = 0.0;
    for ( ; time < 1.0; time += 0.25 )
    {
        throttle.update( time, config, Internal::kMaxBytesPerSecond, Internal::makeStats( 0.05f, 0.0f ) );
    }
    const float64 changeTime = time;
    for ( ; time < changeTime + 5.0; time += 0.25 )
    {
        throttle.update( time, config, Internal::kMaxBytesPerSecond, Internal::makeStats( 0.25f, 0.0f ) );
    }
    SW_EXPECT_NEAR( 20000.0, throttle.getBytesPerSecond( Internal::kMaxBytesPerSecond ), 1.0e-6 ); // 창 안에서는 줄 지연과 구별하지 못해 줄인다
    for ( ; time < changeTime + 2.0 * NetSendThrottle::kMinRttWindowSeconds + 5.0; time += 0.25 )
    {
        throttle.update( time, config, Internal::kMaxBytesPerSecond, Internal::makeStats( 0.25f, 0.0f ) );
    }
    SW_EXPECT_NEAR( 100000.0, throttle.getBytesPerSecond( Internal::kMaxBytesPerSecond ), 1.0e-6 );
}

/**
 * @brief [NetSendThrottleTest] 손실 20 % 회선(같은 흉내 씨앗)에서 조절하는 연결은 고정 버킷보다 신뢰 메시지를 적게 다시 보내고, 송신 속도 · 처리량은 하한 밑으로 내려가지 않는다.
 *        손실이 0 으로 돌아오면 속도가 상한까지 회복된다
 * @details 손 시각(1/60 초)이라 결정적이다. 처리량은 하한 × 시간에 잃은 몫(20 %)과 머리 몫을 넉넉히 뺀 0.6 배를 바닥으로 본다.
 */
SW_TEST_CASE( NetSendThrottleTest, LossyLineResendsLessThanTheFixedBucketAndRecovers )
{
    using Internal               = NetSendThrottleTestInternal;
    constexpr uint32  kSeed      = 71u;
    constexpr float64 kLossyTime = 10.0;

    NetEndpointConfig fixedSettings;
    fixedSettings._maxBytesPerSecond   = Internal::kMaxBytesPerSecond;
    NetEndpointConfig adaptiveSettings = fixedSettings;
    adaptiveSettings._sendThrottle     = Internal::makeConfig();

    test::LoopbackCluster fixedCluster( kSeed );
    NetTestUtil::addServerAndClient( fixedCluster, fixedSettings );
    SW_REQUIRE_TRUE( fixedCluster.connectClients( 30 ) );
    fixedCluster.setConditions( Internal::makeLossyLine( 0.2f ) );
    const Internal::RunResult fixedRun    = Internal::runSaturated( fixedCluster, kLossyTime );
    const uint64              fixedResent = Internal::getClientResentCount( fixedCluster );
    SW_REQUIRE_TRUE( fixedCluster.areClientsConnected() );
    SW_EXPECT_NEAR( static_cast<float32>( Internal::kMaxBytesPerSecond ), fixedRun._lowestSendRate, 1.0f ); // 고정 버킷은 손실에도 상한 그대로다

    test::LoopbackCluster adaptiveCluster{ kSeed };
    NetTestUtil::addServerAndClient( adaptiveCluster, adaptiveSettings );
    SW_REQUIRE_TRUE( adaptiveCluster.connectClients( 30 ) );
    adaptiveCluster.setConditions( Internal::makeLossyLine( 0.2f ) );
    const Internal::RunResult lossyRun       = Internal::runSaturated( adaptiveCluster, kLossyTime );
    const uint64              adaptiveResent = Internal::getClientResentCount( adaptiveCluster );
    SW_REQUIRE_TRUE( adaptiveCluster.areClientsConnected() );

    SW_EXPECT_TRUE( fixedResent > 0 );
    SW_EXPECT_TRUE_MSG( adaptiveResent * 4 < fixedResent * 3, "the throttled connection resends clearly less than the fixed bucket on the same lossy line" );
    SW_EXPECT_TRUE_MSG( lossyRun._lowestSendRate >= static_cast<float32>( Internal::kMinBytesPerSecond ) - 1.0f, "the send rate never drops below the floor" );
    // 손실이 계속되면 하한 곁에 머문다(손실 표본이 한 번 문턱 밑으로 흔들리면 한 걸음 오른 뒤 다음 조절에 다시 내려간다).
    SW_EXPECT_TRUE( lossyRun._lastSendRate <= static_cast<float32>( Internal::kMinBytesPerSecond + Internal::makeConfig()._increaseBytesPerSecond ) + 1.0f );
    SW_EXPECT_TRUE_MSG( static_cast<float64>( lossyRun._deliveredBytes ) >= 0.6 * Internal::kMinBytesPerSecond * kLossyTime, "reliable throughput stays at the floor despite the loss" );

    adaptiveCluster.setConditions( Internal::makeLossyLine( 0.0f ) );
    const Internal::RunResult clearRun = Internal::runSaturated( adaptiveCluster, 10.0 );
    SW_EXPECT_NEAR( static_cast<float32>( Internal::kMaxBytesPerSecond ), clearRun._lastSendRate, 1.0f ); // 맑은 회선에서 상한까지 돌아온다
    SW_EXPECT_TRUE( clearRun._deliveredBytes > lossyRun._deliveredBytes );
}
