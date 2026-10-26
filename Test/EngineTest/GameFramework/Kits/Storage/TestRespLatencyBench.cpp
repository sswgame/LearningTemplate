#include "pch.h"

#include "Core/Network/Transport/DelayedStreamTransport.h"
#include "Core/Network/Transport/LoopbackStreamTransport.h"
#include "Core/Time/MonotonicClock.h"

#include "EngineTest/GameFramework/Kits/Storage/FakeRespServer.h"
#include "EngineTest/GameFramework/Online/EphemeralStoreContract.h"

#include "GameFramework/Kits/Feature/Storage/CacheStore/Server/Driver/Resp/RespEphemeralStore.h"

#include "TestFramework/Harness/TestBench.h"
#include "TestFramework/TestFramework.h"

// 서버와 캐시 사이의 왕복 시간이 망 지연에 어떻게 따라가는지 — 한 PC 에서 `DelayedStreamTransport` 로 먼 망을 흉내 낸다(OnlineMultiServerLatency 의 한 PC 판).
// RESP 저장소 앞의 전송을 지연 장식자로 감싸 가짜 RESP 서버에 GET 을 되풀이하고 왕복 표본을 `[Bench]` 줄로 찍는다. 지연은 요청 방향에만 걸리므로 왕복 ≈ 지연 + 처리다.
// 실제 Valkey · Garnet 은 필요 없고 값은 지연이 지배하므로 Debug 에서도 읽힌다. 같은 모양의 서버 간 버스(`EphemeralServerBus`)가 이 저장소 위에서 돈다.

SW_TEST_LOAD_SUITE( RespLatencyBenchTest, "RESP cache round trip under 0 / 20 / 80 ms injected one-way delay" );
SW_TEST_TIME_BUDGET( RespLatencyBenchTest, 20000, "미측정 — 지연 합 약 4 s(표본 40 × (0 + 20 + 80 ms))의 다섯 배, CI 러너 여유. 첫 측정 뒤 네 배로 고친다" );

using namespace sw;

namespace
{
    struct RespLatencyBenchInternal
    {
        static constexpr int32 kSampleCount = 40;

        /** @brief 지연 @p delaySeconds 를 건 RESP 저장소로 GET 을 @p kSampleCount 번 왕복해 마이크로초 표본을 모으고 줄을 찍는다. 중앙값(us)을 @p outMedianMicro 에 둔다. */
        static void measure( float64 delaySeconds, const utf8* pLabel, int64& outMedianMicro )
        {
            LoopbackStreamNetwork network{ 5u };
            test::FakeRespServer  server{ network };
            RespEphemeralStore    store{ server };

            StreamTransportConfig transportSettings;
            transportSettings._ioThreadCount      = 0; // 루프백 — 지연 장식자도 수동 판이라 부르는 쪽이 돈다
            transportSettings._idleTimeoutSeconds = 0.0;
            RespStoreConfig settings;
            settings._address   = server.getAddress();
            settings._keyPrefix = "bench:";
            settings._timeoutMs = 5000;
            string error;
            SW_ASSERT_TRUE_MSG( store.initialize( make_unique<DelayedStreamTransport>( server.createClientTransport(), delaySeconds ), transportSettings, settings, nullptr, error ),
                                error.c_str() );

            const vector<uint8> value( 64, 7 );
            SW_ASSERT_TRUE( test::EphemeralStoreContract::executeResult( store, EphemeralRequest::makeSet( "k", value, 0 ) ) == EphemeralResult::Ok );
            vector<int64> listMicro;
            for ( int32 sample = 0; sample < kSampleCount; ++sample )
            {
                EphemeralReply  reply;
                const Stopwatch stopwatch;
                SW_ASSERT_TRUE( test::EphemeralStoreContract::executeRequest( store, EphemeralRequest::makeGet( "k" ), reply ) );
                listMicro.push_back( stopwatch.getElapsedMicroseconds() );
                SW_EXPECT_TRUE( reply._value == value );
            }
            test::logBenchSamples( pLabel, listMicro );
            outMedianMicro = test::getPercentile( listMicro, 50 );
        }
    };
} // namespace

/**
 * @brief [RespLatencyBenchTest] 지연 0 · 20 · 80 ms — 왕복 중앙값이 지연보다 작지 않고 지연이 클수록 늘어난다(지연이 곱으로 쌓이지 않는지는 줄로 읽는다)
 */
SW_TEST_CASE( RespLatencyBenchTest, RoundTripGrowsWithTheInjectedDelay )
{
    int64 noDelayMicro  = 0;
    int64 midDelayMicro = 0;
    int64 farDelayMicro = 0;
    RespLatencyBenchInternal::measure( 0.0, "resp get delay 0 ms", noDelayMicro );
    RespLatencyBenchInternal::measure( 0.020, "resp get delay 20 ms", midDelayMicro );
    RespLatencyBenchInternal::measure( 0.080, "resp get delay 80 ms", farDelayMicro );
    SW_EXPECT_TRUE( midDelayMicro >= 20000 );
    SW_EXPECT_TRUE( farDelayMicro >= 80000 );
    SW_EXPECT_TRUE( farDelayMicro > midDelayMicro );
    SW_EXPECT_TRUE( midDelayMicro > noDelayMicro );
}
