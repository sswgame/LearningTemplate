#include "pch.h"

#include "GameFramework/Kits/Online/Server/Chat/ChatSpamGuard.h"

#include "TestFramework/TestFramework.h"

// 도배 막이 — 몰아 쓰기 뒤 간격, 같은 글(대소 · 끼움 글자 · 전각만 다른 것 포함) 셋째 거절, 창이 지나면 다시 허락, 계정마다 따로, 떠난 계정은 반복 기록을 잊는다.

using namespace sw;

SW_TEST_CASE( ChatSpamGuardTest, BurstThenRateLimitPerAccount )
{
    ChatSpamGuard guard;
    guard.initialize( ChatSpamSettings{} );
    int64 retryAfterMs = 0;
    for ( int32 index = 0; index < 5; ++index )
    {
        string text( "line " );
        text += static_cast<utf8>( 'a' + index );
        SW_EXPECT_TRUE( guard.check( 1, text, 1000, retryAfterMs ) == ChatSpamVerdict::Allowed );
    }
    SW_EXPECT_TRUE( guard.check( 1, "line f", 1000, retryAfterMs ) == ChatSpamVerdict::RateLimited );
    SW_EXPECT_EQUAL( retryAfterMs, int64( 1000 ) );
    SW_EXPECT_TRUE( guard.check( 2, "line f", 1000, retryAfterMs ) == ChatSpamVerdict::Allowed ); // 다른 계정
    SW_EXPECT_TRUE( guard.check( 1, "line f", 2000, retryAfterMs ) == ChatSpamVerdict::Allowed );
    SW_EXPECT_EQUAL( retryAfterMs, int64( 0 ) );
}

SW_TEST_CASE( ChatSpamGuardTest, ThirdRepeatInsideWindowIsRejected )
{
    ChatSpamGuard guard;
    guard.initialize( ChatSpamSettings{} );
    int64 retryAfterMs = 0;
    SW_EXPECT_TRUE( guard.check( 1, "buy gold", 1000, retryAfterMs ) == ChatSpamVerdict::Allowed );
    SW_EXPECT_TRUE( guard.check( 1, "BUY  GOLD", 2000, retryAfterMs ) == ChatSpamVerdict::Allowed );
    SW_EXPECT_TRUE( guard.check( 1, "b.u.y gold", 3000, retryAfterMs ) == ChatSpamVerdict::Repeated );
    SW_EXPECT_EQUAL( retryAfterMs, int64( 10000 ) );
    SW_EXPECT_TRUE( guard.check( 1, "\xEF\xBD\x82uy gold", 4000, retryAfterMs ) == ChatSpamVerdict::Repeated ); // 전각 'ｂ'
    SW_EXPECT_TRUE( guard.check( 2, "buy gold", 4000, retryAfterMs ) == ChatSpamVerdict::Allowed );             // 다른 계정
    SW_EXPECT_TRUE( guard.check( 1, "buy gold", 12001, retryAfterMs ) == ChatSpamVerdict::Allowed );            // 둘 다 10 초 창 밖
}

SW_TEST_CASE( ChatSpamGuardTest, ForgetClearsRepeatHistory )
{
    ChatSpamGuard guard;
    guard.initialize( ChatSpamSettings{} );
    int64 retryAfterMs = 0;
    SW_EXPECT_TRUE( guard.check( 1, "hello", 1000, retryAfterMs ) == ChatSpamVerdict::Allowed );
    SW_EXPECT_TRUE( guard.check( 1, "hello", 1000, retryAfterMs ) == ChatSpamVerdict::Allowed );
    SW_EXPECT_TRUE( guard.check( 1, "hello", 1000, retryAfterMs ) == ChatSpamVerdict::Repeated );
    guard.forget( 1 );
    SW_EXPECT_TRUE( guard.check( 1, "hello", 1000, retryAfterMs ) == ChatSpamVerdict::Allowed );
    SW_EXPECT_TRUE( ChatSpamGuard::computeTextHash( "He-Llo" ) == ChatSpamGuard::computeTextHash( "hello" ) );
    SW_EXPECT_TRUE( ChatSpamGuard::computeTextHash( "hello" ) != ChatSpamGuard::computeTextHash( "help" ) );
}
