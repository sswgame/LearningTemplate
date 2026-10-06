#include "pch.h"

#include "GameFramework/Base/Online/Guard/TokenBucketMap.h"

#include "TestFramework/TestFramework.h"

// 토큰 버킷 — 버스트 뒤 간격마다 하나, 남은 조각 시간 이월, 되돌아간 시각, 기다릴 시간, 키마다 버킷과 상한에서 가득 찬 것 지우기.

using namespace sw;

SW_TEST_CASE( TokenBucketTest, BurstThenOnePerInterval )
{
    TokenBucket bucket;
    bucket.initialize( 3, 1000, 0 );
    SW_EXPECT_TRUE( bucket.tryConsume( 0 ) );
    SW_EXPECT_TRUE( bucket.tryConsume( 0 ) );
    SW_EXPECT_TRUE( bucket.tryConsume( 0 ) );
    SW_EXPECT_FALSE( bucket.tryConsume( 10 ) );
    SW_EXPECT_EQUAL( int64( 990 ), bucket.computeWaitMs( 10 ) );
    SW_EXPECT_TRUE( bucket.tryConsume( 1000 ) );
    SW_EXPECT_FALSE( bucket.tryConsume( 1999 ) );
    SW_EXPECT_TRUE( bucket.tryConsume( 2000 ) );
}

SW_TEST_CASE( TokenBucketTest, PartialIntervalCarriesOver )
{
    TokenBucket bucket;
    bucket.initialize( 2, 1000, 0 );
    SW_ASSERT_TRUE( bucket.tryConsume( 0, 2 ) );
    SW_EXPECT_TRUE( bucket.tryConsume( 1500 ) ); // 1000 에 하나 찼고 500 은 이월
    SW_EXPECT_TRUE( bucket.tryConsume( 2000 ) ); // 이월된 500 + 500
    SW_EXPECT_FALSE( bucket.tryConsume( 2000 ) );
}

SW_TEST_CASE( TokenBucketTest, CapsAtCapacityAndIgnoresClockGoingBack )
{
    TokenBucket bucket;
    bucket.initialize( 2, 100, 1000 );
    SW_ASSERT_TRUE( bucket.tryConsume( 1000, 2 ) );
    SW_EXPECT_EQUAL( 0, bucket.computeTokenCount( 500 ) ); // 되돌아간 시각은 채우지 않는다
    SW_EXPECT_FALSE( bucket.tryConsume( 500 ) );
    SW_EXPECT_EQUAL( 2, bucket.computeTokenCount( 100000 ) );
    SW_EXPECT_TRUE( bucket.isFull( 100000 ) );
}

SW_TEST_CASE( TokenBucketTest, MapTracksKeysSeparatelyAndDropsFullBucketsAtTheCap )
{
    TokenBucketMap limiter;
    limiter.initialize( 1, 1000, 2 );
    int64 retryAfterMs = 0;
    SW_EXPECT_TRUE( limiter.tryConsume( 1, 0, retryAfterMs ) );
    SW_EXPECT_FALSE( limiter.tryConsume( 1, 0, retryAfterMs ) );
    SW_EXPECT_EQUAL( int64( 1000 ), retryAfterMs );
    SW_EXPECT_TRUE( limiter.tryConsume( 2, 0, retryAfterMs ) );    // 다른 키는 따로
    SW_EXPECT_TRUE( limiter.tryConsume( 3, 5000, retryAfterMs ) ); // 상한 2 — 5 초 뒤라 1 · 2 는 가득 차 지워진다
    SW_EXPECT_EQUAL( size_t( 1 ), limiter.getTrackedKeyCount() );
}
