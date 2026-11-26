// AudioRenderThread — 장치 콜백 대신 앞서 렌더하는 스레드와 링: 켜자마자 꺼낼 것이 있고, 모자라면 무음으로 채워 언더런을 센다. 장치 없음.
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/TestFramework/TestFramework.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Audio/AudioRenderThread.h"
#include "Engine/Audio/Tests/OfflineAudioEngine.h"

using namespace sw;

namespace
{
    /** @brief 채널 수(스테레오)입니다. */
    constexpr uint32 kChannelCount = 2;
    /** @brief 장치가 한 번에 당기는 프레임 수입니다(블루투스 버스트 — 렌더 블록의 배수가 아니다). */
    constexpr uint32 kBurstFrameCount = 960;
} // namespace

/**
 * @brief [AudioRenderThreadTest] 켜면 링이 차 있어 첫 콜백이 모자라지 않고, 꺼낸 만큼 렌더 스레드가 다시 채운다
 */
SW_TEST_CASE( AudioRenderThreadTest, FirstReadsAreServedAndTheRingRefills )
{
    test::OfflineAudioEngine offline;
    offline.getEngine().releaseAudioThread();
    AudioRenderThread renderThread{ offline.getEngine() };
    renderThread.start( kChannelCount, kBurstFrameCount );

    // 버스트 하나는 늘 채워 둔 것으로 받는다. 다음 버스트 앞에는 다시 채워질 때까지 기다린다(게시한 렌더 프레임 수 — 시한은 상한일 뿐이다).
    vector<float32>  listOutput( static_cast<size_t>( kBurstFrameCount ) * kChannelCount, 1.0f );
    constexpr uint32 kReadCount = 4;
    for ( uint32 index = 0; index < kReadCount; ++index )
    {
        const uint64   neededFrames = static_cast<uint64>( index + 1 ) * kBurstFrameCount;
        const Deadline deadline     = Deadline::afterMilliseconds( 10000 );
        // 게시한 수는 링에 넣기 직전 블록까지다 — 블록 하나를 더 기다려야 그 앞이 모두 링에 든 것이 확실하다.
        while ( offline.getEngine().getStats()._renderedFrameCount < neededFrames + AudioRenderThread::kRenderFrameCount && deadline.isExpired() == false )
        {
            ThreadUtil::yield();
        }
        renderThread.read( listOutput.data(), kBurstFrameCount );
    }
    SW_EXPECT_EQUAL( uint64{ 0 }, renderThread.getUnderrunCount() );
    renderThread.stop();
    offline.getEngine().releaseAudioThread();
}

/**
 * @brief [AudioRenderThreadTest] 링보다 많이 꺼내면 나머지는 무음이고 언더런을 한 번 센다
 */
SW_TEST_CASE( AudioRenderThreadTest, ShortReadIsSilenceAndCounted )
{
    test::OfflineAudioEngine offline;
    offline.getEngine().releaseAudioThread();
    AudioRenderThread renderThread{ offline.getEngine() };
    renderThread.start( kChannelCount, kBurstFrameCount );
    renderThread.stop(); // 채우는 쪽을 멈춰 모자람을 만든다

    const uint32    frameCount = kBurstFrameCount + AudioRenderThread::kSpareFrameCount + AudioRenderThread::kRenderFrameCount;
    vector<float32> listOutput( static_cast<size_t>( frameCount ) * kChannelCount, 1.0f );
    renderThread.read( listOutput.data(), frameCount );
    SW_EXPECT_EQUAL( uint64{ 1 }, renderThread.getUnderrunCount() );
    SW_EXPECT_EQUAL( 0.0f, listOutput.back() );

    renderThread.read( listOutput.data(), AudioRenderThread::kRenderFrameCount );
    SW_EXPECT_EQUAL( uint64{ 2 }, renderThread.getUnderrunCount() );
    SW_EXPECT_EQUAL( 0.0f, listOutput[0] );
    offline.getEngine().releaseAudioThread();
}
