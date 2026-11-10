#include "pch.h"

#include "Core/Testing/TestFramework.h"
#include "Core/Time/MonotonicClock.h"

#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Service/OnlineProtocolService.h"
#include "GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.h"

// OnlineServerCallTableTest — 서버 호출 표: 응답은 한 번이고 어느 순서로 끝나도 남은 수가 맞다. 대기 수 P 에 따른 완료 비용을 잰다(nogpu).
//
// 재는 법(통합 담당): `App --test --test-filter=OnlineServerCallTableTest.CompletionCostByPending` 를 Release 로 5 번 돌려
// 로그 `OnlineServerCallTableTest.CompletionCost` 의 P 별 ns/완료 중앙값을 본다. 판정: 지표 `service_store_pending_peak` 의 실제 최대치 P 에서
// 선형 표(`HEAD~1` 의 벡터)와 색인 표의 차이가 실행 간 흔들림보다 크면 색인을 둔다. 아니면 그 커밋을 되돌리고 값을 DesignBacklog 판단 절에 적는다.

using namespace sw;

namespace
{
    struct OnlineServerCallTableTestInternal
    {
        /** @brief @p pendingCount 개를 연 뒤 가운데부터 바깥으로 번갈아 끝냅니다(벡터 표의 평균 이동 거리가 가장 큰 순서). 끝낸 수를 돌려줍니다. */
        static int32 openAndFailAll( OnlineServerCallTable& table, OnlineServiceHost& host, int32 pendingCount )
        {
            vector<uint64> listCallID;
            listCallID.reserve( static_cast<size_t>( pendingCount ) );
            for ( int32 index = 0; index < pendingCount; ++index )
            {
                listCallID.push_back( table.open( host, NetRequestToken{}, 0 ) );
            }
            int32 failedCount = 0;
            for ( int32 step = 0; step < pendingCount; ++step )
            {
                const int32 offset    = ( step + 1 ) / 2;
                const int32 callIndex = pendingCount / 2 + ( ( step % 2 ) == 0 ? offset : -offset );
                if ( table.isOpen( listCallID[static_cast<size_t>( callIndex )] ) && table.fail( listCallID[static_cast<size_t>( callIndex )], OnlineError::kInternal ) )
                    ++failedCount;
            }
            return failedCount;
        }
    };
} // namespace

/** @brief [OnlineServerCallTableTest] 호출은 한 번만 끝나고, 끝난 번호 · 모르는 번호는 false 이다 */
SW_TEST_CASE( OnlineServerCallTableTest, CallIsAnsweredOnce )
{
    OnlineServiceHost     host;
    OnlineServerCallTable table;
    const uint64          first  = table.open( host, NetRequestToken{}, 0 );
    const uint64          second = table.open( host, NetRequestToken{}, 0 );
    SW_EXPECT_NOT_EQUAL( first, second );
    SW_EXPECT_EQUAL( 2, table.getCount() );
    SW_EXPECT_TRUE( table.fail( second, OnlineError::kInternal ) );
    SW_EXPECT_FALSE( table.isOpen( second ) );
    SW_EXPECT_TRUE( table.isOpen( first ) );
    SW_EXPECT_LOG( "ServiceServerCallTable.UnknownCall" ); // 끝난 번호를 다시 끝내면 모르는 호출로 알린다
    SW_EXPECT_FALSE( table.fail( second, OnlineError::kInternal ) );
    SW_EXPECT_TRUE( table.fail( first, OnlineError::kInternal ) );
    SW_EXPECT_TRUE( table.isEmpty() );
}

/** @brief [OnlineServerCallTableTest] 열린 순서와 다르게 끝내도 모두 한 번씩 빠진다 */
SW_TEST_CASE( OnlineServerCallTableTest, OutOfOrderCompletionLeavesNothing )
{
    OnlineServiceHost     host;
    OnlineServerCallTable table;
    SW_EXPECT_EQUAL( 37, OnlineServerCallTableTestInternal::openAndFailAll( table, host, 37 ) );
    SW_EXPECT_TRUE( table.isEmpty() );
}

/** @brief [OnlineServerCallTableTest] 대기 수 P 별 완료 비용(ns/완료)을 로그로 남깁니다. 시간 문턱은 두지 않는다 — 판정은 통합 담당의 반복 측정이다 */
SW_TEST_CASE( OnlineServerCallTableTest, CompletionCostByPending, ._tags = "bench" )
{
    static constexpr int32 kArrPendingCount[] = { 16, 256, 4096 };
    static constexpr int32 kRepeatCount       = 8;

    OnlineServiceHost host;
    for ( const int32 pendingCount : kArrPendingCount )
    {
        OnlineServerCallTable table;
        const int64           startNanoseconds = MonotonicClock::nowNanoseconds();
        for ( int32 repeat = 0; repeat < kRepeatCount; ++repeat )
        {
            SW_EXPECT_EQUAL( pendingCount, OnlineServerCallTableTestInternal::openAndFailAll( table, host, pendingCount ) );
        }
        const int64 elapsedNanoseconds = MonotonicClock::nowNanoseconds() - startNanoseconds;
        SW_EXPECT_TRUE( table.isEmpty() );
        SW_LOG_INFO_ID( "OnlineServerCallTableTest.CompletionCost", "Server call table: pending %# -> %# ns per completion", pendingCount,
                        elapsedNanoseconds / ( static_cast<int64>( pendingCount ) * kRepeatCount ) );
    }
}
