#include "pch.h"

#include "Core/Network/Connection/NetChallenge.h"
#include "Core/Network/NetTypes.h"

#include "TestFramework/TestFramework.h"

// NetChallenge — 같은 시드는 같은 솔트 수열과 같은 챌린지 값을 낸다. 값은 주소 · 클라이언트 솔트 · 시간 윈도우에 묶이고,
// 만든 윈도우와 그다음 윈도우에서만 유효하며 0 이 되지 않는다.

using namespace sw;

SW_TEST_CASE( NetChallengeTest, SameSeedGivesSameSaltsAndTokens )
{
    NetChallenge first;
    NetChallenge second;
    first.initialize( 1234 );
    second.initialize( 1234 );
    const NetAddress address = NetAddress::makeLoopback( 4000 );
    SW_EXPECT_EQUAL( first.makeToken( address, 77, 1.0 ), second.makeToken( address, 77, 1.0 ) );
    SW_EXPECT_EQUAL( first.nextSalt(), second.nextSalt() );

    NetChallenge other;
    other.initialize( 4321 );
    SW_EXPECT_TRUE( other.makeToken( address, 77, 1.0 ) != first.makeToken( address, 77, 1.0 ) ); // 비밀 키가 다르다
}

SW_TEST_CASE( NetChallengeTest, TokenIsBoundToAddressSaltAndWindow )
{
    NetChallenge challenge;
    challenge.initialize( 99 );
    const NetAddress address = NetAddress::makeLoopback( 4000 );
    const float64    now     = 12.0;
    const uint64     token   = challenge.makeToken( address, 5, now );
    SW_EXPECT_TRUE( ( token & 1u ) != 0 ); // 0 은 "챌린지를 아직 받지 못했다" 라 쓰지 않는다
    SW_EXPECT_TRUE( challenge.isTokenValid( address, 5, token, now ) );
    SW_EXPECT_FALSE( challenge.isTokenValid( NetAddress::makeLoopback( 4001 ), 5, token, now ) );
    SW_EXPECT_FALSE( challenge.isTokenValid( address, 6, token, now ) );

    // 만든 윈도우의 다음 윈도우까지만 유효하다.
    SW_EXPECT_TRUE( challenge.isTokenValid( address, 5, token, now + NetChallenge::kWindowSeconds ) );
    SW_EXPECT_FALSE( challenge.isTokenValid( address, 5, token, now + NetChallenge::kWindowSeconds * 2.0 ) );
    SW_EXPECT_FALSE( challenge.isTokenValid( address, 5, token, now - NetChallenge::kWindowSeconds ) );
    SW_EXPECT_EQUAL( static_cast<int64>( 2 ), NetChallenge::computeWindow( now ) );
}

SW_TEST_CASE( NetChallengeTest, ZeroSeedDrawsAnUnpredictableSecret )
{
    NetChallenge first;
    NetChallenge second;
    first.initialize( 0 );
    second.initialize( 0 );
    const NetAddress address = NetAddress::makeLoopback( 4000 );
    SW_EXPECT_TRUE( first.makeToken( address, 1, 0.0 ) != second.makeToken( address, 1, 0.0 ) );
}
