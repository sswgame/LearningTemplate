#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Audio/AudioClip.h"

using namespace sw;

namespace
{
    /** @brief 모노 샘플 @p frameCount 개짜리 클립입니다(바이트는 frameCount × 4). */
    shared_ptr<const AudioClipData> makeClip( uint32 frameCount )
    {
        shared_ptr<AudioClipData> pClip = make_shared<AudioClipData>();
        pClip->_listSample.assign( frameCount, 0.0f );
        pClip->_frameCount   = frameCount;
        pClip->_sampleRate   = 48000;
        pClip->_channelCount = 1;
        return pClip;
    }
} // namespace

/**
 * @brief [AudioClipCacheTest] 예산을 넘으면 아무도 잡지 않은 클립을 가장 오래 안 쓴 것부터 뺀다
 * @details 예전에는 캐시가 더하기만 해서 한 번 재생한 소리의 PCM 이 프로세스 끝까지 남았다(감사 S-3). 변이: `storeResult` 의 퇴출 호출을 빼면 첫 단언이 진다.
 */
SW_TEST_CASE( AudioClipCacheTest, EvictsUnreferencedClipsOverBudget )
{
    AudioClipCache cache;
    cache.setBudgetBytes( 1000 );
    cache.addClip( hashed_string( "a.wav" ), makeClip( 100 ) ); // 400 바이트
    cache.addClip( hashed_string( "b.wav" ), makeClip( 100 ) );
    SW_EXPECT_TRUE( cache.findClip( hashed_string( "a.wav" ) ) != nullptr ); // a 를 썼다 — 이제 b 가 가장 오래 안 쓴 것이다
    cache.addClip( hashed_string( "c.wav" ), makeClip( 100 ) );              // 1200 > 1000

    SW_EXPECT_FALSE( cache.findLoadState( hashed_string( "b.wav" ) ).has_value() );
    SW_EXPECT_TRUE( cache.findClip( hashed_string( "a.wav" ) ) != nullptr );
    SW_EXPECT_TRUE( cache.findClip( hashed_string( "c.wav" ) ) != nullptr );
    SW_EXPECT_EQUAL( uint64{ 800 }, cache.getCachedBytes() );
}

/** @brief [AudioClipCacheTest] 재생 중인 보이스가 잡은 클립은 빼지 않는다 — 모두 잡혀 있으면 예산을 넘긴 채 둔다. */
SW_TEST_CASE( AudioClipCacheTest, PlayingClipIsNotEvicted )
{
    AudioClipCache cache;
    cache.setBudgetBytes( 1000 );
    cache.addClip( hashed_string( "a.wav" ), makeClip( 100 ) );
    const shared_ptr<const AudioClipData> pPlayingA = cache.findClip( hashed_string( "a.wav" ) ); // 보이스가 재생 중이다
    cache.addClip( hashed_string( "b.wav" ), makeClip( 100 ) );
    const shared_ptr<const AudioClipData> pPlayingB = cache.findClip( hashed_string( "b.wav" ) );
    cache.addClip( hashed_string( "c.wav" ), makeClip( 100 ) ); // 넘었지만 a · b 는 재생 중, c 는 방금 넣었다

    SW_EXPECT_TRUE( cache.findLoadState( hashed_string( "a.wav" ) ).has_value() );
    SW_EXPECT_TRUE( cache.findLoadState( hashed_string( "b.wav" ) ).has_value() );
    SW_EXPECT_TRUE( cache.findLoadState( hashed_string( "c.wav" ) ).has_value() );
    SW_EXPECT_EQUAL( uint64{ 1200 }, cache.getCachedBytes() );

    // 다음 넣기는 잡히지 않은 c 만 뺄 수 있다 — a · b 가 c 보다 오래됐어도 재생 중이라 남는다.
    cache.addClip( hashed_string( "d.wav" ), makeClip( 25 ) ); // 100 바이트, 1300 > 1000
    SW_EXPECT_FALSE( cache.findLoadState( hashed_string( "c.wav" ) ).has_value() );
    SW_EXPECT_TRUE( cache.findLoadState( hashed_string( "a.wav" ) ).has_value() );
    SW_EXPECT_EQUAL( uint64{ 900 }, cache.getCachedBytes() );
}

/** @brief [AudioClipCacheTest] 같은 경로를 바꿔 넣으면 바이트 합이 옛 클립을 빼고 새 클립을 더한다(핫 리로드). */
SW_TEST_CASE( AudioClipCacheTest, ReplacingClipKeepsByteCountExact )
{
    AudioClipCache cache;
    cache.setBudgetBytes( 1000 );
    cache.addClip( hashed_string( "a.wav" ), makeClip( 100 ) );
    cache.addClip( hashed_string( "a.wav" ), makeClip( 50 ) );
    SW_EXPECT_EQUAL( uint64{ 200 }, cache.getCachedBytes() );
    cache.clear();
    SW_EXPECT_EQUAL( uint64{ 0 }, cache.getCachedBytes() );
}
