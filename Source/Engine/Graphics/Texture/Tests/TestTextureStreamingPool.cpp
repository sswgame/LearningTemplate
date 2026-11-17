#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Texture/TextureStreamingPool.h"

// 텍스처 밉 스트리밍의 풀 예산(R-6) — 예산을 넘는 승급은 예약을 받지 못하고, 예약은 쥔 쪽(텍스처 항목)이 사라질 때 풀에 돌아간다.

using namespace sw;

/** @brief [TextureStreamingPoolTest] 예산 안의 승급만 예약을 받고, 넘는 승급은 받지 못한다(그 텍스처는 아래쪽 밉 그대로다) */
SW_TEST_CASE( TextureStreamingPoolTest, RefusesUpgradesOverBudget )
{
    constexpr uint64     kBudget = 100;
    TextureStreamingPool pool;
    {
        optional<TextureStreamingReservation> first = pool.tryReserve( 60, kBudget );
        SW_REQUIRE_TRUE( first.has_value() );
        SW_EXPECT_EQUAL( uint64( 60 ), pool.getUsedBytes() );
        optional<TextureStreamingReservation> second = pool.tryReserve( 50, kBudget );
        SW_EXPECT_FALSE( second.has_value() ); // 60 + 50 > 100
        SW_EXPECT_EQUAL( uint64( 60 ), pool.getUsedBytes() );
        optional<TextureStreamingReservation> third = pool.tryReserve( 40, kBudget );
        SW_EXPECT_TRUE( third.has_value() ); // 꼭 맞으면 된다
        SW_EXPECT_EQUAL( uint64( 100 ), pool.getUsedBytes() );
    }
    SW_EXPECT_EQUAL( uint64( 0 ), pool.getUsedBytes() ); // 예약이 사라지면 돌려준다
}

/** @brief [TextureStreamingPoolTest] 예약은 옮겨도 한 번만 돌려주고, 빈 예약으로 덮으면 그 자리에서 돌려준다(승급 실패 · 항목 지움) */
SW_TEST_CASE( TextureStreamingPoolTest, ReservationReturnsBytesOnce )
{
    TextureStreamingPool pool;
    {
        vector<TextureStreamingReservation>   listHeld;
        optional<TextureStreamingReservation> reservation = pool.tryReserve( 30, 100 );
        SW_REQUIRE_TRUE( reservation.has_value() );
        listHeld.push_back( std::move( reservation.value() ) );
        listHeld.push_back( std::move( pool.tryReserve( 20, 100 ).value() ) ); // 옮기며 늘어나도 바이트가 두 번 빠지지 않는다
        SW_EXPECT_EQUAL( uint64( 50 ), pool.getUsedBytes() );
        listHeld[0] = TextureStreamingReservation{};
        SW_EXPECT_EQUAL( uint64( 20 ), pool.getUsedBytes() );
    }
    SW_EXPECT_EQUAL( uint64( 0 ), pool.getUsedBytes() );
}

/**
 * @brief [TextureStreamingPoolTest] 밉 바꾸기가 실패하면 예약은 실제 크기(옛 크기) 그대로다 — 성공하면 새 크기가 옛 크기를 대신한다(R6-6 내리기 실패)
 * @details 변이: 실패 갈래가 예약을 비우면(예전 길) 텍스처는 큰 채인데 풀이 0 을 세어 첫 단언 묶음이 진다.
 */
SW_TEST_CASE( TextureStreamingPoolTest, FailedChangeKeepsActualBytes )
{
    constexpr uint64                kBudget = 100;
    TextureStreamingPool            pool;
    TextureStreamingReservationSlot slot;
    slot.beginChange( std::move( pool.tryReserve( 80, kBudget ).value() ) );
    slot.commitChange(); // 80 바이트로 올라갔다
    SW_EXPECT_EQUAL( uint64( 80 ), pool.getUsedBytes() );

    // 내리기(20 바이트) — 작아지므로 예산을 보지 않는다. 바꾸는 동안 두 크기를 함께 센다.
    slot.beginChange( std::move( pool.tryReserve( 20, UINT64_MAX ).value() ) );
    SW_EXPECT_EQUAL( uint64( 100 ), pool.getUsedBytes() );
    slot.abandonChange(); // 실패 — 텍스처는 80 바이트 그대로다
    SW_EXPECT_EQUAL( uint64( 80 ), slot.getBytes() );
    SW_EXPECT_EQUAL( uint64( 80 ), pool.getUsedBytes() );

    slot.beginChange( std::move( pool.tryReserve( 20, UINT64_MAX ).value() ) );
    slot.commitChange(); // 성공 — 20 바이트가 80 바이트를 대신한다
    SW_EXPECT_EQUAL( uint64( 20 ), slot.getBytes() );
    SW_EXPECT_EQUAL( uint64( 20 ), pool.getUsedBytes() );
}
