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
