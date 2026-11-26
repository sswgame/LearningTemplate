#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "GameFramework/Runtime/Online/Guard/Server/TokenBucketMap.h"

// TokenBucketMapTest — 키마다 버킷의 청소: 모두 쓰는 중인 키가 상한을 넘어도 새 키마다 전체를 훑지 않고(상각 O(1)),
// 가득 찬 버킷은 청소에서 지워지며, 쓰는 중인 키의 제한은 청소 뒤에도 그대로다. 시각은 손으로 넘긴다(nogpu).

SW_TEST_CASE( TokenBucketMapTest, ManyBusyKeysDoNotScanOnEveryNewKey )
{
    // 남용: 주소 · 계정을 많이 가진 쪽이 버킷을 비운 채(쓰는 중) 새 키를 계속 내면, 예전에는 상한 뒤 새 키마다 전체를 훑었다(996 번).
    sw::TokenBucketMap map;
    map.initialize( 1, 1000, 4 );
    int64 retryAfterMs = 0;
    for ( uint64 key = 1; key <= 1000; ++key )
    {
        SW_EXPECT_TRUE( map.tryConsume( key, 0, retryAfterMs ) );
    }
    SW_EXPECT_EQUAL( size_t{ 1000 }, map.getTrackedKeyCount() );
    // 문턱 4 · 8 · 16 · … · 512 에서 한 번씩 — 두 배 문턱이라 log2(1000 / 4) 번 남짓이다.
    SW_EXPECT_TRUE_MSG( map.getSweepCount().get() <= 8, "sweep count must grow logarithmically with busy keys" );
}

SW_TEST_CASE( TokenBucketMapTest, SweepErasesFullBucketsAndKeepsBusyLimits )
{
    sw::TokenBucketMap map;
    map.initialize( 1, 1000, 4 );
    int64 retryAfterMs = 0;
    for ( uint64 key = 1; key <= 3; ++key )
    {
        SW_EXPECT_TRUE( map.tryConsume( key, 0, retryAfterMs ) );
    }
    SW_EXPECT_TRUE( map.tryConsume( 4, 5000, retryAfterMs ) ); // 키 1 ~ 3 은 5 초 뒤 다시 가득 찼다
    SW_EXPECT_TRUE( map.tryConsume( 5, 5000, retryAfterMs ) ); // 문턱 4 에 닿아 청소 — 가득 찬 셋이 지워진다
    SW_EXPECT_EQUAL( size_t{ 1 }, map.getSweepCount().get() );
    SW_EXPECT_EQUAL( size_t{ 2 }, map.getTrackedKeyCount() );

    // 청소에서 살아남은 쓰는 중인 키는 제한이 그대로다.
    SW_EXPECT_FALSE( map.tryConsume( 4, 5000, retryAfterMs ) );
    SW_EXPECT_EQUAL( int64{ 1000 }, retryAfterMs );
}
