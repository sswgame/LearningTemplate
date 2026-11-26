#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Service/OnlineProtocolService.h"
#include "GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.h"

// OnlineServerCallTableTest — 서버 호출 표: 응답은 한 번이고 어느 순서로 끝나도 남은 수가 맞다(nogpu).

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

/** @brief [OnlineServerCallTableTest] 상한 검사는 방금 연 호출도 센다 — 상한 N 이면 N 개까지 받고 N+1 번째가 넘친다(넘친 요청은 `kBusy`, 감사 O-3) */
SW_TEST_CASE( OnlineServerCallTableTest, OverLimitCountsTheCallJustOpened )
{
    OnlineServiceHost     host;
    OnlineServerCallTable table;
    (void)table.open( host, NetRequestToken{}, 0 );
    (void)table.open( host, NetRequestToken{}, 0 );
    SW_EXPECT_FALSE( table.isOverLimit( 2 ) );
    const uint64 third = table.open( host, NetRequestToken{}, 0 );
    SW_EXPECT_TRUE( table.isOverLimit( 2 ) );
    SW_EXPECT_TRUE( table.fail( third, OnlineError::kBusy ) );
    SW_EXPECT_FALSE( table.isOverLimit( 2 ) );
    table.clear();
}
